#include "ReverseRenderer.h"

#include "MediaProbe.h"

#include <QCoreApplication>
#include <QFile>

#include <algorithm>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace drift {

namespace {

// Short GOP rather than all-intra: the proxy is read forwards during playback, so a keyframe
// every twelve frames is enough to keep scrubbing cheap at roughly a third of the size.
constexpr int kProxyGopSize = 12;

// Writes the reversed frames. Structured like MatteWriter (open / writeFrame / finish / abort with
// the same .part-then-rename discipline) rather than reusing Exporter::run, which is a single
// goto-cleanup function and does not compose.
class ProxyEncoder
{
public:
    ~ProxyEncoder() { abort(); }

    bool open(const QString &path, const AVCodecContext *dec, AVRational timeBase,
              AVRational frameRate, int rotationDegrees, QString *errorOut);
    bool writeFrame(const AVFrame *src, int64_t pts, QString *errorOut);
    bool finish(QString *errorOut);
    void abort();

private:
    bool drainPackets(QString *errorOut);
    void teardown();

    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_ctx = nullptr;
    AVStream *m_stream = nullptr;
    AVFrame *m_frame = nullptr;
    AVPacket *m_pkt = nullptr;
    SwsContext *m_sws = nullptr;
    QString m_path;
    QString m_tmpPath;
    int64_t m_lastPts = INT64_MIN;
    bool m_finished = false;
};

bool ProxyEncoder::open(const QString &path, const AVCodecContext *dec, AVRational timeBase,
                        AVRational frameRate, int rotationDegrees, QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        teardown();
        return false;
    };

    m_path = path;
    m_tmpPath = path + QStringLiteral(".part");
    if (QFile::exists(m_tmpPath))
        QFile::remove(m_tmpPath);

    const QByteArray tmpUtf8 = m_tmpPath.toUtf8();
    avformat_alloc_output_context2(&m_fmt, nullptr, "mp4", tmpUtf8.constData());
    if (!m_fmt)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not create the reversed container"));

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        return fail(QCoreApplication::translate("ReverseRenderer", "H.264 encoder not available"));

    m_stream = avformat_new_stream(m_fmt, nullptr);
    if (!m_stream)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not create the reversed stream"));

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not allocate the reversed encoder"));

    // Source resolution, not preview resolution: export goes through the same FrameCompositor, so
    // preview and export have to read identical pixels out of the proxy.
    m_ctx->width = dec->width;
    m_ctx->height = dec->height;
    m_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_ctx->time_base = timeBase;
    m_ctx->framerate = frameRate;
    m_ctx->gop_size = kProxyGopSize;
    // No reorder delay, so a seek into the proxy produces a frame immediately.
    m_ctx->max_b_frames = 0;
    // Carried across or the proxy comes back colour-shifted against the source it replaces.
    m_ctx->color_range = dec->color_range;
    m_ctx->colorspace = dec->colorspace;
    m_ctx->color_primaries = dec->color_primaries;
    m_ctx->color_trc = dec->color_trc;
    if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER)
        m_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(m_ctx->priv_data, "crf", "16", 0);
    av_opt_set(m_ctx->priv_data, "preset", "veryfast", 0);
    av_opt_set(m_ctx->priv_data, "tune", "fastdecode", 0);

    if (avcodec_open2(m_ctx, codec, nullptr) < 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not open the reversed encoder"));

    avcodec_parameters_from_context(m_stream->codecpar, m_ctx);
    // The proxy keeps the source's pixels untouched, so it has to keep the source's
    // display matrix too — otherwise a rotated clip decodes upright from the original
    // and sideways from its reversed proxy. _set takes a clockwise angle while _get
    // (behind displayRotationOf) reports counterclockwise, so this passes it unnegated.
    if (rotationDegrees != 0) {
        AVPacketSideData *sd = av_packet_side_data_new(&m_stream->codecpar->coded_side_data,
                                                       &m_stream->codecpar->nb_coded_side_data,
                                                       AV_PKT_DATA_DISPLAYMATRIX,
                                                       sizeof(int32_t) * 9, 0);
        if (sd)
            av_display_rotation_set(reinterpret_cast<int32_t *>(sd->data), rotationDegrees);
    }
    m_stream->time_base = m_ctx->time_base;
    // Without this the muxer infers the rate from packet timestamps and lands on 29.97 for a
    // 30 fps source, the same trap MatteWriter documents.
    m_stream->avg_frame_rate = frameRate;
    m_stream->r_frame_rate = frameRate;

    if (!(m_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_fmt->pb, tmpUtf8.constData(), AVIO_FLAG_WRITE) < 0)
            return fail(QCoreApplication::translate("ReverseRenderer", "Could not open the reversed file for writing"));
    }
    if (avformat_write_header(m_fmt, nullptr) < 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not write the reversed header"));

    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    if (!m_pkt || !m_frame)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not allocate reversed frame buffers"));

    m_frame->format = AV_PIX_FMT_YUV420P;
    m_frame->width = m_ctx->width;
    m_frame->height = m_ctx->height;
    if (av_frame_get_buffer(m_frame, 0) < 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not allocate the reversed frame"));

    return true;
}

bool ProxyEncoder::writeFrame(const AVFrame *src, int64_t pts, QString *errorOut)
{
    if (!m_ctx || !m_frame) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ReverseRenderer", "Reversed writer is not open");
        return false;
    }

    m_sws = sws_getCachedContext(m_sws, src->width, src->height,
                                 static_cast<AVPixelFormat>(src->format), m_ctx->width,
                                 m_ctx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr,
                                 nullptr);
    if (!m_sws) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ReverseRenderer", "Could not convert a frame for the reversed encoder");
        return false;
    }
    if (av_frame_make_writable(m_frame) < 0) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ReverseRenderer", "Could not make the reversed frame writable");
        return false;
    }
    sws_scale(m_sws, src->data, src->linesize, 0, src->height, m_frame->data, m_frame->linesize);

    // x264 rejects a non-advancing timestamp. Sources with duplicate timestamps are rare but do
    // exist, and one of them must not abort a render that is otherwise fine.
    if (pts <= m_lastPts)
        pts = m_lastPts + 1;
    m_lastPts = pts;
    m_frame->pts = pts;

    if (avcodec_send_frame(m_ctx, m_frame) < 0) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ReverseRenderer", "Reversed encoder rejected a frame");
        return false;
    }
    return drainPackets(errorOut);
}

bool ProxyEncoder::drainPackets(QString *errorOut)
{
    for (;;) {
        const int rc = avcodec_receive_packet(m_ctx, m_pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (errorOut)
                *errorOut = QCoreApplication::translate("ReverseRenderer", "Failed to read an encoded reversed packet");
            return false;
        }
        av_packet_rescale_ts(m_pkt, m_ctx->time_base, m_stream->time_base);
        m_pkt->stream_index = m_stream->index;
        const int wrc = av_interleaved_write_frame(m_fmt, m_pkt);
        av_packet_unref(m_pkt);
        if (wrc < 0) {
            if (errorOut)
                *errorOut = QCoreApplication::translate("ReverseRenderer", "Failed to write a reversed packet");
            return false;
        }
    }
}

bool ProxyEncoder::finish(QString *errorOut)
{
    if (m_finished)
        return true;
    if (!m_ctx || !m_fmt) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ReverseRenderer", "Reversed writer is not open");
        return false;
    }

    if (avcodec_send_frame(m_ctx, nullptr) < 0) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ReverseRenderer", "Could not flush the reversed encoder");
        return false;
    }
    if (!drainPackets(errorOut))
        return false;
    if (av_write_trailer(m_fmt) < 0) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ReverseRenderer", "Could not write the reversed trailer");
        return false;
    }

    teardown();

    if (QFile::exists(m_path))
        QFile::remove(m_path);
    if (!QFile::rename(m_tmpPath, m_path)) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ReverseRenderer", "Could not move the reversed clip into place");
        return false;
    }

    m_finished = true;
    return true;
}

void ProxyEncoder::abort()
{
    if (m_finished)
        return;
    teardown();
    if (!m_tmpPath.isEmpty() && QFile::exists(m_tmpPath))
        QFile::remove(m_tmpPath);
    m_tmpPath.clear();
}

void ProxyEncoder::teardown()
{
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    if (m_frame)
        av_frame_free(&m_frame);
    if (m_pkt)
        av_packet_free(&m_pkt);
    if (m_ctx)
        avcodec_free_context(&m_ctx);
    if (m_fmt) {
        if (m_fmt->pb && !(m_fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&m_fmt->pb);
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
    }
    m_stream = nullptr;
}

// Owns the demuxer and decoder for the source being reversed. Its own context, not one from
// ClipReaderPool, so preview playback keeps running off the live path while a render is going.
struct Source
{
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    int stream = -1;

    ~Source()
    {
        if (dec)
            avcodec_free_context(&dec);
        if (fmt)
            avformat_close_input(&fmt);
    }
};

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

    Source source;
    const QByteArray pathUtf8 = sourcePath.toUtf8();
    if (avformat_open_input(&source.fmt, pathUtf8.constData(), nullptr, nullptr) < 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not open the clip"));
    if (avformat_find_stream_info(source.fmt, nullptr) < 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not read the clip's streams"));

    source.stream = av_find_best_stream(source.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (source.stream < 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "The clip has no video to reverse"));

    AVStream *stream = source.fmt->streams[source.stream];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec)
        return fail(QCoreApplication::translate("ReverseRenderer", "No decoder for this clip"));

    source.dec = avcodec_alloc_context3(codec);
    if (!source.dec)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not allocate the decoder"));
    if (avcodec_parameters_to_context(source.dec, stream->codecpar) < 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not configure the decoder"));
    source.dec->thread_count = 0; // let libavcodec pick; this is a batch job, not playback
    if (avcodec_open2(source.dec, codec, nullptr) < 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "Could not open the decoder"));
    if (source.dec->width <= 0 || source.dec->height <= 0)
        return fail(QCoreApplication::translate("ReverseRenderer", "The clip has no usable video size"));

    const AVRational timeBase = stream->time_base;
    const int64_t coverInTs = av_rescale_q(coverInUs, {1, AV_TIME_BASE}, timeBase);
    const int64_t coverOutTs = av_rescale_q(coverOutUs, {1, AV_TIME_BASE}, timeBase);

    AVRational frameRate = stream->avg_frame_rate;
    if (frameRate.num <= 0 || frameRate.den <= 0)
        frameRate = stream->r_frame_rate;
    if (frameRate.num <= 0 || frameRate.den <= 0)
        frameRate = AVRational{30, 1};

    // How many decoded frames the byte budget allows in one batch. In the common case a whole GOP
    // fits and every GOP is decoded exactly once; a GOP larger than the budget gets re-decoded per
    // sub-batch, which is bounded and a one-time render cost rather than a per-frame playback one.
    const int frameBytes = std::max(
        1, av_image_get_buffer_size(source.dec->pix_fmt, source.dec->width, source.dec->height, 32));
    const int maxBatch = int(std::max<qint64>(1, kReverseBatchByteBudget / frameBytes));

    ProxyEncoder encoder;
    if (!encoder.open(outPath, source.dec, timeBase, frameRate, displayRotationOf(stream), errorOut))
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
