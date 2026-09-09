#include "MediaCodecImagePool.h"

#ifdef Q_OS_ANDROID

#include <QMutex>
#include <QMutexLocker>
#include <QThread>

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

extern "C" {
#include <libavcodec/mediacodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace drift {
namespace {

QMutex g_reasonMutex;
QString g_reason;

void setFailureReason(const QString &why)
{
    QMutexLocker lock(&g_reasonMutex);
    g_reason = why;
}

// The AImage owns the gralloc buffer; the extra AHardwareBuffer reference is what lets the GL
// importer keep using it independently of AImage_delete. `pool` is the keepalive described in the
// header — dropping it here is what finally allows AImageReader_delete to run.
void releaseLatchedImage(void *opaque, uint8_t *data)
{
    Q_UNUSED(data);
    auto *latched = static_cast<LatchedMediaCodecImage *>(opaque);
    if (!latched)
        return;
    if (latched->buffer)
        AHardwareBuffer_release(latched->buffer);
    if (latched->image)
        AImage_delete(latched->image);
    delete latched;
}

} // namespace

std::shared_ptr<MediaCodecImagePool> MediaCodecImagePool::create(int width, int height, int maxImages)
{
    if (width <= 0 || height <= 0 || maxImages <= 0)
        return nullptr;

    AImageReader *reader = nullptr;
    // AIMAGE_FORMAT_PRIVATE is the only format a video decoder is guaranteed to be able to write:
    // it leaves the layout to the driver, which is what makes the buffer importable as a GL
    // external texture without a copy. GPU_SAMPLED_IMAGE is the usage that promise depends on.
    const media_status_t status =
        AImageReader_newWithUsage(width, height, AIMAGE_FORMAT_PRIVATE,
                                  AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE, maxImages, &reader);
    if (status != AMEDIA_OK || !reader) {
        setFailureReason(QStringLiteral("AImageReader_newWithUsage failed (%1)").arg(int(status)));
        return nullptr;
    }

    ANativeWindow *window = nullptr;
    if (AImageReader_getWindow(reader, &window) != AMEDIA_OK || !window) {
        AImageReader_delete(reader);
        setFailureReason(QStringLiteral("AImageReader_getWindow failed"));
        return nullptr;
    }

    // Not std::make_shared: the constructor is private.
    std::shared_ptr<MediaCodecImagePool> pool(new MediaCodecImagePool);
    pool->m_reader = reader;
    pool->m_window = window;
    return pool;
}

MediaCodecImagePool::~MediaCodecImagePool()
{
    // Safe without any explicit ordering: every latched AImage holds a shared_ptr to this pool,
    // so the last one to be freed is what runs this.
    if (m_reader)
        AImageReader_delete(m_reader);
    // The window belongs to the reader — AImageReader_getWindow's contract is explicit that it
    // must not be released separately.
}

AVFrame *MediaCodecImagePool::latch(const std::shared_ptr<MediaCodecImagePool> &self,
                                    AVFrame *mediaCodecFrame)
{
    if (!self || !self->m_reader || !mediaCodecFrame)
        return nullptr;
    if (mediaCodecFrame->format != AV_PIX_FMT_MEDIACODEC)
        return nullptr;

    auto *codecBuffer = reinterpret_cast<AVMediaCodecBuffer *>(mediaCodecFrame->data[3]);
    if (!codecBuffer) {
        setFailureReason(QStringLiteral("MediaCodec frame carried no buffer"));
        return nullptr;
    }

    // render=1 hands the buffer to the surface. After this the codec owns it again, so the
    // frame's data[3] must not be touched, whatever happens below.
    if (av_mediacodec_release_buffer(codecBuffer, 1) < 0) {
        setFailureReason(QStringLiteral("av_mediacodec_release_buffer failed"));
        return nullptr;
    }

    AImage *image = nullptr;
    // The buffer is queued asynchronously, so the image is not necessarily there on the first
    // ask. This is a bounded spin rather than a condition variable because the wait is normally
    // zero or one iteration and this runs on the decode thread, which has nothing else to do.
    media_status_t status = AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE;
    for (int attempt = 0; attempt < 64; ++attempt) {
        status = AImageReader_acquireNextImage(self->m_reader, &image);
        if (status != AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE)
            break;
        QThread::usleep(500);
    }
    if (status != AMEDIA_OK || !image) {
        setFailureReason(QStringLiteral("AImageReader_acquireNextImage failed (%1)").arg(int(status)));
        return nullptr;
    }

    AHardwareBuffer *hardware = nullptr;
    if (AImage_getHardwareBuffer(image, &hardware) != AMEDIA_OK || !hardware) {
        AImage_delete(image);
        setFailureReason(QStringLiteral("AImage_getHardwareBuffer failed"));
        return nullptr;
    }

    AVFrame *out = av_frame_alloc();
    if (!out) {
        AImage_delete(image);
        return nullptr;
    }

    auto *latched = new LatchedMediaCodecImage;
    latched->pool = self;
    latched->image = image;
    latched->buffer = hardware;
    // The AImage owns the buffer, but the GL importer addresses it by AHardwareBuffer, and the
    // two are freed together below — take the reference so the ordering cannot matter.
    AHardwareBuffer_acquire(hardware);

    AVBufferRef *ref = av_buffer_create(reinterpret_cast<uint8_t *>(latched), sizeof(LatchedMediaCodecImage),
                                        releaseLatchedImage, latched, 0);
    if (!ref) {
        releaseLatchedImage(latched, nullptr);
        av_frame_free(&out);
        return nullptr;
    }

    // Copy timing/colour first: av_frame_copy_props would overwrite the fields set after it.
    av_frame_copy_props(out, mediaCodecFrame);
    out->format = AV_PIX_FMT_MEDIACODEC;
    out->width = mediaCodecFrame->width;
    out->height = mediaCodecFrame->height;
    out->buf[0] = ref;
    // Not data[3]: that slot means "an AVMediaCodecBuffer the decoder still owns" everywhere else
    // in FFmpeg, and this frame deliberately no longer has one. The importer reads buf[0].
    out->data[3] = nullptr;

    // The gralloc buffer is normally larger than the picture — 1088 rows for 1080p is the usual
    // case — and the crop rect is the only thing that says where the real pixels sit inside it.
    // Getting this wrong is the classic garbage-strip-along-the-edge bug.
    //
    // Convention for a latched frame, which differs from FFmpeg's and is relied on by
    // GlRuntime::importMediaCodecImage: width/height are the *picture* size, and crop_left /
    // crop_top are the picture's offset within the buffer. crop_right/crop_bottom are unused and
    // forced to zero, because av_frame_copy_props above may have brought the decoder's own values
    // across and they mean something different. Nothing else in the project reads these fields.
    out->crop_left = 0;
    out->crop_top = 0;
    out->crop_right = 0;
    out->crop_bottom = 0;
    AImageCropRect crop{};
    if (AImage_getCropRect(image, &crop) == AMEDIA_OK) {
        const int cropW = crop.right - crop.left;
        const int cropH = crop.bottom - crop.top;
        if (cropW > 0 && cropH > 0) {
            out->crop_left = crop.left > 0 ? crop.left : 0;
            out->crop_top = crop.top > 0 ? crop.top : 0;
            out->width = cropW;
            out->height = cropH;
        }
    }
    return out;
}

QString MediaCodecImagePool::lastFailureReason()
{
    QMutexLocker lock(&g_reasonMutex);
    return g_reason;
}

} // namespace drift

#endif // Q_OS_ANDROID
