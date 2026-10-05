#pragma once

#include "engine/audio/AudioStage.h"

namespace drift {

using LinearDelayLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;

// tremolo=f:d — amplitude modulation. The quarter-turn phase offset is af_tremolo's, and is what
// makes the effect start at unity gain instead of dipping on the first sample.
class TremoloProcessor final : public AudioEffectProcessor
{
public:
    void setRate(float hz);
    void setDepth(float depth);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    juce::SmoothedValue<float> m_depth;
    double m_sampleRate = 48000.0;
    double m_phase = 0.0;
    float m_rate = 5.0f;
};

// vibrato=f:d — pitch modulation from a swept delay, fully wet.
class VibratoProcessor final : public AudioEffectProcessor
{
public:
    void setRate(float hz);
    void setDepth(float depth);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    LinearDelayLine m_delay{2048};
    juce::SmoothedValue<float> m_depth;
    double m_sampleRate = 48000.0;
    double m_phase = 0.0;
    float m_rate = 4.0f;
};

// flanger=delay:depth:regen:width:speed:phase. Unlike avfilter's, this one has no 10 ms ceiling on
// `depth` — the manifest always advertised 1-20 ms and the filter silently failed above 10.
class FlangerProcessor final : public AudioEffectProcessor
{
public:
    void setDelayMs(float ms);
    void setDepthMs(float ms);
    void setRegenPercent(float percent);
    void setWidthPercent(float percent);
    void setRate(float hz);
    void setPhaseDegrees(float degrees);
    // stereotools=phasel/phaser in the chain flipped both channels, which is inaudible. Flipping
    // only the right channel is what the control was reaching for.
    void setInvertRight(float enabled);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    LinearDelayLine m_delay{4096};
    double m_sampleRate = 48000.0;
    double m_phase = 0.0;
    float m_delayMs = 5.0f;
    float m_depthMs = 2.0f;
    float m_regen = 0.0f;
    float m_width = 71.0f;
    float m_rate = 0.5f;
    float m_phaseOffset = 0.0f;
    bool m_invertRight = false;
};

// chorus=in_gain:out_gain:delay:decay:speed:depth around juce::dsp::Chorus.
class ChorusProcessor final : public AudioEffectProcessor
{
public:
    void setInGain(float linear);
    void setOutGain(float linear);
    void setDelayMs(float ms);
    void setDecay(float decay);
    void setRate(float hz);
    void setDepthMs(float ms);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    juce::dsp::Chorus<float> m_chorus;
    juce::SmoothedValue<float> m_inGain;
    juce::SmoothedValue<float> m_outGain;
    float m_inTarget = 1.0f;
    float m_outTarget = 1.0f;
};

// aphaser=in_gain:out_gain:delay:decay:speed around juce::dsp::Phaser. avfilter's `delay` sets the
// all-pass spacing, which is a centre frequency here — shorter delay, higher notches.
class PhaserProcessor final : public AudioEffectProcessor
{
public:
    void setInGain(float linear);
    void setOutGain(float linear);
    void setDelayMs(float ms);
    void setDecay(float decay);
    void setRate(float hz);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    juce::dsp::Phaser<float> m_phaser;
    juce::SmoothedValue<float> m_inGain;
    juce::SmoothedValue<float> m_outGain;
    double m_sampleRate = 48000.0;
    float m_inTarget = 1.0f;
    float m_outTarget = 1.0f;
};

// apulsator=hz:amount:level_in:level_out. The default offset_l=0 / offset_r=0.5 puts the two
// channel LFOs half a cycle apart, which is what makes it pan rather than tremolo.
class AutoPanProcessor final : public AudioEffectProcessor
{
public:
    void setRate(float hz);
    void setAmount(float amount);
    void setLevelIn(float linear);
    void setLevelOut(float linear);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    juce::SmoothedValue<float> m_amount;
    juce::SmoothedValue<float> m_levelIn;
    juce::SmoothedValue<float> m_levelOut;
    double m_sampleRate = 48000.0;
    double m_phase = 0.0;
    float m_rate = 0.5f;
    float m_levelInTarget = 1.0f;
    float m_levelOutTarget = 1.0f;
};

// stereowiden=delay:feedback:crossfeed:drymix — subtract each channel's delayed opposite.
class StereoWidenProcessor final : public AudioEffectProcessor
{
public:
    void setDelayMs(float ms);
    void setFeedback(float feedback);
    void setCrossfeed(float crossfeed);
    void setDryMix(float dryMix);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    LinearDelayLine m_delay{8192};
    juce::SmoothedValue<float> m_feedback;
    juce::SmoothedValue<float> m_crossfeed;
    juce::SmoothedValue<float> m_dryMix;
    double m_sampleRate = 48000.0;
    float m_delayMs = 20.0f;
};

// aecho=in_gain:out_gain:delay:decay. A single tap off the input, with no regeneration — that is
// what avfilter's aecho does with one delay, so one repeat, not a repeating tail.
class EchoProcessor final : public AudioEffectProcessor
{
public:
    void setInGain(float linear);
    void setOutGain(float linear);
    void setDelayMs(float ms);
    void setDecay(float decay);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

    int latencySamples() const override { return 0; }

private:
    LinearDelayLine m_delay{65536};
    juce::SmoothedValue<float> m_inGain;
    juce::SmoothedValue<float> m_outGain;
    juce::SmoothedValue<float> m_decay;
    double m_sampleRate = 48000.0;
    float m_delayMs = 60.0f;
    float m_inTarget = 1.0f;
    float m_outTarget = 1.0f;
};

} // namespace drift
