#include "engine/audio/analysis/Stft.h"

#include <algorithm>
#include <cmath>

// MSVC does not define M_PI in <cmath>, and this is the guard the audio analysis beside it uses.
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

namespace drift {

namespace {

bool isPowerOfTwo(int value)
{
    return value > 0 && (value & (value - 1)) == 0;
}

} // namespace

struct Stft::Impl
{
    AVTXContext *fwdTx = nullptr;
    av_tx_fn fwdFn = nullptr;
    AVTXContext *invTx = nullptr;
    av_tx_fn invFn = nullptr;
    float *timeBuf = nullptr; // fftSize reals
    float *specBuf = nullptr; // interleaved complex, bins + 1 slots of slack for av_tx
    std::vector<float> window;

    ~Impl()
    {
        if (fwdTx)
            av_tx_uninit(&fwdTx);
        if (invTx)
            av_tx_uninit(&invTx);
        av_free(timeBuf);
        av_free(specBuf);
    }
};

Stft::Stft(int fftSize, int hop)
    : d(std::make_unique<Impl>())
{
    if (!isPowerOfTwo(fftSize) || hop <= 0 || hop > fftSize)
        return;

    const int bins = fftSize / 2 + 1;
    d->timeBuf = static_cast<float *>(av_malloc(sizeof(float) * size_t(fftSize)));
    d->specBuf = static_cast<float *>(av_malloc(sizeof(float) * 2 * size_t(bins + 1)));
    if (!d->timeBuf || !d->specBuf)
        return;

    float scale = 1.0f;
    if (av_tx_init(&d->fwdTx, &d->fwdFn, AV_TX_FLOAT_RDFT, 0, fftSize, &scale, 0) < 0)
        return;
    scale = 1.0f;
    if (av_tx_init(&d->invTx, &d->invFn, AV_TX_FLOAT_RDFT, 1, fftSize, &scale, 0) < 0)
        return;

    // Periodic Hann, the window these models are trained with. Periodic rather than symmetric
    // (denominator fftSize, not fftSize - 1) is what makes the squared-window sum flat under
    // overlap-add, so the normalization below has nothing to correct in the steady state.
    d->window.resize(size_t(fftSize));
    for (int i = 0; i < fftSize; ++i)
        d->window[size_t(i)] = 0.5f - 0.5f * std::cos(2.0 * M_PI * i / double(fftSize));

    m_fftSize = fftSize;
    m_hop = hop;
}

Stft::~Stft() = default;

bool Stft::ok() const
{
    return m_fftSize > 0 && d && d->fwdFn && d->invFn && !d->window.empty();
}

int Stft::frameCount(int64_t samples) const
{
    if (!ok() || samples <= 0)
        return 0;
    // Frame i starts at i * hop in the padded signal, which runs half a window ahead of sample 0.
    // Rounding up rather than down is what guarantees the final frame's window still reaches the
    // last sample when the length is not a multiple of the hop.
    return int((samples + m_hop - 1) / m_hop) + 1;
}

void Stft::analyze(const std::vector<float> &pcm, std::vector<float> *out)
{
    if (!out)
        return;
    out->clear();
    if (!ok() || pcm.empty())
        return;

    const int frames = frameCount(int64_t(pcm.size()));
    const int binCount = bins();
    const int half = m_fftSize / 2;
    out->assign(size_t(frames) * size_t(binCount) * 2, 0.0f);

    const int64_t length = int64_t(pcm.size());
    for (int frame = 0; frame < frames; ++frame) {
        const int64_t start = int64_t(frame) * m_hop - half;
        for (int k = 0; k < m_fftSize; ++k) {
            const int64_t index = start + k;
            // Zero outside the signal: the pad is silence, not a reflection, matching what the
            // reference pipelines feed a model at the edges.
            const float sample = (index >= 0 && index < length) ? pcm[size_t(index)] : 0.0f;
            d->timeBuf[k] = sample * d->window[size_t(k)];
        }
        d->fwdFn(d->fwdTx, d->specBuf, d->timeBuf, sizeof(float));
        std::copy(d->specBuf, d->specBuf + size_t(binCount) * 2,
                  out->begin() + int64_t(frame) * binCount * 2);
    }
}

void Stft::synthesize(const std::vector<float> &spectra, int64_t samples, std::vector<float> *out)
{
    if (!out)
        return;
    out->clear();
    if (!ok() || samples <= 0)
        return;

    const int binCount = bins();
    const int half = m_fftSize / 2;
    const int64_t stride = int64_t(binCount) * 2;
    const int frames = int(std::min<int64_t>(frameCount(samples), int64_t(spectra.size()) / stride));

    // Accumulated over the padded span, so a frame overhanging either end has somewhere to land
    // and is simply dropped when the result is cropped back.
    const size_t paddedLength = size_t(samples) + size_t(m_fftSize);
    std::vector<float> acc(paddedLength, 0.0f);
    std::vector<float> windowSum(paddedLength, 0.0f);

    const float inverseScale = 1.0f / float(m_fftSize);
    for (int frame = 0; frame < frames; ++frame) {
        std::copy(spectra.begin() + int64_t(frame) * stride,
                  spectra.begin() + int64_t(frame + 1) * stride, d->specBuf);
        d->invFn(d->invTx, d->timeBuf, d->specBuf, sizeof(float) * 2);

        const int64_t start = int64_t(frame) * m_hop - half;
        for (int k = 0; k < m_fftSize; ++k) {
            const int64_t index = start + k + half; // into the padded accumulator
            if (index < 0 || index >= int64_t(paddedLength))
                continue;
            const float w = d->window[size_t(k)];
            // The transform pair multiplies by fftSize, and the signal was already windowed once
            // on the way in; windowing again here is what makes the overlap-add weight w^2, which
            // the division below then removes exactly.
            acc[size_t(index)] += d->timeBuf[k] * inverseScale * w;
            windowSum[size_t(index)] += w * w;
        }
    }

    out->assign(size_t(samples), 0.0f);
    for (int64_t n = 0; n < samples; ++n) {
        const size_t index = size_t(n) + size_t(half);
        const float sum = windowSum[index];
        // Dividing by the measured sum rather than a COLA constant keeps the first and last half
        // window correct, where fewer frames overlap and the sum has not reached its plateau.
        (*out)[size_t(n)] = sum > 1e-8f ? acc[index] / sum : 0.0f;
    }
}

} // namespace drift
