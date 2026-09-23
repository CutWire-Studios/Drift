#include "AudioFileWriter.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
}

#include <algorithm>
#include <cmath>
#include <vector>

namespace drift {

namespace {

int16_t toS16(float sample)
{
    // Clamp before scaling: denoising is a gain change, and a clip that was already near full
    // scale can come back over it.
    const float clamped = std::clamp(sample, -1.0f, 1.0f);
    return int16_t(std::lround(clamped * 32767.0f));
}

} // namespace

struct AudioFileWriter::Impl
{
    AVFormatContext *fmt = nullptr;
    AVCodecContext *ctx = nullptr;
    AVStream *stream = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *pkt = nullptr;

    QString path;
    QString tmpPath;
    int channels = 2;
    int frameSize = 4608;
    int64_t nextPts = 0;
    bool finished = false;

    // Partial encoder frame: callers write whatever block size suits them, the encoder wants a
    // fixed one.
    std::vector<float> pending;

    bool drainPackets(QString *errorOut);
    bool encodeOneFrame(const float *interleaved, QString *errorOut);
    void teardown();
};

bool AudioFileWriter::Impl::drainPackets(QString *errorOut)
{
    for (;;) {
        const int rc = avcodec_receive_packet(ctx, pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to read an encoded audio packet");
            return false;
        }
        av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        const int wrc = av_interleaved_write_frame(fmt, pkt);
        av_packet_unref(pkt);
        if (wrc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to write an audio packet");
            return false;
        }
    }
}

bool AudioFileWriter::Impl::encodeOneFrame(const float *interleaved, QString *errorOut)
{
    if (av_frame_make_writable(frame) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Audio frame not writable");
        return false;
    }

    auto *out = reinterpret_cast<int16_t *>(frame->data[0]);
    for (int i = 0; i < frameSize * channels; ++i)
        out[i] = toS16(interleaved[i]);

    frame->pts = nextPts;
    nextPts += frameSize;

    if (avcodec_send_frame(ctx, frame) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Audio encoder rejected a frame");
        return false;
    }
    return drainPackets(errorOut);
}

void AudioFileWriter::Impl::teardown()
{
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

AudioFileWriter::AudioFileWriter() : d(std::make_unique<Impl>()) {}

AudioFileWriter::~AudioFileWriter()
{
    abort();
}

bool AudioFileWriter::open(const QString &path, int sampleRate, int channels, QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        d->teardown();
        return false;
    };

    if (sampleRate <= 0 || channels <= 0)
        return fail(QStringLiteral("Invalid audio format"));

    d->path = path;
    // Same temp-then-rename discipline as MatteWriter and Exporter: a cancelled run must not leave
    // a file that looks like a finished render.
    d->tmpPath = path + QStringLiteral(".part");
    d->channels = channels;
    if (QFile::exists(d->tmpPath))
        QFile::remove(d->tmpPath);

    const QByteArray tmpUtf8 = d->tmpPath.toUtf8();
    avformat_alloc_output_context2(&d->fmt, nullptr, "flac", tmpUtf8.constData());
    if (!d->fmt)
        return fail(QStringLiteral("Could not create the audio container"));

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_FLAC);
    if (!codec)
        return fail(QStringLiteral("FLAC encoder not available"));

    d->stream = avformat_new_stream(d->fmt, nullptr);
    if (!d->stream)
        return fail(QStringLiteral("Could not create the audio stream"));

    d->ctx = avcodec_alloc_context3(codec);
    if (!d->ctx)
        return fail(QStringLiteral("Could not allocate the audio encoder"));

    d->ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    d->ctx->sample_rate = sampleRate;
    av_channel_layout_default(&d->ctx->ch_layout, channels);
    d->ctx->time_base = AVRational{1, sampleRate};
    if (d->fmt->oformat->flags & AVFMT_GLOBALHEADER)
        d->ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(d->ctx, codec, nullptr) < 0)
        return fail(QStringLiteral("Could not open the FLAC encoder"));

    avcodec_parameters_from_context(d->stream->codecpar, d->ctx);
    d->stream->time_base = d->ctx->time_base;
    d->frameSize = d->ctx->frame_size > 0 ? d->ctx->frame_size : 4608;

    if (!(d->fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&d->fmt->pb, tmpUtf8.constData(), AVIO_FLAG_WRITE) < 0)
            return fail(QStringLiteral("Could not open the audio file for writing"));
    }

    if (avformat_write_header(d->fmt, nullptr) < 0)
        return fail(QStringLiteral("Could not write the audio header"));

    d->pkt = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (!d->pkt || !d->frame)
        return fail(QStringLiteral("Could not allocate audio buffers"));

    d->frame->format = AV_SAMPLE_FMT_S16;
    d->frame->sample_rate = sampleRate;
    av_channel_layout_copy(&d->frame->ch_layout, &d->ctx->ch_layout);
    d->frame->nb_samples = d->frameSize;
    if (av_frame_get_buffer(d->frame, 0) < 0)
        return fail(QStringLiteral("Could not allocate the audio frame"));

    d->pending.reserve(size_t(d->frameSize) * size_t(channels));
    return true;
}

bool AudioFileWriter::writeFrames(const float *interleaved, int frames, QString *errorOut)
{
    if (!d->ctx || !d->frame) {
        if (errorOut)
            *errorOut = QStringLiteral("Audio writer is not open");
        return false;
    }
    if (frames <= 0)
        return true;

    d->pending.insert(d->pending.end(), interleaved, interleaved + size_t(frames) * d->channels);

    const size_t block = size_t(d->frameSize) * size_t(d->channels);
    size_t offset = 0;
    while (d->pending.size() - offset >= block) {
        if (!d->encodeOneFrame(d->pending.data() + offset, errorOut))
            return false;
        offset += block;
    }
    d->pending.erase(d->pending.begin(), d->pending.begin() + offset);
    return true;
}

bool AudioFileWriter::finish(QString *errorOut)
{
    if (d->finished)
        return true;
    if (!d->ctx || !d->fmt) {
        if (errorOut)
            *errorOut = QStringLiteral("Audio writer is not open");
        return false;
    }

    // Zero-pad the last partial frame. FLAC is exact, so this lengthens the file by under a frame;
    // the clip's own duration is what bounds playback.
    if (!d->pending.empty()) {
        d->pending.resize(size_t(d->frameSize) * size_t(d->channels), 0.0f);
        if (!d->encodeOneFrame(d->pending.data(), errorOut))
            return false;
        d->pending.clear();
    }

    if (avcodec_send_frame(d->ctx, nullptr) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not flush the audio encoder");
        return false;
    }
    if (!d->drainPackets(errorOut))
        return false;
    if (av_write_trailer(d->fmt) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write the audio trailer");
        return false;
    }

    d->teardown();

    if (QFile::exists(d->path))
        QFile::remove(d->path);
    if (!QFile::rename(d->tmpPath, d->path)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not move the audio file into place");
        return false;
    }

    d->finished = true;
    return true;
}

void AudioFileWriter::abort()
{
    if (d->finished)
        return;
    d->teardown();
    if (!d->tmpPath.isEmpty() && QFile::exists(d->tmpPath))
        QFile::remove(d->tmpPath);
    d->tmpPath.clear();
}

QString denoiseCacheDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("denoised"));
    QDir().mkpath(dir);
    return dir;
}

QString generatedAudioDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("generated-audio"));
    QDir().mkpath(dir);
    return dir;
}

QString newVoiceoverPath()
{
    const QString dir = generatedAudioDir();
    if (dir.isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QDir(dir).filePath(QStringLiteral("voiceover-") + id + QStringLiteral(".flac"));
}

QString newDenoisePath(const QString &suffix)
{
    const QString dir = denoiseCacheDir();
    if (dir.isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QDir(dir).filePath(id + suffix + QStringLiteral(".flac"));
}

void sweepDenoisePreviews()
{
    const QString path = denoiseCacheDir();
    if (path.isEmpty())
        return;
    QDir dir(path);
    const QStringList stale =
        dir.entryList({QStringLiteral("*-preview.flac"), QStringLiteral("*-original.flac"),
                       QStringLiteral("*.flac.part")},
                      QDir::Files);
    for (const QString &name : stale)
        QFile::remove(dir.filePath(name));
}

} // namespace drift
