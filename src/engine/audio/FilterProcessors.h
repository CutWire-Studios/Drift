#pragma once

#include "engine/audio/AudioStage.h"

namespace drift {

// bass=g:f=110 + equalizer=f=1000:t=q:w=1.2:g + treble=g:f=8000.
// Coefficients are recomputed on change but filter state carries over, so a gain move glides
// rather than clicking.
class ThreeBandEqProcessor final : public AudioEffectProcessor
{
public:
    void setLowGainDb(float db);
    void setMidGainDb(float db);
    void setHighGainDb(float db);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    using Duplicator =
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    void updateCoefficients();

    Duplicator m_low;
    Duplicator m_mid;
    Duplicator m_high;
    double m_sampleRate = 48000.0;
    float m_lowDb = 0.0f;
    float m_midDb = 0.0f;
    float m_highDb = 0.0f;
};

// highpass / lowpass / bandpass. avfilter's highpass and lowpass default to poles=2, which is a
// second-order Butterworth — the same thing juce's single-argument makeHighPass/makeLowPass build.
class BandFilterProcessor final : public AudioEffectProcessor
{
public:
    enum class Mode { HighPass, LowPass, BandPass };

    explicit BandFilterProcessor(Mode mode, float frequencyHz = 1000.0f);

    void setFrequency(float hz);
    // bandpass=width_type=h:w=W is a width in Hz; Q is centre/width.
    void setWidthHz(float hz);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    using Duplicator =
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    void updateCoefficients();

    Duplicator m_filter;
    Mode m_mode;
    double m_sampleRate = 48000.0;
    float m_frequency = 1000.0f;
    float m_widthHz = 1000.0f;
};

// acrusher: amplitude quantisation plus sample-rate reduction, blended against dry by `mix`.
// These are the standard formulas rather than a bit-exact copy of af_acrusher's internals.
class BitCrusherProcessor final : public AudioEffectProcessor
{
public:
    enum class Mode { Linear, Logarithmic };

    explicit BitCrusherProcessor(Mode mode = Mode::Linear, float bits = 8.0f);

    void setBits(float bits);
    void setSampleReduction(float samples);
    void setMix(float mix);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    float crush(float x) const;

    Mode m_mode;
    juce::SmoothedValue<float> m_mix;
    float m_bits = 8.0f;
    float m_samples = 1.0f;
    float m_hold[2] = {0.0f, 0.0f};
    int m_holdCount[2] = {0, 0};
};

// crystalizer: a first-difference exciter, then quantisation to `colors` steps.
//
// The avfilter chain this replaces passed crystalizer an option it does not have (n=), so the
// graph never built and the effect was a silent no-op. `colors` now drives the quantiser.
class CrystalizerProcessor final : public AudioEffectProcessor
{
public:
    void setIntensity(float intensity);
    void setColors(float colors);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    juce::SmoothedValue<float> m_intensity;
    float m_colors = 8.0f;
    float m_previous[2] = {0.0f, 0.0f};
};

// A plain smoothed linear gain — volume=, level_in=, out_gain= and friends.
class GainProcessor final : public AudioEffectProcessor
{
public:
    explicit GainProcessor(float linearGain = 1.0f);

    void setGain(float linear);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    juce::SmoothedValue<float> m_gain;
    float m_target = 1.0f;
};

} // namespace drift
