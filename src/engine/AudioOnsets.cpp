#include "engine/AudioOnsets.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

namespace {

constexpr int kNFft = 1024;
constexpr int kHop = 256;
constexpr int kNBins = kNFft / 2 + 1;

// Flux is a difference against the previous frame, so a transient sitting at sample 0
// would produce none at all. Pad the front with a whole window of silence so the first
// real frame has something to differ from.
constexpr int kPadFrames = kNFft;
// A transient first shows up in the earliest frame whose window overlaps it, which is
// up to (kNFft - kHop) samples ahead of that window's nominal start. Reporting the
// window start would therefore place every onset early by up to 35 ms.
constexpr int kDetectionLatency = kNFft - kHop;

// Peak picking.
constexpr double kMedianWindowSec = 0.15; // half-width of the adaptive threshold window
constexpr double kMedianScale = 1.6;      // threshold = median * scale + floor
constexpr double kThresholdFloor = 0.02;
constexpr double kRefractorySec = 0.05; // minimum gap between two onsets

// Tempo search.
constexpr double kMinBpm = 60.0;
constexpr double kMaxBpm = 200.0;
constexpr double kPreferredBpm = 120.0;
constexpr double kOctaveSigma = 0.9; // width of the log-normal tempo prior
// A sub-multiple lag has to keep this much of the winner's raw correlation to be preferred. Set
// below 1.0 because the faster tempo explains strictly more onsets: it is expected to score a
// little lower on an accented pattern, not higher.
constexpr double kOctaveAcceptance = 0.55;
// Two octaves scoring this close to each other means the answer is not really decided.
constexpr double kOctaveAmbiguous = 0.9;
constexpr double kMinTempoConfidence = 0.25;
constexpr double kMinAnalysisSec = AudioOnsets::kMinAnalysisSec;

// A beat comb is scored against the ODF; each hit is smeared over this many frames
// either side so a slightly-off grid still scores.
constexpr int kCombTolerance = 1;

// Analysis-frame index -> seconds within the unpadded input.
inline double frameToSeconds(double frame, int sampleRate)
{
    return (frame * kHop + kDetectionLatency - kPadFrames) / sampleRate;
}

struct TxScratch
{
    AVTXContext *tx = nullptr;
    av_tx_fn fn = nullptr;
    float *in = nullptr;
    void *out = nullptr;

    ~TxScratch()
    {
        if (tx)
            av_tx_uninit(&tx);
        av_free(in);
        av_free(out);
    }

    bool init()
    {
        in = static_cast<float *>(av_malloc(sizeof(float) * kNFft));
        out = av_malloc(sizeof(float) * 2 * (kNBins + 1));
        if (!in || !out)
            return false;
        float scale = 1.0f;
        return av_tx_init(&tx, &fn, AV_TX_FLOAT_RDFT, 0, kNFft, &scale, 0) >= 0;
    }
};

// Half-wave-rectified log-magnitude spectral flux, one value per STFT frame.
std::vector<float> onsetEnvelope(const float *mono, int frameCount, TxScratch &tx)
{
    const int frames = (frameCount - kNFft) / kHop + 1;
    if (frames <= 1)
        return {};

    std::vector<float> hann(kNFft);
    for (int n = 0; n < kNFft; ++n)
        hann[n] = 0.5f * (1.0f - std::cos(2.0 * M_PI * n / kNFft)); // periodic Hann

    std::vector<float> prevMag(kNBins, 0.0f);
    std::vector<float> mag(kNBins, 0.0f);
    std::vector<float> odf(frames, 0.0f);
    auto *cout = static_cast<float *>(tx.out); // interleaved re,im

    for (int t = 0; t < frames; ++t) {
        const int start = t * kHop;
        for (int i = 0; i < kNFft; ++i)
            tx.in[i] = mono[start + i] * hann[i];
        tx.fn(tx.tx, cout, tx.in, sizeof(float));

        float flux = 0.0f;
        for (int k = 0; k < kNBins; ++k) {
            const float re = cout[2 * k];
            const float im = cout[2 * k + 1];
            mag[k] = std::log1p(std::sqrt(re * re + im * im));
            if (t > 0)
                flux += std::max(0.0f, mag[k] - prevMag[k]);
        }
        odf[t] = flux;
        prevMag.swap(mag);
    }

    const float peak = *std::max_element(odf.begin(), odf.end());
    if (peak > 1e-9f) {
        for (float &v : odf)
            v /= peak;
    }
    return odf;
}

// Local maxima above a running-median threshold, with a refractory gap.
QList<AudioOnset> pickPeaks(const std::vector<float> &odf, int sampleRate, double startSeconds)
{
    QList<AudioOnset> out;
    const double framesPerSec = static_cast<double>(sampleRate) / kHop;
    const int n = static_cast<int>(odf.size());
    const int half = std::max(1, static_cast<int>(kMedianWindowSec * framesPerSec));
    const int refractory = std::max(1, static_cast<int>(kRefractorySec * framesPerSec));

    std::vector<float> window;
    window.reserve(2 * half + 1);
    int lastPeak = -refractory;
    float strongest = 0.0f;

    for (int t = 1; t + 1 < n; ++t) {
        if (odf[t] < odf[t - 1] || odf[t] < odf[t + 1])
            continue;
        if (t - lastPeak < refractory)
            continue;

        const int lo = std::max(0, t - half);
        const int hi = std::min(n, t + half + 1);
        window.assign(odf.begin() + lo, odf.begin() + hi);
        const auto mid = window.begin() + window.size() / 2;
        std::nth_element(window.begin(), mid, window.end());
        const double threshold = *mid * kMedianScale + kThresholdFloor;
        if (odf[t] < threshold)
            continue;

        out.append({startSeconds + frameToSeconds(t, sampleRate), odf[t]});
        strongest = std::max(strongest, odf[t]);
        lastPeak = t;
    }

    if (strongest > 1e-9f) {
        for (AudioOnset &o : out)
            o.strength /= strongest;
    }
    return out;
}

// Autocorrelation over the beat-period range, weighted by a log-normal prior around
// kPreferredBpm so a half/double-tempo lag does not win on raw correlation alone.
// Returns the best lag in frames, or 0 when nothing stands out.
int estimatePeriod(const std::vector<float> &odf, double framesPerSec, double &confidence)
{
    confidence = 0.0;
    const int n = static_cast<int>(odf.size());
    const int minLag = std::max(1, static_cast<int>(framesPerSec * 60.0 / kMaxBpm));
    const int maxLag = std::min(n / 2, static_cast<int>(framesPerSec * 60.0 / kMinBpm));
    if (maxLag <= minLag)
        return 0;

    double mean = 0.0;
    for (float v : odf)
        mean += v;
    mean /= n;

    std::vector<double> centered(n);
    for (int i = 0; i < n; ++i)
        centered[i] = odf[i] - mean;

    double best = 0.0;
    double sum = 0.0;
    int bestLag = 0;
    std::vector<double> raw(static_cast<size_t>(maxLag) + 1, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double acc = 0.0;
        for (int i = lag; i < n; ++i)
            acc += centered[i] * centered[i - lag];
        // Biased estimator: divide by the whole length, not by the number of overlapping samples.
        // The unbiased form divides by a shrinking count, which inflates long lags exactly where
        // the half-time candidate sits — and at the end of the search range there is nothing left
        // to out-score it.
        acc /= n;
        raw[static_cast<size_t>(lag)] = acc;

        const double bpm = 60.0 * framesPerSec / lag;
        const double d = std::log(bpm / kPreferredBpm) / kOctaveSigma;
        const double weighted = acc * std::exp(-0.5 * d * d);

        sum += std::max(0.0, weighted);
        if (weighted > best) {
            best = weighted;
            bestLag = lag;
        }
    }

    const double meanCorrelation = sum / (maxLag - minLag + 1);
    if (bestLag == 0 || meanCorrelation <= 1e-12)
        return 0;

    // Octave correction. Music with alternating strong and weak beats correlates *better* at twice
    // the beat period than at the beat itself, so raw correlation alone reliably picks half time —
    // it reported 60 BPM on a 120 BPM track, with full confidence. A sub-multiple that still
    // carries most of the winner's correlation is the better answer, because it explains the same
    // peaks and more besides. The prior alone cannot do this: at half time it costs only ~26%.
    const int firstChoice = bestLag;
    for (int divisor : {2, 3}) {
        for (;;) {
            const int candidate = bestLag / divisor;
            if (bestLag % divisor != 0 || candidate < minLag)
                break;
            if (raw[static_cast<size_t>(candidate)] < kOctaveAcceptance * raw[static_cast<size_t>(bestLag)])
                break;
            bestLag = candidate;
        }
    }

    // How far the winner stands above the average lag — a ratio of 1.0 means "no better
    // than anything else", so shift and squash into 0..1.
    confidence = std::min(1.0, (best / meanCorrelation - 1.0) / 3.0);

    // When the correction had to overrule the raw winner, the two octaves were close enough to
    // argue about — say so in the confidence instead of reporting certainty about which one the
    // music is in. Where raw correlation and the prior already agreed, nothing was in doubt.
    if (bestLag != firstChoice && raw[static_cast<size_t>(bestLag)] > 1e-12) {
        const double ratio =
            raw[static_cast<size_t>(firstChoice)] / raw[static_cast<size_t>(bestLag)];
        if (ratio > kOctaveAmbiguous)
            confidence *= 0.5;
    }
    return bestLag;
}

// Slide a comb of period `lag` over the ODF and keep the offset that collects the
// most energy.
int estimatePhase(const std::vector<float> &odf, int lag)
{
    const int n = static_cast<int>(odf.size());
    double best = -1.0;
    int bestOffset = 0;
    for (int offset = 0; offset < lag; ++offset) {
        double acc = 0.0;
        for (int t = offset; t < n; t += lag) {
            const int lo = std::max(0, t - kCombTolerance);
            const int hi = std::min(n - 1, t + kCombTolerance);
            float local = 0.0f;
            for (int i = lo; i <= hi; ++i)
                local = std::max(local, odf[i]);
            acc += local;
        }
        if (acc > best) {
            best = acc;
            bestOffset = offset;
        }
    }
    return bestOffset;
}

} // namespace

AudioBeatAnalysis AudioOnsets::analyze(const float *mono, int frameCount, int sampleRate,
                                       double startSeconds)
{
    AudioBeatAnalysis result;
    if (!mono || sampleRate <= 0 || frameCount < kNFft + kHop)
        return result;

    TxScratch tx;
    if (!tx.init())
        return result;

    std::vector<float> padded(static_cast<size_t>(frameCount) + kPadFrames, 0.0f);
    std::copy(mono, mono + frameCount, padded.begin() + kPadFrames);

    const std::vector<float> odf = onsetEnvelope(padded.data(), static_cast<int>(padded.size()), tx);
    if (odf.empty())
        return result;

    const double framesPerSec = static_cast<double>(sampleRate) / kHop;
    result.onsets = pickPeaks(odf, sampleRate, startSeconds);

    const double durationSec = static_cast<double>(frameCount) / sampleRate;
    if (durationSec < kMinAnalysisSec)
        return result;

    double confidence = 0.0;
    const int lag = estimatePeriod(odf, framesPerSec, confidence);
    if (lag <= 0 || confidence < kMinTempoConfidence)
        return result;

    result.bpm = 60.0 * framesPerSec / lag;
    result.confidence = confidence;

    const int phase = estimatePhase(odf, lag);
    for (int t = phase; t < static_cast<int>(odf.size()); t += lag) {
        const double at = startSeconds + frameToSeconds(t, sampleRate);
        if (at >= startSeconds)
            result.beats.append(at);
    }

    // Bar lines: of the beatsPerBar candidate offsets, take the one whose beats
    // carry the most onset strength.
    const double barTolerance = 2.0 / framesPerSec;
    double bestBarScore = -1.0;
    for (int offset = 0; offset < result.beatsPerBar; ++offset) {
        double acc = 0.0;
        for (int b = offset; b < result.beats.size(); b += result.beatsPerBar) {
            for (const AudioOnset &o : result.onsets) {
                if (std::abs(o.seconds - result.beats[b]) < barTolerance)
                    acc += o.strength;
            }
        }
        if (acc > bestBarScore) {
            bestBarScore = acc;
            result.firstDownbeat = offset;
        }
    }

    return result;
}
