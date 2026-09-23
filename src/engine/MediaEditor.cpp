#include "MediaEditor.h"

#include "AudioFileWriter.h"
#include "MediaProbe.h"
#include "core/Time.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include "StillImage.h"

#include <QStandardPaths>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

namespace drift {

namespace {

QString trEdit(const char *text)
{
    return QCoreApplication::translate("MediaEditor", text);
}

bool cropIsFull(double x, double y, double w, double h)
{
    return x <= 0.001 && y <= 0.001 && w >= 0.999 && h >= 0.999;
}

// Display-normalized crop → even-sized pixel rect, so yuv420 encoders accept it.
QRect pixelCropRect(int width, int height, double x, double y, double w, double h)
{
    if (width <= 0 || height <= 0)
        return {};
    x = std::clamp(x, 0.0, 1.0);
    y = std::clamp(y, 0.0, 1.0);
    w = std::clamp(w, 0.0, 1.0 - x);
    h = std::clamp(h, 0.0, 1.0 - y);
    if (w <= 0.0 || h <= 0.0)
        return {};

    int px = static_cast<int>(std::floor(x * width));
    int py = static_cast<int>(std::floor(y * height));
    int pw = static_cast<int>(std::ceil(w * width));
    int ph = static_cast<int>(std::ceil(h * height));
    px = std::clamp(px, 0, width - 2);
    py = std::clamp(py, 0, height - 2);
    pw = std::clamp(pw, 2, width - px);
    ph = std::clamp(ph, 2, height - py);
    px &= ~1;
    py &= ~1;
    pw &= ~1;
    ph &= ~1;
    if (pw < 2)
        pw = 2;
    if (ph < 2)
        ph = 2;
    if (px + pw > width)
        pw = (width - px) & ~1;
    if (py + ph > height)
        ph = (height - py) & ~1;
    if (pw < 2 || ph < 2)
        return {};
    return QRect(px, py, pw, ph);
}

QImage applyCrop(const QImage &source, double x, double y, double w, double h)
{
    if (source.isNull())
        return {};
    if (cropIsFull(x, y, w, h))
        return source;
    const QRect rect = pixelCropRect(source.width(), source.height(), x, y, w, h);
    if (!rect.isValid())
        return {};
    return source.copy(rect);
}

bool cancelled(const std::function<bool(double)> &onProgress, double fraction)
{
    return onProgress && !onProgress(std::clamp(fraction, 0.0, 1.0));
}

bool editImage(const MediaEditSpec &spec, QString *errorOut,
               const std::function<bool(double)> &onProgress)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    if (cancelled(onProgress, 0.0))
        return fail(trEdit("Cancelled"));

    const QImage image = drift::decodeStillImage(spec.inputPath);
    if (image.isNull())
        return fail(trEdit("Could not read that image"));

    if (cancelled(onProgress, 0.4))
        return fail(trEdit("Cancelled"));

    const QImage cropped = applyCrop(image, spec.cropX, spec.cropY, spec.cropW, spec.cropH);
    if (cropped.isNull())
        return fail(trEdit("The crop left nothing to save"));

    const QString tmp = spec.outputPath + QStringLiteral(".part");
    if (QFile::exists(tmp))
        QFile::remove(tmp);
    if (!cropped.save(tmp, "PNG")) {
        QFile::remove(tmp);
        return fail(trEdit("Could not write the cropped image"));
    }
    if (QFile::exists(spec.outputPath))
        QFile::remove(spec.outputPath);
    if (!QFile::rename(tmp, spec.outputPath)) {
        QFile::remove(tmp);
        return fail(trEdit("Could not move the cropped image into place"));
    }
    if (onProgress)
        onProgress(1.0);
    return true;
}

int64_t framePtsUs(const AVFrame *frame, AVRational timeBase)
{
    const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                            ? frame->best_effort_timestamp
                            : frame->pts;
    if (pts == AV_NOPTS_VALUE)
        return 0;
    return av_rescale_q(pts, timeBase, {1, AV_TIME_BASE});
}

// The nearest broadcast rate to `rate`, for conforming a variable-rate recording. Anything that
// isn't close to one (a 120 fps slow-motion clip) keeps its own rate, rounded to a whole number.
AVRational standardFrameRate(AVRational rate)
{
    static constexpr AVRational kRates[] = {{24000, 1001}, {24, 1}, {25, 1}, {30000, 1001},
                                            {30, 1},       {50, 1}, {60000, 1001}, {60, 1}};
    const double fps = av_q2d(rate);
    AVRational best = kRates[0];
    double bestDiff = std::numeric_limits<double>::max();
    for (const AVRational candidate : kRates) {
        const double diff = std::abs(av_q2d(candidate) - fps);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = candidate;
        }
    }
    if (bestDiff / fps <= 0.03)
        return best;
    return AVRational{std::max(1, int(std::lround(fps))), 1};
}

int sourceBitDepth(const AVCodecContext *dec)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(dec->pix_fmt);
    return desc ? desc->comp[0].depth : 8;
}

bool x264Supports(AVPixelFormat format)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    const void *configs = nullptr;
    if (!codec
        || avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs,
                                        nullptr)
               < 0
        || !configs)
        return false;
    for (auto *p = static_cast<const AVPixelFormat *>(configs); *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == format)
            return true;
    }
    return false;
}

// buffer (source frames, microsecond timestamps) -> `chain` -> buffersink.
bool buildFilterGraph(const AVCodecContext *dec, const AVStream *stream, const QString &chain,
                      AVFilterGraph **graphOut, AVFilterContext **srcOut, AVFilterContext **sinkOut)
{
    AVFilterGraph *graph = avfilter_graph_alloc();
    if (!graph)
        return false;

    AVRational sar = dec->sample_aspect_ratio;
    if (sar.num <= 0 || sar.den <= 0)
        sar = stream->sample_aspect_ratio;
    if (sar.num <= 0 || sar.den <= 0)
        sar = AVRational{1, 1};
    const QByteArray args =
        QStringLiteral("video_size=%1x%2:pix_fmt=%3:time_base=1/%4:pixel_aspect=%5/%6"
                       ":colorspace=%7:range=%8")
            .arg(dec->width)
            .arg(dec->height)
            .arg(int(dec->pix_fmt))
            .arg(AV_TIME_BASE)
            .arg(sar.num)
            .arg(sar.den)
            .arg(int(dec->colorspace))
            .arg(int(dec->color_range))
            .toUtf8();

    AVFilterContext *src = nullptr;
    AVFilterContext *sink = nullptr;
    AVFilterInOut *outputs = nullptr;
    AVFilterInOut *inputs = nullptr;
    bool ok = avfilter_graph_create_filter(&src, avfilter_get_by_name("buffer"), "in",
                                           args.constData(), nullptr, graph)
            >= 0
        && avfilter_graph_create_filter(&sink, avfilter_get_by_name("buffersink"), "out", nullptr,
                                        nullptr, graph)
            >= 0;
    if (ok) {
        outputs = avfilter_inout_alloc();
        inputs = avfilter_inout_alloc();
        ok = outputs && inputs;
    }
    if (ok) {
        outputs->name = av_strdup("in");
        outputs->filter_ctx = src;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink;
        const QByteArray chainUtf8 = chain.toUtf8();
        ok = avfilter_graph_parse_ptr(graph, chainUtf8.constData(), &inputs, &outputs, nullptr) >= 0
            && avfilter_graph_config(graph, nullptr) >= 0;
    }
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (!ok) {
        avfilter_graph_free(&graph);
        return false;
    }
    *graphOut = graph;
    *srcOut = src;
    *sinkOut = sink;
    return true;
}

bool editAudio(const MediaEditSpec &spec, QString *errorOut,
               const std::function<bool(double)> &onProgress)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    AVFormatContext *fmt = nullptr;
    const QByteArray pathUtf8 = spec.inputPath.toUtf8();
    if (avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr) < 0)
        return fail(trEdit("Could not open the audio"));
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return fail(trEdit("Could not read the audio"));
    }

    const int streamIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (streamIndex < 0) {
        avformat_close_input(&fmt);
        return fail(trEdit("That file has no audio"));
    }

    AVStream *stream = fmt->streams[streamIndex];
    const AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext *dec = decoder ? avcodec_alloc_context3(decoder) : nullptr;
    if (!dec || avcodec_parameters_to_context(dec, stream->codecpar) < 0
        || avcodec_open2(dec, decoder, nullptr) < 0) {
        if (dec)
            avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return fail(trEdit("Could not decode the audio"));
    }

    const int sampleRate = dec->sample_rate > 0 ? dec->sample_rate : stream->codecpar->sample_rate;
    const int channels = 2;
    SwrContext *swr = nullptr;
    AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, sampleRate, &dec->ch_layout,
                            static_cast<AVSampleFormat>(dec->sample_fmt), dec->sample_rate, 0,
                            nullptr)
            < 0
        || swr_init(swr) < 0) {
        if (swr)
            swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return fail(trEdit("Could not convert the audio"));
    }

    const TimeUs inUs = secondsToUs(std::max(0.0, spec.inSeconds));
    TimeUs outUs = spec.outSeconds < 0 ? 0 : secondsToUs(spec.outSeconds);
    const TimeUs durationUs = fmt->duration > 0 ? fmt->duration : stream->duration > 0
                                ? av_rescale_q(stream->duration, stream->time_base, {1, AV_TIME_BASE})
                                : 0;
    if (outUs <= 0)
        outUs = durationUs > 0 ? durationUs : std::numeric_limits<TimeUs>::max();
    if (outUs <= inUs) {
        swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return fail(trEdit("Nothing to keep"));
    }

    if (inUs > 0) {
        const int64_t ts = av_rescale_q(inUs, {1, AV_TIME_BASE}, stream->time_base);
        av_seek_frame(fmt, streamIndex, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);
    }

    AudioFileWriter writer;
    if (!writer.open(spec.outputPath, sampleRate, channels, errorOut)) {
        swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return false;
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    std::vector<float> scratch;
    bool ok = true;
    bool sentFlush = false;
    int64_t samplesWritten = 0;
    const int64_t skipSamples = (inUs * sampleRate) / kUsPerSecond;
    const int64_t keepSamples = outUs == std::numeric_limits<TimeUs>::max()
                                    ? std::numeric_limits<int64_t>::max()
                                    : std::max<int64_t>(1, ((outUs - inUs) * sampleRate) / kUsPerSecond);
    int64_t decodedSamples = 0;

    auto cleanup = [&] {
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
    };

    while (ok && samplesWritten < keepSamples) {
        const int rc = avcodec_receive_frame(dec, frame);
        if (rc == AVERROR(EAGAIN)) {
            if (sentFlush)
                break;
            if (av_read_frame(fmt, packet) < 0) {
                avcodec_send_packet(dec, nullptr);
                sentFlush = true;
                continue;
            }
            if (packet->stream_index != streamIndex) {
                av_packet_unref(packet);
                continue;
            }
            avcodec_send_packet(dec, packet);
            av_packet_unref(packet);
            continue;
        }
        if (rc < 0)
            break;

        const int maxOut = swr_get_out_samples(swr, frame->nb_samples);
        scratch.resize(std::max(0, maxOut) * channels);
        uint8_t *outData[1] = {reinterpret_cast<uint8_t *>(scratch.data())};
        const int converted = swr_convert(swr, outData, maxOut,
                                          const_cast<const uint8_t **>(frame->data), frame->nb_samples);
        av_frame_unref(frame);
        if (converted <= 0)
            continue;

        int offset = 0;
        int count = converted;
        if (decodedSamples + count <= skipSamples) {
            decodedSamples += count;
            continue;
        }
        if (decodedSamples < skipSamples) {
            offset = static_cast<int>(skipSamples - decodedSamples);
            count -= offset;
            decodedSamples = skipSamples;
        } else {
            decodedSamples += converted;
        }
        if (samplesWritten + count > keepSamples)
            count = static_cast<int>(keepSamples - samplesWritten);
        if (count <= 0)
            continue;

        if (!writer.writeFrames(scratch.data() + offset * channels, count, errorOut)) {
            ok = false;
            break;
        }
        samplesWritten += count;
        const double span = static_cast<double>(keepSamples);
        if (cancelled(onProgress, span > 0 ? static_cast<double>(samplesWritten) / span : 1.0)) {
            if (errorOut)
                *errorOut = trEdit("Cancelled");
            ok = false;
            break;
        }
    }

    if (ok)
        ok = writer.finish(errorOut);
    if (!ok)
        writer.abort();
    cleanup();
    return ok && samplesWritten > 0 ? true
                                    : (ok ? fail(trEdit("Nothing to keep")) : false);
}

class Mp4Writer
{
public:
    ~Mp4Writer() { abort(); }

    // Size, pixel format, aspect and colour tags all come from the first frame the filter graph
    // produced, so they describe exactly what gets encoded.
    bool open(const QString &path, const AVFrame *firstVideo, AVRational frameRate,
              bool withAudio, int audioRate, QString *errorOut);
    bool isOpen() const { return m_fmt != nullptr; }
    // pts in 1/frameRate units.
    bool writeVideo(AVFrame *frame, QString *errorOut);
    bool writeAudio(const float *interleaved, int frames, QString *errorOut);
    bool finish(QString *errorOut);
    void abort();

    int audioRate() const { return m_audioRate; }

private:
    bool drainVideo(QString *errorOut);
    bool drainAudio(QString *errorOut);
    bool encodeAudioFrame(AVFrame *frame, QString *errorOut);
    void teardown();

    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_videoCtx = nullptr;
    AVCodecContext *m_audioCtx = nullptr;
    AVStream *m_videoStream = nullptr;
    AVStream *m_audioStream = nullptr;
    AVFrame *m_audioFrame = nullptr;
    AVPacket *m_pkt = nullptr;
    AVAudioFifo *m_fifo = nullptr;
    QString m_path;
    QString m_tmpPath;
    int64_t m_lastVideoPts = INT64_MIN;
    int64_t m_audioPts = 0;
    int m_audioRate = 48000;
    bool m_finished = false;
};

bool Mp4Writer::open(const QString &path, const AVFrame *firstVideo, AVRational frameRate,
                     bool withAudio, int audioRate, QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        teardown();
        return false;
    };

    m_path = path;
    m_tmpPath = path + QStringLiteral(".part");
    m_audioRate = audioRate > 0 ? audioRate : 48000;
    if (QFile::exists(m_tmpPath))
        QFile::remove(m_tmpPath);

    const QByteArray tmpUtf8 = m_tmpPath.toUtf8();
    avformat_alloc_output_context2(&m_fmt, nullptr, "mp4", tmpUtf8.constData());
    if (!m_fmt)
        return fail(trEdit("Could not create the output file"));

    const AVCodec *vCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vCodec)
        return fail(trEdit("H.264 encoder not available"));

    m_videoStream = avformat_new_stream(m_fmt, nullptr);
    if (!m_videoStream)
        return fail(trEdit("Could not create the video stream"));

    m_videoCtx = avcodec_alloc_context3(vCodec);
    if (!m_videoCtx)
        return fail(trEdit("Could not allocate the video encoder"));

    if (frameRate.num <= 0 || frameRate.den <= 0)
        frameRate = AVRational{30, 1};

    m_videoCtx->width = firstVideo->width;
    m_videoCtx->height = firstVideo->height;
    m_videoCtx->pix_fmt = static_cast<AVPixelFormat>(firstVideo->format);
    m_videoCtx->sample_aspect_ratio = firstVideo->sample_aspect_ratio;
    m_videoCtx->color_range = firstVideo->color_range;
    m_videoCtx->colorspace = firstVideo->colorspace;
    m_videoCtx->color_primaries = firstVideo->color_primaries;
    m_videoCtx->color_trc = firstVideo->color_trc;
    m_videoCtx->time_base = AVRational{frameRate.den, frameRate.num};
    m_videoCtx->framerate = frameRate;
    // The result is timeline media, so it keeps the short GOP that keeps scrubbing it cheap.
    m_videoCtx->gop_size = 12;
    m_videoCtx->max_b_frames = 0;
    if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER)
        m_videoCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    // It replaces the original in the project and is what export reads, so near-transparent.
    av_opt_set(m_videoCtx->priv_data, "crf", "16", 0);
    av_opt_set(m_videoCtx->priv_data, "preset", "fast", 0);

    if (avcodec_open2(m_videoCtx, vCodec, nullptr) < 0)
        return fail(trEdit("Could not open the video encoder"));
    avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCtx);
    m_videoStream->time_base = m_videoCtx->time_base;
    m_videoStream->sample_aspect_ratio = m_videoCtx->sample_aspect_ratio;
    m_videoStream->avg_frame_rate = frameRate;
    m_videoStream->r_frame_rate = frameRate;

    if (withAudio) {
        const AVCodec *aCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!aCodec)
            return fail(trEdit("AAC encoder not available"));
        m_audioStream = avformat_new_stream(m_fmt, nullptr);
        if (!m_audioStream)
            return fail(trEdit("Could not create the audio stream"));
        m_audioCtx = avcodec_alloc_context3(aCodec);
        if (!m_audioCtx)
            return fail(trEdit("Could not allocate the audio encoder"));
        m_audioCtx->sample_rate = m_audioRate;
        m_audioCtx->bit_rate = 192000;
        av_channel_layout_default(&m_audioCtx->ch_layout, 2);
        m_audioCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
        m_audioCtx->time_base = AVRational{1, m_audioRate};
        if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER)
            m_audioCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(m_audioCtx, aCodec, nullptr) < 0)
            return fail(trEdit("Could not open the audio encoder"));
        avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCtx);
        m_audioStream->time_base = m_audioCtx->time_base;
        m_fifo = av_audio_fifo_alloc(m_audioCtx->sample_fmt, m_audioCtx->ch_layout.nb_channels, 1024);
        if (!m_fifo)
            return fail(trEdit("Could not allocate the audio buffer"));
        m_audioFrame = av_frame_alloc();
        if (!m_audioFrame)
            return fail(trEdit("Could not allocate an audio frame"));
        m_audioFrame->format = m_audioCtx->sample_fmt;
        m_audioFrame->ch_layout = m_audioCtx->ch_layout;
        m_audioFrame->sample_rate = m_audioCtx->sample_rate;
        m_audioFrame->nb_samples = m_audioCtx->frame_size > 0 ? m_audioCtx->frame_size : 1024;
        if (av_frame_get_buffer(m_audioFrame, 0) < 0)
            return fail(trEdit("Could not allocate audio frame buffers"));
    }

    if (!(m_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_fmt->pb, tmpUtf8.constData(), AVIO_FLAG_WRITE) < 0)
            return fail(trEdit("Could not open the output file for writing"));
    }
    if (avformat_write_header(m_fmt, nullptr) < 0)
        return fail(trEdit("Could not write the output header"));

    m_pkt = av_packet_alloc();
    if (!m_pkt)
        return fail(trEdit("Could not allocate encoder buffers"));
    return true;
}

bool Mp4Writer::drainVideo(QString *errorOut)
{
    for (;;) {
        const int rc = avcodec_receive_packet(m_videoCtx, m_pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (errorOut)
                *errorOut = trEdit("Failed to read an encoded video packet");
            return false;
        }
        av_packet_rescale_ts(m_pkt, m_videoCtx->time_base, m_videoStream->time_base);
        m_pkt->stream_index = m_videoStream->index;
        const int wrc = av_interleaved_write_frame(m_fmt, m_pkt);
        av_packet_unref(m_pkt);
        if (wrc < 0) {
            if (errorOut)
                *errorOut = trEdit("Failed to write a video packet");
            return false;
        }
    }
}

bool Mp4Writer::drainAudio(QString *errorOut)
{
    if (!m_audioCtx)
        return true;
    for (;;) {
        const int rc = avcodec_receive_packet(m_audioCtx, m_pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (errorOut)
                *errorOut = trEdit("Failed to read an encoded audio packet");
            return false;
        }
        av_packet_rescale_ts(m_pkt, m_audioCtx->time_base, m_audioStream->time_base);
        m_pkt->stream_index = m_audioStream->index;
        const int wrc = av_interleaved_write_frame(m_fmt, m_pkt);
        av_packet_unref(m_pkt);
        if (wrc < 0) {
            if (errorOut)
                *errorOut = trEdit("Failed to write an audio packet");
            return false;
        }
    }
}

bool Mp4Writer::writeVideo(AVFrame *frame, QString *errorOut)
{
    // x264 rejects a non-advancing timestamp; the fps filter never repeats one, but a stray
    // duplicate must not abort an otherwise fine edit.
    if (frame->pts <= m_lastVideoPts)
        frame->pts = m_lastVideoPts + 1;
    m_lastVideoPts = frame->pts;
    frame->pict_type = AV_PICTURE_TYPE_NONE;
    if (avcodec_send_frame(m_videoCtx, frame) < 0) {
        if (errorOut)
            *errorOut = trEdit("Video encoder rejected a frame");
        return false;
    }
    return drainVideo(errorOut);
}

bool Mp4Writer::encodeAudioFrame(AVFrame *frame, QString *errorOut)
{
    if (avcodec_send_frame(m_audioCtx, frame) < 0) {
        if (errorOut)
            *errorOut = trEdit("Audio encoder rejected a frame");
        return false;
    }
    return drainAudio(errorOut);
}

bool Mp4Writer::writeAudio(const float *interleaved, int frames, QString *errorOut)
{
    if (!m_audioCtx || frames <= 0)
        return true;

    // AAC wants planar float; de-interleave into the FIFO via a pair of planes.
    std::vector<float> left(static_cast<size_t>(frames));
    std::vector<float> right(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        left[static_cast<size_t>(i)] = interleaved[i * 2];
        right[static_cast<size_t>(i)] = interleaved[i * 2 + 1];
    }
    void *planes[2] = {left.data(), right.data()};
    if (av_audio_fifo_write(m_fifo, planes, frames) < frames) {
        if (errorOut)
            *errorOut = trEdit("Could not buffer audio");
        return false;
    }

    const int frameSize = m_audioFrame->nb_samples;
    while (av_audio_fifo_size(m_fifo) >= frameSize) {
        if (av_frame_make_writable(m_audioFrame) < 0) {
            if (errorOut)
                *errorOut = trEdit("Could not make the audio frame writable");
            return false;
        }
        void *dst[2] = {m_audioFrame->data[0], m_audioFrame->data[1]};
        if (av_audio_fifo_read(m_fifo, dst, frameSize) < frameSize) {
            if (errorOut)
                *errorOut = trEdit("Could not read buffered audio");
            return false;
        }
        m_audioFrame->pts = m_audioPts;
        m_audioPts += frameSize;
        if (!encodeAudioFrame(m_audioFrame, errorOut))
            return false;
    }
    return true;
}

bool Mp4Writer::finish(QString *errorOut)
{
    if (m_finished)
        return true;
    if (!m_fmt) {
        if (errorOut)
            *errorOut = trEdit("Writer is not open");
        return false;
    }

    if (m_audioCtx && m_fifo && av_audio_fifo_size(m_fifo) > 0) {
        const int leftover = av_audio_fifo_size(m_fifo);
        if (av_frame_make_writable(m_audioFrame) < 0) {
            if (errorOut)
                *errorOut = trEdit("Could not make the audio frame writable");
            return false;
        }
        av_samples_set_silence(m_audioFrame->data, 0, m_audioFrame->nb_samples,
                               m_audioCtx->ch_layout.nb_channels, m_audioCtx->sample_fmt);
        void *dst[2] = {m_audioFrame->data[0], m_audioFrame->data[1]};
        av_audio_fifo_read(m_fifo, dst, leftover);
        m_audioFrame->nb_samples = leftover;
        m_audioFrame->pts = m_audioPts;
        m_audioPts += leftover;
        if (!encodeAudioFrame(m_audioFrame, errorOut))
            return false;
        m_audioFrame->nb_samples = m_audioCtx->frame_size > 0 ? m_audioCtx->frame_size : 1024;
    }

    if (avcodec_send_frame(m_videoCtx, nullptr) < 0) {
        if (errorOut)
            *errorOut = trEdit("Could not flush the video encoder");
        return false;
    }
    if (!drainVideo(errorOut))
        return false;
    if (m_audioCtx) {
        if (avcodec_send_frame(m_audioCtx, nullptr) < 0) {
            if (errorOut)
                *errorOut = trEdit("Could not flush the audio encoder");
            return false;
        }
        if (!drainAudio(errorOut))
            return false;
    }
    if (av_write_trailer(m_fmt) < 0) {
        if (errorOut)
            *errorOut = trEdit("Could not write the output trailer");
        return false;
    }

    teardown();
    if (QFile::exists(m_path))
        QFile::remove(m_path);
    if (!QFile::rename(m_tmpPath, m_path)) {
        if (errorOut)
            *errorOut = trEdit("Could not move the edited file into place");
        return false;
    }
    m_finished = true;
    return true;
}

void Mp4Writer::abort()
{
    if (m_finished)
        return;
    teardown();
    if (!m_tmpPath.isEmpty() && QFile::exists(m_tmpPath))
        QFile::remove(m_tmpPath);
    m_tmpPath.clear();
}

void Mp4Writer::teardown()
{
    if (m_fifo) {
        av_audio_fifo_free(m_fifo);
        m_fifo = nullptr;
    }
    if (m_audioFrame)
        av_frame_free(&m_audioFrame);
    if (m_pkt)
        av_packet_free(&m_pkt);
    if (m_videoCtx)
        avcodec_free_context(&m_videoCtx);
    if (m_audioCtx)
        avcodec_free_context(&m_audioCtx);
    if (m_fmt) {
        if (m_fmt->pb && !(m_fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&m_fmt->pb);
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
    }
    m_videoStream = nullptr;
    m_audioStream = nullptr;
}

bool editVideo(const MediaEditSpec &spec, QString *errorOut,
               const std::function<bool(double)> &onProgress)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    AVFormatContext *fmt = nullptr;
    const QByteArray pathUtf8 = spec.inputPath.toUtf8();
    if (avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr) < 0)
        return fail(trEdit("Could not open the video"));
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return fail(trEdit("Could not read the video"));
    }

    const int videoIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex < 0) {
        avformat_close_input(&fmt);
        return editAudio(spec, errorOut, onProgress);
    }
    const int audioIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    AVStream *vStream = fmt->streams[videoIndex];
    const AVCodec *vDecCodec = avcodec_find_decoder(vStream->codecpar->codec_id);
    AVCodecContext *vDec = vDecCodec ? avcodec_alloc_context3(vDecCodec) : nullptr;
    if (!vDec || avcodec_parameters_to_context(vDec, vStream->codecpar) < 0
        || avcodec_open2(vDec, vDecCodec, nullptr) < 0) {
        if (vDec)
            avcodec_free_context(&vDec);
        avformat_close_input(&fmt);
        return fail(trEdit("Could not decode the video"));
    }
    vDec->thread_count = 0;
    if (vDec->width <= 0 || vDec->height <= 0) {
        avcodec_free_context(&vDec);
        avformat_close_input(&fmt);
        return fail(trEdit("The video has no usable size"));
    }

    const int rotation = spec.rotationOverride >= 0 ? spec.rotationOverride : displayRotationOf(vStream);
    int displayW = vDec->width;
    int displayH = vDec->height;
    if (rotation == 90 || rotation == 270)
        std::swap(displayW, displayH);
    QRect crop = cropIsFull(spec.cropX, spec.cropY, spec.cropW, spec.cropH)
        ? QRect()
        : pixelCropRect(displayW, displayH, spec.cropX, spec.cropY, spec.cropW, spec.cropH);
    // yuv420 needs even dimensions; a full frame with an odd edge loses that one row or column.
    if (!crop.isValid())
        crop = QRect(0, 0, displayW & ~1, displayH & ~1);
    if (crop.width() < 2 || crop.height() < 2) {
        avcodec_free_context(&vDec);
        avformat_close_input(&fmt);
        return fail(trEdit("The crop left nothing to save"));
    }

    AVRational frameRate = vStream->avg_frame_rate;
    if (frameRate.num <= 0 || frameRate.den <= 0)
        frameRate = vStream->r_frame_rate;
    if (frameRate.num <= 0 || frameRate.den <= 0)
        frameRate = AVRational{30, 1};
    if (spec.conformFrameRate) {
        // A recording that sagged in low light averages well under the rate it was shot at;
        // r_frame_rate is FFmpeg's read of the base rate the timestamps sit on, which is that one.
        // Only trusted in a plausible range — some muxers report the time base there.
        const AVRational base = vStream->r_frame_rate;
        if (base.num > 0 && base.den > 0 && av_q2d(base) <= 121.0
            && av_q2d(base) >= av_q2d(frameRate))
            frameRate = base;
        frameRate = standardFrameRate(frameRate);
    }

    const TimeUs inUs = secondsToUs(std::max(0.0, spec.inSeconds));
    // "Through the end" is unbounded rather than the container's duration: that figure is only
    // an estimate, and on a variable-rate file it can fall short of the last frame.
    const TimeUs outUs = spec.outSeconds < 0 ? std::numeric_limits<TimeUs>::max()
                                             : secondsToUs(spec.outSeconds);
    const TimeUs durationUs = fmt->duration > 0 ? fmt->duration
                                                : av_rescale_q(vStream->duration, vStream->time_base,
                                                               {1, AV_TIME_BASE});
    if (outUs <= inUs) {
        avcodec_free_context(&vDec);
        avformat_close_input(&fmt);
        return fail(trEdit("Nothing to keep"));
    }

    AVCodecContext *aDec = nullptr;
    SwrContext *swr = nullptr;
    if (audioIndex >= 0) {
        AVStream *aStream = fmt->streams[audioIndex];
        const AVCodec *aDecCodec = avcodec_find_decoder(aStream->codecpar->codec_id);
        aDec = aDecCodec ? avcodec_alloc_context3(aDecCodec) : nullptr;
        if (aDec && avcodec_parameters_to_context(aDec, aStream->codecpar) == 0
            && avcodec_open2(aDec, aDecCodec, nullptr) == 0) {
            AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
            if (swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_FLT, 48000, &aDec->ch_layout,
                                    static_cast<AVSampleFormat>(aDec->sample_fmt),
                                    aDec->sample_rate, 0, nullptr)
                    < 0
                || swr_init(swr) < 0) {
                if (swr)
                    swr_free(&swr);
                avcodec_free_context(&aDec);
                aDec = nullptr;
            }
        } else if (aDec) {
            avcodec_free_context(&aDec);
            aDec = nullptr;
        }
    }

    const bool tenBit = sourceBitDepth(vDec) > 8 && x264Supports(AV_PIX_FMT_YUV420P10LE);
    const AVPixelFormat outFormat = tenBit ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;

    AVFilterGraph *graph = nullptr;
    AVFilterContext *filterSrc = nullptr;
    AVFilterContext *filterSink = nullptr;
    {
        QStringList chain;
        if (rotation == 90)
            chain << QStringLiteral("transpose=clock");
        else if (rotation == 180)
            chain << QStringLiteral("hflip,vflip");
        else if (rotation == 270)
            chain << QStringLiteral("transpose=cclock");
        if (crop != QRect(0, 0, displayW, displayH))
            chain << QStringLiteral("crop=%1:%2:%3:%4:exact=1")
                         .arg(crop.width())
                         .arg(crop.height())
                         .arg(crop.x())
                         .arg(crop.y());
        // start_time=0 pads from the trim's in-point, so a first frame that lands late still
        // lines up with the audio's first sample.
        chain << QStringLiteral("fps=fps=%1/%2:start_time=0").arg(frameRate.num).arg(frameRate.den)
              << QStringLiteral("format=%1").arg(QString::fromLatin1(av_get_pix_fmt_name(outFormat)));
        if (!buildFilterGraph(vDec, vStream, chain.join(QLatin1Char(',')), &graph, &filterSrc,
                              &filterSink)) {
            if (swr)
                swr_free(&swr);
            if (aDec)
                avcodec_free_context(&aDec);
            avcodec_free_context(&vDec);
            avformat_close_input(&fmt);
            return fail(trEdit("Could not set up the video conversion"));
        }
    }

    Mp4Writer writer;

    if (inUs > 0) {
        const int64_t ts = av_rescale_q(inUs, {1, AV_TIME_BASE}, vStream->time_base);
        av_seek_frame(fmt, videoIndex, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(vDec);
        if (aDec)
            avcodec_flush_buffers(aDec);
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *filtered = av_frame_alloc();
    std::vector<float> pcm;
    // Audio decoded before the first filtered video frame opens the writer.
    std::vector<float> pendingPcm;
    bool ok = packet && frame && filtered;
    bool wroteVideo = false;
    const TimeUs spanUs = (outUs == std::numeric_limits<TimeUs>::max() ? durationUs : outUs) - inUs;

    auto cleanup = [&] {
        av_frame_free(&filtered);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avfilter_graph_free(&graph);
        if (swr)
            swr_free(&swr);
        if (aDec)
            avcodec_free_context(&aDec);
        avcodec_free_context(&vDec);
        avformat_close_input(&fmt);
    };

    auto writeAudio = [&](const float *interleaved, int count) -> bool {
        if (!writer.isOpen()) {
            pendingPcm.insert(pendingPcm.end(), interleaved, interleaved + count * 2);
            return true;
        }
        return writer.writeAudio(interleaved, count, errorOut);
    };

    auto drainFilter = [&]() -> bool {
        for (;;) {
            const int rc = av_buffersink_get_frame(filterSink, filtered);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0)
                return fail(trEdit("Could not convert a frame"));
            if (!writer.isOpen()) {
                // Primaries and transfer ride the stream, not every decoded frame; without them an
                // HDR source would come out tagged as SDR.
                if (filtered->color_primaries == AVCOL_PRI_UNSPECIFIED)
                    filtered->color_primaries = vDec->color_primaries;
                if (filtered->color_trc == AVCOL_TRC_UNSPECIFIED)
                    filtered->color_trc = vDec->color_trc;
                if (!writer.open(spec.outputPath, filtered, frameRate, aDec != nullptr, 48000,
                                 errorOut)) {
                    av_frame_unref(filtered);
                    return false;
                }
                if (!pendingPcm.empty()
                    && !writer.writeAudio(pendingPcm.data(), int(pendingPcm.size() / 2), errorOut)) {
                    av_frame_unref(filtered);
                    return false;
                }
                pendingPcm.clear();
            }
            const bool written = writer.writeVideo(filtered, errorOut);
            av_frame_unref(filtered);
            if (!written)
                return false;
            wroteVideo = true;
        }
    };

    auto handleVideo = [&](AVFrame *decoded) -> bool {
        const TimeUs ptsUs = framePtsUs(decoded, vStream->time_base);
        if (ptsUs + 1000 < inUs)
            return true;
        if (ptsUs >= outUs)
            return true;
        // Real timestamps into the fps filter, relative to the in-point: it is what places each
        // frame on the output grid, instead of the frames simply being counted.
        decoded->pts = ptsUs - inUs;
        // Same time base as pts, or the filter holds the clip's last frame for a sliver of it.
        decoded->duration = av_rescale_q(decoded->duration, vStream->time_base, {1, AV_TIME_BASE});
        if (av_buffersrc_add_frame_flags(filterSrc, decoded, AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
            return fail(trEdit("Could not convert a frame"));
        if (!drainFilter())
            return false;
        if (spanUs > 0 && cancelled(onProgress, double(ptsUs - inUs) / double(spanUs))) {
            if (errorOut)
                *errorOut = trEdit("Cancelled");
            return false;
        }
        return true;
    };

    auto handleAudio = [&](AVFrame *decoded) -> bool {
        if (!aDec || !swr)
            return true;
        AVStream *aStream = fmt->streams[audioIndex];
        const TimeUs ptsUs = framePtsUs(decoded, aStream->time_base);
        if (ptsUs + 20000 < inUs)
            return true;
        if (ptsUs >= outUs)
            return true;
        const int maxOut = swr_get_out_samples(swr, decoded->nb_samples);
        pcm.resize(std::max(0, maxOut) * 2);
        uint8_t *outData[1] = {reinterpret_cast<uint8_t *>(pcm.data())};
        const int converted = swr_convert(swr, outData, maxOut,
                                          const_cast<const uint8_t **>(decoded->data),
                                          decoded->nb_samples);
        if (converted <= 0)
            return true;
        int offset = 0;
        int count = converted;
        if (ptsUs < inUs) {
            const int drop = static_cast<int>(((inUs - ptsUs) * 48000) / kUsPerSecond);
            if (drop >= count)
                return true;
            offset = drop;
            count -= drop;
        }
        if (outUs != std::numeric_limits<TimeUs>::max()) {
            const TimeUs endUs = ptsUs + (static_cast<TimeUs>(converted) * kUsPerSecond) / 48000;
            if (endUs > outUs) {
                const int keep = static_cast<int>(((outUs - std::max(ptsUs, inUs)) * 48000) / kUsPerSecond);
                count = std::min(count, std::max(0, keep));
            }
        }
        if (count <= 0)
            return true;
        return writeAudio(pcm.data() + offset * 2, count);
    };

    while (ok) {
        if (av_read_frame(fmt, packet) < 0)
            break;
        AVCodecContext *dec = nullptr;
        if (packet->stream_index == videoIndex)
            dec = vDec;
        else if (aDec && packet->stream_index == audioIndex)
            dec = aDec;
        else {
            av_packet_unref(packet);
            continue;
        }
        const int send = avcodec_send_packet(dec, packet);
        av_packet_unref(packet);
        if (send < 0 && send != AVERROR(EAGAIN))
            continue;
        for (;;) {
            const int rc = avcodec_receive_frame(dec, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0) {
                ok = false;
                break;
            }
            if (dec == vDec)
                ok = handleVideo(frame);
            else
                ok = handleAudio(frame);
            av_frame_unref(frame);
            if (!ok)
                break;
        }
    }

    if (ok) {
        avcodec_send_packet(vDec, nullptr);
        for (;;) {
            const int rc = avcodec_receive_frame(vDec, frame);
            if (rc < 0)
                break;
            ok = handleVideo(frame);
            av_frame_unref(frame);
            if (!ok)
                break;
        }
        if (ok) {
            // Flushes the fps filter's held frame, the clip's last.
            ok = av_buffersrc_add_frame_flags(filterSrc, nullptr, 0) >= 0 && drainFilter();
        }
        if (ok && aDec) {
            avcodec_send_packet(aDec, nullptr);
            for (;;) {
                const int rc = avcodec_receive_frame(aDec, frame);
                if (rc < 0)
                    break;
                ok = handleAudio(frame);
                av_frame_unref(frame);
                if (!ok)
                    break;
            }
        }
    }

    if (ok && !wroteVideo) {
        writer.abort();
        cleanup();
        return fail(trEdit("No frames could be decoded from this clip"));
    }
    if (ok)
        ok = writer.finish(errorOut);
    if (!ok)
        writer.abort();
    cleanup();
    if (ok && onProgress)
        onProgress(1.0);
    return ok;
}

} // namespace

QString newEditedMediaPath(const QString &projectId, const QString &kind)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};

    const QString dir = QDir(base).filePath(QStringLiteral("projects/%1/media").arg(projectId));
    if (!QDir().mkpath(dir))
        return {};

    QString suffix = QStringLiteral("mp4");
    if (kind == QLatin1String("image"))
        suffix = QStringLiteral("png");
    else if (kind == QLatin1String("audio"))
        suffix = QStringLiteral("flac");

    return QDir(dir).filePath(QStringLiteral("edit-%1.%2")
                                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), suffix));
}

bool editMedia(const MediaEditSpec &spec, QString *errorOut,
               const std::function<bool(double)> &onProgress)
{
    if (spec.inputPath.isEmpty() || spec.outputPath.isEmpty()) {
        if (errorOut)
            *errorOut = trEdit("Missing media path");
        return false;
    }
    if (spec.kind == QLatin1String("image"))
        return editImage(spec, errorOut, onProgress);
    if (spec.kind == QLatin1String("audio"))
        return editAudio(spec, errorOut, onProgress);
    return editVideo(spec, errorOut, onProgress);
}

} // namespace drift
