#pragma once

#include <vector>

namespace drift {

// Turns a separation model's per-stem magnitude estimates into ratio masks over the mixture's own
// complex spectra. Kept apart from the ONNX session so the arithmetic that decides what each stem
// actually contains can be reasoned about, and tested, without any weights.
//
// `mixSpectra` is `frames * bins` interleaved (re, im) pairs, frame-major — what Stft::analyze
// produces. `magnitudes` holds one non-negative buffer per stem, each `frames * modelBins`, in
// the model's output order.
//
// Each stem gets mask_i = mag_i / sum_j(mag_j), applied to the mixture's complex value, so the
// mixture's phase is reused unchanged and the stems sum back to it exactly. Where every estimate
// is zero — silence, or a model declining to commit — the bin is split equally rather than left
// as a division by zero, which keeps that sum true through the quiet passages too.
//
// Bins at or above `modelBins` are outside what the graph predicts. Each stem takes an equal
// share of them, which preserves the sum; see StemSeparator.h for why that beats discarding them.
//
// Returns one spectra buffer per stem, each the same size and layout as `mixSpectra`. Empty when
// the inputs do not agree on a size.
std::vector<std::vector<float>>
stemSpectraFromMagnitudes(const std::vector<float> &mixSpectra, int frames, int bins,
                          const std::vector<std::vector<float>> &magnitudes, int modelBins);

} // namespace drift
