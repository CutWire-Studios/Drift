#include "engine/audio/AudioEffectProcessor.h"

namespace drift {

void ChainProcessor::bind(const char *paramId, std::function<void(float)> setter)
{
    m_bindings.emplace_back(paramId, std::move(setter));
}

int ChainProcessor::parameterIndex(std::string_view paramId) const
{
    for (size_t i = 0; i < m_bindings.size(); ++i) {
        if (m_bindings[i].first == paramId)
            return static_cast<int>(i);
    }
    return -1;
}

void ChainProcessor::setParameter(int index, float value)
{
    if (index >= 0 && index < static_cast<int>(m_bindings.size()))
        m_bindings[static_cast<size_t>(index)].second(value);
}

void ChainProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    for (auto &stage : m_stages)
        stage->prepare(spec);
}

void ChainProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    for (auto &stage : m_stages)
        stage->process(block);
}

void ChainProcessor::reset()
{
    for (auto &stage : m_stages)
        stage->reset();
}

int ChainProcessor::latencySamples() const
{
    int total = 0;
    for (const auto &stage : m_stages)
        total += stage->latencySamples();
    return total;
}

} // namespace drift
