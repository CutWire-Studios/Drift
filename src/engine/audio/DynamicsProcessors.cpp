#include "engine/audio/DynamicsProcessors.h"

#include <cmath>

namespace drift {

namespace {

// A linear amplitude of 0 is -inf dB, which no dynamics stage can use as a threshold.
float linearToDecibels(float linear)
{
    return juce::Decibels::gainToDecibels(juce::jmax(linear, 1.0e-5f));
}

// Per-sample coefficient for a one-pole envelope follower with the given time constant.
float timeConstantCoefficient(float milliseconds, double sampleRate)
{
    const double seconds = juce::jmax(0.1, static_cast<double>(milliseconds)) / 1000.0;
    return static_cast<float>(std::exp(-1.0 / (seconds * sampleRate)));
}

} // namespace

// ---- CompressorProcessor ----------------------------------------------------------------

void CompressorProcessor::setThresholdDb(float db)
{
    m_compressor.setThreshold(db);
}

void CompressorProcessor::setRatio(float ratio)
{
    m_compressor.setRatio(juce::jmax(1.0f, ratio));
}

void CompressorProcessor::setAttackMs(float ms)
{
    m_compressor.setAttack(juce::jmax(0.01f, ms));
}

void CompressorProcessor::setReleaseMs(float ms)
{
    m_compressor.setRelease(juce::jmax(0.01f, ms));
}

void CompressorProcessor::setMakeupLinear(float linear)
{
    m_makeupTarget = juce::jmax(0.0f, linear);
    m_makeup.setTargetValue(m_makeupTarget);
}

void CompressorProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_compressor.prepare(spec);
    m_makeup.reset(spec.sampleRate, kParamRampSeconds);
    m_makeup.setCurrentAndTargetValue(m_makeupTarget);
}

void CompressorProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    juce::dsp::ProcessContextReplacing<float> context(block);
    m_compressor.process(context);

    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());
    for (int i = 0; i < frames; ++i) {
        const float makeup = m_makeup.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * makeup);
    }
}

void CompressorProcessor::reset()
{
    m_compressor.reset();
    m_makeup.setCurrentAndTargetValue(m_makeupTarget);
}

// ---- LimiterProcessor -------------------------------------------------------------------

void LimiterProcessor::setDriveLinear(float linear)
{
    m_driveTarget = juce::jmax(0.0f, linear);
    m_drive.setTargetValue(m_driveTarget);
}

void LimiterProcessor::setCeilingLinear(float linear)
{
    m_ceiling = juce::jlimit(1.0e-4f, 1.0f, linear);
}

void LimiterProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_drive.reset(spec.sampleRate, kParamRampSeconds);
    m_drive.setCurrentAndTargetValue(m_driveTarget);
    // alimiter=release=50 in the manifest chain.
    const double releaseSeconds = 0.05;
    m_releaseCoeff = spec.sampleRate > 0.0
        ? float(1.0 - std::exp(-1.0 / (releaseSeconds * spec.sampleRate)))
        : 1.0f;
    m_gain = 1.0f;
}

void LimiterProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());
    for (int i = 0; i < frames; ++i) {
        const float drive = m_drive.getNextValue();

        // Gain is linked across channels so the stereo image does not shift when one side peaks.
        float peak = 0.0f;
        for (int channel = 0; channel < channels; ++channel) {
            const float driven = block.getSample(channel, i) * drive;
            block.setSample(channel, i, driven);
            peak = juce::jmax(peak, std::abs(driven));
        }

        // Attack is instantaneous and computed from this sample's own peak, which is what makes
        // the ceiling a guarantee rather than an average.
        const float desired = peak > m_ceiling ? m_ceiling / peak : 1.0f;
        if (desired < m_gain)
            m_gain = desired;
        else
            m_gain += (desired - m_gain) * m_releaseCoeff;

        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * m_gain);
    }
}

void LimiterProcessor::reset()
{
    m_drive.setCurrentAndTargetValue(m_driveTarget);
    m_gain = 1.0f;
}

// ---- GateProcessor ----------------------------------------------------------------------

void GateProcessor::setThresholdLinear(float linear)
{
    m_gate.setThreshold(linearToDecibels(linear));
}

void GateProcessor::setRatio(float ratio)
{
    m_gate.setRatio(juce::jmax(1.0f, ratio));
}

void GateProcessor::setAttackMs(float ms)
{
    m_gate.setAttack(juce::jmax(0.01f, ms));
}

void GateProcessor::setReleaseMs(float ms)
{
    m_gate.setRelease(juce::jmax(0.01f, ms));
}

void GateProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_gate.prepare(spec);
}

void GateProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    juce::dsp::ProcessContextReplacing<float> context(block);
    m_gate.process(context);
}

void GateProcessor::reset()
{
    m_gate.reset();
}

// ---- DeEsserProcessor -------------------------------------------------------------------

void DeEsserProcessor::setIntensity(float intensity)
{
    m_intensity.setTargetValue(juce::jlimit(0.0f, 1.0f, intensity));
}

void DeEsserProcessor::setAmount(float amount)
{
    m_amount.setTargetValue(juce::jlimit(0.0f, 1.0f, amount));
}

void DeEsserProcessor::setFrequency(float normalised)
{
    m_frequency = juce::jlimit(0.0f, 1.0f, normalised);
    updateCoefficients();
}

void DeEsserProcessor::updateCoefficients()
{
    // 0..1 spans the range sibilance actually lives in.
    const float hz = 2000.0f + m_frequency * 10000.0f;
    const float clamped = juce::jlimit(1000.0f, static_cast<float>(m_sampleRate * 0.45), hz);
    *m_sibilance.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(m_sampleRate, clamped);
}

void DeEsserProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_sibilance.prepare(spec);
    m_band.setSize(static_cast<int>(spec.numChannels), static_cast<int>(spec.maximumBlockSize));
    m_intensity.reset(spec.sampleRate, kParamRampSeconds);
    m_amount.reset(spec.sampleRate, kParamRampSeconds);
    // Fast enough to catch an "s", slow enough not to chew the vowel after it.
    m_attack = timeConstantCoefficient(1.0f, spec.sampleRate);
    m_release = timeConstantCoefficient(60.0f, spec.sampleRate);
    updateCoefficients();
    reset();
}

void DeEsserProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());

    // Isolate the sibilance band in a scratch copy; the dry signal stays in `block`.
    for (int channel = 0; channel < channels; ++channel)
        m_band.copyFrom(channel, 0, block.getChannelPointer(channel), frames);

    juce::dsp::AudioBlock<float> bandBlock(m_band.getArrayOfWritePointers(), channels, 0, frames);
    juce::dsp::ProcessContextReplacing<float> context(bandBlock);
    m_sibilance.process(context);

    for (int i = 0; i < frames; ++i) {
        const float intensity = m_intensity.getNextValue();
        const float amount = m_amount.getNextValue();
        for (int channel = 0; channel < channels; ++channel) {
            const float band = m_band.getSample(channel, i);
            const float magnitude = std::abs(band);
            const float coefficient = magnitude > m_envelope[channel] ? m_attack : m_release;
            m_envelope[channel] = coefficient * m_envelope[channel] + (1.0f - coefficient) * magnitude;

            // More energy in the band means more duck, scaled by intensity and floored by amount.
            const float reduction = juce::jlimit(
                0.0f, amount, intensity * m_envelope[channel] * 4.0f);
            block.setSample(channel, i, block.getSample(channel, i) - band * reduction);
        }
    }
}

void DeEsserProcessor::reset()
{
    snapToTarget(m_intensity);
    snapToTarget(m_amount);
    m_sibilance.reset();
    m_band.clear();
    m_envelope[0] = 0.0f;
    m_envelope[1] = 0.0f;
}

// ---- LevelerProcessor -------------------------------------------------------------------

void LevelerProcessor::setStrength(float expansion)
{
    m_strength = juce::jlimit(1.0f, 50.0f, expansion);
}

void LevelerProcessor::setPeak(float peak)
{
    m_peak = juce::jlimit(0.05f, 1.0f, peak);
}

void LevelerProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_envelopeRelease = timeConstantCoefficient(400.0f, spec.sampleRate);
    // r=0.0005 and f=0.001 in the chain this replaces: per-sample gain increments, so the level
    // walks rather than jumps. Scaled to the block rate so the feel holds at any sample rate.
    const float scale = static_cast<float>(48000.0 / juce::jmax(1.0, spec.sampleRate));
    m_raise = 0.0005f * scale;
    m_fall = 0.001f * scale;
    reset();
}

void LevelerProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());

    for (int i = 0; i < frames; ++i) {
        float magnitude = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            magnitude = juce::jmax(magnitude, std::abs(block.getSample(channel, i)));

        // Peak-hold with a slow decay: the loudest recent sample sets the working level.
        m_envelope = magnitude > m_envelope
                         ? magnitude
                         : m_envelopeRelease * m_envelope + (1.0f - m_envelopeRelease) * magnitude;

        const float target = m_envelope > 1.0e-4f
                                 ? juce::jlimit(1.0f, m_strength, m_peak / m_envelope)
                                 : 1.0f;
        m_gain += target > m_gain ? juce::jmin(m_raise, target - m_gain)
                                  : -juce::jmin(m_fall, m_gain - target);

        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * m_gain);
    }
}

void LevelerProcessor::reset()
{
    m_gain = 1.0f;
    m_envelope = 0.0f;
}

} // namespace drift
