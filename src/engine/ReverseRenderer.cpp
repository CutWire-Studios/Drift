#include "ReverseRenderer.h"

#include "MediaProbe.h"
#include "ProxyEncoder.h"

#include <QCoreApplication>
#include <QFile>

#include <algorithm>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}

namespace drift {

namespace {

void freeBatch(std::vector<AVFrame *> &batch)
{
    for (AVFrame *frame : batch)
        av_frame_free(&frame);
    batch.clear();
}

} // namespace

bool renderReversed(const QString &sourcePath, TimeUs coverInUs, TimeUs coverOutUs,
                    const QString &outPath, QString *errorOut,
                    const std::function<bool(double)> &onProgress)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    if (coverOutUs <= coverInUs)
        return fail(QCoreApplication::translate("ReverseRenderer", "Nothing to reverse"));

    ProxySource source;
    if (!source.open(sourcePath, errorOut))
        return false;
    AVStream *stream = source.fmt->streams[source.stream];

    const AVRational timeBase = stream->time_base;
    const int64_t coverInTs = av_rescale_q(coverInUs, {1, AV_TIME_BASE}, timeBase);
    const int64_t coverOutTs = av_rescale_q(coverOutUs, {1, AV_TIME_BASE}, timeBase);

    const AVRational frameRate = source.frameRate();

    // How many decoded frames the byte budget allows in one batch. In the common case a whole GOP
    // fits and every GOP is decoded exactly once; a GOP larger than the budget gets re-decoded per
    // sub-batch, which is bounded and a one-time render cost rather than a per-frame playback one.
    const int frameBytes = std::max(
        1, av_image_get_buffer_size(source.dec->pix_fmt, source.dec->width, source.dec->height, 32));
    const int maxBatch = int(std::max<qint64>(1, kReverseBatchByteBudget / frameBytes));

    ProxyEncoder encoder;
    // Source resolution, not preview resolution: export goes through the same FrameCompositor, so
    // preview and export have to read identical pixels out of the proxy.
    if (!encoder.open(outPath, source.dec, timeBase, frameRate, displayRotationOf(stream), {},
                      errorOut))
        return false;

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    std::vector<AVFrame *> batch;
    if (!frame || !packet) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        encoder.abort();
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not allocate decode buffers"));
    }

    // Seek to the keyframe at or before T, decode forward, and keep the frames in
    // [coverInTs, T] nearest to T that fit the budget.
    auto decodeBatch = [&](int64_t upperTs) -> bool {
        freeBatch(batch);

        if (av_seek_frame(source.fmt, source.stream, upperTs, AVSEEK_FLAG_BACKWARD) < 0) {
            // An upper bound past the end of the file lands here on some demuxers. Starting from
            // the bottom of the range still collects the right frames, just with more decoding.
            if (av_seek_frame(source.fmt, source.stream, coverInTs, AVSEEK_FLAG_BACKWARD) < 0)
                return false;
        }
        avcodec_flush_buffers(source.dec);

        bool done = false;
        auto receiveFrames = [&] {
            while (!done) {
                const int rc = avcodec_receive_frame(source.dec, frame);
                if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                    break;
                if (rc < 0) {
                    av_frame_unref(frame);
                    done = true;
                    break;
                }

                const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                        ? frame->best_effort_timestamp
                                        : frame->pts;
                if (pts == AV_NOPTS_VALUE) {
                    av_frame_unref(frame);
                    continue;
                }
                if (pts > upperTs) {
                    av_frame_unref(frame);
                    done = true;
                    break;
                }
                if (pts >= coverInTs) {
                    AVFrame *kept = av_frame_alloc();
                    if (kept && av_frame_ref(kept, frame) == 0) {
                        kept->pts = pts;
                        batch.push_back(kept);
                        // Over budget: drop the earliest frame. The next batch picks up from
                        // where the surviving run starts, so nothing is lost.
                        if (int(batch.size()) > maxBatch) {
                            av_frame_free(&batch.front());
                            batch.erase(batch.begin());
                        }
                    } else {
                        av_frame_free(&kept);
                    }
                }
                av_frame_unref(frame);
            }
        };

        bool eof = false;
        while (!done) {
            if (av_read_frame(source.fmt, packet) < 0) {
                eof = true;
                break;
            }
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

        // A frame-threaded decoder still holds several frames after the last packet, so running
        // out of packets is not running out of frames. The first batch is exactly the one that
        // reaches the end of the file, so without this drain the clip's tail never gets written.
        if (eof && !done) {
            avcodec_send_packet(source.dec, nullptr);
            receiveFrames();
        }
        return true;
    };

    const auto cleanup = [&] {
        freeBatch(batch);
        av_frame_free(&frame);
        av_packet_free(&packet);
    };

    const double spanTs = double(coverOutTs - coverInTs);
    bool wroteAny = false;
    int64_t upperTs = coverOutTs;
    while (upperTs >= coverInTs) {
        if (!decodeBatch(upperTs))
            break;
        if (batch.empty())
            break;

        // Descending source order is what makes the file play backwards; each frame keeps the
        // exact mirror of its own timestamp, so the mapping holds on variable-rate sources.
        for (int i = int(batch.size()) - 1; i >= 0; --i) {
            if (!encoder.writeFrame(batch[i], coverOutTs - batch[i]->pts, errorOut)) {
                cleanup();
                encoder.abort();
                return false;
            }
        }
        wroteAny = true;

        upperTs = batch.front()->pts - 1;

        if (onProgress) {
            const double done = spanTs > 0.0 ? double(coverOutTs - upperTs) / spanTs : 1.0;
            if (!onProgress(std::clamp(done, 0.0, 1.0))) {
                cleanup();
                encoder.abort();
                return fail(QCoreApplication::translate("ReverseRenderer", "Reversing cancelled"));
            }
        }
    }

    cleanup();

    if (!wroteAny) {
        encoder.abort();
        return fail(QCoreApplication::translate("ReverseRenderer", "No frames could be decoded from this clip"));
    }
    if (!encoder.finish(errorOut))
        return false;

    if (onProgress)
        onProgress(1.0);
    return true;
}

} // namespace drift
