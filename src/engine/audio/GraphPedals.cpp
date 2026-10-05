#include "engine/audio/GraphPedals.h"

#include "engine/audio/AudioEffectFactory.h"
#include "engine/audio/FilterProcessors.h"
#include "engine/audio/PedalCatalog.h"

#include <cmath>

namespace drift::audiofx {

namespace {

using LinearDelayLine = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear>;

float dbToGain(float db)
{
    return juce::Decibels::decibelsToGain(db, -60.0f);
}

int choiceIndex(float value, int count)
{
    return juce::jlimit(0, count - 1, static_cast<int>(std::lround(value)));
}

// State-variable filter. JUCE's TPT filter does not smooth its own coefficients, so cutoff and
// resonance glide here and the filter is retuned per sample only while they move.
class SvfStage final : public AudioEffectProcessor
{
public:
    void setMode(float mode)
    {
        static constexpr juce::dsp::StateVariableTPTFilterType types[] = {
            juce::dsp::StateVariableTPTFilterType::lowpass, juce::dsp::StateVariableTPTFilterType::highpass,
            juce::dsp::StateVariableTPTFilterType::bandpass};
        m_filter.setType(types[choiceIndex(mode, 3)]);
    }
    void setCutoff(float hz) { m_cutoff.setTargetValue(juce::jmax(20.0f, hz)); }
    void setResonance(float q) { m_resonance.setTargetValue(juce::jmax(0.1f, q)); }

    void prepare(const juce::dsp::ProcessSpec &spec) override
    {
        m_nyquistLimit = static_cast<float>(spec.sampleRate * 0.45);
        m_filter.prepare(spec);
        m_cutoff.reset(spec.sampleRate, kParamRampSeconds);
        m_resonance.reset(spec.sampleRate, kParamRampSeconds);
        reset();
    }

    void process(juce::dsp::AudioBlock<float> &block) override
    {
        const int channels = static_cast<int>(block.getNumChannels());
        const int frames = static_cast<int>(block.getNumSamples());
        for (int i = 0; i < frames; ++i) {
            if (m_cutoff.isSmoothing() || m_resonance.isSmoothing())
                tune(m_cutoff.getNextValue(), m_resonance.getNextValue());
            for (int channel = 0; channel < channels; ++channel)
                block.setSample(channel, i, m_filter.processSample(channel, block.getSample(channel, i)));
        }
    }

    void reset() override
    {
        snapToTarget(m_cutoff);
        snapToTarget(m_resonance);
        tune(m_cutoff.getTargetValue(), m_resonance.getTargetValue());
        m_filter.reset();
    }

private:
    void tune(float cutoff, float resonance)
    {
        m_filter.setCutoffFrequency(juce::jmin(cutoff, m_nyquistLimit));
        m_filter.setResonance(resonance);
    }

    juce::dsp::StateVariableTPTFilter<float> m_filter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> m_cutoff{1200.0f};
    juce::SmoothedValue<float> m_resonance{0.707f};
    float m_nyquistLimit = 20000.0f;
};

// Moog-style ladder. juce::dsp::LadderFilter smooths cutoff and resonance itself.
class LadderStage final : public AudioEffectProcessor
{
public:
    void setMode(float mode)
    {
        using Mode = juce::dsp::LadderFilterMode;
        static constexpr Mode modes[] = {Mode::LPF12, Mode::HPF12, Mode::BPF12, Mode::LPF24, Mode::HPF24, Mode::BPF24};
        m_filter.setMode(modes[choiceIndex(mode, 6)]);
    }
    void setCutoff(float hz) { m_cutoff = hz; m_filter.setCutoffFrequencyHz(juce::jmin(hz, m_nyquistLimit)); }
    void setResonance(float r) { m_filter.setResonance(juce::jlimit(0.0f, 1.0f, r)); }
    void setDrive(float drive) { m_filter.setDrive(juce::jmax(1.0f, drive)); }

    void prepare(const juce::dsp::ProcessSpec &spec) override
    {
        m_nyquistLimit = static_cast<float>(spec.sampleRate * 0.45);
        m_filter.prepare(spec);
        setCutoff(m_cutoff);
        m_filter.reset();
    }

    void process(juce::dsp::AudioBlock<float> &block) override
    {
        juce::dsp::ProcessContextReplacing<float> context(block);
        m_filter.process(context);
    }

    void reset() override { m_filter.reset(); }

private:
    juce::dsp::LadderFilter<float> m_filter;
    float m_cutoff = 1200.0f;
    float m_nyquistLimit = 20000.0f;
};

// Waveshaping distortion at 2x oversampling, so the harmonics it creates fold back far less. The
// dry path is delayed by the oversampler's latency before the mix so the two stay phase-aligned.
class DriveStage final : public AudioEffectProcessor
{
public:
    void setShape(float shape) { m_shape = choiceIndex(shape, 4); }
    void setDriveDb(float db) { m_drive.setTargetValue(juce::Decibels::decibelsToGain(db)); }
    void setMix(float mix) { m_mixer.setWetMixProportion(juce::jlimit(0.0f, 1.0f, mix)); }
    void setOutputDb(float db) { m_output.setTargetValue(dbToGain(db)); }

    void prepare(const juce::dsp::ProcessSpec &spec) override
    {
        m_oversampling.initProcessing(spec.maximumBlockSize);
        m_latency = static_cast<int>(std::lround(m_oversampling.getLatencyInSamples()));
        m_mixer.prepare(spec);
        m_mixer.setWetLatency(static_cast<float>(m_latency));
        m_drive.reset(spec.sampleRate * 2.0, kParamRampSeconds);
        m_output.reset(spec.sampleRate, kParamRampSeconds);
        m_dcCoeff = static_cast<float>(1.0 - (2.0 * juce::MathConstants<double>::pi * 10.0 / spec.sampleRate));
        reset();
    }

    void process(juce::dsp::AudioBlock<float> &block) override
    {
        m_mixer.pushDrySamples(block);
        auto up = m_oversampling.processSamplesUp(block);
        const int channels = static_cast<int>(up.getNumChannels());
        const int frames = static_cast<int>(up.getNumSamples());
        for (int i = 0; i < frames; ++i) {
            const float drive = m_drive.getNextValue();
            for (int channel = 0; channel < channels; ++channel)
                up.setSample(channel, i, shape(up.getSample(channel, i) * drive));
        }
        m_oversampling.processSamplesDown(block);

        const int outFrames = static_cast<int>(block.getNumSamples());
        for (int i = 0; i < outFrames; ++i) {
            const float output = m_output.getNextValue();
            for (int channel = 0; channel < channels; ++channel) {
                // DC blocker: the asymmetric curve shifts the waveform's centre.
                const float x = block.getSample(channel, i);
                const float y = x - m_dcIn[channel] + m_dcCoeff * m_dcOut[channel];
                m_dcIn[channel] = x;
                m_dcOut[channel] = y;
                block.setSample(channel, i, y * output);
            }
        }
        m_mixer.mixWetSamples(block);
    }

    void reset() override
    {
        snapToTarget(m_drive);
        snapToTarget(m_output);
        m_oversampling.reset();
        m_mixer.reset();
        m_dcIn[0] = m_dcIn[1] = m_dcOut[0] = m_dcOut[1] = 0.0f;
    }

    int latencySamples() const override { return 0; } // the mixer realigns dry and wet internally

private:
    float shape(float x) const
    {
        switch (m_shape) {
        case 1: return juce::jlimit(-1.0f, 1.0f, x);
        case 2: return std::sin(x * juce::MathConstants<float>::halfPi);
        case 3: return x >= 0.0f ? std::tanh(x) : 0.6f * std::tanh(x / 0.6f);
        default: return std::tanh(x);
        }
    }

    juce::dsp::Oversampling<float> m_oversampling{
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true};
    juce::dsp::DryWetMixer<float> m_mixer{256};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> m_drive{4.0f};
    juce::SmoothedValue<float> m_output{0.5f};
    int m_shape = 0;
    int m_latency = 0;
    float m_dcCoeff = 0.999f;
    float m_dcIn[2] = {};
    float m_dcOut[2] = {};
};

// juce::dsp::Reverb (Freeverb). It smooths its own gains and damping.
class ReverbStage final : public AudioEffectProcessor
{
public:
    void setSize(float v) { m_params.roomSize = juce::jlimit(0.0f, 1.0f, v); apply(); }
    void setDamping(float v) { m_params.damping = juce::jlimit(0.0f, 1.0f, v); apply(); }
    void setWidth(float v) { m_params.width = juce::jlimit(0.0f, 1.0f, v); apply(); }
    void setMix(float v)
    {
        const float mix = juce::jlimit(0.0f, 1.0f, v);
        m_params.wetLevel = mix;
        m_params.dryLevel = 1.0f - mix;
        apply();
    }
    void setFreeze(float v) { m_params.freezeMode = v >= 0.5f ? 1.0f : 0.0f; apply(); }

    void prepare(const juce::dsp::ProcessSpec &spec) override
    {
        m_reverb.prepare(spec);
        apply();
        m_reverb.reset();
    }

    void process(juce::dsp::AudioBlock<float> &block) override
    {
        juce::dsp::ProcessContextReplacing<float> context(block);
        m_reverb.process(context);
    }

    void reset() override { m_reverb.reset(); }

private:
    void apply() { m_reverb.setParameters(m_params); }

    juce::dsp::Reverb m_reverb;
    juce::dsp::Reverb::Parameters m_params{0.5f, 0.5f, 0.3f, 0.7f, 1.0f, 0.0f};
};

// Convolution with an impulse response from the package, then optional pre-delay and low/high
// cut on the wet path. The IR is handed to JUCE before prepare(), which installs it synchronously
// — no loader thread involved, so the first block already convolves with it.
class ConvolutionStage final : public AudioEffectProcessor
{
public:
    explicit ConvolutionStage(std::shared_ptr<const IrData> ir)
        : m_ir(std::move(ir))
    {
    }

    void setMix(float mix) { m_mixer.setWetMixProportion(juce::jlimit(0.0f, 1.0f, mix)); }
    void setLowCut(float hz) { m_lowCut.setTargetValue(juce::jmax(20.0f, hz)); }
    void setHighCut(float hz) { m_highCut.setTargetValue(juce::jmax(20.0f, hz)); }
    void setPreDelayMs(float ms) { m_preDelayMs.setTargetValue(juce::jlimit(0.0f, 200.0f, ms)); }

    void prepare(const juce::dsp::ProcessSpec &spec) override
    {
        m_sampleRate = spec.sampleRate;
        m_nyquistLimit = static_cast<float>(spec.sampleRate * 0.45);

        juce::AudioBuffer<float> ir(2, juce::jmax(1, m_ir ? m_ir->frames : 1));
        ir.clear();
        if (m_ir) {
            for (int i = 0; i < m_ir->frames; ++i) {
                for (int channel = 0; channel < 2; ++channel) {
                    const int source = juce::jmin(channel, m_ir->channels - 1);
                    ir.setSample(channel, i, m_ir->samples[size_t(i * m_ir->channels + source)]);
                }
            }
        }
        m_convolution.loadImpulseResponse(std::move(ir), m_ir ? m_ir->sampleRate : spec.sampleRate,
                                          juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                          juce::dsp::Convolution::Normalise::yes);
        m_convolution.prepare(spec);

        m_preDelay.setMaximumDelayInSamples(static_cast<int>(spec.sampleRate * 0.21) + 2);
        m_preDelay.prepare(spec);
        m_lowFilter.setType(juce::dsp::StateVariableTPTFilterType::highpass);
        m_highFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
        m_lowFilter.prepare(spec);
        m_highFilter.prepare(spec);
        m_mixer.prepare(spec);
        m_mixer.setWetLatency(static_cast<float>(m_convolution.getLatency()));
        m_lowCut.reset(spec.sampleRate, kParamRampSeconds);
        m_highCut.reset(spec.sampleRate, kParamRampSeconds);
        m_preDelayMs.reset(spec.sampleRate, kParamRampSeconds);
        reset();
    }

    void process(juce::dsp::AudioBlock<float> &block) override
    {
        m_mixer.pushDrySamples(block);
        const int channels = static_cast<int>(block.getNumChannels());
        const int frames = static_cast<int>(block.getNumSamples());

        for (int i = 0; i < frames; ++i) {
            const float delay = juce::jmax(0.0f, m_preDelayMs.getNextValue() * static_cast<float>(m_sampleRate / 1000.0));
            for (int channel = 0; channel < channels; ++channel) {
                m_preDelay.pushSample(channel, block.getSample(channel, i));
                block.setSample(channel, i, m_preDelay.popSample(channel, delay));
            }
        }

        juce::dsp::ProcessContextReplacing<float> context(block);
        m_convolution.process(context);

        for (int i = 0; i < frames; ++i) {
            if (m_lowCut.isSmoothing() || m_highCut.isSmoothing())
                tune(m_lowCut.getNextValue(), m_highCut.getNextValue());
            for (int channel = 0; channel < channels; ++channel) {
                const float x = m_lowFilter.processSample(channel, block.getSample(channel, i));
                block.setSample(channel, i, m_highFilter.processSample(channel, x));
            }
        }
        m_mixer.mixWetSamples(block);
    }

    void reset() override
    {
        snapToTarget(m_lowCut);
        snapToTarget(m_highCut);
        snapToTarget(m_preDelayMs);
        tune(m_lowCut.getTargetValue(), m_highCut.getTargetValue());
        m_convolution.reset();
        m_preDelay.reset();
        m_lowFilter.reset();
        m_highFilter.reset();
        m_mixer.reset();
    }

    int latencySamples() const override { return 0; } // the mixer realigns dry and wet internally

private:
    void tune(float lowCut, float highCut)
    {
        m_lowFilter.setCutoffFrequency(juce::jmin(lowCut, m_nyquistLimit));
        m_highFilter.setCutoffFrequency(juce::jmin(highCut, m_nyquistLimit));
    }

    std::shared_ptr<const IrData> m_ir;
    juce::dsp::Convolution m_convolution{juce::dsp::Convolution::NonUniform{256}};
    LinearDelayLine m_preDelay{16384};
    juce::dsp::StateVariableTPTFilter<float> m_lowFilter;
    juce::dsp::StateVariableTPTFilter<float> m_highFilter;
    juce::dsp::DryWetMixer<float> m_mixer{4096};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> m_lowCut{20.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> m_highCut{20000.0f};
    juce::SmoothedValue<float> m_preDelayMs;
    double m_sampleRate = 48000.0;
    float m_nyquistLimit = 20000.0f;
};

// A regenerating stereo delay with a low-pass in the feedback loop, so repeats darken as they
// decay. Ping-pong feeds the mono input into the left line and crosses the feedback.
class DelayStage final : public AudioEffectProcessor
{
public:
    void setTimeMs(float ms) { m_timeMs.setTargetValue(juce::jlimit(1.0f, 2000.0f, ms)); }
    void setFeedback(float v) { m_feedback.setTargetValue(juce::jlimit(0.0f, 0.95f, v)); }
    void setTone(float hz) { m_tone = hz; updateTone(); }
    void setMix(float v) { m_mix.setTargetValue(juce::jlimit(0.0f, 1.0f, v)); }
    void setPingPong(float v) { m_pingPong = v >= 0.5f; }

    void prepare(const juce::dsp::ProcessSpec &spec) override
    {
        m_sampleRate = spec.sampleRate;
        m_line.setMaximumDelayInSamples(static_cast<int>(spec.sampleRate * 2.05) + 2);
        m_line.prepare(spec);
        m_timeMs.reset(spec.sampleRate, 0.1); // longer glide: a delay time jump is a pitch sweep
        m_feedback.reset(spec.sampleRate, kParamRampSeconds);
        m_mix.reset(spec.sampleRate, kParamRampSeconds);
        updateTone();
        reset();
    }

    void process(juce::dsp::AudioBlock<float> &block) override
    {
        const int channels = static_cast<int>(block.getNumChannels());
        const int frames = static_cast<int>(block.getNumSamples());
        for (int i = 0; i < frames; ++i) {
            const float delay = m_timeMs.getNextValue() * static_cast<float>(m_sampleRate / 1000.0);
            const float feedback = m_feedback.getNextValue();
            const float mix = m_mix.getNextValue();

            float wet[2] = {};
            for (int channel = 0; channel < channels; ++channel) {
                const float tap = m_line.popSample(channel, delay);
                m_toneState[channel] += m_toneCoeff * (tap - m_toneState[channel]);
                wet[channel] = m_toneState[channel];
            }
            const float left = block.getSample(0, i);
            const float right = channels > 1 ? block.getSample(1, i) : left;
            if (m_pingPong && channels > 1) {
                m_line.pushSample(0, 0.5f * (left + right) + wet[1] * feedback);
                m_line.pushSample(1, wet[0] * feedback);
            } else {
                for (int channel = 0; channel < channels; ++channel)
                    m_line.pushSample(channel, block.getSample(channel, i) + wet[channel] * feedback);
            }
            for (int channel = 0; channel < channels; ++channel)
                block.setSample(channel, i, block.getSample(channel, i) + wet[channel] * mix);
        }
    }

    void reset() override
    {
        snapToTarget(m_timeMs);
        snapToTarget(m_feedback);
        snapToTarget(m_mix);
        m_line.reset();
        m_toneState[0] = m_toneState[1] = 0.0f;
    }

private:
    void updateTone()
    {
        const double hz = juce::jlimit(20.0, m_sampleRate * 0.45, static_cast<double>(m_tone));
        m_toneCoeff = static_cast<float>(1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * hz / m_sampleRate));
    }

    LinearDelayLine m_line{131072};
    juce::SmoothedValue<float> m_timeMs{350.0f};
    juce::SmoothedValue<float> m_feedback{0.4f};
    juce::SmoothedValue<float> m_mix{0.35f};
    double m_sampleRate = 48000.0;
    float m_tone = 8000.0f;
    float m_toneCoeff = 1.0f;
    float m_toneState[2] = {};
    bool m_pingPong = false;
};

// Mid/side width, then a balance pan: the centre is unity and panning only attenuates the far side.
class PanStage final : public AudioEffectProcessor
{
public:
    void setPan(float v) { m_pan.setTargetValue(juce::jlimit(-1.0f, 1.0f, v)); }
    void setWidth(float v) { m_width.setTargetValue(juce::jlimit(0.0f, 2.0f, v)); }

    void prepare(const juce::dsp::ProcessSpec &spec) override
    {
        m_pan.reset(spec.sampleRate, kParamRampSeconds);
        m_width.reset(spec.sampleRate, kParamRampSeconds);
        reset();
    }

    void process(juce::dsp::AudioBlock<float> &block) override
    {
        if (block.getNumChannels() < 2)
            return;
        const int frames = static_cast<int>(block.getNumSamples());
        for (int i = 0; i < frames; ++i) {
            const float pan = m_pan.getNextValue();
            const float width = m_width.getNextValue();
            const float mid = 0.5f * (block.getSample(0, i) + block.getSample(1, i));
            const float side = 0.5f * (block.getSample(0, i) - block.getSample(1, i)) * width;
            block.setSample(0, i, (mid + side) * juce::jmin(1.0f, 1.0f - pan));
            block.setSample(1, i, (mid - side) * juce::jmin(1.0f, 1.0f + pan));
        }
    }

    void reset() override
    {
        snapToTarget(m_pan);
        snapToTarget(m_width);
    }

private:
    juce::SmoothedValue<float> m_pan;
    juce::SmoothedValue<float> m_width{1.0f};
};

template <typename T>
using Setter = void (T::*)(float);

} // namespace

std::unique_ptr<ChainProcessor> createPedal(std::string_view type, std::shared_ptr<const IrData> ir)
{
    if (const std::string_view processorId = classicProcessorId(type); !processorId.empty())
        return createProcessor(processorId);

    auto chain = std::make_unique<ChainProcessor>();
    if (type == "filter") {
        auto *s = chain->addStage<SvfStage>();
        chain->bind("mode", s, &SvfStage::setMode);
        chain->bind("cutoff", s, &SvfStage::setCutoff);
        chain->bind("resonance", s, &SvfStage::setResonance);
    } else if (type == "ladder") {
        auto *s = chain->addStage<LadderStage>();
        chain->bind("mode", s, &LadderStage::setMode);
        chain->bind("cutoff", s, &LadderStage::setCutoff);
        chain->bind("resonance", s, &LadderStage::setResonance);
        chain->bind("drive", s, &LadderStage::setDrive);
    } else if (type == "drive") {
        auto *s = chain->addStage<DriveStage>();
        chain->bind("shape", s, &DriveStage::setShape);
        chain->bind("drive", s, &DriveStage::setDriveDb);
        chain->bind("mix", s, &DriveStage::setMix);
        chain->bind("output", s, &DriveStage::setOutputDb);
    } else if (type == "reverb") {
        auto *s = chain->addStage<ReverbStage>();
        chain->bind("size", s, &ReverbStage::setSize);
        chain->bind("damping", s, &ReverbStage::setDamping);
        chain->bind("width", s, &ReverbStage::setWidth);
        chain->bind("mix", s, &ReverbStage::setMix);
        chain->bind("freeze", s, &ReverbStage::setFreeze);
    } else if (type == "convolution") {
        auto *s = chain->addStage<ConvolutionStage>(std::move(ir));
        chain->bind("mix", s, &ConvolutionStage::setMix);
        chain->bind("lowcut", s, &ConvolutionStage::setLowCut);
        chain->bind("highcut", s, &ConvolutionStage::setHighCut);
        chain->bind("predelay", s, &ConvolutionStage::setPreDelayMs);
    } else if (type == "delay") {
        auto *s = chain->addStage<DelayStage>();
        chain->bind("time", s, &DelayStage::setTimeMs);
        chain->bind("feedback", s, &DelayStage::setFeedback);
        chain->bind("tone", s, &DelayStage::setTone);
        chain->bind("mix", s, &DelayStage::setMix);
        chain->bind("pingpong", s, &DelayStage::setPingPong);
    } else if (type == "pan") {
        auto *s = chain->addStage<PanStage>();
        chain->bind("pan", s, &PanStage::setPan);
        chain->bind("width", s, &PanStage::setWidth);
    } else if (type == "gain") {
        auto *s = chain->addStage<GainProcessor>();
        chain->bind("gain", [s](float db) { s->setGain(dbToGain(db)); });
    } else {
        return nullptr;
    }
    return chain;
}

} // namespace drift::audiofx
