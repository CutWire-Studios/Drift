#include "engine/capture/CaptureWriter.h"

#include <QFile>

#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace drift {

namespace {

// Microseconds. A screen grab's frames are irregular, so the stream has to be able to express any
// instant one arrived at rather than a multiple of a frame duration.
constexpr AVRational kVideoTimeBase{1, 1'000'000};

// What AAC takes per call. The FIFO exists purely to turn a capture backend's arbitrary buffer
// sizes into exactly this.
constexpr int kAacFrameSamples = 1024;

} // namespace

struct CaptureWriter::Impl
{
    AVFormatContext *fmt = nullptr;

    AVCodecContext *video = nullptr;
    AVStream *videoStream = nullptr;
    AVFrame *videoFrame = nullptr;
    SwsContext *sws = nullptr;

    AVCodecContext *audio = nullptr;
    AVStream *audioStream = nullptr;
    AVFrame *audioFrame = nullptr;
    AVAudioFifo *fifo = nullptr;
    SwrContext *swr = nullptr;
    int64_t audioSamplesWritten = 0;

    AVPacket *pkt = nullptr;

    QString path;
    QString tmpPath;
    QSize size;
    Settings settings;
    int64_t lastVideoPts = -1;
    int dropped = 0;
    bool headerWritten = false;
    bool finished = false;

    bool drain(AVCodecContext *codec, AVStream *stream, QString *errorOut);
    bool encodeAudioFrame(QString *errorOut);
    void teardown();
};

bool CaptureWriter::Impl::drain(AVCodecContext *codec, AVStream *stream, QString *errorOut)
{
    for (;;) {
        const int rc = avcodec_receive_packet(codec, pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to read an encoded packet");
            return false;
        }
        av_packet_rescale_ts(pkt, codec->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        const int wrc = av_interleaved_write_frame(fmt, pkt);
        av_packet_unref(pkt);
        if (wrc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to write a packet to the recording");
            return false;
        }
    }
}

void CaptureWriter::Impl::teardown()
{
    if (sws) {
        sws_freeContext(sws);
        sws = nullptr;
    }
    if (swr)
        swr_free(&swr);
    if (fifo) {
        av_audio_fifo_free(fifo);
        fifo = nullptr;
    }
    if (videoFrame)
        av_frame_free(&videoFrame);
    if (audioFrame)
        av_frame_free(&audioFrame);
    if (pkt)
        av_packet_free(&pkt);
    if (video)
        avcodec_free_context(&video);
    if (audio)
        avcodec_free_context(&audio);
    if (fmt) {
        if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
        fmt = nullptr;
    }
    videoStream = nullptr;
    audioStream = nullptr;
}

CaptureWriter::CaptureWriter()
    : d(std::make_unique<Impl>())
{
}

CaptureWriter::~CaptureWriter()
{
    abort();
}

int CaptureWriter::droppedFrames() const
{
    return d->dropped;
}

bool CaptureWriter::open(const QString &path, const Settings &settings, QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        d->teardown();
        return false;
    };

    if (settings.size.isEmpty())
        return fail(QStringLiteral("The recording has no size"));
    if (settings.size.width() % 2 != 0 || settings.size.height() % 2 != 0)
        return fail(QStringLiteral("A recording's width and height must both be even"));
    if (path.isEmpty())
        return fail(QStringLiteral("The recording has nowhere to go"));

    d->path = path;
    d->settings = settings;
    d->size = settings.size;
    d->tmpPath = path + QStringLiteral(".part");
    if (QFile::exists(d->tmpPath))
        QFile::remove(d->tmpPath);

    const QByteArray tmpUtf8 = d->tmpPath.toUtf8();
    avformat_alloc_output_context2(&d->fmt, nullptr, "mp4", tmpUtf8.constData());
    if (!d->fmt)
        return fail(QStringLiteral("Could not create the recording container"));

    const AVCodec *h264 = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!h264)
        return fail(QStringLiteral("H.264 encoder not available"));

    d->videoStream = avformat_new_stream(d->fmt, nullptr);
    d->video = avcodec_alloc_context3(h264);
    if (!d->videoStream || !d->video)
        return fail(QStringLiteral("Could not allocate the recording's video stream"));

    d->video->width = d->size.width();
    d->video->height = d->size.height();
    // yuv420p and 8-bit, deliberately: the widest thing that decodes everywhere, and what keeps a
    // long screen recording from being larger than the session it recorded.
    d->video->pix_fmt = AV_PIX_FMT_YUV420P;
    d->video->time_base = kVideoTimeBase;
    d->video->framerate = AVRational{qMax(1, settings.fps), 1};
    // Two seconds between keyframes. Every frame a keyframe (what a matte needs) would multiply
    // the size of a desktop recording for no benefit, while a long GOP makes scrubbing it on the
    // timeline slow.
    d->video->gop_size = qMax(1, settings.fps) * 2;
    // Screen content is mostly static with sudden whole-screen changes, which is exactly where
    // B-frames cost latency and buy little, and they complicate the irregular timestamps.
    d->video->max_b_frames = 0;
    if (d->fmt->oformat->flags & AVFMT_GLOBALHEADER)
        d->video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(d->video->priv_data, "crf", QByteArray::number(settings.crf).constData(), 0);
    // A recorder must not fall behind what it is recording: this has to encode faster than real
    // time on the machine being captured, which is also busy doing whatever is being recorded.
    av_opt_set(d->video->priv_data, "preset", "veryfast", 0);
    av_opt_set(d->video->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(d->video, h264, nullptr) < 0)
        return fail(QStringLiteral("Could not open the H.264 encoder"));
    avcodec_parameters_from_context(d->videoStream->codecpar, d->video);
    d->videoStream->time_base = kVideoTimeBase;
    d->videoStream->avg_frame_rate = AVRational{qMax(1, settings.fps), 1};

    if (settings.audio) {
        const AVCodec *aac = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!aac)
            return fail(QStringLiteral("AAC encoder not available"));
        if (settings.audioSampleRate <= 0 || settings.audioChannels <= 0)
            return fail(QStringLiteral("The recording's audio format is invalid"));

        d->audioStream = avformat_new_stream(d->fmt, nullptr);
        d->audio = avcodec_alloc_context3(aac);
        if (!d->audioStream || !d->audio)
            return fail(QStringLiteral("Could not allocate the recording's audio stream"));

        d->audio->sample_rate = settings.audioSampleRate;
        d->audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
        d->audio->bit_rate = 192000;
        d->audio->time_base = AVRational{1, settings.audioSampleRate};
        av_channel_layout_default(&d->audio->ch_layout, settings.audioChannels);
        if (d->fmt->oformat->flags & AVFMT_GLOBALHEADER)
            d->audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(d->audio, aac, nullptr) < 0)
            return fail(QStringLiteral("Could not open the AAC encoder"));
        avcodec_parameters_from_context(d->audioStream->codecpar, d->audio);
        d->audioStream->time_base = d->audio->time_base;

        // Capture backends hand over whatever buffer size their device uses; AAC wants exactly
        // 1024 samples a frame. The FIFO is what reconciles the two without dropping samples at
        // every boundary, which would be an audible tick per buffer.
        d->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, settings.audioChannels, kAacFrameSamples * 8);
        if (!d->fifo)
            return fail(QStringLiteral("Could not allocate the audio buffer"));

        if (swr_alloc_set_opts2(&d->swr, &d->audio->ch_layout, AV_SAMPLE_FMT_FLTP,
                                settings.audioSampleRate, &d->audio->ch_layout,
                                AV_SAMPLE_FMT_FLT, settings.audioSampleRate, 0, nullptr)
                < 0
            || swr_init(d->swr) < 0) {
            return fail(QStringLiteral("Could not create the audio converter"));
        }

        d->audioFrame = av_frame_alloc();
        if (!d->audioFrame)
            return fail(QStringLiteral("Could not allocate the audio frame"));
        d->audioFrame->format = AV_SAMPLE_FMT_FLTP;
        d->audioFrame->sample_rate = settings.audioSampleRate;
        d->audioFrame->nb_samples = kAacFrameSamples;
        av_channel_layout_copy(&d->audioFrame->ch_layout, &d->audio->ch_layout);
        if (av_frame_get_buffer(d->audioFrame, 0) < 0)
            return fail(QStringLiteral("Could not allocate the audio frame buffer"));
    }

    if (!(d->fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&d->fmt->pb, tmpUtf8.constData(), AVIO_FLAG_WRITE) < 0)
            return fail(QStringLiteral("Could not open the recording for writing"));
    }
    if (avformat_write_header(d->fmt, nullptr) < 0)
        return fail(QStringLiteral("Could not write the recording header"));
    d->headerWritten = true;

    d->pkt = av_packet_alloc();
    d->videoFrame = av_frame_alloc();
    if (!d->pkt || !d->videoFrame)
        return fail(QStringLiteral("Could not allocate recording buffers"));

    d->videoFrame->format = AV_PIX_FMT_YUV420P;
    d->videoFrame->width = d->size.width();
    d->videoFrame->height = d->size.height();
    if (av_frame_get_buffer(d->videoFrame, 0) < 0)
        return fail(QStringLiteral("Could not allocate the video frame"));

    // Screen and camera frames arrive as 32-bit BGRA. Converting here rather than asking the
    // backend to is what lets one writer serve the portal, Windows and macOS alike.
    d->sws = sws_getContext(d->size.width(), d->size.height(), AV_PIX_FMT_BGRA, d->size.width(),
                            d->size.height(), AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr,
                            nullptr);
    if (!d->sws)
        return fail(QStringLiteral("Could not create the recording's colour converter"));

    return true;
}

bool CaptureWriter::writeVideoFrame(const QImage &image, TimeUs timestampUs, QString *errorOut)
{
    if (!d->video || !d->videoFrame || d->finished) {
        if (errorOut)
            *errorOut = QStringLiteral("The recording is not open");
        return false;
    }

    // A PTS that does not advance makes the file unseekable, and a capture backend repeating a
    // timestamp is normal enough (two frames inside the same clock tick) that it cannot be fatal.
    const int64_t pts = qMax<int64_t>(0, timestampUs);
    if (pts <= d->lastVideoPts) {
        ++d->dropped;
        return true;
    }

    QImage src = image.format() == QImage::Format_ARGB32
                     ? image
                     : image.convertToFormat(QImage::Format_ARGB32);
    if (src.size() != d->size)
        src = src.scaled(d->size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    if (av_frame_make_writable(d->videoFrame) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not make the video frame writable");
        return false;
    }

    const uint8_t *planes[1] = {src.constBits()};
    const int strides[1] = {int(src.bytesPerLine())};
    sws_scale(d->sws, planes, strides, 0, d->size.height(), d->videoFrame->data,
              d->videoFrame->linesize);

    d->videoFrame->pts = pts;
    d->lastVideoPts = pts;
    if (avcodec_send_frame(d->video, d->videoFrame) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not encode a recorded frame");
        return false;
    }
    return d->drain(d->video, d->videoStream, errorOut);
}

bool CaptureWriter::Impl::encodeAudioFrame(QString *errorOut)
{
    if (av_audio_fifo_size(fifo) < kAacFrameSamples)
        return true;
    if (av_frame_make_writable(audioFrame) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not make the audio frame writable");
        return false;
    }
    if (av_audio_fifo_read(fifo, reinterpret_cast<void **>(audioFrame->data), kAacFrameSamples)
        < kAacFrameSamples) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not read buffered audio");
        return false;
    }
    // Timed by samples written, not by a wall clock: a device that delivers slightly early or late
    // must not shift the audio against the video, and counting is exact.
    audioFrame->pts = audioSamplesWritten;
    audioSamplesWritten += kAacFrameSamples;
    if (avcodec_send_frame(audio, audioFrame) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not encode recorded audio");
        return false;
    }
    return drain(audio, audioStream, errorOut);
}

bool CaptureWriter::writeAudio(const float *interleaved, int frames, QString *errorOut)
{
    if (!d->audio || !d->fifo || d->finished) {
        if (errorOut)
            *errorOut = QStringLiteral("This recording has no audio");
        return false;
    }
    if (!interleaved || frames <= 0)
        return true;

    const int channels = d->settings.audioChannels;
    // Deinterleave into the planar layout AAC wants. swresample does this and nothing else here,
    // since the rate and channel count already match the device.
    std::vector<float> planar(size_t(frames) * size_t(channels));
    std::vector<uint8_t *> out(static_cast<size_t>(channels), nullptr);
    for (int c = 0; c < channels; ++c)
        out[size_t(c)] = reinterpret_cast<uint8_t *>(planar.data() + size_t(c) * size_t(frames));
    const uint8_t *in[1] = {reinterpret_cast<const uint8_t *>(interleaved)};
    if (swr_convert(d->swr, out.data(), frames, in, frames) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not convert recorded audio");
        return false;
    }

    if (av_audio_fifo_realloc(d->fifo, av_audio_fifo_size(d->fifo) + frames) < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not grow the audio buffer");
        return false;
    }
    if (av_audio_fifo_write(d->fifo, reinterpret_cast<void **>(out.data()), frames) < frames) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not buffer recorded audio");
        return false;
    }

    while (av_audio_fifo_size(d->fifo) >= kAacFrameSamples) {
        if (!d->encodeAudioFrame(errorOut))
            return false;
    }
    return true;
}

bool CaptureWriter::finish(QString *errorOut)
{
    if (d->finished)
        return true;
    if (!d->fmt || !d->headerWritten) {
        if (errorOut)
            *errorOut = QStringLiteral("The recording is not open");
        return false;
    }

    // Whatever is left in the FIFO is a partial AAC frame. Padding it with silence is better than
    // dropping it: the alternative truncates up to 21 ms off the end of the sound.
    if (d->audio && d->fifo && av_audio_fifo_size(d->fifo) > 0) {
        const int remaining = av_audio_fifo_size(d->fifo);
        std::vector<float> silence(size_t(kAacFrameSamples - remaining), 0.0f);
        std::vector<uint8_t *> pads(size_t(d->settings.audioChannels));
        for (int c = 0; c < d->settings.audioChannels; ++c)
            pads[size_t(c)] = reinterpret_cast<uint8_t *>(silence.data());
        if (av_audio_fifo_realloc(d->fifo, kAacFrameSamples) >= 0)
            av_audio_fifo_write(d->fifo, reinterpret_cast<void **>(pads.data()),
                                kAacFrameSamples - remaining);
        d->encodeAudioFrame(errorOut);
    }

    if (d->video) {
        avcodec_send_frame(d->video, nullptr);
        d->drain(d->video, d->videoStream, errorOut);
    }
    if (d->audio) {
        avcodec_send_frame(d->audio, nullptr);
        d->drain(d->audio, d->audioStream, errorOut);
    }

    const bool trailer = av_write_trailer(d->fmt) >= 0;
    d->teardown();
    d->finished = true;

    if (!trailer) {
        QFile::remove(d->tmpPath);
        if (errorOut)
            *errorOut = QStringLiteral("Could not finish writing the recording");
        return false;
    }

    if (QFile::exists(d->path))
        QFile::remove(d->path);
    if (!QFile::rename(d->tmpPath, d->path)) {
        QFile::remove(d->tmpPath);
        if (errorOut)
            *errorOut = QStringLiteral("Could not move the recording into place");
        return false;
    }
    return true;
}

void CaptureWriter::abort()
{
    if (d->finished) {
        d->finished = true;
        return;
    }
    d->teardown();
    d->finished = true;
    if (!d->tmpPath.isEmpty())
        QFile::remove(d->tmpPath);
}

} // namespace drift
