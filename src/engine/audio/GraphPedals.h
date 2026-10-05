#pragma once

#include "engine/audio/AudioEffectProcessor.h"
#include "engine/audio/WavReader.h"

#include <memory>
#include <string_view>

namespace drift::audiofx {

// The chain behind one pedal type, with every knob in its PedalSpec bound by id. classic.* types
// come from the factory; the rest are juce::dsp building blocks defined in GraphPedals.cpp.
// `ir` is required by "convolution" and ignored otherwise. Null for an unknown type.
std::unique_ptr<ChainProcessor> createPedal(std::string_view type, std::shared_ptr<const IrData> ir = {});

} // namespace drift::audiofx
