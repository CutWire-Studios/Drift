#pragma once

#include "engine/audio/AudioStage.h"

#include <memory>
#include <vector>

namespace soundtouch {
class SoundTouch;
}

namespace drift {

// Pitch shift with the duration preserved — what the asetrate + aresample + atempo chain these
// effects used to carry did the long way round.
//
// SoundTouch is not sample-count preserving per call: it buffers internally and emits in bursts
// tied to its sequence length, while every stage here has to be strictly 1:1 in and out. A FIFO
// absorbs that, and the stage reports the resulting delay through latencySamples() so the rack
// primes it with real audio before the first block instead of letting the stream open on silence.
class SoundTouchPitchProcessor final : public AudioEffectProcessor
{
public:
    SoundTouchPitchProcessor();
    ~SoundTouchPitchProcessor() override;

    void setRatio(float ratio);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;

    int latencySamples() const override { return m_latency; }

private:
    void applyRatio();
    void drainIntoFifo();

    std::unique_ptr<soundtouch::SoundTouch> m_shifter;
    std::vector<float> m_interleaved; // scratch for one sub-block, interleaved for SoundTouch
    std::vector<float> m_fifo;        // interleaved output waiting to be emitted
    size_t m_fifoRead = 0;
    double m_sampleRate = 48000.0;
    float m_ratio = 1.0f;
    int m_latency = 0;
    int m_reservoir = 0;
    bool m_filling = true;
};

} // namespace drift
