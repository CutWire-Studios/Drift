#include "MediaWaveform.h"

#include <QFileInfo>
#include <QVector>
#include <QtMath>

#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/samplefmt.h>
}

namespace {

// One pull window: 256k frames is 2 MB of interleaved stereo float. Small enough that a
// feature-length span costs megabytes rather than hundreds of them, large enough that the
// per-window call overhead stays in the noise.
constexpr int kVoiceChunkFrames = 1 << 18;

float frameSampleAbs(const AVFrame *frame, int channels, int channel, int sample)
{
    switch (frame->format) {
    case AV_SAMPLE_FMT_FLTP:
        return qAbs(reinterpret_cast<const float *>(frame->data[channel])[sample]);
    case AV_SAMPLE_FMT_FLT:
        return qAbs(reinterpret_cast<const float *>(frame->data[0])[sample * channels + channel]);
    case AV_SAMPLE_FMT_S16P:
        return qAbs(reinterpret_cast<const int16_t *>(frame->data[channel])[sample]) / 32768.0f;
    case AV_SAMPLE_FMT_S16:
        return qAbs(reinterpret_cast<const int16_t *>(frame->data[0])[sample * channels + channel])
            / 32768.0f;
    case AV_SAMPLE_FMT_S32P:
        return qAbs(reinterpret_cast<const int32_t *>(frame->data[channel])[sample])
            / 2147483648.0f;
    case AV_SAMPLE_FMT_S32:
        return qAbs(reinterpret_cast<const int32_t *>(frame->data[0])[sample * channels + channel])
            / 2147483648.0f;
    case AV_SAMPLE_FMT_DBLP:
        return static_cast<float>(
            qAbs(reinterpret_cast<const double *>(frame->data[channel])[sample]));
    case AV_SAMPLE_FMT_DBL:
        return static_cast<float>(
            qAbs(reinterpret_cast<const double *>(frame->data[0])[sample * channels + channel]));
    default:
        return 0.0f;
    }
}

int64_t estimateDurationSamples(const AVFormatContext *fmt, const AVStream *stream, int sampleRate)
{
    if (sampleRate <= 0)
        return 0;

    if (stream->duration > 0 && stream->time_base.num > 0 && stream->time_base.den > 0) {
        return av_rescale_q(stream->duration, stream->time_base, AVRational{1, sampleRate});
    }

    // fmt->duration is in AV_TIME_BASE (µs), not samples.
    if (fmt->duration > 0)
        return av_rescale(fmt->duration, sampleRate, AV_TIME_BASE);

    return 0;
}

void ingestFramePeaks(const AVFrame *frame, int channels, int sampleCount, int64_t durationSamples,
                      int64_t &totalSamples, QVector<float> &buckets)
{
    const int samples = frame->nb_samples;
    if (samples <= 0 || channels <= 0)
        return;

    for (int s = 0; s < samples; ++s) {
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c)
            peak = qMax(peak, frameSampleAbs(frame, channels, c, s));

        const int64_t denom = durationSamples > 0 ? durationSamples : qMax<int64_t>(totalSamples + 1, 1);
        const int idx = qBound(0, static_cast<int>((totalSamples * sampleCount) / denom),
                              sampleCount - 1);
        buckets[idx] = qMax(buckets[idx], peak);
        ++totalSamples;
    }
}

QVector<float> downsamplePeaks(const QVector<float> &fine, int sampleCount)
{
    if (fine.isEmpty() || sampleCount <= 0)
        return {};
    if (fine.size() <= sampleCount)
        return fine;

    QVector<float> buckets(sampleCount, 0.0f);
    for (int i = 0; i < fine.size(); ++i) {
        const int idx = qBound(0, static_cast<int>((static_cast<int64_t>(i) * sampleCount) / fine.size()),
                              sampleCount - 1);
        buckets[idx] = qMax(buckets[idx], fine[i]);
    }
    return buckets;
}

// Direct-form-I biquad, for the speech band-pass below. driftengine is kept JUCE-free, so
// the effect-rack filters in engine/audio aren't reachable from here.
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    double process(double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// RBJ cookbook. `q` selects the section's damping; cascading the two Butterworth Qs below
// gives a maximally flat 4th-order response.
Biquad makeBiquad(double freqHz, double sampleRate, double q, bool highPass)
{
    Biquad f;
    // Keep the corner clear of Nyquist, where the bilinear transform warps badly.
    const double freq = qBound(1.0, freqHz, sampleRate * 0.45);
    const double w0 = 2.0 * M_PI * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;

    if (highPass) {
        f.b0 = ((1.0 + cosw) / 2.0) / a0;
        f.b1 = -(1.0 + cosw) / a0;
        f.b2 = f.b0;
    } else {
        f.b0 = ((1.0 - cosw) / 2.0) / a0;
        f.b1 = (1.0 - cosw) / a0;
        f.b2 = f.b0;
    }
    f.a1 = (-2.0 * cosw) / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
}

// Butterworth section Qs for a 4th-order cascade.
constexpr double kButterQ1 = 0.54119610;
constexpr double kButterQ2 = 1.30656296;

bool openAudioDecoder(const QString &absolutePath, AVFormatContext **fmtOut, int *streamIndexOut,
                      AVCodecContext **codecCtxOut, int streamOrdinal = 0)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, absolutePath.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    int audioStreamIndex = -1;
    int audioCount = 0;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audioCount == streamOrdinal) {
                audioStreamIndex = static_cast<int>(i);
                break;
            }
            ++audioCount;
        }
    }

    if (audioStreamIndex < 0 && audioCount > 0) {
        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                audioStreamIndex = static_cast<int>(i);
                break;
            }
        }
    }

    if (audioStreamIndex < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    const AVCodecParameters *codecPar = fmt->streams[audioStreamIndex]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        avformat_close_input(&fmt);
        return false;
    }

    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecPar);
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt);
        return false;
    }

    *fmtOut = fmt;
    *streamIndexOut = audioStreamIndex;
    *codecCtxOut = codecCtx;
    return true;
}

} // namespace

MediaWaveform::Dense MediaWaveform::densePeaks(const QString &sourcePath, int peaksPerSecond,
                                               int maxPeaks, int streamOrdinal)
{
    Dense result;
    if (peaksPerSecond <= 0 || maxPeaks <= 0)
        return result;

    const QString absolutePath = QFileInfo(sourcePath).absoluteFilePath();
    if (absolutePath.isEmpty())
        return result;

    AVFormatContext *fmt = nullptr;
    int audioStreamIndex = -1;
    AVCodecContext *codecCtx = nullptr;
    if (!openAudioDecoder(absolutePath, &fmt, &audioStreamIndex, &codecCtx, streamOrdinal))
        return result;

    const AVStream *stream = fmt->streams[audioStreamIndex];
    const AVCodecParameters *codecPar = stream->codecpar;
    const int sampleRate = codecCtx->sample_rate > 0 ? codecCtx->sample_rate : codecPar->sample_rate;
    int64_t durationSamples = estimateDurationSamples(fmt, stream, sampleRate);

    const double knownDuration = (sampleRate > 0 && durationSamples > 0)
        ? static_cast<double>(durationSamples) / sampleRate
        : 0.0;
    // Fallback when container duration is missing: ~200 peaks/sec, then downsample.
    const bool useFine = knownDuration <= 0.0;
    const int sampleCount = useFine
        ? 0
        : qBound<int>(1, static_cast<int>(std::ceil(knownDuration * peaksPerSecond)), maxPeaks);

    QVector<float> buckets(sampleCount, 0.0f);
    QVector<float> finePeaks;
    const int fineHop = qMax(1, sampleRate > 0 ? sampleRate / 200 : 1);
    float fineMax = 0.0f;
    int fineCount = 0;

    int64_t totalSamples = 0;

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    auto processFrame = [&](AVFrame *decoded) {
        const int samples = decoded->nb_samples;
        const int channels = codecCtx->ch_layout.nb_channels;
        if (samples <= 0 || channels <= 0)
            return;
        if (av_get_bytes_per_sample(static_cast<AVSampleFormat>(decoded->format)) <= 0)
            return;

        if (useFine) {
            for (int s = 0; s < samples; ++s) {
                float peak = 0.0f;
                for (int c = 0; c < channels; ++c)
                    peak = qMax(peak, frameSampleAbs(decoded, channels, c, s));
                fineMax = qMax(fineMax, peak);
                if (++fineCount >= fineHop) {
                    finePeaks.append(fineMax);
                    fineMax = 0.0f;
                    fineCount = 0;
                }
                ++totalSamples;
            }
        } else {
            ingestFramePeaks(decoded, channels, sampleCount, durationSamples, totalSamples, buckets);
        }
    };

    while (packet && frame && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index != audioStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(codecCtx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (true) {
            const int rc = avcodec_receive_frame(codecCtx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0)
                break;
            processFrame(frame);
        }
    }

    // Drain decoder.
    if (packet && frame) {
        avcodec_send_packet(codecCtx, nullptr);
        while (true) {
            const int rc = avcodec_receive_frame(codecCtx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0)
                break;
            processFrame(frame);
        }
    }

    if (useFine && fineCount > 0)
        finePeaks.append(fineMax);

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmt);

    if (totalSamples == 0) {
        result.peaks = QVector<float>(sampleCount, 0.15f);
        result.durationSeconds = knownDuration;
        return result;
    }

    if (useFine) {
        // Duration is whatever we actually decoded, at fineHop samples per fine peak.
        const double decodedSeconds = sampleRate > 0
            ? static_cast<double>(totalSamples) / sampleRate
            : 0.0;
        const int target = qBound<int>(
            1, static_cast<int>(std::ceil(decodedSeconds * peaksPerSecond)), maxPeaks);
        result.peaks = downsamplePeaks(finePeaks, target);
        result.durationSeconds = decodedSeconds;
        return result;
    }

    result.durationSeconds = knownDuration;

    // If the container overstated duration, peaks only filled a prefix — spread
    // them across the full bucket range using the samples we actually decoded, which
    // also makes the decoded length the span the buckets now cover.
    if (durationSamples > totalSamples + totalSamples / 10) {
        QVector<float> fixed(sampleCount, 0.0f);
        for (int b = 0; b < sampleCount; ++b) {
            if (buckets[b] <= 0.0f)
                continue;
            const int b2 = qBound(
                0,
                static_cast<int>((static_cast<int64_t>(b) * durationSamples) / totalSamples),
                sampleCount - 1);
            fixed[b2] = qMax(fixed[b2], buckets[b]);
        }
        buckets = fixed;
        if (sampleRate > 0)
            result.durationSeconds = static_cast<double>(totalSamples) / sampleRate;
    }

    result.peaks = buckets;
    return result;
}

QVector<float> MediaWaveform::peaksForRange(const QString &sourcePath, double startSeconds,
                                            double endSeconds, int peaksPerSecond, int streamOrdinal)
{
    if (peaksPerSecond <= 0 || endSeconds <= startSeconds)
        return {};

    const QString absolutePath = QFileInfo(sourcePath).absoluteFilePath();
    if (absolutePath.isEmpty())
        return {};

    AVFormatContext *fmt = nullptr;
    int audioStreamIndex = -1;
    AVCodecContext *codecCtx = nullptr;
    if (!openAudioDecoder(absolutePath, &fmt, &audioStreamIndex, &codecCtx, streamOrdinal))
        return {};

    AVStream *stream = fmt->streams[audioStreamIndex];
    const int sampleRate = codecCtx->sample_rate > 0 ? codecCtx->sample_rate
                                                     : stream->codecpar->sample_rate;
    if (sampleRate <= 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt);
        return {};
    }

    const double start = qMax(0.0, startSeconds);
    const int bucketCount = static_cast<int>(std::ceil((endSeconds - start) * peaksPerSecond));
    if (bucketCount <= 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmt);
        return {};
    }

    QVector<float> buckets(bucketCount, 0.0f);

    // Stream timestamps are offset by start_time in some containers (MPEG-TS especially), and
    // both the seek target and the decoded pts have to account for it or the whole range
    // lands somewhere else in the file.
    const int64_t startTimeTs = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;

    // Land before the target so the first wanted sample is never inside a skipped packet.
    const int64_t seekTs = av_rescale_q(static_cast<int64_t>(start * AV_TIME_BASE),
                                        {1, AV_TIME_BASE}, stream->time_base)
                           + startTimeTs;
    av_seek_frame(fmt, audioStreamIndex, seekTs, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codecCtx);

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool anySample = false;
    bool pastEnd = false;
    // Position of the next decoded sample, tracked from frame timestamps so a seek that
    // lands early still maps samples to the right source time. Falls back to running on
    // from the previous frame when a codec gives no timestamp.
    double cursorSeconds = start;
    bool cursorValid = false;

    auto processFrame = [&](const AVFrame *decoded) {
        const int samples = decoded->nb_samples;
        const int channels = codecCtx->ch_layout.nb_channels;
        if (samples <= 0 || channels <= 0)
            return;
        if (av_get_bytes_per_sample(static_cast<AVSampleFormat>(decoded->format)) <= 0)
            return;

        const int64_t pts = decoded->best_effort_timestamp != AV_NOPTS_VALUE
                                ? decoded->best_effort_timestamp
                                : decoded->pts;
        if (pts != AV_NOPTS_VALUE) {
            cursorSeconds = (pts - startTimeTs) * av_q2d(stream->time_base);
            cursorValid = true;
        } else if (!cursorValid) {
            // No timestamps at all: assume the seek landed exactly on the requested time.
            cursorValid = true;
        }

        const double frameStart = cursorSeconds;
        if (frameStart >= endSeconds) {
            pastEnd = true;
            return;
        }
        cursorSeconds += static_cast<double>(samples) / sampleRate;
        if (cursorSeconds <= start)
            return;

        for (int s = 0; s < samples; ++s) {
            const double t = frameStart + static_cast<double>(s) / sampleRate;
            if (t < start)
                continue;
            const int idx = static_cast<int>((t - start) * peaksPerSecond);
            if (idx < 0 || idx >= bucketCount)
                continue;

            float peak = 0.0f;
            for (int c = 0; c < channels; ++c)
                peak = qMax(peak, frameSampleAbs(decoded, channels, c, s));
            buckets[idx] = qMax(buckets[idx], peak);
            anySample = true;
        }
    };

    while (!pastEnd && packet && frame && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index != audioStreamIndex) {
            av_packet_unref(packet);
            continue;
        }

        if (avcodec_send_packet(codecCtx, packet) < 0) {
            av_packet_unref(packet);
            continue;
        }
        av_packet_unref(packet);

        while (true) {
            const int rc = avcodec_receive_frame(codecCtx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0)
                break;
            processFrame(frame);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmt);

    if (!anySample)
        return {};
    return buckets;
}

QVariantList MediaWaveform::voicePeaks(qint64 totalFrames, int sampleRate, int buckets,
                                       const FillChunk &fill)
{
    QVariantList result;
    if (totalFrames <= 0 || buckets <= 0 || sampleRate <= 0 || !fill)
        return result;

    // Restrict to the speech band before measuring, so the lane tracks dialogue rather than
    // whatever is loudest. Roughly the telephone band: every formant that carries
    // intelligibility lives inside it, while bass, kick drums and low music sit below and
    // cymbals/hiss above.
    //
    // The rolloff matters as much as the corners. This was a pair of one-pole filters at
    // 150 Hz / 3.5 kHz, which is 6 dB/octave — a 60 Hz bassline still came through at
    // roughly 40% and drove the peaks. Four poles at the bottom put that same bassline
    // ~45 dB down.
    //
    // The top corner is 3 kHz rather than the usual 3.4: the caller mixes at 8 kHz, so
    // Nyquist is 4 kHz and a corner any higher has no room left to roll off in.
    Biquad highPass1 = makeBiquad(300.0, sampleRate, kButterQ1, true);
    Biquad highPass2 = makeBiquad(300.0, sampleRate, kButterQ2, true);
    Biquad lowPass1 = makeBiquad(3000.0, sampleRate, kButterQ1, false);
    Biquad lowPass2 = makeBiquad(3000.0, sampleRate, kButterQ2, false);

    QVector<float> peaks(buckets, 0.0f);
    float maxPeak = 0.0f;

    QVector<float> chunk(static_cast<qsizetype>(kVoiceChunkFrames) * 2);
    for (qint64 done = 0; done < totalFrames;) {
        const int want = static_cast<int>(qMin<qint64>(kVoiceChunkFrames, totalFrames - done));
        const int got = fill(chunk.data(), done, want);
        if (got <= 0)
            break;

        for (int i = 0; i < got; ++i) {
            const double mono = 0.5 * (static_cast<double>(chunk[i * 2])
                                       + static_cast<double>(chunk[i * 2 + 1]));
            double voice = highPass2.process(highPass1.process(mono));
            voice = lowPass2.process(lowPass1.process(voice));

            const int bucket = qBound(
                0, static_cast<int>(((done + i) * buckets) / totalFrames), buckets - 1);
            const float amp = static_cast<float>(qAbs(voice));
            if (amp > peaks[bucket])
                peaks[bucket] = amp;
            maxPeak = qMax(maxPeak, amp);
        }
        done += got;
    }

    // Normalize to the loudest voice moment so speech fills the lane while
    // silence/music sit near the floor.
    const double norm = maxPeak > 1e-6f ? 1.0 / static_cast<double>(maxPeak) : 0.0;
    for (int i = 0; i < buckets; ++i)
        result.append(qBound(0.05, static_cast<double>(peaks[i]) * norm, 1.0));

    return result;
}

QVector<float> MediaWaveform::mixedPeaks(qint64 totalFrames, int sampleRate, int buckets,
                                         const FillChunk &fill)
{
    if (totalFrames <= 0 || buckets <= 0 || sampleRate <= 0 || !fill)
        return {};

    QVector<float> peaks(buckets, 0.0f);

    QVector<float> chunk(static_cast<qsizetype>(kVoiceChunkFrames) * 2);
    for (qint64 done = 0; done < totalFrames;) {
        const int want = static_cast<int>(qMin<qint64>(kVoiceChunkFrames, totalFrames - done));
        const int got = fill(chunk.data(), done, want);
        if (got <= 0)
            break;

        for (int i = 0; i < got; ++i) {
            // Loudest of the two channels, not their average: a hard-panned hit would be
            // halved by a mono fold and read as quieter than it sounds.
            const float amp = qMax(qAbs(chunk[i * 2]), qAbs(chunk[i * 2 + 1]));
            const int bucket = qBound(
                0, static_cast<int>(((done + i) * buckets) / totalFrames), buckets - 1);
            if (amp > peaks[bucket])
                peaks[bucket] = amp;
        }
        done += got;
    }

    // The mixer soft-clips at 0.95 but nothing guarantees the pull did; clamp so the contract
    // holds for any FillChunk.
    for (float &p : peaks)
        p = qBound(0.0f, p, 1.0f);

    return peaks;
}

QVector<float> MediaWaveform::speechPeaks(qint64 totalFrames, int sampleRate, int buckets,
                                          const FillChunk &fill)
{
    if (totalFrames <= 0 || buckets <= 0 || sampleRate <= 0 || !fill)
        return {};

    Biquad highPass1 = makeBiquad(300.0, sampleRate, kButterQ1, true);
    Biquad highPass2 = makeBiquad(300.0, sampleRate, kButterQ2, true);
    Biquad lowPass1 = makeBiquad(3000.0, sampleRate, kButterQ1, false);
    Biquad lowPass2 = makeBiquad(3000.0, sampleRate, kButterQ2, false);

    QVector<float> peaks(buckets, 0.0f);
    QVector<float> chunk(static_cast<qsizetype>(kVoiceChunkFrames) * 2);
    for (qint64 done = 0; done < totalFrames;) {
        const int want = static_cast<int>(qMin<qint64>(kVoiceChunkFrames, totalFrames - done));
        const int got = fill(chunk.data(), done, want);
        if (got <= 0)
            break;
        for (int i = 0; i < got; ++i) {
            const double mono = 0.5 * (static_cast<double>(chunk[i * 2])
                                       + static_cast<double>(chunk[i * 2 + 1]));
            double voice = highPass2.process(highPass1.process(mono));
            voice = lowPass2.process(lowPass1.process(voice));
            const int bucket = qBound(
                0, static_cast<int>(((done + i) * buckets) / totalFrames), buckets - 1);
            const float amp = static_cast<float>(qAbs(voice));
            if (amp > peaks[bucket])
                peaks[bucket] = amp;
        }
        done += got;
    }
    return peaks;
}
