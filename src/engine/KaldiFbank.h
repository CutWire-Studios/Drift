#pragma once

#include <cstddef>
#include <vector>

namespace drift {

// Log mel filterbank matching kaldi-native-fbank as sherpa-onnx configures it for speaker
// embeddings: 16 kHz, 25 ms Povey window every 10 ms, snip_edges=false (reflected edges), DC
// removal, 0.97 pre-emphasis, 512-point power spectrum, 80 mel bins from 20 Hz to 7600 Hz, no
// dither. The embedding models were trained on exactly this; any drift quietly degrades them.
class KaldiFbank
{
public:
    static constexpr int kBins = 80;

    KaldiFbank();
    ~KaldiFbank();

    // Frames x kBins, row-major. scaleToInt16 multiplies samples by 32768 first (models whose
    // metadata says normalize_samples=0).
    std::vector<float> compute(const float *pcm, size_t samples, bool scaleToInt16) const;
    static size_t frameCount(size_t samples);

    KaldiFbank(const KaldiFbank &) = delete;
    KaldiFbank &operator=(const KaldiFbank &) = delete;

private:
    struct Impl;
    Impl *d;
};

} // namespace drift
