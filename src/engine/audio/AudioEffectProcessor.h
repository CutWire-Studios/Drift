#pragma once

#include "engine/audio/AudioStage.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace drift {

// One catalog effect: ordered stages plus a table binding manifest parameter identifiers to stage
// setters. Composition replaces the comma-joined avfilter chain string; the binding table replaces
// the {placeholder} text substitution.
//
// setParameter never rebuilds anything. The old graph tore itself down on every value change,
// which is exactly what made dragging a slider click.
class ChainProcessor final : public AudioEffectProcessor
{
public:
    template <typename T, typename... Args>
    T *addStage(Args &&...args)
    {
        auto stage = std::make_unique<T>(std::forward<Args>(args)...);
        T *raw = stage.get();
        m_stages.push_back(std::move(stage));
        return raw;
    }

    template <typename T>
    void bind(const char *paramId, T *stage, void (T::*setter)(float))
    {
        bind(paramId, [stage, setter](float v) { (stage->*setter)(v); });
    }

    void bind(const char *paramId, std::function<void(float)> setter);

    // -1 for an unknown id: a project may carry a parameter an effect no longer has. Resolve once
    // and keep the index; setParameter is what runs per block.
    int parameterIndex(std::string_view paramId) const;
    void setParameter(int index, float value);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;
    int latencySamples() const override;

private:
    std::vector<std::unique_ptr<AudioEffectProcessor>> m_stages;
    std::vector<std::pair<std::string, std::function<void(float)>>> m_bindings;
};

} // namespace drift
