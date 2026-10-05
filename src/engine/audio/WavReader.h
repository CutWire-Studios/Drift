#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace drift::audiofx {

// Decoded audio, interleaved. Impulse responses use it, so it is immutable once built and shared
// between every graph instance of a catalog entry.
struct IrData
{
    std::vector<float> samples;
    int channels = 0;
    int frames = 0;
    double sampleRate = 0.0;
};

// A minimal RIFF/WAVE reader: 16/24/32-bit PCM and 32-bit float, mono or stereo. Native Drift and
// Forge's WebAssembly preview both decode impulse responses with it, so they convolve identical
// samples.
bool readWav(const uint8_t *data, size_t size, IrData *out, std::string *error);

} // namespace drift::audiofx
