#pragma once

#include <QtGlobal>

#ifdef Q_OS_ANDROID

#include <QString>

#include <memory>

extern "C" {
struct AVFrame;
}

struct AImageReader;
struct ANativeWindow;
struct AImage;
struct AHardwareBuffer;

namespace drift {
class MediaCodecImagePool;

// What a latched frame carries in buf[0]. Declared here rather than privately in the .cpp because
// GlRuntime reads `buffer` back out to import it, and two independent declarations of the same
// layout is exactly the kind of thing that stays correct until someone reorders a member.
struct LatchedMediaCodecImage
{
    std::shared_ptr<MediaCodecImagePool> pool;
    AImage *image = nullptr;
    AHardwareBuffer *buffer = nullptr;
};
} // namespace drift

namespace drift {

// Output surface for a MediaCodec decoder, and the ring of gralloc buffers behind it.
//
// The surfaceless MediaCodec path copies every output buffer into system memory, then sws-scales
// it, then uploads it again — three passes over each frame. Rendering to a surface instead keeps
// the frame in graphics memory the whole way to the compositor.
//
// The reason this is an AImageReader and not the more obvious SurfaceTexture is the frame cache.
// ClipReader holds several decoded frames at once so a backward scrub is served without
// re-seeking, and a SurfaceTexture can only have one buffer latched into its external texture at
// a time — the two are irreconcilable. An AImageReader hands out `maxImages` independent AImages,
// each owning its own AHardwareBuffer, which is exactly the shape the cache already has.
//
// Frames are latched at decode time, not at draw time: as soon as a MediaCodec frame arrives it is
// rendered to this surface and the resulting AImage is wrapped in a plain AVFrame. That severs the
// link to the codec, so the cached frame no longer holds a codec output slot and
// avcodec_flush_buffers becomes harmless. Holding real MediaCodec frames across a flush is not
// survivable in either configuration FFmpeg offers: with delay_flush=0 the flush bumps the
// decoder's serial and every held frame silently becomes garbage, and with delay_flush=1 the
// flush is refused and receive_frame returns EAGAIN forever until the cache is dropped.
class MediaCodecImagePool
{
public:
    // Null when the reader cannot be created (unsupported usage flags, out of buffers).
    // `maxImages` bounds how many frames may be held at once; it consumes codec output slots.
    static std::shared_ptr<MediaCodecImagePool> create(int width, int height, int maxImages);
    ~MediaCodecImagePool();

    MediaCodecImagePool(const MediaCodecImagePool &) = delete;
    MediaCodecImagePool &operator=(const MediaCodecImagePool &) = delete;

    // Hand to AVMediaCodecDeviceContext::native_window. Owned by the pool.
    ANativeWindow *window() const { return m_window; }

    // Renders `mediaCodecFrame` (an AV_PIX_FMT_MEDIACODEC frame) to the surface and returns a new
    // AVFrame holding the resulting image, or null on failure. The returned frame carries the
    // source's timing and colour metadata and is independent of the decoder from here on, so the
    // caller may unref the input immediately either way.
    //
    // `self` must be the shared_ptr owning this pool: every wrapped frame keeps a reference to
    // it, because AImageReader_delete requires that no AImage outlive the reader and frames
    // routinely outlive the ClipReader that decoded them.
    static AVFrame *latch(const std::shared_ptr<MediaCodecImagePool> &self, AVFrame *mediaCodecFrame);

    // Why the last create() or latch() failed. Empty when nothing has.
    static QString lastFailureReason();

private:
    MediaCodecImagePool() = default;

    AImageReader *m_reader = nullptr;
    ANativeWindow *m_window = nullptr;
};

} // namespace drift

#endif // Q_OS_ANDROID
