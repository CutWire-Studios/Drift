#pragma once

#include "engine/audio/AudioGraph.h"
#include "engine/audio/AudioStage.h"

#include <memory>
#include <string_view>
#include <vector>

namespace drift::audiofx {

// Runs an AudioGraphDesc: pedals in series, splits, modulators and parameter bindings.
//
// Control work (parameter values, modulators, bypass and blend targets) happens at the start of
// every 128-frame slice counted from the last reset(), and the audio between boundaries is
// processed in whatever pieces the caller's block sizes produce. Results therefore depend only on
// the input, never on how it was chunked — which is what lets Drift's rack (1024-frame blocks) and
// Forge's AudioWorklet (128-frame quanta) produce the same samples.
class GraphProcessor final : public AudioEffectProcessor
{
public:
    static constexpr int kSliceFrames = 128;
    static constexpr int kScopeLength = 64;
    static constexpr int kTapStride = 4 + kScopeLength; // peakL, peakR, rmsL, rmsR, scope

    explicit GraphProcessor(std::shared_ptr<const AudioGraphDesc> graph);
    ~GraphProcessor() override;

    const AudioGraphDesc &graph() const;

    // Index into the manifest's parameters, or -1. Values land at the next slice boundary.
    int parameterIndex(std::string_view identifier) const;
    void setParameter(int index, float value);

    // Live edits of literal knobs, for Forge: dragging a knob that is not exposed as a slider.
    int nodeIndex(std::string_view id) const;
    void setKnob(int node, int knob, float value);
    // A pedal's footswitch, when it is not bound to a parameter.
    void setBypass(int node, bool bypassed);
    int modulatorIndex(std::string_view id) const;
    void setModulatorKnob(int modulator, int knob, float value);
    void setModulatorStep(int modulator, int step, float value);

    // Clip time the next reset() lands on. LFO and step phases are seeded from it, so after a seek
    // they are where continuous playback would have left them (exactly, while rates are constant).
    void setClipTime(double seconds);

    // Meters for Forge. Slots are the nodes in flat order, then the graph input, then its output,
    // kTapStride floats each. Collecting restarts the peak and RMS windows.
    void enableTaps(bool enabled);
    const float *collectTaps();
    int tapSlots() const;
    // Current output of each modulator: LFOs -1..1, envelopes and steps 0..1.
    const float *modulatorValues() const;

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;
    int latencySamples() const override;

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

} // namespace drift::audiofx
