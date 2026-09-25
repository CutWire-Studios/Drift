#include "engine/LoudnessMeter.h"

#include <QtMath>

#include <array>
#include <cmath>

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

// K-weighting analogue: RLB high-pass at 38 Hz plus a high shelf of +4 dB at 1682 Hz.
Biquad makeHighPass(double freqHz, double sampleRate)
{
    Biquad f;
    const double freq = qBound(1.0, freqHz, sampleRate * 0.45);
    const double w0 = 2.0 * M_PI * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double alpha = std::sin(w0) / (2.0 * 0.70710678);
    const double a0 = 1.0 + alpha;
    f.b0 = ((1.0 + cosw) / 2.0) / a0;
    f.b1 = -(1.0 + cosw) / a0;
    f.b2 = f.b0;
    f.a1 = (-2.0 * cosw) / a0;
    f.a2 = (1.0 - alpha) / a0;
    return f;
}

Biquad makeHighShelf(double freqHz, double gainDb, double sampleRate)
{
    Biquad f;
    const double freq = qBound(1.0, freqHz, sampleRate * 0.45);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * freq / sampleRate;
    const double cosw = std::cos(w0);
    const double sinw = std::sin(w0);
    const double alpha = sinw / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / 0.70710678 - 1.0) + 2.0);
    const double twoSqrtA = 2.0 * std::sqrt(A) * alpha;
    const double a0 = (A + 1.0) - (A - 1.0) * cosw + twoSqrtA;
    f.b0 = (A * ((A + 1.0) + (A - 1.0) * cosw + twoSqrtA)) / a0;
    f.b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosw)) / a0;
    f.b2 = (A * ((A + 1.0) + (A - 1.0) * cosw - twoSqrtA)) / a0;
    f.a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cosw)) / a0;
    f.a2 = ((A + 1.0) - (A - 1.0) * cosw - twoSqrtA) / a0;
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

    Biquad hpL = makeHighPass(38.0, sampleRate);
    Biquad hpR = makeHighPass(38.0, sampleRate);
    Biquad shL = makeHighShelf(1682.0, 4.0, sampleRate);
    Biquad shR = makeHighShelf(1682.0, 4.0, sampleRate);

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

} // namespace drift
