#include "KaldiFbank.h"

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

#include <cfloat>
#include <cmath>

namespace drift {

namespace {

constexpr int kRate = 16000;
constexpr int kFrameLength = 400; // 25 ms
constexpr int kFrameShift = 160;  // 10 ms
constexpr int kFft = 512;
constexpr float kPreemph = 0.97f;
constexpr float kLowFreq = 20.0f;
constexpr float kHighFreq = kRate / 2.0f - 400.0f;

double melScale(double hz)
{
    return 1127.0 * std::log(1.0 + hz / 700.0);
}

} // namespace

struct KaldiFbank::Impl
{
    std::vector<float> window;
    // Per mel bin: first FFT bin and its weights.
    std::vector<int> firstBin;
    std::vector<std::vector<float>> weights;
    AVTXContext *tx = nullptr;
    av_tx_fn txFn = nullptr;
};

KaldiFbank::KaldiFbank() : d(new Impl)
{
    d->window.resize(kFrameLength);
    for (int i = 0; i < kFrameLength; ++i)
        d->window[i] = static_cast<float>(
            std::pow(0.5 - 0.5 * std::cos(2.0 * M_PI * i / (kFrameLength - 1)), 0.85));

    const int fftBins = kFft / 2;
    const double binWidth = static_cast<double>(kRate) / kFft;
    const double melLow = melScale(kLowFreq);
    const double melHigh = melScale(kHighFreq);
    const double melDelta = (melHigh - melLow) / (kBins + 1);
    d->firstBin.assign(kBins, 0);
    d->weights.assign(kBins, {});
    for (int b = 0; b < kBins; ++b) {
        const double left = melLow + b * melDelta;
        const double center = melLow + (b + 1) * melDelta;
        const double right = melLow + (b + 2) * melDelta;
        int first = -1;
        std::vector<float> w;
        for (int i = 0; i < fftBins; ++i) {
            const double mel = melScale(binWidth * i);
            if (mel > left && mel < right) {
                const double weight = mel <= center ? (mel - left) / (center - left) : (right - mel) / (right - center);
                if (first < 0)
                    first = i;
                w.resize(static_cast<size_t>(i - first + 1), 0.0f);
                w.back() = static_cast<float>(weight);
            }
        }
        d->firstBin[b] = std::max(0, first);
        d->weights[b] = w;
    }
    float scale = 1.0f;
    av_tx_init(&d->tx, &d->txFn, AV_TX_FLOAT_RDFT, 0, kFft, &scale, 0);
}

KaldiFbank::~KaldiFbank()
{
    if (d->tx)
        av_tx_uninit(&d->tx);
    delete d;
}

size_t KaldiFbank::frameCount(size_t samples)
{
    return (samples + kFrameShift / 2) / kFrameShift;
}

std::vector<float> KaldiFbank::compute(const float *pcm, size_t samples, bool scaleToInt16) const
{
    const size_t frames = frameCount(samples);
    std::vector<float> out(frames * kBins, 0.0f);
    if (frames == 0 || !d->tx)
        return out;
    float *in = static_cast<float *>(av_malloc(sizeof(float) * (kFft + 2)));
    float *spec = static_cast<float *>(av_malloc(sizeof(float) * (kFft + 2)));
    std::vector<float> frame(kFrameLength);
    std::vector<float> power(kFft / 2 + 1);
    const float gain = scaleToInt16 ? 32768.0f : 1.0f;
    const auto n = static_cast<int64_t>(samples);

    for (size_t f = 0; f < frames; ++f) {
        // snip_edges=false: frames are centred on multiples of the shift, reading reflected
        // samples past either end of the signal.
        const int64_t start = static_cast<int64_t>(f) * kFrameShift + kFrameShift / 2 - kFrameLength / 2;
        double mean = 0.0;
        for (int i = 0; i < kFrameLength; ++i) {
            int64_t s = start + i;
            while (s < 0 || s >= n) {
                if (s < 0)
                    s = -s - 1;
                if (s >= n)
                    s = 2 * n - 1 - s;
            }
            frame[i] = pcm[s] * gain;
            mean += frame[i];
        }
        mean /= kFrameLength;
        for (float &v : frame)
            v -= static_cast<float>(mean);
        for (int i = kFrameLength - 1; i > 0; --i)
            frame[i] -= kPreemph * frame[i - 1];
        frame[0] -= kPreemph * frame[0];
        for (int i = 0; i < kFft; ++i)
            in[i] = i < kFrameLength ? frame[i] * d->window[i] : 0.0f;
        d->txFn(d->tx, spec, in, sizeof(float) * 2);
        for (int k = 0; k <= kFft / 2; ++k)
            power[k] = spec[2 * k] * spec[2 * k] + spec[2 * k + 1] * spec[2 * k + 1];
        float *row = out.data() + f * kBins;
        for (int b = 0; b < kBins; ++b) {
            double energy = 0.0;
            const std::vector<float> &w = d->weights[b];
            for (size_t k = 0; k < w.size(); ++k)
                energy += w[k] * power[d->firstBin[b] + k];
            row[b] = static_cast<float>(std::log(std::max(energy, static_cast<double>(FLT_EPSILON))));
        }
    }
    av_free(in);
    av_free(spec);
    return out;
}

} // namespace drift
