#pragma once

#include "engine/audio/AudioEffectProcessor.h"

namespace drift {

// acompressor + makeup. Thresholds are dB here; manifests that store a linear threshold convert
// in their factory binding.
class CompressorProcessor final : public AudioEffectProcessor
{
public:
    void setThresholdDb(float db);
    void setRatio(float ratio);
    void setAttackMs(float ms);
    void setReleaseMs(float ms);
    void setMakeupLinear(float linear);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    juce::dsp::Compressor<float> m_compressor;
    juce::SmoothedValue<float> m_makeup;
    float m_makeupTarget = 1.0f;
};

// alimiter=level_in={drive}:limit={ceiling}. Both are linear in avfilter; juce's Limiter takes dB.
class LimiterProcessor final : public AudioEffectProcessor
{
public:
    void setDriveLinear(float linear);
    void setCeilingLinear(float linear);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    // juce::dsp::Limiter is a loudness maximiser: it applies a fixed +3.75 dB plus -threshold dB of
    // automatic makeup, so mapping avfilter's linear `limit` (a ceiling) onto its threshold made
    // the stage add gain, and made asking it to limit harder add more. This is a plain
    // feed-forward peak limiter instead: gain never exceeds 1, and the output never exceeds the
    // ceiling.
    juce::SmoothedValue<float> m_drive;
    float m_driveTarget = 1.0f;
    float m_ceiling = 1.0f;
    float m_gain = 1.0f;
    float m_releaseCoeff = 0.0f;
};

// agate. avfilter's threshold is linear 0..1; juce's NoiseGate takes dB.
class GateProcessor final : public AudioEffectProcessor
{
public:
    void setThresholdLinear(float linear);
    void setRatio(float ratio);
    void setAttackMs(float ms);
    void setReleaseMs(float ms);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    juce::dsp::NoiseGate<float> m_gate;
};

// deesser: split off the sibilance band, duck it by its own envelope, add it back.
// avfilter's i/m/f are all normalised 0..1; `frequency` maps onto 2-12 kHz.
class DeEsserProcessor final : public AudioEffectProcessor
{
public:
    void setIntensity(float intensity);
    void setAmount(float amount);
    void setFrequency(float normalised);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    void updateCoefficients();

    using Duplicator =
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>>;

    Duplicator m_sibilance;
    juce::AudioBuffer<float> m_band;
    juce::SmoothedValue<float> m_intensity;
    juce::SmoothedValue<float> m_amount;
    double m_sampleRate = 48000.0;
    float m_frequency = 0.5f;
    float m_envelope[2] = {0.0f, 0.0f};
    float m_attack = 0.0f;
    float m_release = 0.0f;
};

// speechnorm: raise quiet speech toward a target peak, bounded by a maximum expansion factor.
// A port of the idea rather than of af_speechnorm's per-period internals — the gain walks toward
// its target by a fixed increment per sample, which is what makes it sound gradual instead of
// pumping.
class LevelerProcessor final : public AudioEffectProcessor
{
public:
    void setStrength(float expansion);
    void setPeak(float peak);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

private:
    float m_strength = 6.0f;
    float m_peak = 0.95f;
    float m_gain = 1.0f;
    float m_envelope = 0.0f;
    float m_envelopeRelease = 0.0f;
    float m_raise = 0.0f;
    float m_fall = 0.0f;
};

} // namespace drift
