#pragma once

#include <juce_dsp/juce_dsp.h>

namespace drift {

// Seconds a changed parameter takes to reach its new value. Long enough that a slider drag is a
// ramp rather than a step, short enough that the effect still feels responsive.
inline constexpr double kParamRampSeconds = 0.02;

// Jump a smoothed parameter straight to its target. The rack applies parameter values after
// prepare(), so without this a freshly built stage would spend its first 20 ms ramping up from a
// default nobody asked for.
inline void snapToTarget(juce::SmoothedValue<float> &value)
{
    value.setCurrentAndTargetValue(value.getTargetValue());
}

// One DSP stage. Deliberately not juce::AudioProcessor: that lives in juce_audio_processors, which
// depends on juce_gui_extra and would pull a GUI stack into a headless engine.
//
// The shape is also what a CLAP host adapter would implement, so plugin hosting could slot in
// behind the same factory later without re-architecting anything.
//
// Everything that includes this header (directly) stays free of Qt: these sources are also built
// to WebAssembly for Drift Forge's live preview.
class AudioEffectProcessor
{
public:
    virtual ~AudioEffectProcessor() = default;

    virtual void prepare(const juce::dsp::ProcessSpec &spec) = 0;
    virtual void process(juce::dsp::AudioBlock<float> &block) = 0;
    virtual void reset() = 0;

    // Delay this stage adds. The rack primes by this much and discards, so a latent stage lines up
    // instead of drifting permanently late — the libavfilter path zero-filled the shortfall and
    // stayed offset forever.
    virtual int latencySamples() const { return 0; }
};

} // namespace drift
