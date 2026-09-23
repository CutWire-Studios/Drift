#include "SpeechAudio.h"

#include "AudioFileWriter.h"
#include "ClipReaderPool.h"
#include "MediaProbe.h"

#include <QDir>
#include <QUuid>
#include <QVector>

namespace drift {

namespace {
// Its own decode cursor, so a transcription scan never fights playback over the same file.
constexpr quint64 kSpeechScanStreamId = 0xA5'11'5C'A4'00'00'00'08ull;
} // namespace

std::vector<float> readMono16k(const QString &path, TimeUs inUs, TimeUs outUs,
                               const std::function<bool(double)> &progress, bool *cancelled)
{
    if (cancelled)
        *cancelled = false;
    if (outUs < 0) {
        outUs = MediaProbe::probe(path).durationUs;
    }
    std::vector<float> mono;
    if (outUs <= inUs)
        return mono;

    const int chunkFrames = 30 * kSpeechSampleRate;
    const TimeUs spanUs = outUs - inUs;
    mono.reserve(speechUsToSamples(spanUs) + 16);
    QVector<float> stereo;
    TimeUs pos = inUs;
    while (pos < outUs) {
        const int frames = static_cast<int>(
            qMin<int64_t>(chunkFrames, ((outUs - pos) * kSpeechSampleRate) / kUsPerSecond + 1));
        if (frames <= 0)
            break;
        stereo.resize(static_cast<qsizetype>(frames) * 2);
        const int got = ClipReaderPool::instance().readAudioInterleaved(
            path, kSpeechScanStreamId, pos, frames, kSpeechSampleRate, stereo.data());
        if (got <= 0)
            break;
        const size_t base = mono.size();
        mono.resize(base + got);
        for (int i = 0; i < got; ++i)
            mono[base + i] = 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
        pos += speechSamplesToUs(static_cast<size_t>(got));
        if (progress && !progress(std::min(1.0, static_cast<double>(pos - inUs) / spanUs))) {
            if (cancelled)
                *cancelled = true;
            return {};
        }
    }
    return mono;
}

QString writeTempFlac16k(const std::vector<float> &pcm, QString *errorOut)
{
    const QString path = QDir::temp().filePath(
        QStringLiteral("drift-speech-%1.flac").arg(QUuid::createUuid().toString(QUuid::Id128).left(12)));
    AudioFileWriter writer;
    if (!writer.open(path, kSpeechSampleRate, 1, errorOut))
        return {};
    constexpr int kBlock = 1 << 16;
    for (size_t off = 0; off < pcm.size(); off += kBlock) {
        const int n = static_cast<int>(std::min<size_t>(kBlock, pcm.size() - off));
        if (!writer.writeFrames(pcm.data() + off, n, errorOut)) {
            writer.abort();
            return {};
        }
    }
    if (!writer.finish(errorOut))
        return {};
    return path;
}

} // namespace drift
