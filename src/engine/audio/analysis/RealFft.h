#pragma once

#include <cstddef>

extern "C" {
#include <libavutil/mem.h>
#include <libavutil/tx.h>
}

namespace drift {

// Forward real FFT of a fixed power-of-two size on FFmpeg's av_tx. `in` is filled by the
// caller; run() leaves n/2+1 interleaved (re, im) pairs in `out`.
class RealFft
{
public:
    explicit RealFft(int size) : m_size(size)
    {
        m_in = static_cast<float *>(av_malloc(sizeof(float) * size));
        m_out = static_cast<float *>(av_malloc(sizeof(float) * 2 * (size / 2 + 1 + 1)));
        float scale = 1.0f;
        if (!m_in || !m_out || av_tx_init(&m_tx, &m_fn, AV_TX_FLOAT_RDFT, 0, size, &scale, 0) < 0)
            m_fn = nullptr;
    }
    ~RealFft()
    {
        if (m_tx)
            av_tx_uninit(&m_tx);
        av_free(m_in);
        av_free(m_out);
    }
    RealFft(const RealFft &) = delete;
    RealFft &operator=(const RealFft &) = delete;

    bool ok() const { return m_fn != nullptr; }
    int size() const { return m_size; }
    float *in() { return m_in; }
    const float *out() const { return m_out; }
    void run() { m_fn(m_tx, m_out, m_in, sizeof(float)); }

private:
    int m_size;
    AVTXContext *m_tx = nullptr;
    av_tx_fn m_fn = nullptr;
    float *m_in = nullptr;
    float *m_out = nullptr;
};

} // namespace drift
