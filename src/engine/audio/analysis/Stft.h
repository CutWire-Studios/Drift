#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace drift {

// Short-time Fourier analysis and synthesis of a whole mono signal, for the spectral models that
// predict a mask over a spectrogram and the resynthesis that turns the masked result back into
// audio.
//
// Analysis is centred: the signal is padded by half a window at each end, so frame i is centred on
// sample i * hop and the first and last samples sit under a full window. That is the convention
// the training pipelines use (librosa's center=True), and handing a model frames aligned any other
// way shifts what it hears against what it learned.
//
// Spectra come back as the plain unnormalized STFT — sum over the windowed frame, no 1/N — because
// that is the scale a model's magnitudes were trained on. The 1/N that makes the transform pair
// unity is applied in synthesize() instead.
//
// Analysis followed by synthesis returns the input to within float rounding for any hop that keeps
// the squared window sum non-zero: synthesis divides the overlap-add by that sum rather than
// assuming the window and hop satisfy COLA. That exactness is the point — it means multiplying the
// spectra by a mask yields the masked signal and nothing else, so a unity mask is audibly a no-op
// and two complementary masks sum back to the original.
//
// Holds FFT scratch, so it is not thread-safe: give each worker thread its own.
class Stft
{
public:
    // `hop` must divide into `fftSize` sensibly for the window sum to stay non-zero; half the
    // window or less is the usual choice. `fftSize` must be a power of two.
    Stft(int fftSize, int hop);
    ~Stft();
    Stft(const Stft &) = delete;
    Stft &operator=(const Stft &) = delete;

    // False when the size was rejected or the transforms could not be built; every other method
    // then does nothing.
    bool ok() const;
    int fftSize() const { return m_fftSize; }
    int hop() const { return m_hop; }
    // Real spectrum length, counting both the DC and Nyquist bins.
    int bins() const { return m_fftSize / 2 + 1; }

    // How many frames analyze() produces for a signal of `samples`.
    int frameCount(int64_t samples) const;

    // frameCount(pcm.size()) * bins() interleaved (re, im) pairs, frame-major.
    void analyze(const std::vector<float> &pcm, std::vector<float> *out);
    // Overlap-adds `spectra` back into exactly `samples` samples — the inverse of analyze().
    void synthesize(const std::vector<float> &spectra, int64_t samples, std::vector<float> *out);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    int m_fftSize = 0;
    int m_hop = 0;
};

} // namespace drift
