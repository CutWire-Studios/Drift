#include "engine/audio/analysis/AudioAlign.h"

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace drift {

QList<SilenceSpan> findSilence(const float *mono, qint64 frames, int sampleRate,
                               const SilenceParams &params)
{
    QList<SilenceSpan> out;
    const qint64 win = std::max<qint64>(1, qint64(params.windowSeconds * sampleRate));
    const qint64 count = frames / win;
    if (count <= 0)
        return out;

    const double thresholdSq = double(params.threshold) * double(params.threshold);
    const double winSeconds = double(win) / sampleRate;
    qint64 run = -1;
    for (qint64 w = 0; w <= count; ++w) {
        bool quiet = false;
        if (w < count) {
            double sum = 0.0;
            for (qint64 i = w * win; i < (w + 1) * win; ++i)
                sum += double(mono[i]) * double(mono[i]);
            quiet = sum / double(win) < thresholdSq;
        }
        if (quiet && run < 0)
            run = w;
        if (!quiet && run >= 0) {
            const double start = run * winSeconds;
            const double end = w * winSeconds;
            if (end - start >= params.minSilence) {
                SilenceSpan span;
                span.start = run == 0 ? start : start + params.padding;
                span.end = w == count ? end : end - params.padding;
                if (span.end > span.start)
                    out.append(span);
            }
            run = -1;
        }
    }
    return out;
}

namespace {

double rmsOf(const std::vector<float> &x)
{
    if (x.empty())
        return 0.0;
    double sum = 0.0;
    for (const float v : x)
        sum += double(v) * double(v);
    return std::sqrt(sum / double(x.size()));
}

struct AlignedBuffer
{
    explicit AlignedBuffer(size_t floats) : data(static_cast<float *>(av_mallocz(floats * sizeof(float)))) {}
    ~AlignedBuffer() { av_free(data); }
    AlignedBuffer(const AlignedBuffer &) = delete;
    AlignedBuffer &operator=(const AlignedBuffer &) = delete;
    float *data = nullptr;
};

} // namespace

AudioOffsetResult findAudioOffset(const std::vector<float> &anchorIn, const std::vector<float> &otherIn,
                                  int sampleRate, const AudioOffsetParams &params,
                                  const std::function<bool()> &keepGoing)
{
    AudioOffsetResult result;
    const qint64 na = qint64(anchorIn.size());
    const qint64 nb = qint64(otherIn.size());
    const qint64 minOverlap = qint64(params.minOverlapSeconds * sampleRate);
    if (na < minOverlap || nb < minOverlap) {
        result.status = AudioOffsetResult::Status::TooShort;
        return result;
    }
    if (rmsOf(anchorIn) < params.silentRms || rmsOf(otherIn) < params.silentRms) {
        result.status = AudioOffsetResult::Status::Silent;
        return result;
    }

    qint64 size = 1;
    while (size < na + nb)
        size <<= 1;

    // Spectra hold size/2 + 1 complex bins; the spare floats keep a packed Nyquist from overrunning.
    const size_t specFloats = size_t(size) + 4;
    AlignedBuffer timeA(size), timeB(size), specA(specFloats), specB(specFloats), corr(size);
    if (!timeA.data || !timeB.data || !specA.data || !specB.data || !corr.data)
        return result;

    auto removeMean = [](const std::vector<float> &x, float *dst) {
        double mean = 0.0;
        for (const float v : x)
            mean += v;
        mean /= double(x.size());
        for (size_t i = 0; i < x.size(); ++i)
            dst[i] = float(double(x[i]) - mean);
    };
    removeMean(anchorIn, timeA.data);
    removeMean(otherIn, timeB.data);

    AVTXContext *fwd = nullptr;
    AVTXContext *inv = nullptr;
    av_tx_fn fwdFn = nullptr;
    av_tx_fn invFn = nullptr;
    float fwdScale = 1.0f;
    float invScale = 1.0f / float(size);
    const bool ready = av_tx_init(&fwd, &fwdFn, AV_TX_FLOAT_RDFT, 0, int(size), &fwdScale, 0) >= 0
                       && av_tx_init(&inv, &invFn, AV_TX_FLOAT_RDFT, 1, int(size), &invScale, 0) >= 0;
    auto release = [&]() {
        if (fwd)
            av_tx_uninit(&fwd);
        if (inv)
            av_tx_uninit(&inv);
    };
    if (!ready) {
        release();
        return result;
    }

    fwdFn(fwd, specA.data, timeA.data, sizeof(float));
    fwdFn(fwd, specB.data, timeB.data, sizeof(float));
    if (keepGoing && !keepGoing()) {
        release();
        return result;
    }
    const qint64 bins = size / 2 + 1;
    for (qint64 i = 0; i < bins; ++i) {
        const float ar = specA.data[2 * i], ai = specA.data[2 * i + 1];
        const float br = specB.data[2 * i], bi = specB.data[2 * i + 1];
        specA.data[2 * i] = ar * br + ai * bi;
        specA.data[2 * i + 1] = ai * br - ar * bi;
    }
    invFn(inv, corr.data, specA.data, sizeof(float));
    release();
    if (keepGoing && !keepGoing()) {
        return result;
    }

    // corr[k mod size] = sum_n anchor[n + k] * other[n]. Dividing by the energy of the two
    // overlapping stretches turns it into a correlation coefficient, so a short overlap at the
    // edge of the search range can't win just by being quiet or loud.
    std::vector<double> sumA(size_t(na) + 1, 0.0), sumB(size_t(nb) + 1, 0.0);
    for (qint64 i = 0; i < na; ++i)
        sumA[size_t(i) + 1] = sumA[size_t(i)] + double(timeA.data[i]) * double(timeA.data[i]);
    for (qint64 i = 0; i < nb; ++i)
        sumB[size_t(i) + 1] = sumB[size_t(i)] + double(timeB.data[i]) * double(timeB.data[i]);

    const qint64 maxLag = qint64(params.maxLagSeconds * sampleRate);
    const qint64 guard = std::max<qint64>(1, qint64(0.1 * sampleRate));
    std::vector<float> ncc(size_t(2 * maxLag + 1), 0.0f);
    qint64 best = 0;
    float bestValue = 0.0f;
    bool any = false;
    for (qint64 k = -maxLag; k <= maxLag; ++k) {
        const qint64 n0 = std::max<qint64>(0, -k);
        const qint64 n1 = std::min(nb, na - k);
        if (n1 - n0 < minOverlap)
            continue;
        const double ea = sumA[size_t(n1 + k)] - sumA[size_t(n0 + k)];
        const double eb = sumB[size_t(n1)] - sumB[size_t(n0)];
        const double denom = std::sqrt(ea * eb);
        if (denom <= 0.0)
            continue;
        const float value = float(double(corr.data[(k + size) % size]) / denom);
        ncc[size_t(k + maxLag)] = value;
        if (!any || value > bestValue) {
            any = true;
            best = k;
            bestValue = value;
        }
    }
    if (!any) {
        result.status = AudioOffsetResult::Status::TooShort;
        return result;
    }

    float second = 0.0f;
    for (qint64 k = -maxLag; k <= maxLag; ++k) {
        if (std::llabs(k - best) <= guard)
            continue;
        second = std::max(second, ncc[size_t(k + maxLag)]);
    }

    result.lagSeconds = double(best) / sampleRate;
    result.peak = bestValue;
    result.secondPeak = second;
    const bool clear = bestValue >= params.minPeak && bestValue >= params.minPeakRatio * second;
    result.status = clear ? AudioOffsetResult::Status::Ok : AudioOffsetResult::Status::Ambiguous;
    return result;
}

} // namespace drift
