#include "MatteWriter.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace drift {

struct MatteWriter::Impl
{
    AVFormatContext *fmt = nullptr;
    AVCodecContext *ctx = nullptr;
    AVStream *stream = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;

    QString path;
    QString tmpPath;
    QSize size;
    MatteWriter::Mode mode = MatteWriter::Mode::Coverage;
    SwsContext *sws = nullptr; // Colour mode only: RGB888 -> YUV420P
    int64_t nextPts = 0;
    bool headerWritten = false;
    bool finished = false;

    bool drainPackets(QString *errorOut);
    void teardown();
};

bool MatteWriter::Impl::drainPackets(QString *errorOut)
{
    for (;;) {
        const int rc = avcodec_receive_packet(ctx, pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to read an encoded matte packet");
            return false;
        }
        av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        const int wrc = av_interleaved_write_frame(fmt, pkt);
        av_packet_unref(pkt);
        if (wrc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to write a matte packet");
            return false;
        }
    }
}

void MatteWriter::Impl::teardown()
{
    if (sws) {
        sws_freeContext(sws);
        sws = nullptr;
    }
    if (frame)
        av_frame_free(&frame);
    if (pkt)
        av_packet_free(&pkt);
    if (ctx)
        avcodec_free_context(&ctx);
    if (fmt) {
        if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
        fmt = nullptr;
    }
    stream = nullptr;
}

MatteWriter::MatteWriter()
    : d(std::make_unique<Impl>())
{
}

MatteWriter::~MatteWriter()
{
    abort();
}

bool MatteWriter::open(const QString &path, const QSize &size, int fpsNum, int fpsDen,
                       QString *errorOut, Mode mode)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        d->teardown();
        return false;
    };

    if (size.isEmpty() || fpsNum <= 0 || fpsDen <= 0)
        return fail(QStringLiteral("Invalid matte dimensions or frame rate"));

    d->path = path;
    d->mode = mode;
    // Same temp-then-rename discipline as Exporter: a cancelled run must not leave a file that
    // looks like a usable matte.
    d->tmpPath = path + QStringLiteral(".part");
    d->size = size;
    if (QFile::exists(d->tmpPath))
        QFile::remove(d->tmpPath);

    const QByteArray tmpUtf8 = d->tmpPath.toUtf8();
    avformat_alloc_output_context2(&d->fmt, nullptr, "mp4", tmpUtf8.constData());
    if (!d->fmt)
        return fail(QStringLiteral("Could not create the matte container"));

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        return fail(QStringLiteral("H.264 encoder not available"));

    d->stream = avformat_new_stream(d->fmt, nullptr);
    if (!d->stream)
        return fail(QStringLiteral("Could not create the matte stream"));

    d->ctx = avcodec_alloc_context3(codec);
    if (!d->ctx)
        return fail(QStringLiteral("Could not allocate the matte encoder"));

    d->ctx->width = size.width();
    d->ctx->height = size.height();
    d->ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    d->ctx->time_base = AVRational{fpsDen, fpsNum};
    d->ctx->framerate = AVRational{fpsNum, fpsDen};
    // Full range, and not optional. Left unset the stream is tagged limited-range and ClipReader
    // expands 16..235 back out to 0..255 on the way in, which shifts every midtone by about 7% and
    // crushes both ends. A binary SAM2 mask survives that because 0 and 255 clamp back to
    // themselves; a soft RVM alpha does not, and neither does a colour foreground.
    d->ctx->color_range = AVCOL_RANGE_JPEG;
    // Every frame a keyframe: the compositor seeks to arbitrary times, and a matte that has to
    // decode a GOP to answer costs far more than the size it saves.
    d->ctx->gop_size = 1;
    d->ctx->max_b_frames = 0;
    if (d->fmt->oformat->flags & AVFMT_GLOBALHEADER)
        d->ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (mode == Mode::Coverage) {
        av_opt_set(d->ctx->priv_data, "qp", "0", 0); // lossless luma: the mask edge is the payload
    } else {
        // Ordinary picture content. Lossless would run to hundreds of megabytes for a clip and buy
        // nothing: this is only ever sampled where the alpha is already non-zero.
        av_opt_set(d->ctx->priv_data, "crf", "16", 0);
    }
    av_opt_set(d->ctx->priv_data, "preset", "veryfast", 0);
    av_opt_set(d->ctx->priv_data, "tune", "fastdecode", 0);

    if (avcodec_open2(d->ctx, codec, nullptr) < 0)
        return fail(QStringLiteral("Could not open the matte encoder"));

    avcodec_parameters_from_context(d->stream->codecpar, d->ctx);
    d->stream->time_base = d->ctx->time_base;
    // Without this the muxer infers the rate from packet timestamps and lands on 29.97 for a
    // 30 fps matte, which slides the mask off the subject further into the clip.
    d->stream->avg_frame_rate = AVRational{fpsNum, fpsDen};
    d->stream->r_frame_rate = AVRational{fpsNum, fpsDen};

    if (!(d->fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&d->fmt->pb, tmpUtf8.constData(), AVIO_FLAG_WRITE) < 0)
            return fail(QStringLiteral("Could not open the matte file for writing"));
    }

    if (avformat_write_header(d->fmt, nullptr) < 0)
        return fail(QStringLiteral("Could not write the matte header"));
    d->headerWritten = true;

    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->pkt || !d->frame)
        return fail(QStringLiteral("Could not allocate matte frame buffers"));

    d->frame->format = AV_PIX_FMT_YUV420P;
    d->frame->width = size.width();
    d->frame->height = size.height();
    d->frame->color_range = AVCOL_RANGE_JPEG;
    if (av_frame_get_buffer(d->frame, 0) < 0)
        return fail(QStringLiteral("Could not allocate the matte frame"));

    if (mode == Mode::Colour) {
        d->sws = sws_getContext(size.width(), size.height(), AV_PIX_FMT_RGB24, size.width(),
                                size.height(), AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr,
                                nullptr);
        if (!d->sws)
            return fail(QStringLiteral("Could not create the matte colour converter"));
        // Match the full-range tagging above, or swscale writes limited-range luma into a stream
        // that says otherwise.
        const int *coeff = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(d->sws, coeff, 1, coeff, 1, 0, 1 << 16, 1 << 16);
    }

    return true;
}

bool MatteWriter::writeFrame(const QImage &image, QString *errorOut)
{
    if (!d->ctx || !d->frame) {
        if (errorOut)
            *errorOut = QStringLiteral("Matte writer is not open");
        return false;
    }

    const QImage::Format want =
        d->mode == Mode::Coverage ? QImage::Format_Grayscale8 : QImage::Format_RGB888;
    QImage src = image.format() == want ? image : image.convertToFormat(want);
    if (src.size() != d->size)
        src = src.scaled(d->size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    if (av_frame_make_writable(d->frame) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not make the matte frame writable");
        return false;
    }

    if (d->mode == Mode::Coverage) {
        // Mask into luma; chroma stays neutral and is never read back.
        for (int y = 0; y < d->size.height(); ++y) {
            memcpy(d->frame->data[0] + y * d->frame->linesize[0], src.constScanLine(y),
                   size_t(d->size.width()));
        }
        for (int p = 1; p <= 2; ++p) {
            for (int y = 0; y < (d->size.height() + 1) / 2; ++y)
                memset(d->frame->data[p] + y * d->frame->linesize[p], 128,
                       size_t((d->size.width() + 1) / 2));
        }
    } else {
        const uint8_t *planes[1] = {src.constBits()};
        const int strides[1] = {int(src.bytesPerLine())};
        sws_scale(d->sws, planes, strides, 0, d->size.height(), d->frame->data,
                  d->frame->linesize);
    }
    d->frame->pts = d->nextPts++;
    d->frame->duration = 1; // one tick of the encoder time base

    if (avcodec_send_frame(d->ctx, d->frame) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Matte encoder rejected a frame");
        return false;
    }
    return d->drainPackets(errorOut);
}

bool MatteWriter::finish(QString *errorOut)
{
    if (d->finished)
        return true;
    if (!d->ctx || !d->fmt) {
        if (errorOut)
            *errorOut = QStringLiteral("Matte writer is not open");
        return false;
    }

    if (avcodec_send_frame(d->ctx, nullptr) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not flush the matte encoder");
        return false;
    }
    if (!d->drainPackets(errorOut))
        return false;
    if (av_write_trailer(d->fmt) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write the matte trailer");
        return false;
    }

    d->teardown();

    if (QFile::exists(d->path))
        QFile::remove(d->path);
    if (!QFile::rename(d->tmpPath, d->path)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not move the matte into place");
        return false;
    }

    d->finished = true;
    return true;
}

void MatteWriter::abort()
{
    if (d->finished)
        return;
    d->teardown();
    if (!d->tmpPath.isEmpty() && QFile::exists(d->tmpPath))
        QFile::remove(d->tmpPath);
    d->tmpPath.clear();
}

QString matteCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("mattes"));
    QDir().mkpath(dir);
    return dir;
}

QString newMattePath()
{
    const QString dir = matteCacheDir();
    if (dir.isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QDir(dir).filePath(id + QStringLiteral(".mp4"));
}

} // namespace drift
