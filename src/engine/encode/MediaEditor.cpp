#include "engine/encode/MediaEditor.h"

#include "engine/audio/analysis/LoudnessMeter.h"
#include "engine/decode/probe/MediaProbe.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/encode/writers/AudioFileWriter.h"
#include "core/base/Time.h"
#include "core/project/Project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include "engine/decode/probe/StillImage.h"

#include <QStandardPaths>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
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
#include <libswscale/swscale.h>
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

bool encoderSupports(AVCodecID id, AVPixelFormat format)
{
    const AVCodec *codec = avcodec_find_encoder(id);
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

QStringList rotationChain(int rotation)
{
    if (rotation == 90)
        return {QStringLiteral("transpose=clock")};
    if (rotation == 180)
        return {QStringLiteral("hflip,vflip")};
    if (rotation == 270)
        return {QStringLiteral("transpose=cclock")};
    return {};
}

// buffer (source frames, microsecond timestamps) -> `chain` -> buffersink.
bool buildFilterGraph(const AVCodecContext *dec, const AVStream *stream, const QString &chain,
                      const QList<FilterOption> &options, AVFilterGraph **graphOut,
                      AVFilterContext **srcOut, AVFilterContext **sinkOut)
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
    bool ok = avfilter_graph_create_filter(&src, avfilter_get_by_name("buffer"), "in",
                                           args.constData(), nullptr, graph)
            >= 0
        && avfilter_graph_create_filter(&sink, avfilter_get_by_name("buffersink"), "out", nullptr,
                                        nullptr, graph)
            >= 0;

    // avfilter_graph_parse_ptr in its steps, so `options` land after the chain's own options and
    // before init: a filter refuses a non-runtime option once it is initialized.
    AVFilterGraphSegment *segment = nullptr;
    AVFilterInOut *freeInputs = nullptr;
    AVFilterInOut *freeOutputs = nullptr;
    if (ok) {
        const QByteArray chainUtf8 = chain.toUtf8();
        ok = avfilter_graph_segment_parse(graph, chainUtf8.constData(), 0, &segment) >= 0
            && avfilter_graph_segment_create_filters(segment, 0) >= 0
            && avfilter_graph_segment_apply_opts(segment, 0) >= 0;
    }
    for (const FilterOption &option : options) {
        if (!ok)
            break;
        bool applied = false;
        for (size_t c = 0; ok && c < segment->nb_chains; ++c) {
            const AVFilterChain *filterChain = segment->chains[c];
            for (size_t f = 0; f < filterChain->nb_filters; ++f) {
                AVFilterContext *ctx = filterChain->filters[f]->filter;
                if (!ctx || option.filter != ctx->filter->name)
                    continue;
                if (av_opt_set(ctx, option.option.constData(), option.value.toUtf8().constData(),
                               AV_OPT_SEARCH_CHILDREN)
                    < 0) {
                    ok = false;
                    break;
                }
                applied = true;
            }
        }
        ok = ok && applied;
    }
    // The chain is one unlabeled run, so it leaves exactly one free pad at each end.
    ok = ok && avfilter_graph_segment_init(segment, 0) >= 0
        && avfilter_graph_segment_link(segment, 0, &freeInputs, &freeOutputs) >= 0
        && freeInputs && !freeInputs->next && freeOutputs && !freeOutputs->next
        && avfilter_link(src, 0, freeInputs->filter_ctx, freeInputs->pad_idx) >= 0
        && avfilter_link(freeOutputs->filter_ctx, freeOutputs->pad_idx, sink, 0) >= 0
        && avfilter_graph_config(graph, nullptr) >= 0;
    avfilter_inout_free(&freeInputs);
    avfilter_inout_free(&freeOutputs);
    avfilter_graph_segment_free(&segment);
    if (!ok) {
        avfilter_graph_free(&graph);
        return false;
    }
    *graphOut = graph;
    *srcOut = src;
    *sinkOut = sink;
    return true;
}

// Audio-only output other than the FLAC AudioFileWriter makes: AAC in m4a, or PCM in wav. Takes
// interleaved stereo float like AudioFileWriter and converts to whatever sample format the
// encoder wants.
class ContainerAudioWriter
{
public:
    ~ContainerAudioWriter() { abort(); }

    bool open(const QString &path, const char *format, AVCodecID codecId, AVSampleFormat sampleFmt,
              int sampleRate, int bitRate, QString *errorOut);
    bool writeFrames(const float *interleaved, int frames, QString *errorOut);
    bool finish(QString *errorOut);
    void abort();

private:
    bool encodeFromFifo(bool flushAll, QString *errorOut);
    bool drain(QString *errorOut);
    void teardown();

    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_ctx = nullptr;
    AVStream *m_stream = nullptr;
    SwrContext *m_swr = nullptr;
    AVAudioFifo *m_fifo = nullptr;
    AVFrame *m_frame = nullptr;
    AVPacket *m_pkt = nullptr;
    QString m_path;
    QString m_tmpPath;
    int m_frameSize = 4096;
    int64_t m_nextPts = 0;
    bool m_finished = false;
};

bool ContainerAudioWriter::open(const QString &path, const char *format, AVCodecID codecId,
                                AVSampleFormat sampleFmt, int sampleRate, int bitRate,
                                QString *errorOut)
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
    avformat_alloc_output_context2(&m_fmt, nullptr, format, tmpUtf8.constData());
    if (!m_fmt)
        return fail(trEdit("Could not create the audio container"));
    const AVCodec *codec = avcodec_find_encoder(codecId);
    if (!codec)
        return fail(trEdit("The audio encoder is not available"));
    m_stream = avformat_new_stream(m_fmt, nullptr);
    m_ctx = avcodec_alloc_context3(codec);
    if (!m_stream || !m_ctx)
        return fail(trEdit("Could not allocate the audio encoder"));

    m_ctx->sample_fmt = sampleFmt;
    m_ctx->sample_rate = sampleRate;
    if (bitRate > 0)
        m_ctx->bit_rate = bitRate;
    av_channel_layout_default(&m_ctx->ch_layout, 2);
    m_ctx->time_base = AVRational{1, sampleRate};
    if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER)
        m_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(m_ctx, codec, nullptr) < 0)
        return fail(trEdit("Could not open the audio encoder"));
    avcodec_parameters_from_context(m_stream->codecpar, m_ctx);
    m_stream->time_base = m_ctx->time_base;
    m_frameSize = m_ctx->frame_size > 0 ? m_ctx->frame_size : 4096;

    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&m_swr, &m_ctx->ch_layout, sampleFmt, sampleRate, &stereo,
                            AV_SAMPLE_FMT_FLT, sampleRate, 0, nullptr)
            < 0
        || swr_init(m_swr) < 0)
        return fail(trEdit("Could not convert the audio"));
    m_fifo = av_audio_fifo_alloc(sampleFmt, 2, m_frameSize);
    m_frame = av_frame_alloc();
    m_pkt = av_packet_alloc();
    if (!m_fifo || !m_frame || !m_pkt)
        return fail(trEdit("Could not allocate the audio buffer"));
    m_frame->format = sampleFmt;
    m_frame->sample_rate = sampleRate;
    av_channel_layout_copy(&m_frame->ch_layout, &m_ctx->ch_layout);
    m_frame->nb_samples = m_frameSize;
    if (av_frame_get_buffer(m_frame, 0) < 0)
        return fail(trEdit("Could not allocate audio frame buffers"));

    if (!(m_fmt->oformat->flags & AVFMT_NOFILE)
        && avio_open(&m_fmt->pb, tmpUtf8.constData(), AVIO_FLAG_WRITE) < 0)
        return fail(trEdit("Could not open the output file for writing"));
    if (avformat_write_header(m_fmt, nullptr) < 0)
        return fail(trEdit("Could not write the output header"));
    return true;
}

bool ContainerAudioWriter::drain(QString *errorOut)
{
    for (;;) {
        const int rc = avcodec_receive_packet(m_ctx, m_pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (errorOut)
                *errorOut = trEdit("Failed to read an encoded audio packet");
            return false;
        }
        av_packet_rescale_ts(m_pkt, m_ctx->time_base, m_stream->time_base);
        m_pkt->stream_index = m_stream->index;
        const int wrc = av_interleaved_write_frame(m_fmt, m_pkt);
        av_packet_unref(m_pkt);
        if (wrc < 0) {
            if (errorOut)
                *errorOut = trEdit("Failed to write an audio packet");
            return false;
        }
    }
}

bool ContainerAudioWriter::encodeFromFifo(bool flushAll, QString *errorOut)
{
    while (av_audio_fifo_size(m_fifo) >= m_frameSize
           || (flushAll && av_audio_fifo_size(m_fifo) > 0)) {
        const int count = std::min(av_audio_fifo_size(m_fifo), m_frameSize);
        m_frame->nb_samples = count;
        if (av_frame_make_writable(m_frame) < 0
            || av_audio_fifo_read(m_fifo, reinterpret_cast<void **>(m_frame->data), count) < count) {
            if (errorOut)
                *errorOut = trEdit("Could not read buffered audio");
            return false;
        }
        m_frame->pts = m_nextPts;
        m_nextPts += count;
        if (avcodec_send_frame(m_ctx, m_frame) < 0) {
            if (errorOut)
                *errorOut = trEdit("Audio encoder rejected a frame");
            return false;
        }
        if (!drain(errorOut))
            return false;
    }
    return true;
}

bool ContainerAudioWriter::writeFrames(const float *interleaved, int frames, QString *errorOut)
{
    if (frames <= 0)
        return true;
    uint8_t **converted = nullptr;
    int lineSize = 0;
    if (av_samples_alloc_array_and_samples(&converted, &lineSize, 2, frames, m_ctx->sample_fmt, 0)
        < 0) {
        if (errorOut)
            *errorOut = trEdit("Could not convert the audio");
        return false;
    }
    const uint8_t *inData[1] = {reinterpret_cast<const uint8_t *>(interleaved)};
    const int count = swr_convert(m_swr, converted, frames, inData, frames);
    const bool buffered = count < 0
        || av_audio_fifo_write(m_fifo, reinterpret_cast<void **>(converted), count) >= count;
    av_freep(&converted[0]);
    av_freep(&converted);
    if (!buffered) {
        if (errorOut)
            *errorOut = trEdit("Could not buffer audio");
        return false;
    }
    return encodeFromFifo(false, errorOut);
}

bool ContainerAudioWriter::finish(QString *errorOut)
{
    if (m_finished)
        return true;
    if (!m_fmt) {
        if (errorOut)
            *errorOut = trEdit("Writer is not open");
        return false;
    }
    if (!encodeFromFifo(true, errorOut))
        return false;
    if (avcodec_send_frame(m_ctx, nullptr) < 0 || !drain(errorOut)) {
        if (errorOut && errorOut->isEmpty())
            *errorOut = trEdit("Could not flush the audio encoder");
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

void ContainerAudioWriter::abort()
{
    if (m_finished)
        return;
    teardown();
    if (!m_tmpPath.isEmpty() && QFile::exists(m_tmpPath))
        QFile::remove(m_tmpPath);
    m_tmpPath.clear();
}

void ContainerAudioWriter::teardown()
{
    if (m_fifo) {
        av_audio_fifo_free(m_fifo);
        m_fifo = nullptr;
    }
    if (m_swr)
        swr_free(&m_swr);
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

// The audio-only destination editAudio writes to: the FLAC writer by default, so the default
// output is unchanged, or a ContainerAudioWriter for the other containers.
class AudioOut
{
public:
    bool open(const QString &path, int sampleRate, const MediaOutputOptions &options,
              QString *errorOut)
    {
        const QString container = options.container.toLower();
        const int bitRate = options.audioBitrateKbps > 0 ? options.audioBitrateKbps * 1000 : 192000;
        if (container.isEmpty() || container == QLatin1String("flac")) {
            m_other = false;
            return m_flac.open(path, sampleRate, 2, errorOut);
        }
        m_other = true;
        if (container == QLatin1String("m4a"))
            return m_container.open(path, "ipod", AV_CODEC_ID_AAC, AV_SAMPLE_FMT_FLTP, sampleRate,
                              bitRate, errorOut);
        if (container == QLatin1String("wav")) {
            const bool deep = options.wavBitDepth == 24;
            return m_container.open(path, "wav", deep ? AV_CODEC_ID_PCM_S24LE : AV_CODEC_ID_PCM_S16LE,
                              deep ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16, sampleRate, 0, errorOut);
        }
        if (errorOut)
            *errorOut = trEdit("Audio can be saved as flac, m4a or wav");
        return false;
    }
    bool write(const float *interleaved, int frames, QString *errorOut)
    {
        return m_other ? m_container.writeFrames(interleaved, frames, errorOut)
                       : m_flac.writeFrames(interleaved, frames, errorOut);
    }
    bool finish(QString *errorOut)
    {
        return m_other ? m_container.finish(errorOut) : m_flac.finish(errorOut);
    }
    void abort()
    {
        m_flac.abort();
        m_container.abort();
    }

private:
    AudioFileWriter m_flac;
    ContainerAudioWriter m_container;
    bool m_other = false;
};

// Speed, gain and the caller's own chain over interleaved stereo float. With nothing to apply it
// is a pass-through that hands the input back untouched.
class AudioProcessor
{
public:
    ~AudioProcessor() { avfilter_graph_free(&m_graph); }

    static QString chainFor(const MediaEditSpec &spec, int rate)
    {
        QStringList stages;
        if (spec.speed != 1.0) {
            if (spec.keepPitch) {
                // atempo takes 0.5..2 per stage.
                double left = spec.speed;
                while (left > 2.0) {
                    stages << QStringLiteral("atempo=2.0");
                    left /= 2.0;
                }
                while (left < 0.5) {
                    stages << QStringLiteral("atempo=0.5");
                    left /= 0.5;
                }
                stages << QStringLiteral("atempo=%1").arg(left, 0, 'f', 6);
            } else {
                stages << QStringLiteral("asetrate=%1").arg(std::llround(rate * spec.speed))
                       << QStringLiteral("aresample=%1").arg(rate);
            }
        }
        if (spec.gainDb != 0.0)
            stages << QStringLiteral("volume=%1dB").arg(spec.gainDb, 0, 'f', 4);
        if (!spec.audioFilter.trimmed().isEmpty())
            stages << spec.audioFilter.trimmed();
        return stages.join(QLatin1Char(','));
    }

    bool open(int rate, const QString &chain)
    {
        m_rate = rate;
        if (chain.isEmpty())
            return true;
        m_graph = avfilter_graph_alloc();
        if (!m_graph)
            return false;
        const QByteArray args =
            QStringLiteral("sample_rate=%1:sample_fmt=flt:channel_layout=stereo:time_base=1/%1")
                .arg(rate)
                .toUtf8();
        if (avfilter_graph_create_filter(&m_src, avfilter_get_by_name("abuffer"), "in",
                                         args.constData(), nullptr, m_graph)
                < 0
            || avfilter_graph_create_filter(&m_sink, avfilter_get_by_name("abuffersink"), "out",
                                            nullptr, nullptr, m_graph)
                < 0)
            return false;
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs = avfilter_inout_alloc();
        if (!outputs || !inputs) {
            avfilter_inout_free(&outputs);
            avfilter_inout_free(&inputs);
            return false;
        }
        outputs->name = av_strdup("in");
        outputs->filter_ctx = m_src;
        outputs->pad_idx = 0;
        outputs->next = nullptr;
        inputs->name = av_strdup("out");
        inputs->filter_ctx = m_sink;
        inputs->pad_idx = 0;
        inputs->next = nullptr;
        const QByteArray description =
            (chain
             + QStringLiteral(",aformat=sample_fmts=flt:channel_layouts=stereo:sample_rates=%1")
                   .arg(rate))
                .toUtf8();
        const int rc =
            avfilter_graph_parse_ptr(m_graph, description.constData(), &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        return rc >= 0 && avfilter_graph_config(m_graph, nullptr) >= 0;
    }

    // `out` points at `outFrames` interleaved stereo frames, valid until the next call.
    bool run(const float *in, int frames, const float **out, int *outFrames)
    {
        if (!m_graph) {
            *out = in;
            *outFrames = frames;
            return true;
        }
        AVFrame *frame = av_frame_alloc();
        if (!frame)
            return false;
        frame->format = AV_SAMPLE_FMT_FLT;
        frame->sample_rate = m_rate;
        frame->nb_samples = frames;
        av_channel_layout_default(&frame->ch_layout, 2);
        frame->pts = m_pts;
        m_pts += frames;
        if (av_frame_get_buffer(frame, 0) < 0) {
            av_frame_free(&frame);
            return false;
        }
        std::memcpy(frame->data[0], in, size_t(frames) * 2 * sizeof(float));
        const int rc = av_buffersrc_add_frame_flags(m_src, frame, 0);
        av_frame_free(&frame);
        return rc >= 0 && pull(out, outFrames);
    }

    bool finish(const float **out, int *outFrames)
    {
        if (!m_graph) {
            *outFrames = 0;
            return true;
        }
        return av_buffersrc_add_frame_flags(m_src, nullptr, 0) >= 0 && pull(out, outFrames);
    }

private:
    bool pull(const float **out, int *outFrames)
    {
        m_out.clear();
        AVFrame *frame = av_frame_alloc();
        if (!frame)
            return false;
        bool ok = true;
        for (;;) {
            const int rc = av_buffersink_get_frame(m_sink, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0) {
                ok = false;
                break;
            }
            const float *samples = reinterpret_cast<const float *>(frame->data[0]);
            m_out.insert(m_out.end(), samples, samples + size_t(frame->nb_samples) * 2);
            av_frame_unref(frame);
        }
        av_frame_free(&frame);
        *out = m_out.data();
        *outFrames = int(m_out.size() / 2);
        return ok;
    }

    AVFilterGraph *m_graph = nullptr;
    AVFilterContext *m_src = nullptr;
    AVFilterContext *m_sink = nullptr;
    std::vector<float> m_out;
    int64_t m_pts = 0;
    int m_rate = 48000;
};

constexpr quint64 kLoudnessScanStreamId = 0xA5'11'5C'A4'00'00'00'03ull;

// Integrated loudness of the audio the edit will keep, read at 48 kHz. `lufs` is left alone when
// there is nothing measurable. A cancel through onProgress comes back as false with `cancelled`.
bool measureKeptLoudness(const MediaEditSpec &spec, double share, double *lufs, bool *wasCancelled,
                         const std::function<bool(double)> &onProgress)
{
    constexpr int kRate = 48000;
    const bool external = !spec.audioSourcePath.isEmpty();
    const QString path = external ? spec.audioSourcePath : spec.inputPath;
    const TimeUs inUs = external ? 0 : secondsToUs(std::max(0.0, spec.inSeconds));
    TimeUs spanUs;
    if (spec.outSeconds > 0) {
        spanUs = secondsToUs(spec.outSeconds) - secondsToUs(std::max(0.0, spec.inSeconds));
    } else {
        const MediaInfo info = MediaProbe::probe(path);
        spanUs = info.ok ? info.durationUs - inUs : 0;
    }
    const qint64 totalFrames = (spanUs * kRate) / kUsPerSecond;
    if (totalFrames <= 0)
        return true;

    bool stopped = false;
    const LoudnessResult measured = measureLoudness(
        totalFrames, kRate, [&](float *out, qint64 offset, int maxFrames) -> int {
            if (stopped)
                return 0;
            const int got = ClipReaderPool::instance().readAudioInterleaved(
                path, kLoudnessScanStreamId, inUs + (offset * kUsPerSecond) / kRate, maxFrames,
                kRate, out);
            if (onProgress && !onProgress(share * double(offset + got) / double(totalFrames))) {
                stopped = true;
                return 0;
            }
            return got;
        });
    *wasCancelled = stopped;
    if (!stopped && measured.ok)
        *lufs = measured.integratedLufs;
    return !stopped;
}

struct VideoOutputPlan
{
    const char *format = "mp4";
    AVCodecID codec = AV_CODEC_ID_H264;
    bool prores = false;
};

bool resolveVideoOutput(const MediaOutputOptions &options, VideoOutputPlan *plan, QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };
    const QString codec = options.videoCodec.toLower();
    if (codec.isEmpty() || codec == QLatin1String("h264")) {
        plan->codec = AV_CODEC_ID_H264;
    } else if (codec == QLatin1String("hevc")) {
        plan->codec = AV_CODEC_ID_HEVC;
    } else if (codec == QLatin1String("prores")) {
        plan->codec = AV_CODEC_ID_PRORES;
        plan->prores = true;
    } else {
        return fail(trEdit("Unknown video codec"));
    }
    const QString container = options.container.toLower();
    if (container.isEmpty())
        plan->format = plan->prores ? "mov" : "mp4";
    else if (container == QLatin1String("mp4"))
        plan->format = "mp4";
    else if (container == QLatin1String("mov"))
        plan->format = "mov";
    else if (container == QLatin1String("mkv"))
        plan->format = "matroska";
    else
        return fail(trEdit("Video can be saved as mp4, mov or mkv"));
    if (plan->prores && container == QLatin1String("mp4"))
        return fail(trEdit("ProRes needs a mov or mkv container"));
    return true;
}

bool isAudioContainer(const QString &container)
{
    const QString lower = container.toLower();
    return lower == QLatin1String("m4a") || lower == QLatin1String("wav")
        || lower == QLatin1String("flac");
}

bool editAudio(const MediaEditSpec &request, QString *errorOut,
               const std::function<bool(double)> &onProgress)
{
    MediaEditSpec spec = request;
    if (!spec.audioSourcePath.isEmpty()) {
        spec.inputPath = spec.audioSourcePath;
        if (spec.outSeconds > 0)
            spec.outSeconds -= std::max(0.0, spec.inSeconds);
        spec.inSeconds = 0;
    }
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    AVFormatContext *fmt = nullptr;
    if (openMediaInput(&fmt, spec.inputPath) < 0)
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

    AudioOut writer;
    AudioProcessor processor;
    if (!processor.open(sampleRate, AudioProcessor::chainFor(spec, sampleRate))) {
        swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return fail(trEdit("Could not set up the audio filter"));
    }
    if (!writer.open(spec.outputPath, sampleRate, spec.output, errorOut)) {
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

        const float *processed = nullptr;
        int processedFrames = 0;
        if (!processor.run(scratch.data() + offset * channels, count, &processed, &processedFrames)) {
            if (errorOut)
                *errorOut = trEdit("Could not apply the audio filter");
            ok = false;
            break;
        }
        if (processedFrames > 0 && !writer.write(processed, processedFrames, errorOut)) {
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

    if (ok) {
        // The speed change holds back a little audio until the input ends.
        const float *processed = nullptr;
        int processedFrames = 0;
        ok = processor.finish(&processed, &processedFrames)
            && (processedFrames <= 0 || writer.write(processed, processedFrames, errorOut));
        if (!ok && errorOut && errorOut->isEmpty())
            *errorOut = trEdit("Could not apply the audio filter");
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
              bool withAudio, int audioRate, const VideoOutputPlan &plan,
              const MediaOutputOptions &options, QString *errorOut);
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
                     bool withAudio, int audioRate, const VideoOutputPlan &plan,
                     const MediaOutputOptions &options, QString *errorOut)
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
    avformat_alloc_output_context2(&m_fmt, nullptr, plan.format, tmpUtf8.constData());
    if (!m_fmt)
        return fail(trEdit("Could not create the output file"));

    const AVCodec *vCodec = avcodec_find_encoder(plan.codec);
    if (!vCodec)
        return fail(plan.codec == AV_CODEC_ID_H264 ? trEdit("H.264 encoder not available")
                                                   : trEdit("That video encoder is not available"));

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
    // The result is timeline media: a keyframe about every second keeps a scrub's seek to a
    // second of decoding at most. B-frames and CRF 18 are what keep the file near the size of a
    // camera original — CRF 16 with a 12-frame GOP and no B-frames ran 1080p at ~45 Mbps.
    m_videoCtx->gop_size = std::max(1, int(std::lround(av_q2d(frameRate))));
    m_videoCtx->max_b_frames = plan.prores ? 0 : 3;
    if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER)
        m_videoCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    // It replaces the original in the project and is what export reads, so still visually
    // lossless.
    if (!plan.prores) {
        av_opt_set(m_videoCtx->priv_data, "crf",
                   QByteArray::number(options.crf >= 0 ? options.crf : 18).constData(), 0);
        av_opt_set(m_videoCtx->priv_data, "preset", "fast", 0);
    }

    if (avcodec_open2(m_videoCtx, vCodec, nullptr) < 0)
        return fail(trEdit("Could not open the video encoder"));
    avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCtx);
    // QuickTime and Apple players only take HEVC in mp4/mov under the hvc1 tag.
    if (plan.codec == AV_CODEC_ID_HEVC && plan.format != std::string_view("matroska"))
        m_videoStream->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
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
        m_audioCtx->bit_rate = options.audioBitrateKbps > 0 ? options.audioBitrateKbps * 1000 : 192000;
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

    VideoOutputPlan plan;
    if (!resolveVideoOutput(spec.output, &plan, errorOut))
        return false;

    AVFormatContext *fmt = nullptr;
    if (openMediaInput(&fmt, spec.inputPath) < 0)
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
    // A pre-trimmed audio file replaces the source's own track; it is read on its own, alongside
    // the video, rather than interleaved with it.
    AVFormatContext *extFmt = nullptr;
    int extIndex = -1;
    if (!spec.audioSourcePath.isEmpty()) {
        if (openMediaInput(&extFmt, spec.audioSourcePath) < 0
            || avformat_find_stream_info(extFmt, nullptr) < 0
            || (extIndex = av_find_best_stream(extFmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0)) < 0) {
            avformat_close_input(&extFmt);
            avcodec_free_context(&vDec);
            avformat_close_input(&fmt);
            return fail(trEdit("Could not read the replacement audio"));
        }
    }
    AVFormatContext *aFmt = extFmt ? extFmt : fmt;
    const int aIndex = extFmt ? extIndex : audioIndex;
    if (aIndex >= 0) {
        AVStream *aStream = aFmt->streams[aIndex];
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
    AudioProcessor audioProcessor;
    if (aDec && !audioProcessor.open(48000, AudioProcessor::chainFor(spec, 48000))) {
        if (swr)
            swr_free(&swr);
        avcodec_free_context(&aDec);
        avformat_close_input(&extFmt);
        avcodec_free_context(&vDec);
        avformat_close_input(&fmt);
        return fail(trEdit("Could not set up the audio filter"));
    }

    const bool tenBit = !spec.frameHook && sourceBitDepth(vDec) > 8
        && encoderSupports(plan.codec, AV_PIX_FMT_YUV420P10LE);
    const AVPixelFormat outFormat = plan.prores ? AV_PIX_FMT_YUV422P10LE
        : tenBit                                ? AV_PIX_FMT_YUV420P10LE
                                                : AV_PIX_FMT_YUV420P;
    // With a hook the graph hands over RGB for it, and the hook's result is converted to
    // outFormat on the way to the encoder.
    const AVPixelFormat graphFormat = spec.frameHook ? AV_PIX_FMT_RGB24 : outFormat;

    AVFilterGraph *graph = nullptr;
    AVFilterContext *filterSrc = nullptr;
    AVFilterContext *filterSink = nullptr;
    {
        QStringList chain = rotationChain(rotation);
        if (!spec.videoFilter.isEmpty())
            chain << spec.videoFilter;
        if (crop != QRect(0, 0, displayW, displayH))
            chain << QStringLiteral("crop=%1:%2:%3:%4:exact=1")
                         .arg(crop.width())
                         .arg(crop.height())
                         .arg(crop.x())
                         .arg(crop.y());
        // With a hook the downscale happens after it, on the hook's picture.
        const int maxHeight = spec.output.maxHeight;
        if (!spec.frameHook && maxHeight > 0 && crop.height() > maxHeight)
            chain << QStringLiteral("scale=-2:%1").arg(std::max(2, maxHeight & ~1));
        if (spec.speed != 1.0)
            chain << QStringLiteral("setpts=PTS/%1").arg(spec.speed, 0, 'f', 8);
        // start_time=0 pads from the trim's in-point, so a first frame that lands late still
        // lines up with the audio's first sample.
        chain << QStringLiteral("fps=fps=%1/%2:start_time=0").arg(frameRate.num).arg(frameRate.den)
              << QStringLiteral("format=%1").arg(QString::fromLatin1(av_get_pix_fmt_name(graphFormat)));
        if (!buildFilterGraph(vDec, vStream, chain.join(QLatin1Char(',')), spec.videoFilterOptions,
                              &graph, &filterSrc, &filterSink)) {
            avformat_close_input(&extFmt);
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
    SwsContext *hookSws = nullptr;
    AVFrame *hooked = spec.frameHook ? av_frame_alloc() : nullptr;

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
    AVPacket *extPacket = extFmt ? av_packet_alloc() : nullptr;
    AVFrame *extFrame = extFmt ? av_frame_alloc() : nullptr;
    bool extEof = false;
    TimeUs lastVideoUs = 0;
    TimeUs extDecodedUs = 0;
    std::vector<float> pcm;
    // Audio decoded before the first filtered video frame opens the writer.
    std::vector<float> pendingPcm;
    bool ok = packet && frame && filtered && (hooked || !spec.frameHook)
        && (!extFmt || (extPacket && extFrame));
    bool wroteVideo = false;
    // Set once a stream decodes past the out-point; with both set, the rest of the file is skipped.
    bool videoPastOut = false;
    bool audioPastOut = false;
    const TimeUs spanUs = (outUs == std::numeric_limits<TimeUs>::max() ? durationUs : outUs) - inUs;

    // The replacement audio is trimmed to the kept range and starts at 0; the source's own audio is
    // cut out of the file at inUs..outUs.
    const TimeUs audioInUs = extFmt ? 0 : inUs;
    const TimeUs audioOutUs = extFmt && outUs != std::numeric_limits<TimeUs>::max()
        ? outUs - inUs
        : outUs;

    auto cleanup = [&] {
        av_frame_free(&extFrame);
        av_packet_free(&extPacket);
        avformat_close_input(&extFmt);
        av_frame_free(&hooked);
        sws_freeContext(hookSws);
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

    auto writeProcessed = [&](const float *interleaved, int count) -> bool {
        if (count <= 0)
            return true;
        if (!writer.isOpen()) {
            pendingPcm.insert(pendingPcm.end(), interleaved, interleaved + count * 2);
            return true;
        }
        return writer.writeAudio(interleaved, count, errorOut);
    };
    auto writeAudio = [&](const float *interleaved, int count) -> bool {
        const float *processed = nullptr;
        int processedFrames = 0;
        if (!audioProcessor.run(interleaved, count, &processed, &processedFrames))
            return fail(trEdit("Could not apply the audio filter"));
        return writeProcessed(processed, processedFrames);
    };

    auto drainFilter = [&]() -> bool {
        for (;;) {
            const int rc = av_buffersink_get_frame(filterSink, filtered);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0)
                return fail(trEdit("Could not convert a frame"));
            if (spec.frameHook) {
                QImage image(filtered->width, filtered->height, QImage::Format_RGB888);
                for (int y = 0; y < filtered->height; ++y)
                    std::memcpy(image.scanLine(y), filtered->data[0] + y * filtered->linesize[0],
                                size_t(filtered->width) * 3);
                QString hookError;
                if (!spec.frameHook(image, &hookError)) {
                    av_frame_unref(filtered);
                    return fail(hookError.isEmpty() ? trEdit("Cancelled") : hookError);
                }
                // yuv420 needs even dimensions.
                const int w = image.width() & ~1;
                const int h = image.height() & ~1;
                int outW = w;
                int outH = h;
                if (spec.output.maxHeight > 0 && h > spec.output.maxHeight) {
                    outH = std::max(2, spec.output.maxHeight & ~1);
                    outW = std::max(2, int(std::lround(double(w) * outH / h)) & ~1);
                }
                if (writer.isOpen() && (outW != hooked->width || outH != hooked->height)) {
                    av_frame_unref(filtered);
                    return fail(trEdit("Could not convert a frame"));
                }
                if (!hooked->data[0]) {
                    hooked->format = outFormat;
                    hooked->width = outW;
                    hooked->height = outH;
                    if (av_frame_get_buffer(hooked, 0) < 0) {
                        av_frame_unref(filtered);
                        return fail(trEdit("Could not convert a frame"));
                    }
                }
                hookSws = sws_getCachedContext(hookSws, w, h, AV_PIX_FMT_RGB24, outW, outH, outFormat,
                                               SWS_BICUBIC, nullptr, nullptr, nullptr);
                if (!hookSws || av_frame_make_writable(hooked) < 0) {
                    av_frame_unref(filtered);
                    return fail(trEdit("Could not convert a frame"));
                }
                // The hook's picture is plain RGB; encode it as limited-range BT.709 and say so.
                const int *bt709 = sws_getCoefficients(SWS_CS_ITU709);
                sws_setColorspaceDetails(hookSws, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
                const uint8_t *srcData[1] = {image.constBits()};
                const int srcStride[1] = {int(image.bytesPerLine())};
                sws_scale(hookSws, srcData, srcStride, 0, h, hooked->data, hooked->linesize);
                hooked->pts = filtered->pts;
                hooked->duration = filtered->duration;
                hooked->color_range = AVCOL_RANGE_MPEG;
                hooked->colorspace = AVCOL_SPC_BT709;
                hooked->color_primaries = filtered->color_primaries;
                hooked->color_trc = filtered->color_trc;
                hooked->sample_aspect_ratio = filtered->sample_aspect_ratio;
                av_frame_unref(filtered);
                // The writer takes its size from the first frame it sees, and unrefs nothing, so
                // hand it a reference and keep the buffer for the next frame.
                av_frame_ref(filtered, hooked);
            }
            if (!writer.isOpen()) {
                // Primaries and transfer ride the stream, not every decoded frame; without them an
                // HDR source would come out tagged as SDR.
                if (filtered->color_primaries == AVCOL_PRI_UNSPECIFIED)
                    filtered->color_primaries = vDec->color_primaries;
                if (filtered->color_trc == AVCOL_TRC_UNSPECIFIED)
                    filtered->color_trc = vDec->color_trc;
                if (!writer.open(spec.outputPath, filtered, frameRate, aDec != nullptr, 48000, plan,
                                 spec.output, errorOut)) {
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
        if (ptsUs >= outUs) {
            videoPastOut = true;
            return true;
        }
        // Real timestamps into the fps filter, relative to the in-point: it is what places each
        // frame on the output grid, instead of the frames simply being counted.
        lastVideoUs = ptsUs - inUs;
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
        AVStream *aStream = aFmt->streams[aIndex];
        const TimeUs ptsUs = framePtsUs(decoded, aStream->time_base);
        if (ptsUs + 20000 < audioInUs)
            return true;
        if (ptsUs >= audioOutUs) {
            audioPastOut = true;
            return true;
        }
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
        if (ptsUs < audioInUs) {
            const int drop = static_cast<int>(((audioInUs - ptsUs) * 48000) / kUsPerSecond);
            if (drop >= count)
                return true;
            offset = drop;
            count -= drop;
        }
        if (audioOutUs != std::numeric_limits<TimeUs>::max()) {
            const TimeUs endUs = ptsUs + (static_cast<TimeUs>(converted) * kUsPerSecond) / 48000;
            if (endUs > audioOutUs) {
                const int keep = static_cast<int>(((audioOutUs - std::max(ptsUs, audioInUs)) * 48000) / kUsPerSecond);
                count = std::min(count, std::max(0, keep));
            }
        }
        if (count <= 0)
            return true;
        return writeAudio(pcm.data() + offset * 2, count);
    };


    // Reads the replacement audio up to `untilUs` (kept-range time), or to its end.
    auto pumpExternalAudio = [&](TimeUs untilUs) -> bool {
        while (!extEof && extDecodedUs < untilUs) {
            if (av_read_frame(extFmt, extPacket) < 0) {
                extEof = true;
                break;
            }
            const bool mine = extPacket->stream_index == extIndex;
            const int send = mine ? avcodec_send_packet(aDec, extPacket) : 0;
            av_packet_unref(extPacket);
            if (send < 0 && send != AVERROR(EAGAIN))
                continue;
            while (mine && avcodec_receive_frame(aDec, extFrame) >= 0) {
                extDecodedUs = framePtsUs(extFrame, extFmt->streams[extIndex]->time_base)
                    + (TimeUs(extFrame->nb_samples) * kUsPerSecond) / std::max(1, aDec->sample_rate);
                const bool handled = handleAudio(extFrame);
                av_frame_unref(extFrame);
                if (!handled)
                    return false;
            }
        }
        return true;
    };

    while (ok && !(videoPastOut && (audioPastOut || !aDec || !swr || extFmt))) {
        if (av_read_frame(fmt, packet) < 0)
            break;
        AVCodecContext *dec = nullptr;
        if (packet->stream_index == videoIndex)
            dec = vDec;
        else if (aDec && !extFmt && packet->stream_index == audioIndex)
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
            // Half a second ahead of the video keeps the writer's two streams close together.
            if (dec == vDec && extFmt)
                ok = pumpExternalAudio(lastVideoUs + 500'000);
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
        if (ok && extFmt)
            ok = pumpExternalAudio(std::numeric_limits<TimeUs>::max());
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
        if (ok && aDec) {
            // The speed change holds back a little audio until the input ends.
            const float *processed = nullptr;
            int processedFrames = 0;
            ok = audioProcessor.finish(&processed, &processedFrames)
                ? writeProcessed(processed, processedFrames)
                : fail(trEdit("Could not apply the audio filter"));
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

bool analyzeVideo(const QString &inputPath, TimeUs inUs, TimeUs outUs, const QString &filter,
                  const QList<FilterOption> &filterOptions, QString *errorOut,
                  const std::function<bool(double)> &onProgress)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return false;
    };

    AVFormatContext *fmt = nullptr;
    if (openMediaInput(&fmt, inputPath) < 0)
        return fail(trEdit("Could not open the video"));
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return fail(trEdit("Could not read the video"));
    }

    const int videoIndex = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoIndex < 0) {
        avformat_close_input(&fmt);
        return fail(trEdit("That file has no video"));
    }

    AVStream *vStream = fmt->streams[videoIndex];
    const AVCodec *vDecCodec = avcodec_find_decoder(vStream->codecpar->codec_id);
    AVCodecContext *vDec = vDecCodec ? avcodec_alloc_context3(vDecCodec) : nullptr;
    if (vDec)
        vDec->thread_count = 0;
    if (!vDec || avcodec_parameters_to_context(vDec, vStream->codecpar) < 0
        || avcodec_open2(vDec, vDecCodec, nullptr) < 0) {
        if (vDec)
            avcodec_free_context(&vDec);
        avformat_close_input(&fmt);
        return fail(trEdit("Could not decode the video"));
    }

    QStringList chain = rotationChain(displayRotationOf(vStream));
    chain << filter;
    AVFilterGraph *graph = nullptr;
    AVFilterContext *filterSrc = nullptr;
    AVFilterContext *filterSink = nullptr;
    if (!buildFilterGraph(vDec, vStream, chain.join(QLatin1Char(',')), filterOptions, &graph,
                          &filterSrc, &filterSink)) {
        avcodec_free_context(&vDec);
        avformat_close_input(&fmt);
        return fail(trEdit("Could not set up the video analysis"));
    }

    inUs = std::max<TimeUs>(0, inUs);
    if (outUs < 0)
        outUs = std::numeric_limits<TimeUs>::max();
    const TimeUs durationUs = fmt->duration > 0 ? fmt->duration
                                                : av_rescale_q(vStream->duration, vStream->time_base,
                                                               {1, AV_TIME_BASE});
    const TimeUs spanUs = (outUs == std::numeric_limits<TimeUs>::max() ? durationUs : outUs) - inUs;
    if (inUs > 0) {
        const int64_t ts = av_rescale_q(inUs, {1, AV_TIME_BASE}, vStream->time_base);
        av_seek_frame(fmt, videoIndex, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(vDec);
    }
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    AVFrame *filtered = av_frame_alloc();
    bool ok = packet && frame && filtered;
    int framesAnalyzed = 0;
    bool pastOut = false;

    auto drainFilter = [&]() -> bool {
        for (;;) {
            const int rc = av_buffersink_get_frame(filterSink, filtered);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0)
                return fail(trEdit("Could not analyze a frame"));
            av_frame_unref(filtered);
        }
    };

    // Same seek, timestamp handling and trim as editVideo's handleVideo, so both passes feed the
    // filter the same frames.
    auto handleVideo = [&](AVFrame *decoded) -> bool {
        const TimeUs ptsUs = framePtsUs(decoded, vStream->time_base);
        if (ptsUs + 1000 < inUs)
            return true;
        if (ptsUs >= outUs) {
            pastOut = true;
            return true;
        }
        decoded->pts = ptsUs - inUs;
        decoded->duration = av_rescale_q(decoded->duration, vStream->time_base, {1, AV_TIME_BASE});
        if (av_buffersrc_add_frame_flags(filterSrc, decoded, AV_BUFFERSRC_FLAG_KEEP_REF) < 0)
            return fail(trEdit("Could not analyze a frame"));
        ++framesAnalyzed;
        if (!drainFilter())
            return false;
        if (spanUs > 0 && cancelled(onProgress, double(ptsUs - inUs) / double(spanUs)))
            return fail(trEdit("Cancelled"));
        return true;
    };

    auto receiveAll = [&]() -> bool {
        for (;;) {
            const int rc = avcodec_receive_frame(vDec, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0)
                return fail(trEdit("Could not decode the video"));
            const bool handled = handleVideo(frame);
            av_frame_unref(frame);
            if (!handled)
                return false;
        }
    };

    while (ok && !pastOut && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index != videoIndex) {
            av_packet_unref(packet);
            continue;
        }
        const int send = avcodec_send_packet(vDec, packet);
        av_packet_unref(packet);
        if (send < 0 && send != AVERROR(EAGAIN))
            continue;
        ok = receiveAll();
    }
    if (ok) {
        avcodec_send_packet(vDec, nullptr);
        ok = receiveAll();
    }
    if (ok)
        ok = av_buffersrc_add_frame_flags(filterSrc, nullptr, 0) >= 0 && drainFilter();
    if (ok && framesAnalyzed == 0)
        ok = fail(trEdit("No frames could be decoded from this clip"));

    av_frame_free(&filtered);
    av_frame_free(&frame);
    av_packet_free(&packet);
    // vidstabdetect writes its result file as the graph is torn down.
    avfilter_graph_free(&graph);
    avcodec_free_context(&vDec);
    avformat_close_input(&fmt);
    if (ok && onProgress)
        onProgress(1.0);
    return ok;
}

bool hasVideoFilter(const char *name)
{
    return avfilter_get_by_name(name) != nullptr;
}

QString stabilizationCacheDir()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty())
        return {};
    const QString dir = QDir(root).filePath(QStringLiteral("stabilization"));
    if (!QDir().mkpath(dir))
        return {};
    return dir;
}

QString newEditedMediaPath(const QString &projectId, const QString &kind)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString key = drift::projectPathKey(projectId);
    if (base.isEmpty() || key.isEmpty())
        return {};

    const QString dir = QDir(base).filePath(QStringLiteral("projects/%1/media").arg(key));
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

bool editMedia(const MediaEditSpec &request, QString *errorOut,
               const std::function<bool(double)> &onProgress)
{
    if (request.inputPath.isEmpty() || request.outputPath.isEmpty()) {
        if (errorOut)
            *errorOut = trEdit("Missing media path");
        return false;
    }
    if (request.kind == QLatin1String("image"))
        return editImage(request, errorOut, onProgress);
    if (!(request.speed > 0.0)) {
        if (errorOut)
            *errorOut = trEdit("Speed must be greater than zero");
        return false;
    }

    MediaEditSpec spec = request;
    std::function<bool(double)> progress = onProgress;
    if (!std::isnan(spec.normalizeLufs)) {
        // The measurement is a share of the progress bar; the encode gets the rest.
        constexpr double kMeasureShare = 0.25;
        double measured = qQNaN();
        bool wasCancelled = false;
        if (!measureKeptLoudness(spec, kMeasureShare, &measured, &wasCancelled, onProgress)) {
            if (errorOut)
                *errorOut = trEdit("Cancelled");
            return false;
        }
        if (!std::isnan(measured))
            spec.gainDb += std::clamp(spec.normalizeLufs - measured, -24.0, 24.0);
        spec.normalizeLufs = qQNaN();
        if (onProgress) {
            progress = [onProgress](double fraction) {
                return onProgress(kMeasureShare + (1.0 - kMeasureShare) * fraction);
            };
        }
    }

    if (spec.kind == QLatin1String("audio") || isAudioContainer(spec.output.container))
        return editAudio(spec, errorOut, progress);
    return editVideo(spec, errorOut, progress);
}

QStringList availableVideoCodecs()
{
    QStringList codecs;
    if (avcodec_find_encoder(AV_CODEC_ID_H264))
        codecs << QStringLiteral("h264");
    if (avcodec_find_encoder(AV_CODEC_ID_HEVC))
        codecs << QStringLiteral("hevc");
    if (avcodec_find_encoder(AV_CODEC_ID_PRORES))
        codecs << QStringLiteral("prores");
    return codecs;
}

TimeUs keyframeAtOrBefore(const QString &path, TimeUs t)
{
    AVFormatContext *fmt = nullptr;
    if (openMediaInput(&fmt, path) < 0)
        return t;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return t;
    }
    int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0)
        index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (index < 0) {
        avformat_close_input(&fmt);
        return t;
    }
    const AVRational timeBase = fmt->streams[index]->time_base;
    if (t > 0
        && av_seek_frame(fmt, index, av_rescale_q(t, {1, AV_TIME_BASE}, timeBase),
                         AVSEEK_FLAG_BACKWARD)
            < 0) {
        avformat_close_input(&fmt);
        return t;
    }

    TimeUs found = t;
    AVPacket *packet = av_packet_alloc();
    while (packet && av_read_frame(fmt, packet) >= 0) {
        const bool wanted = packet->stream_index == index && (packet->flags & AV_PKT_FLAG_KEY);
        const int64_t stamp = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        av_packet_unref(packet);
        if (wanted && stamp != AV_NOPTS_VALUE) {
            found = av_rescale_q(stamp, timeBase, {1, AV_TIME_BASE});
            break;
        }
    }
    av_packet_free(&packet);
    avformat_close_input(&fmt);
    return found;
}

bool remuxRange(const QString &input, const QString &output, TimeUs inUs, TimeUs outUs,
                TimeUs *actualInUs, QString *error, const std::function<bool(double)> &onProgress)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    inUs = std::max<TimeUs>(0, inUs);
    if (outUs < 0)
        outUs = std::numeric_limits<TimeUs>::max();
    if (outUs <= inUs)
        return fail(trEdit("Nothing to keep"));
    const TimeUs startUs = keyframeAtOrBefore(input, inUs);
    if (actualInUs)
        *actualInUs = startUs;

    AVFormatContext *in = nullptr;
    if (openMediaInput(&in, input) < 0)
        return fail(trEdit("Could not open the video"));
    if (avformat_find_stream_info(in, nullptr) < 0) {
        avformat_close_input(&in);
        return fail(trEdit("Could not read the video"));
    }
    int ref = av_find_best_stream(in, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (ref < 0)
        ref = av_find_best_stream(in, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (ref < 0) {
        avformat_close_input(&in);
        return fail(trEdit("That file has nothing to copy"));
    }

    const QString tmpPath = output + QStringLiteral(".part");
    QFile::remove(tmpPath);
    const QByteArray outUtf8 = output.toUtf8();
    const QByteArray tmpUtf8 = tmpPath.toUtf8();
    AVFormatContext *out = nullptr;
    avformat_alloc_output_context2(&out, nullptr, nullptr, outUtf8.constData());
    if (!out) {
        avformat_close_input(&in);
        return fail(trEdit("Could not create the output file"));
    }

    auto cleanup = [&](bool removeTmp) {
        if (out) {
            if (out->pb && !(out->oformat->flags & AVFMT_NOFILE))
                avio_closep(&out->pb);
            avformat_free_context(out);
            out = nullptr;
        }
        avformat_close_input(&in);
        if (removeTmp)
            QFile::remove(tmpPath);
    };

    std::vector<int> map(in->nb_streams, -1);
    for (unsigned i = 0; i < in->nb_streams; ++i) {
        const AVStream *ist = in->streams[i];
        const AVMediaType type = ist->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_VIDEO && type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_SUBTITLE)
            continue;
        AVStream *ost = avformat_new_stream(out, nullptr);
        if (!ost || avcodec_parameters_copy(ost->codecpar, ist->codecpar) < 0) {
            cleanup(false);
            return fail(trEdit("Could not create the output stream"));
        }
        ost->codecpar->codec_tag = 0;
        ost->time_base = ist->time_base;
        ost->sample_aspect_ratio = ist->sample_aspect_ratio;
        ost->avg_frame_rate = ist->avg_frame_rate;
        ost->disposition = ist->disposition;
        av_dict_copy(&ost->metadata, ist->metadata, 0);
        map[i] = ost->index;
    }

    if (!(out->oformat->flags & AVFMT_NOFILE)
        && avio_open(&out->pb, tmpUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
        cleanup(false);
        return fail(trEdit("Could not open the output file for writing"));
    }
    if (avformat_write_header(out, nullptr) < 0) {
        cleanup(false);
        return fail(trEdit("Could not write the output header"));
    }

    if (inUs > 0)
        av_seek_frame(in, ref, av_rescale_q(inUs, {1, AV_TIME_BASE}, in->streams[ref]->time_base),
                      AVSEEK_FLAG_BACKWARD);

    AVPacket *packet = av_packet_alloc();
    bool ok = packet != nullptr;
    bool sawKey = false;
    bool refDone = false;
    const double span = outUs == std::numeric_limits<TimeUs>::max()
        ? double(in->duration > 0 ? in->duration - startUs : 0)
        : double(outUs - startUs);
    while (ok && av_read_frame(in, packet) >= 0) {
        const int si = packet->stream_index;
        const AVStream *ist = in->streams[si];
        const bool isRef = si == ref;
        const int64_t dts = packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
        const int64_t pts = packet->pts != AV_NOPTS_VALUE ? packet->pts : packet->dts;
        if (map[si] < 0 || pts == AV_NOPTS_VALUE) {
            av_packet_unref(packet);
            continue;
        }
        const TimeUs ptsUs = av_rescale_q(pts, ist->time_base, {1, AV_TIME_BASE});
        const TimeUs dtsUs = av_rescale_q(dts, ist->time_base, {1, AV_TIME_BASE});
        if (isRef && !sawKey) {
            if (!(packet->flags & AV_PKT_FLAG_KEY)) {
                av_packet_unref(packet);
                continue;
            }
            sawKey = true;
        }
        // Decode order: once the reference stream's dts reaches the out-point nothing later in it
        // is wanted. The other streams run on a little further so none stops short.
        if (isRef && dtsUs >= outUs)
            refDone = true;
        if (refDone && dtsUs >= outUs + 1'000'000) {
            av_packet_unref(packet);
            break;
        }
        if (ptsUs < startUs || ptsUs >= outUs) {
            av_packet_unref(packet);
            continue;
        }

        const int64_t shift = av_rescale_q(startUs, {1, AV_TIME_BASE}, ist->time_base);
        packet->pts -= shift;
        if (packet->dts != AV_NOPTS_VALUE)
            packet->dts -= shift;
        av_packet_rescale_ts(packet, ist->time_base, out->streams[map[si]]->time_base);
        packet->stream_index = map[si];
        packet->pos = -1;
        const int wrc = av_interleaved_write_frame(out, packet);
        av_packet_unref(packet);
        if (wrc < 0) {
            if (error)
                *error = trEdit("Failed to write a packet");
            ok = false;
            break;
        }
        if (isRef && span > 0 && onProgress && !onProgress(std::clamp((ptsUs - startUs) / span, 0.0, 1.0))) {
            if (error)
                *error = trEdit("Cancelled");
            ok = false;
        }
    }
    av_packet_free(&packet);

    if (ok && av_write_trailer(out) < 0) {
        if (error)
            *error = trEdit("Could not write the output trailer");
        ok = false;
    }
    cleanup(!ok);
    if (!ok)
        return false;
    QFile::remove(output);
    if (!QFile::rename(tmpPath, output))
        return fail(trEdit("Could not move the edited file into place"));
    if (onProgress)
        onProgress(1.0);
    return true;
}

} // namespace drift
