#include "engine/ml/audio/StemMask.h"

#include <algorithm>
#include <cmath>

namespace drift {

std::vector<std::vector<float>>
stemSpectraFromMagnitudes(const std::vector<float> &mixSpectra, int frames, int bins,
                          const std::vector<std::vector<float>> &magnitudes, int modelBins)
{
    const int stems = int(magnitudes.size());
    if (frames <= 0 || bins <= 0 || stems <= 0 || modelBins < 0 || modelBins > bins)
        return {};

    const size_t spectraSize = size_t(frames) * size_t(bins) * 2;
    if (mixSpectra.size() != spectraSize)
        return {};
    const size_t magnitudeSize = size_t(frames) * size_t(modelBins);
    for (const std::vector<float> &magnitude : magnitudes) {
        if (magnitude.size() != magnitudeSize)
            return {};
    }

    std::vector<std::vector<float>> out(size_t(stems), std::vector<float>(spectraSize, 0.0f));
    const float equalShare = 1.0f / float(stems);

    for (int frame = 0; frame < frames; ++frame) {
        for (int bin = 0; bin < bins; ++bin) {
            const size_t spectrum = (size_t(frame) * size_t(bins) + size_t(bin)) * 2;
            const float re = mixSpectra[spectrum];
            const float im = mixSpectra[spectrum + 1];

            if (bin >= modelBins) {
                for (int stem = 0; stem < stems; ++stem) {
                    out[size_t(stem)][spectrum] = re * equalShare;
                    out[size_t(stem)][spectrum + 1] = im * equalShare;
                }
                continue;
            }

            const size_t cell = size_t(frame) * size_t(modelBins) + size_t(bin);
            float total = 0.0f;
            for (int stem = 0; stem < stems; ++stem)
                total += std::max(0.0f, magnitudes[size_t(stem)][cell]);

            for (int stem = 0; stem < stems; ++stem) {
                // A zero total means no stem claimed the bin. Splitting it keeps the stems summing
                // to the mixture; weighting by a vanishing total would amplify rounding noise into
                // whichever stem happened to be least close to zero.
                const float mask = total > 0.0f
                                       ? std::max(0.0f, magnitudes[size_t(stem)][cell]) / total
                                       : equalShare;
                out[size_t(stem)][spectrum] = re * mask;
                out[size_t(stem)][spectrum + 1] = im * mask;
            }
        }
    }

    return out;
}

} // namespace drift
