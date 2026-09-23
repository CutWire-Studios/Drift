#include "PreviewProxyRenderer.h"

#include "MediaProbe.h"
#include "ProxyEncoder.h"

#include <QCoreApplication>
#include <QSize>

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace drift {

namespace {

QSize proxySizeFor(int width, int height, int shortSide)
{
    const int sourceShort = std::min(width, height);
    if (sourceShort <= shortSide)
        return {width & ~1, height & ~1};
    const double scale = double(shortSide) / sourceShort;
    return {std::max(2, int(std::lround(width * scale / 2.0)) * 2),
            std::max(2, int(std::lround(height * scale / 2.0)) * 2)};
}

} // namespace

bool renderPreviewProxy(const QString &sourcePath, int shortSide, const QString &outPath,
                        QString *errorOut, const std::function<bool(double)> &onProgress)
{
    ProxySource source;
    if (!source.open(sourcePath, errorOut))
        return false;
    AVStream *stream = source.fmt->streams[source.stream];
    // The proxy is plain yuv420p; previewing a transparent clip opaque would be wrong, not slow.
    if (videoStreamHasAlpha(stream)) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Clips with transparency can't use a proxy");
        return false;
    }

    ProxyEncoder::Options options;
    options.size = proxySizeFor(source.dec->width, source.dec->height, shortSide);
    options.crf = "23";
    options.preset = "superfast";

    ProxyEncoder encoder;
    if (!encoder.open(outPath, source.dec, stream->time_base, source.frameRate(),
                      displayRotationOf(stream), options, errorOut))
        return false;

    const int64_t startTs = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    int64_t durationTs = stream->duration;
    if (durationTs <= 0 && source.fmt->duration > 0)
        durationTs = av_rescale_q(source.fmt->duration, {1, AV_TIME_BASE}, stream->time_base);

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    if (!frame || !packet) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        encoder.abort();
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Could not allocate decode buffers");
        return false;
    }

    bool ok = true;
    bool cancelled = false;
    bool wroteAny = false;
    auto receiveFrames = [&] {
        while (ok) {
            const int rc = avcodec_receive_frame(source.dec, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return;
            if (rc < 0)
                return;
            int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                              ? frame->best_effort_timestamp
                              : frame->pts;
            if (pts == AV_NOPTS_VALUE) {
                av_frame_unref(frame);
                continue;
            }
            pts -= startTs;
            ok = encoder.writeFrame(frame, pts, errorOut);
            av_frame_unref(frame);
            wroteAny = true;
            if (ok && onProgress && durationTs > 0
                && !onProgress(std::clamp(double(pts) / double(durationTs), 0.0, 1.0))) {
                cancelled = true;
                ok = false;
            }
        }
    };

    while (ok) {
        if (av_read_frame(source.fmt, packet) < 0)
            break;
        if (packet->stream_index != source.stream) {
            av_packet_unref(packet);
            continue;
        }
        const int rc = avcodec_send_packet(source.dec, packet);
        av_packet_unref(packet);
        if (rc < 0 && rc != AVERROR(EAGAIN))
            continue;
        receiveFrames();
    }
    // A frame-threaded decoder still holds several frames after the last packet.
    if (ok) {
        avcodec_send_packet(source.dec, nullptr);
        receiveFrames();
    }

    av_frame_free(&frame);
    av_packet_free(&packet);

    if (!ok || !wroteAny) {
        encoder.abort();
        if (errorOut && cancelled)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Proxy cancelled");
        else if (errorOut && !wroteAny)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "No frames could be decoded from this clip");
        return false;
    }
    if (!encoder.finish(errorOut))
        return false;
    if (onProgress)
        onProgress(1.0);
    return true;
}

} // namespace drift
