#include "engine/audio/analysis/AudioFrameAnalysis.h"

#include "core/project/Project.h"
#include "engine/audio/AudioMixer.h"
#include "engine/audio/analysis/AudioOnsets.h"
#include "engine/decode/readers/ClipReaderPool.h"

#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <vector>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

namespace {

constexpr int kWindowFrames = 2048;
constexpr int kAnalysisRate = 48000;
constexpr drift::TimeUs kHalfWindowUs = drift::TimeUs(kWindowFrames / 2) * drift::kUsPerSecond / kAnalysisRate;
constexpr int kTextureBins = 512;
constexpr int kFftBins = kWindowFrames / 2; // the Nyquist bin is past 20 kHz at any rate this runs at

constexpr double kMinHz = 20.0;
constexpr double kMaxHz = 20000.0;
constexpr double kLogRatio = kMaxHz / kMinHz;
constexpr double kPi = 3.14159265358979323846;
constexpr double kBassHz = 150.0;
constexpr double kFloorDb = -200.0;
constexpr double kSpectrumLowDb = -90.0;
constexpr double kSpectrumHighDb = -10.0;
constexpr double kLevelLowDb = -60.0;
constexpr double kLevelHighDb = 0.0;
constexpr double kBeatDecaySec = 0.09;

// The beat follower in the rest of the app runs on this rate, so the onsets match the MCP tool's.
constexpr int kOnsetRate = 22050;
constexpr int kOnsetChunkFrames = 1 << 18;
constexpr int kMaxOnsetEntries = 16;

// Each analysis read and each onset pass keeps its own decode cursor, so a background onset pass
// never seeks the window the preview is reading.
quint64 frameStreamSalt()
{
    return qHash(QStringLiteral("audio-input"));
}

quint64 onsetStreamSalt()
{
    return qHash(QStringLiteral("audio-onsets"));
}

float unit(double v)
{
    return static_cast<float>(std::clamp(v, 0.0, 1.0));
}

// dB mapped from [lo, hi] to [0, 1], clamped.
float ratio(double db, double lo, double hi)
{
    return unit((db - lo) / (hi - lo));
}

double toDb(double power)
{
    return std::max(kFloorDb, 10.0 * std::log10(std::max(power, 1e-20)));
}

// Track-level sources carry audio on audio tracks and on video tracks; adjustment and
// text/shape tracks never do.
bool carriesAudio(const drift::Track &track)
{
    return track.type == drift::TrackType::Audio || track.type == drift::TrackType::Video;
}

// The clips a source reads: every audible clip of the named track, or of every unmuted track for
// the Timeline mix. Composite clips are skipped here; they would need a nested mixer, and only the
// clip's own file is read.
template <typename Visit>
void forEachSourceClip(const drift::Project &project, const QString &trackId, Visit &&visit)
{
    for (const drift::Track &track : project.tracks()) {
        if (!carriesAudio(track))
            continue;
        if (trackId.isEmpty() ? track.muted : track.id != trackId)
            continue;
        for (const drift::Clip &clip : track.clips) {
            if (!clip.enabled || clip.path.isEmpty() || !clip.sequenceId.isEmpty())
                continue;
            if (track.type == drift::TrackType::Video
                && (clip.type != drift::ClipType::Video || clip.suppressEmbeddedAudio)) {
                continue;
            }
            visit(track, clip);
        }
    }
}

QVector<float> readSourceMonoAs(const drift::Project &project, const QString &trackId, drift::TimeUs startUs,
                                int frames, int sampleRate, quint64 salt)
{
    QVector<float> mono(std::max(0, frames), 0.0f);
    if (frames <= 0)
        return mono;
    forEachSourceClip(project, trackId, [&](const drift::Track &track, const drift::Clip &clip) {
        const QVector<float> stereo = AudioMixer::readClipGainedAudio(
            project, track, clip, ClipReaderPool::streamIdForClip(clip.id) ^ salt, startUs, frames, sampleRate);
        for (int i = 0; i < frames; ++i)
            mono[i] += 0.5f * (stereo[i * 2] + stereo[i * 2 + 1]);
    });
    return mono;
}

// Everything that changes what the source's onsets come out as. Project edits carry no serial
// the compositor can see, so the content is hashed instead.
quint64 audioFingerprint(const drift::Project &project, const QString &trackId)
{
    quint64 hash = 0;
    for (const drift::Track &track : project.tracks()) {
        if (!carriesAudio(track) || (!trackId.isEmpty() && track.id != trackId))
            continue;
        hash = qHashMulti(hash, track.id, int(track.muted), int(track.type));
        for (const drift::Transition &transition : track.transitions) {
            hash = qHashMulti(hash, transition.fromClipId, transition.toClipId, transition.kindId,
                              transition.durationUs);
        }
        for (const drift::Clip &clip : track.clips) {
            hash = qHashMulti(hash, clip.id, clip.path, clip.sequenceId, int(clip.type), clip.timelineStart,
                              clip.timelineDuration, clip.srcIn, clip.srcOut, clip.speed, int(clip.reverse),
                              clip.audioStreamIndex, int(clip.suppressEmbeddedAudio), clip.pan,
                              clip.fadeInUs, clip.fadeOutUs, clip.audioFadeInUs, clip.audioFadeOutUs,
                              int(clip.fadeCurve), int(clip.enabled));
            const auto &volumeKeys = clip.volume.keyframes();
            for (auto it = volumeKeys.cbegin(); it != volumeKeys.cend(); ++it)
                hash = qHashMulti(hash, it.key(), it.value().value);
        }
    }
    return hash;
}

// Onset times in absolute timeline seconds over the source's whole extent.
QList<double> detectOnsets(const drift::Project &project, const QString &trackId)
{
    drift::TimeUs firstUs = 0;
    drift::TimeUs lastUs = 0;
    bool any = false;
    forEachSourceClip(project, trackId, [&](const drift::Track &, const drift::Clip &clip) {
        firstUs = any ? std::min(firstUs, clip.timelineStart) : clip.timelineStart;
        lastUs = any ? std::max(lastUs, clip.timelineEnd()) : clip.timelineEnd();
        any = true;
    });
    if (!any || lastUs <= firstUs)
        return {};

    const int frames = static_cast<int>((lastUs - firstUs) * kOnsetRate / drift::kUsPerSecond);
    QVector<float> mono(frames);
    for (int done = 0; done < frames; done += kOnsetChunkFrames) {
        const int count = std::min(kOnsetChunkFrames, frames - done);
        const drift::TimeUs at = firstUs + static_cast<drift::TimeUs>(done) * drift::kUsPerSecond / kOnsetRate;
        const QVector<float> chunk = readSourceMonoAs(project, trackId, at, count, kOnsetRate, onsetStreamSalt());
        std::copy(chunk.cbegin(), chunk.cend(), mono.begin() + done);
    }

    QList<double> times;
    const AudioBeatAnalysis analysis = AudioOnsets::analyze(mono.constData(), frames, kOnsetRate,
                                                            static_cast<double>(firstUs) / drift::kUsPerSecond);
    for (const AudioOnset &onset : analysis.onsets)
        times.append(onset.seconds);
    return times;
}

using OnsetTimes = QList<double>;
using OnsetFuture = std::shared_future<std::shared_ptr<const OnsetTimes>>;

struct OnsetCacheEntry
{
    QString key;
    quint64 fingerprint = 0;
    OnsetFuture onsets;
};

QMutex &onsetCacheMutex()
{
    static QMutex mutex;
    return mutex;
}

// Most recent first. Entries hold a future, so one still being computed is shared, not restarted.
QList<OnsetCacheEntry> &onsetCache()
{
    static QList<OnsetCacheEntry> cache;
    return cache;
}

// The source's onsets, or null while they are still being computed. With `wait`, computes them on
// this thread when nothing is cached or in flight.
std::shared_ptr<const OnsetTimes> onsetsFor(const drift::Project &project, const QString &trackId, bool wait)
{
    const QString key = trackId.isEmpty() ? QStringLiteral("<mix>") : trackId;
    const quint64 fingerprint = audioFingerprint(project, trackId);

    OnsetFuture future;
    {
        QMutexLocker lock(&onsetCacheMutex());
        for (const OnsetCacheEntry &entry : onsetCache()) {
            if (entry.key == key && entry.fingerprint == fingerprint) {
                future = entry.onsets;
                break;
            }
        }
    }

    if (!future.valid()) {
        if (wait) {
            std::promise<std::shared_ptr<const OnsetTimes>> done;
            done.set_value(std::make_shared<const OnsetTimes>(detectOnsets(project, trackId)));
            future = done.get_future().share();
        } else {
            // The worker reads a copy: the GUI thread keeps editing the live project.
            drift::Project snapshot = project;
            future = std::async(std::launch::async, [snapshot = std::move(snapshot), trackId]() {
                         return std::make_shared<const OnsetTimes>(detectOnsets(snapshot, trackId));
                     }).share();
        }

        QMutexLocker lock(&onsetCacheMutex());
        auto &cache = onsetCache();
        for (int i = cache.size() - 1; i >= 0; --i) {
            if (cache.at(i).key == key)
                cache.removeAt(i);
        }
        cache.prepend(OnsetCacheEntry{key, fingerprint, future});
        while (cache.size() > kMaxOnsetEntries)
            cache.removeLast();
    }

    if (!wait && future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return nullptr;
    return future.get();
}

float beatAt(const drift::Project &project, const QString &trackId, drift::TimeUs timelineUs, bool wait)
{
    const std::shared_ptr<const OnsetTimes> onsets = onsetsFor(project, trackId, wait);
    if (!onsets || onsets->isEmpty())
        return 0.0f;
    const double seconds = static_cast<double>(timelineUs) / drift::kUsPerSecond;
    const auto next = std::upper_bound(onsets->cbegin(), onsets->cend(), seconds);
    if (next == onsets->cbegin())
        return 0.0f;
    return static_cast<float>(std::exp(-(seconds - *std::prev(next)) / kBeatDecaySec));
}

} // namespace

QByteArray AudioFrame::rgba() const
{
    QByteArray out(kTextureBins * 2 * 4, '\0');
    auto *pixels = reinterpret_cast<unsigned char *>(out.data());
    for (int row = 0; row < 2; ++row) {
        const std::array<float, 512> &values = row == 0 ? spectrum : wave;
        for (int x = 0; x < kTextureBins; ++x) {
            const auto byte = static_cast<unsigned char>(std::lround(unit(values[x]) * 255.0f));
            unsigned char *px = pixels + (row * kTextureBins + x) * 4;
            px[0] = px[1] = px[2] = byte;
            px[3] = 255;
        }
    }
    return out;
}

// Normalisation: the window is multiplied by a periodic Hann and transformed. A bin's magnitude is
// scaled by 2 / sum(window), so a sine of amplitude A reads A at its peak bin, and a full-scale
// sine reads 0 dBFS (up to the Hann scalloping loss of at most 1.4 dB between bins). Power is
// the square of that magnitude, and dB is 10 log10 of it.
AudioFrame analyzeWindow(const float *mono, int sampleRate)
{
    AudioFrame frame;

    constexpr int kWavePerBin = kWindowFrames / kTextureBins;
    for (int x = 0; x < kTextureBins; ++x) {
        double sum = 0.0;
        for (int j = 0; j < kWavePerBin; ++j)
            sum += mono[x * kWavePerBin + j];
        frame.wave[x] = unit((sum / kWavePerBin + 1.0) / 2.0);
    }

    double sumSquares = 0.0;
    for (int n = 0; n < kWindowFrames; ++n)
        sumSquares += static_cast<double>(mono[n]) * mono[n];
    frame.level = ratio(toDb(sumSquares / kWindowFrames), kLevelLowDb, kLevelHighDb);

    AVTXContext *tx = nullptr;
    av_tx_fn fn = nullptr;
    float txScale = 1.0f;
    if (av_tx_init(&tx, &fn, AV_TX_FLOAT_RDFT, 0, kWindowFrames, &txScale, 0) < 0)
        return frame;
    std::unique_ptr<float, void (*)(void *)> in(static_cast<float *>(av_malloc(sizeof(float) * kWindowFrames)),
                                               av_free);
    std::unique_ptr<float, void (*)(void *)> out(
        static_cast<float *>(av_malloc(sizeof(float) * 2 * (kFftBins + 1))), av_free);
    if (!in || !out) {
        av_tx_uninit(&tx);
        return frame;
    }

    double windowSum = 0.0;
    for (int n = 0; n < kWindowFrames; ++n) {
        const double hann = 0.5 * (1.0 - std::cos(2.0 * kPi * n / kWindowFrames));
        windowSum += hann;
        in.get()[n] = static_cast<float>(mono[n] * hann);
    }
    fn(tx, out.get(), in.get(), sizeof(float));
    av_tx_uninit(&tx);

    const double magnitudeScale = 2.0 / windowSum;
    std::vector<double> db(kFftBins);
    std::vector<double> power(kFftBins);
    for (int i = 0; i < kFftBins; ++i) {
        const double re = out.get()[2 * i];
        const double im = out.get()[2 * i + 1];
        const double magnitude = std::hypot(re, im) * magnitudeScale;
        power[i] = magnitude * magnitude;
        db[i] = toDb(power[i]);
    }

    // Each log bin takes the loudest FFT bin it covers. Log bins narrower than an FFT bin cover
    // none at the bottom of the range; they read the FFT bin nearest their centre.
    constexpr double kLowest = std::numeric_limits<double>::lowest();
    std::vector<double> maxDb(kTextureBins, kLowest);
    for (int i = 0; i < kFftBins; ++i) {
        const double hz = static_cast<double>(i) * sampleRate / kWindowFrames;
        if (hz < kMinHz || hz >= kMaxHz)
            continue;
        const int k = std::min(kTextureBins - 1,
                               static_cast<int>(std::floor(std::log(hz / kMinHz) / std::log(kLogRatio)
                                                           * kTextureBins)));
        maxDb[k] = std::max(maxDb[k], db[i]);
    }
    for (int k = 0; k < kTextureBins; ++k) {
        if (maxDb[k] != kLowest)
            continue;
        const double centre = kMinHz * std::pow(kMaxHz / kMinHz, (k + 0.5) / kTextureBins);
        const int i = std::clamp(static_cast<int>(std::lround(centre * kWindowFrames / sampleRate)), 0,
                                 kFftBins - 1);
        maxDb[k] = db[i];
    }
    for (int k = 0; k < kTextureBins; ++k)
        frame.spectrum[k] = ratio(maxDb[k], kSpectrumLowDb, kSpectrumHighDb);

    // Mean power of the bins below 150 Hz, DC included.
    double bassPower = 0.0;
    int bassBins = 0;
    for (int i = 0; i < kFftBins && static_cast<double>(i) * sampleRate / kWindowFrames < kBassHz; ++i) {
        bassPower += power[i];
        ++bassBins;
    }
    frame.bass = ratio(toDb(bassPower / std::max(1, bassBins)), kLevelLowDb, kLevelHighDb);
    return frame;
}

AudioFrame analyzeAt(const drift::Project &project, const QString &trackId, drift::TimeUs timelineUs,
                     bool waitForOnsets)
{
    const QVector<float> mono =
        readSourceMono(project, trackId, timelineUs - kHalfWindowUs, kWindowFrames, kAnalysisRate);
    AudioFrame frame = analyzeWindow(mono.constData(), kAnalysisRate);
    frame.beat = beatAt(project, trackId, timelineUs, waitForOnsets);
    return frame;
}

QVector<float> readSourceMono(const drift::Project &project, const QString &trackId, drift::TimeUs startUs,
                              int frames, int sampleRate)
{
    return readSourceMonoAs(project, trackId, startUs, frames, sampleRate, frameStreamSalt());
}
