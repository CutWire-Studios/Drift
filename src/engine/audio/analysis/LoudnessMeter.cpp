#include "engine/audio/analysis/LoudnessMeter.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace drift {
namespace {

// Direct-form I biquad. Coefficients are computed for the actual sample rate so a
// waveform-rate mix (8 kHz) and a loudness pass (48 kHz) share this file.
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

// BS.1770 K-weighting: the standard's high shelf and RLB high-pass, re-derived for the actual
// sample rate. At 48 kHz these reproduce the coefficients tabulated in the standard; the textbook
// RBJ shelf and a Q 0.707 high-pass read about 0.7 dB hot on typical material.
Biquad makeKShelf(double sampleRate)
{
    constexpr double f0 = 1681.974450955533;
    constexpr double gainDb = 3.999843853973347;
    constexpr double q = 0.7071752369554196;
    const double k = std::tan(M_PI * f0 / sampleRate);
    const double vh = std::pow(10.0, gainDb / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    Biquad f;
    f.b0 = (vh + vb * k / q + k * k) / a0;
    f.b1 = 2.0 * (k * k - vh) / a0;
    f.b2 = (vh - vb * k / q + k * k) / a0;
    f.a1 = 2.0 * (k * k - 1.0) / a0;
    f.a2 = (1.0 - k / q + k * k) / a0;
    return f;
}

Biquad makeKHighPass(double sampleRate)
{
    constexpr double f0 = 38.13547087602444;
    constexpr double q = 0.5003270373238773;
    const double k = std::tan(M_PI * f0 / sampleRate);
    const double a0 = 1.0 + k / q + k * k;
    Biquad f;
    f.b0 = 1.0;
    f.b1 = -2.0;
    f.b2 = 1.0;
    f.a1 = 2.0 * (k * k - 1.0) / a0;
    f.a2 = (1.0 - k / q + k * k) / a0;
    return f;
}

constexpr int kChunkFrames = 1 << 16;

// BS.1770 measures true peak on an oversampled signal: two consecutive samples can both sit under
// full scale while the waveform between them does not. Linear interpolation cannot show that —
// a straight line between two points never leaves the range they already span — so the peak has to
// come from a band-limited reconstruction. 4x is the minimum the standard accepts.
constexpr int kOversample = 4;
constexpr int kTapsPerPhase = 12;
constexpr int kKernelLength = kOversample * kTapsPerPhase;

const std::array<double, kKernelLength> &truePeakKernel()
{
    static const std::array<double, kKernelLength> kernel = [] {
        std::array<double, kKernelLength> h{};
        const double center = (kKernelLength - 1) / 2.0;
        for (int i = 0; i < kKernelLength; ++i) {
            const double x = (i - center) / double(kOversample);
            // Windowed sinc: a lowpass at the original Nyquist, which is what reconstructs the
            // waveform between samples. The Hann window keeps the ripple down at this length.
            const double sinc = qFuzzyIsNull(x) ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
            const double window = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kKernelLength - 1));
            h[i] = sinc * window;
        }
        // Normalise each phase to unity gain, so a steady signal reconstructs at its own level and
        // the estimate can only ever be raised by real inter-sample content.
        for (int phase = 0; phase < kOversample; ++phase) {
            double sum = 0.0;
            for (int tap = 0; tap < kTapsPerPhase; ++tap)
                sum += h[phase + tap * kOversample];
            if (!qFuzzyIsNull(sum)) {
                for (int tap = 0; tap < kTapsPerPhase; ++tap)
                    h[phase + tap * kOversample] /= sum;
            }
        }
        return h;
    }();
    return kernel;
}

// Tracks one channel's recent samples and reports the largest reconstructed magnitude.
class TruePeakFollower
{
public:
    double push(double sample)
    {
        m_history[m_cursor] = sample;
        m_cursor = (m_cursor + 1) % kTapsPerPhase;

        const std::array<double, kKernelLength> &h = truePeakKernel();
        double peak = qAbs(sample);
        for (int phase = 0; phase < kOversample; ++phase) {
            double acc = 0.0;
            for (int tap = 0; tap < kTapsPerPhase; ++tap) {
                // Newest sample first, walking back through the ring.
                const int idx = (m_cursor - 1 - tap + kTapsPerPhase * 2) % kTapsPerPhase;
                acc += m_history[idx] * h[phase + tap * kOversample];
            }
            peak = qMax(peak, qAbs(acc));
        }
        return peak;
    }

private:
    std::array<double, kTapsPerPhase> m_history{};
    int m_cursor = 0;
};

} // namespace

LoudnessResult measureLoudness(qint64 totalFrames, int sampleRate,
                               const MediaWaveform::FillChunk &fill)
{
    LoudnessResult result;
    if (totalFrames <= 0 || sampleRate <= 0 || !fill)
        return result;

    result.durationSeconds = double(totalFrames) / double(sampleRate);

    Biquad hpL = makeKHighPass(sampleRate);
    Biquad hpR = makeKHighPass(sampleRate);
    Biquad shL = makeKShelf(sampleRate);
    Biquad shR = makeKShelf(sampleRate);

    // BS.1770 sums the per-channel mean squares with weight 1.0 for L and R; averaging them
    // instead is a flat -3.01 dB error on every reading, so the blocks below add, not mean.
    const int hop = qMax(1, sampleRate / 10);          // 100 ms
    const int block = qMax(hop, sampleRate * 4 / 10);  // 400 ms
    QVector<double> blockEnergy;

    double truePeak = 1e-12;
    double blockAcc[2] = {0.0, 0.0};
    int blockCount = 0;
    int sinceHop = 0;
    TruePeakFollower peakL;
    TruePeakFollower peakR;

    QVector<float> chunk(static_cast<qsizetype>(kChunkFrames) * 2);
    for (qint64 done = 0; done < totalFrames;) {
        const int want = static_cast<int>(qMin<qint64>(kChunkFrames, totalFrames - done));
        const int got = fill(chunk.data(), done, want);
        if (got <= 0)
            break;
        for (int i = 0; i < got; ++i) {
            const double l = chunk[i * 2];
            const double r = chunk[i * 2 + 1];
            truePeak = qMax(truePeak, peakL.push(l));
            truePeak = qMax(truePeak, peakR.push(r));

            const double kl = shL.process(hpL.process(l));
            const double kr = shR.process(hpR.process(r));
            blockAcc[0] += kl * kl;
            blockAcc[1] += kr * kr;
            ++blockCount;
            ++sinceHop;
            if (sinceHop >= hop && blockCount >= block) {
                const double mean = (blockAcc[0] + blockAcc[1]) / double(blockCount);
                blockEnergy.append(mean);
                // Overlap 75%: drop the oldest 100 ms of energy.
                const double dropFrac = double(hop) / double(blockCount);
                blockAcc[0] *= (1.0 - dropFrac);
                blockAcc[1] *= (1.0 - dropFrac);
                blockCount -= hop;
                sinceHop = 0;
            }
        }
        done += got;
    }
    if (blockCount > 0) {
        const double mean = (blockAcc[0] + blockAcc[1]) / double(blockCount);
        blockEnergy.append(mean);
    }

    auto lufsOf = [](double meanSquare) {
        if (meanSquare <= 1e-12)
            return -70.0;
        return -0.691 + 10.0 * std::log10(meanSquare);
    };

    // Absolute gate at -70 LUFS, then relative gate at gated loudness − 10 LU.
    double sum = 0.0;
    int n = 0;
    for (double e : blockEnergy) {
        if (lufsOf(e) > -70.0) {
            sum += e;
            ++n;
        }
    }
    const double absGated = n > 0 ? sum / double(n) : 0.0;
    const double relThresh = lufsOf(absGated) - 10.0;
    sum = 0.0;
    n = 0;
    for (double e : blockEnergy) {
        if (lufsOf(e) > relThresh && lufsOf(e) > -70.0) {
            sum += e;
            ++n;
        }
    }
    const double gated = n > 0 ? sum / double(n) : absGated;

    result.ok = true;
    result.integratedLufs = lufsOf(gated);
    result.truePeakDb = 20.0 * std::log10(truePeak);
    return result;
}

namespace {

double lufsFromPower(double meanPower)
{
    return -0.691 + 10.0 * std::log10(meanPower + 1e-20);
}

double percentileOf(std::vector<double> v, double pct)
{
    std::sort(v.begin(), v.end());
    const double rank = pct / 100.0 * double(v.size() - 1);
    const size_t lo = size_t(rank);
    const size_t hi = std::min(lo + 1, v.size() - 1);
    return v[lo] + (v[hi] - v[lo]) * (rank - double(lo));
}

// Mean power of `window` consecutive 50 ms segments, one value per `step` segments.
std::vector<double> slidingPower(const std::vector<double> &segPower, int window, int step)
{
    std::vector<double> out;
    for (size_t i = 0; i + size_t(window) <= segPower.size(); i += size_t(step)) {
        double sum = 0.0;
        for (int k = 0; k < window; ++k)
            sum += segPower[i + size_t(k)];
        out.push_back(sum / window);
    }
    return out;
}

LoudnessCurve curveOf(const std::vector<double> &power, double firstCentre, double dt, int stride)
{
    LoudnessCurve c;
    c.t0 = firstCentre;
    c.dt = dt;
    for (size_t i = 0; i < power.size(); i += size_t(stride))
        c.lufs.append(float(std::max(lufsFromPower(power[i]), -70.0)));
    return c;
}

} // namespace

LoudnessReport analyzeLoudness(qint64 totalFrames, int sampleRate,
                               const MediaWaveform::FillChunk &fill)
{
    LoudnessReport report;
    if (totalFrames <= 0 || sampleRate <= 0 || !fill)
        return report;
    report.durationSeconds = double(totalFrames) / double(sampleRate);

    // Everything below is built from 50 ms segments of summed K-weighted power, per channel
    // summed as BS.1770 does: 0.1 s, 0.4 s and 3 s windows are 2, 8 and 60 of them.
    const int seg = qMax(1, sampleRate / 20);
    Biquad hp[2] = {makeKHighPass(sampleRate), makeKHighPass(sampleRate)};
    Biquad sh[2] = {makeKShelf(sampleRate), makeKShelf(sampleRate)};
    TruePeakFollower peak[2];

    std::vector<double> segPower;
    std::vector<double> segSamplePeak; // per 50 ms, for the gap kind
    double acc = 0.0;
    double segPk = 0.0;
    int inSeg = 0;
    double truePeak = 0.0;
    double samplePeak = 0.0;
    int clipRun = 0;
    qint64 clipStart = 0;

    QVector<float> chunk(static_cast<qsizetype>(kChunkFrames) * 2);
    qint64 done = 0;
    const auto closeRun = [&]() {
        if (clipRun >= 3)
            report.clippingRuns.append({double(clipStart) / sampleRate, clipRun});
        clipRun = 0;
    };
    while (done < totalFrames) {
        const int want = static_cast<int>(qMin<qint64>(kChunkFrames, totalFrames - done));
        const int got = fill(chunk.data(), done, want);
        if (got <= 0)
            break;
        for (int i = 0; i < got; ++i) {
            bool clipped = false;
            for (int ch = 0; ch < 2; ++ch) {
                const double x = chunk[i * 2 + ch];
                const double a = std::abs(x);
                samplePeak = std::max(samplePeak, a);
                segPk = std::max(segPk, a);
                if (a >= 0.999)
                    clipped = true;
                truePeak = std::max(truePeak, peak[ch].push(x));
                const double k = sh[ch].process(hp[ch].process(x));
                acc += k * k;
            }
            if (clipped) {
                ++report.clippedSamples;
                if (clipRun == 0)
                    clipStart = done + i;
                ++clipRun;
            } else if (clipRun > 0) {
                closeRun();
            }
            if (++inSeg == seg) {
                segPower.push_back(acc / seg);
                segSamplePeak.push_back(segPk);
                acc = 0.0;
                segPk = 0.0;
                inSeg = 0;
            }
        }
        done += got;
    }
    closeRun();
    if (inSeg > 0) {
        segPower.push_back(acc / inSeg);
        segSamplePeak.push_back(segPk);
    }

    report.ok = true;
    report.samplePeakDb = samplePeak > 0.0 ? 20.0 * std::log10(samplePeak) : -120.0;
    report.hasTruePeak = samplePeak > 0.0;
    report.truePeakDb = truePeak > 0.0 ? 20.0 * std::log10(truePeak) : -120.0;
    const double dur = report.durationSeconds;

    // Momentary: 0.4 s windows every 0.1 s.
    const std::vector<double> mom = slidingPower(segPower, 8, 2);
    if (dur >= 0.4 && !mom.empty()) {
        report.momentary = curveOf(mom, 0.2, 0.1, 1);
        const auto it = std::max_element(report.momentary.lufs.begin(), report.momentary.lufs.end());
        report.maxMomentaryLufs = *it;
        report.maxMomentaryAt = report.momentary.t0 + report.momentary.dt * double(it - report.momentary.lufs.begin());

        std::vector<double> gated;
        for (double p : mom) {
            if (lufsFromPower(p) > -70.0)
                gated.push_back(p);
        }
        if (!gated.empty()) {
            double mean = 0.0;
            for (double p : gated)
                mean += p;
            mean /= double(gated.size());
            const double rel = lufsFromPower(mean) - 10.0;
            double sum = 0.0;
            int n = 0;
            for (double p : gated) {
                if (lufsFromPower(p) > rel) {
                    sum += p;
                    ++n;
                }
            }
            report.hasIntegrated = true;
            report.integratedLufs = n > 0 ? lufsFromPower(sum / n) : rel + 10.0;
        }
    }

    // Short-term: 3 s windows; the curve is every 0.5 s, loudness range reads every 0.1 s.
    const std::vector<double> st = slidingPower(segPower, 60, 2);
    if (dur >= 3.0 && !st.empty()) {
        report.shortTerm = curveOf(st, 1.5, 0.5, 5);
        report.maxShortTermLufs = *std::max_element(report.shortTerm.lufs.begin(), report.shortTerm.lufs.end());
        if (dur >= 6.0) {
            std::vector<double> s;
            for (double p : st) {
                if (lufsFromPower(p) > -70.0)
                    s.push_back(p);
            }
            if (s.size() >= 5) {
                double mean = 0.0;
                for (double p : s)
                    mean += p;
                const double rel = lufsFromPower(mean / double(s.size())) - 20.0;
                std::vector<double> levels;
                for (double p : s) {
                    if (lufsFromPower(p) > rel)
                        levels.push_back(lufsFromPower(p));
                }
                if (levels.size() > 4) {
                    report.hasLra = true;
                    report.lraLu = percentileOf(levels, 95.0) - percentileOf(levels, 10.0);
                }
            }
        }
    }

    // Gaps: the fast (0.1 s) loudness, a window every 50 ms.
    const std::vector<double> fast = slidingPower(segPower, 2, 1);
    if (dur >= 0.5 && fast.size() >= 5) {
        const int n = int(fast.size());
        std::vector<double> fl(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            fl[size_t(i)] = std::max(lufsFromPower(fast[size_t(i)]), -70.0);
        const double thr = !report.hasIntegrated || report.integratedLufs <= -60.0
                               ? -70.0
                               : std::max(-60.0, report.integratedLufs - 20.0);
        std::vector<char> low(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i)
            low[size_t(i)] = report.hasIntegrated ? fl[size_t(i)] < thr : fl[size_t(i)] <= thr;
        // Close one-frame holes: a dilation then an erosion over 7 frames, edges replicated.
        constexpr int kHalf = 3;
        const int padded = n + 2 * kHalf;
        std::vector<char> pad(static_cast<size_t>(padded)), dil(static_cast<size_t>(padded), 0), ero(static_cast<size_t>(padded), 0);
        for (int i = 0; i < padded; ++i)
            pad[size_t(i)] = low[size_t(std::clamp(i - kHalf, 0, n - 1))];
        for (int i = 0; i < padded; ++i) {
            for (int k = -kHalf; k <= kHalf; ++k) {
                if (i + k >= 0 && i + k < padded && pad[size_t(i + k)]) {
                    dil[size_t(i)] = 1;
                    break;
                }
            }
        }
        for (int i = 0; i < padded; ++i) {
            bool all = true;
            for (int k = -kHalf; k <= kHalf && all; ++k)
                all = i + k >= 0 && i + k < padded && dil[size_t(i + k)];
            ero[size_t(i)] = all;
        }
        for (int i = 0; i < n; ++i)
            low[size_t(i)] = ero[size_t(i + kHalf)] && fl[size_t(i)] < thr + 6.0;

        for (int s = 0; s < n;) {
            if (!low[size_t(s)]) {
                ++s;
                continue;
            }
            int e = s;
            while (e < n && low[size_t(e)])
                ++e;
            if (e - s >= 5) {
                double a = std::max(0.0, s * 0.05);
                double b = std::min(dur, (e - 1) * 0.05 + 0.1);
                if (s == 0)
                    a = 0.0;
                if (e == n)
                    b = dur;
                double pk = 0.0;
                const size_t i0 = size_t(a * 20.0);
                const size_t i1 = std::min(segSamplePeak.size(), size_t(std::ceil(b * 20.0)));
                for (size_t i = i0; i < i1; ++i)
                    pk = std::max(pk, segSamplePeak[i]);
                double fmax = -70.0;
                double fsum = 0.0;
                for (int i = s; i < e; ++i) {
                    fmax = std::max(fmax, fl[size_t(i)]);
                    fsum += fl[size_t(i)];
                }
                LoudnessGap g;
                g.start = a;
                g.end = b;
                g.levelLufs = fsum / (e - s);
                g.kind = pk < 1e-5 ? QStringLiteral("digital silence")
                                   : (fmax < -60.0 ? QStringLiteral("near silence") : QStringLiteral("drop"));
                const bool head = a <= 0.05;
                const bool tail = b >= dur - 0.05;
                if (head || tail)
                    g.where = head && tail ? QStringLiteral("whole file")
                                           : (head ? QStringLiteral("lead-in") : QStringLiteral("tail"));
                report.gaps.append(g);
            }
            s = e;
        }
    }
    return report;
}

} // namespace drift
