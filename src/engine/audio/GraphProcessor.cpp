#include "engine/audio/GraphProcessor.h"

#include "engine/audio/GraphPedals.h"
#include "engine/audio/PedalCatalog.h"

#include <cmath>
#include <limits>

namespace drift::audiofx {

namespace {

constexpr int kSlice = GraphProcessor::kSliceFrames;
// Bypass and crossfade ramps, in samples. A fixed count rather than a duration, so the ramp is
// sample-identical at every rate and under every chunking.
constexpr int kSwitchRampSamples = 256;

using AlignDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;

const KnobSpec kBlendKnob{"blend", "Blend", 0.0f, 1.0f, 0.5f};
const KnobSpec kCrossoverKnob{"crossover", "Crossover", kCrossoverMinHz, kCrossoverMaxHz, 1000.0f,
                              KnobSpec::Scale::Log, "Hz"};

float toNorm(const KnobSpec &spec, float value)
{
    if (spec.scale == KnobSpec::Scale::Log)
        return std::log(value / spec.min) / std::log(spec.max / spec.min);
    return (value - spec.min) / (spec.max - spec.min);
}

float fromNorm(const KnobSpec &spec, float norm)
{
    if (spec.scale == KnobSpec::Scale::Log)
        return spec.min * std::pow(spec.max / spec.min, norm);
    return spec.min + norm * (spec.max - spec.min);
}

// Deterministic noise for the random LFO: one value per cycle, -1..1.
float cycleNoise(int64_t cycle)
{
    uint64_t x = static_cast<uint64_t>(cycle) + 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return static_cast<float>(static_cast<double>(x >> 11) / 9007199254740992.0 * 2.0 - 1.0);
}

void resetRamp(juce::SmoothedValue<float> &value, float target)
{
    value.reset(kSwitchRampSamples);
    value.setCurrentAndTargetValue(target);
}

} // namespace

struct GraphProcessor::Impl
{
    struct Series;

    struct NodeRt
    {
        const GraphNode *desc = nullptr;
        const PedalSpec *spec = nullptr; // null for splits
        std::vector<float> literals;     // live-editable copies of the literal knob values

        virtual ~NodeRt() = default;
        virtual void prepare(const juce::dsp::ProcessSpec &spec) = 0;
        virtual void process(juce::dsp::AudioBlock<float> &block, Impl &graph) = 0;
        virtual void reset() = 0;
        virtual int latency() const = 0;
    };

    struct Series
    {
        std::vector<std::unique_ptr<NodeRt>> nodes;

        void prepare(const juce::dsp::ProcessSpec &spec)
        {
            for (auto &node : nodes)
                node->prepare(spec);
        }
        void process(juce::dsp::AudioBlock<float> &block, Impl &graph)
        {
            for (auto &node : nodes) {
                node->process(block, graph);
                graph.observe(node->desc->flat, block);
            }
        }
        void reset()
        {
            for (auto &node : nodes)
                node->reset();
        }
        int latency() const
        {
            int total = 0;
            for (const auto &node : nodes)
                total += node->latency();
            return total;
        }
    };

    struct PedalRt final : NodeRt
    {
        std::unique_ptr<ChainProcessor> chain;
        std::vector<int> bindings;
        std::vector<float> sent;
        juce::SmoothedValue<float> bypass; // 0 = processing, 1 = bypassed
        juce::AudioBuffer<float> dry{2, kSlice};
        AlignDelay dryDelay{1};
        int chainLatency = 0;

        void prepare(const juce::dsp::ProcessSpec &spec) override
        {
            chain->prepare(spec);
            chainLatency = chain->latencySamples();
            // Bypassing a latent pedal must not jump the timing, so the dry path is delayed to match.
            if (chainLatency > 0) {
                dryDelay.setMaximumDelayInSamples(chainLatency + 1);
                dryDelay.prepare(spec);
                dryDelay.setDelay(static_cast<float>(chainLatency));
            }
            resetRamp(bypass, bypass.getTargetValue());
        }

        void process(juce::dsp::AudioBlock<float> &block, Impl &) override
        {
            const int channels = static_cast<int>(block.getNumChannels());
            const int frames = static_cast<int>(block.getNumSamples());
            const bool mixing = bypass.isSmoothing() || bypass.getTargetValue() > 0.0f;

            if (chainLatency > 0) {
                for (int channel = 0; channel < channels; ++channel) {
                    for (int i = 0; i < frames; ++i) {
                        dryDelay.pushSample(channel, block.getSample(channel, i));
                        dry.setSample(channel, i, dryDelay.popSample(channel));
                    }
                }
            } else if (mixing) {
                for (int channel = 0; channel < channels; ++channel)
                    dry.copyFrom(channel, 0, block.getChannelPointer(size_t(channel)), frames);
            }

            // The pedal keeps running while bypassed, so its tail and timing are intact when it
            // comes back.
            chain->process(block);
            if (!mixing)
                return;

            for (int i = 0; i < frames; ++i) {
                const float b = bypass.getNextValue();
                for (int channel = 0; channel < channels; ++channel)
                    block.setSample(channel, i, block.getSample(channel, i) * (1.0f - b) + dry.getSample(channel, i) * b);
            }
        }

        void reset() override
        {
            chain->reset();
            dryDelay.reset();
            bypass.setCurrentAndTargetValue(bypass.getTargetValue());
        }

        int latency() const override { return chainLatency; }
    };

    struct SplitRt final : NodeRt
    {
        std::vector<Series> lanes;
        std::vector<juce::AudioBuffer<float>> buffers;
        std::vector<AlignDelay> align;
        std::vector<int> alignSamples;
        // Bands: crossovers[j] splits band j from everything above it; allpasses[j] then puts band
        // j through the phase shift of every crossover above j, so the bands sum back flat.
        std::vector<juce::dsp::LinkwitzRileyFilter<float>> crossovers;
        std::vector<std::vector<juce::dsp::LinkwitzRileyFilter<float>>> allpasses;
        juce::SmoothedValue<float> blend;
        int splitLatency = 0;

        void prepare(const juce::dsp::ProcessSpec &spec) override
        {
            const size_t laneCount = lanes.size();
            buffers.assign(laneCount, juce::AudioBuffer<float>(2, kSlice));
            splitLatency = 0;
            for (auto &lane : lanes) {
                lane.prepare(spec);
                splitLatency = std::max(splitLatency, lane.latency());
            }
            align.clear();
            alignSamples.clear();
            for (auto &lane : lanes) {
                const int delay = splitLatency - lane.latency();
                align.emplace_back(delay + 1);
                align.back().prepare(spec);
                align.back().setDelay(static_cast<float>(delay));
                alignSamples.push_back(delay);
            }
            if (desc->bands) {
                crossovers.assign(laneCount - 1, {});
                allpasses.assign(laneCount - 1, {});
                for (size_t j = 0; j + 1 < laneCount; ++j) {
                    crossovers[j].prepare(spec);
                    allpasses[j].assign(laneCount - 2 - j, {});
                    for (auto &allpass : allpasses[j]) {
                        allpass.setType(juce::dsp::LinkwitzRileyFilterType::allpass);
                        allpass.prepare(spec);
                    }
                }
            }
            resetRamp(blend, blend.getTargetValue());
        }

        void setCrossovers(const float *hz, double sampleRate)
        {
            const float limit = static_cast<float>(sampleRate * 0.45);
            float floor = 0.0f;
            std::vector<float> f(crossovers.size());
            for (size_t j = 0; j < crossovers.size(); ++j) {
                // Kept ascending: a crossover below the previous one would make a band of nothing.
                f[j] = juce::jlimit(std::max(kCrossoverMinHz, floor * 1.05f), limit, hz[j]);
                floor = f[j];
                crossovers[j].setCutoffFrequency(f[j]);
            }
            for (size_t j = 0; j < allpasses.size(); ++j) {
                for (size_t k = 0; k < allpasses[j].size(); ++k)
                    allpasses[j][k].setCutoffFrequency(f[j + 1 + k]);
            }
        }

        void process(juce::dsp::AudioBlock<float> &block, Impl &graph) override
        {
            const int channels = static_cast<int>(block.getNumChannels());
            const int frames = static_cast<int>(block.getNumSamples());
            const size_t laneCount = lanes.size();

            for (int channel = 0; channel < channels; ++channel) {
                for (int i = 0; i < frames; ++i) {
                    float x = block.getSample(channel, i);
                    if (!desc->bands) {
                        for (auto &buffer : buffers)
                            buffer.setSample(channel, i, x);
                        continue;
                    }
                    for (size_t j = 0; j + 1 < laneCount; ++j) {
                        float low = 0.0f;
                        float high = 0.0f;
                        crossovers[j].processSample(channel, x, low, high);
                        for (auto &allpass : allpasses[j])
                            low = allpass.processSample(channel, low);
                        buffers[j].setSample(channel, i, low);
                        x = high;
                    }
                    buffers[laneCount - 1].setSample(channel, i, x);
                }
            }

            for (size_t j = 0; j < laneCount; ++j) {
                juce::dsp::AudioBlock<float> lane =
                    juce::dsp::AudioBlock<float>(buffers[j]).getSubBlock(0, size_t(frames)).getSubsetChannelBlock(0, size_t(channels));
                lanes[j].process(lane, graph);
                if (alignSamples[j] > 0) {
                    for (int channel = 0; channel < channels; ++channel) {
                        for (int i = 0; i < frames; ++i) {
                            align[j].pushSample(channel, lane.getSample(channel, i));
                            lane.setSample(channel, i, align[j].popSample(channel));
                        }
                    }
                }
            }

            for (int i = 0; i < frames; ++i) {
                float gains[kMaxSplitLanes];
                for (size_t j = 0; j < laneCount; ++j)
                    gains[j] = desc->laneGains[j];
                if (desc->crossfade) {
                    const float angle = blend.getNextValue() * juce::MathConstants<float>::halfPi;
                    gains[0] *= std::cos(angle);
                    gains[1] *= std::sin(angle);
                }
                for (int channel = 0; channel < channels; ++channel) {
                    float sum = 0.0f;
                    for (size_t j = 0; j < laneCount; ++j)
                        sum += buffers[j].getSample(channel, i) * gains[j];
                    block.setSample(channel, i, sum);
                }
            }
        }

        void reset() override
        {
            for (auto &lane : lanes)
                lane.reset();
            for (auto &delay : align)
                delay.reset();
            for (auto &crossover : crossovers)
                crossover.reset();
            for (auto &row : allpasses) {
                for (auto &allpass : row)
                    allpass.reset();
            }
            blend.setCurrentAndTargetValue(blend.getTargetValue());
        }

        int latency() const override { return splitLatency; }
    };

    struct ModRt
    {
        const GraphModulator *desc = nullptr;
        const PedalSpec *spec = nullptr;
        std::vector<float> literals;
        std::vector<float> steps;
        double phase = 0.0;    // LFO and steps: cycles since clip time 0
        float envelope = 0.0f; // envelope follower state
        float value = 0.0f;
    };

    struct Tap
    {
        float slicePeak = 0.0f;      // this slice, for envelope followers
        float peak[2] = {};
        double sumSquares[2] = {};
        int64_t count = 0;
        float sliceExtreme = 0.0f;   // signed, the scope point for this slice
        float scope[kScopeLength] = {};
        int scopeWrite = 0;
    };

    std::shared_ptr<const AudioGraphDesc> graph;
    Series root;
    std::vector<NodeRt *> nodes; // by flat index
    std::vector<ModRt> modulators;
    std::vector<float> params;
    std::vector<float> modValues;
    std::vector<Tap> taps;           // nodes, then input, then output
    std::vector<bool> peakWanted;    // envelope sources
    std::vector<float> tapOut;
    bool tapsEnabled = false;
    double sampleRate = 48000.0;
    double clipTime = 0.0;
    int sliceFrame = 0;

    explicit Impl(std::shared_ptr<const AudioGraphDesc> g)
        : graph(std::move(g))
    {
        nodes.resize(size_t(graph->nodeCount), nullptr);
        build(graph->chain, &root);

        params = graph->paramDefaults;
        for (const GraphModulator &desc : graph->modulators) {
            ModRt modulator;
            modulator.desc = &desc;
            modulator.spec = modulatorSpec(desc.type);
            for (const KnobValue &knob : desc.knobs)
                modulator.literals.push_back(knob.literal);
            modulator.steps = desc.steps;
            modulators.push_back(std::move(modulator));
        }
        modValues.assign(modulators.size(), 0.0f);

        taps.resize(size_t(graph->nodeCount) + 2);
        peakWanted.assign(taps.size(), false);
        for (const GraphModulator &desc : graph->modulators) {
            if (desc.type == "envelope")
                peakWanted[desc.source >= 0 ? size_t(desc.source) : inputSlot()] = true;
        }
        tapOut.assign(taps.size() * size_t(kTapStride), 0.0f);
    }

    size_t inputSlot() const { return size_t(graph->nodeCount); }
    size_t outputSlot() const { return size_t(graph->nodeCount) + 1; }

    void build(const std::vector<GraphNode> &descs, Series *series)
    {
        for (const GraphNode &desc : descs) {
            std::unique_ptr<NodeRt> node;
            if (desc.kind == GraphNode::Kind::Split) {
                auto split = std::make_unique<SplitRt>();
                split->lanes.resize(desc.lanes.size());
                for (size_t j = 0; j < desc.lanes.size(); ++j)
                    build(desc.lanes[j], &split->lanes[j]);
                node = std::move(split);
            } else {
                auto pedal = std::make_unique<PedalRt>();
                pedal->spec = pedalSpec(desc.type);
                pedal->chain = createPedal(desc.type, desc.ir);
                for (const KnobSpec &knob : pedal->spec->knobs)
                    pedal->bindings.push_back(pedal->chain->parameterIndex(knob.id));
                pedal->sent.assign(desc.knobs.size(), std::numeric_limits<float>::quiet_NaN());
                node = std::move(pedal);
            }
            node->desc = &desc;
            for (const KnobValue &knob : desc.knobs)
                node->literals.push_back(knob.literal);
            nodes[size_t(desc.flat)] = node.get();
            series->nodes.push_back(std::move(node));
        }
    }

    float paramOr(const KnobValue &knob, float literal, const KnobSpec &spec) const
    {
        if (knob.param < 0)
            return literal;
        return juce::jlimit(spec.min, spec.max, params[size_t(knob.param)]);
    }

    float modKnob(const ModRt &m, int index) const
    {
        const KnobSpec &spec = m.spec->knobs[size_t(index)];
        return paramOr(m.desc->knobs[size_t(index)], m.literals[size_t(index)], spec);
    }

    float rateOf(const ModRt &m) const
    {
        return m.desc->type == "lfo" ? modKnob(m, 1) : modKnob(m, 0);
    }

    // Recomputes every modulator output and pushes knob values for the coming slice. Reads state
    // only, so running it twice for the same slice changes nothing.
    void control()
    {
        for (size_t i = 0; i < modulators.size(); ++i) {
            ModRt &m = modulators[i];
            const std::string &type = m.desc->type;
            if (type == "lfo") {
                const double position = m.phase + modKnob(m, 2);
                const double p = position - std::floor(position);
                switch (static_cast<int>(std::lround(modKnob(m, 0)))) {
                case 1: m.value = float(p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0); break;
                case 2: m.value = p < 0.5 ? 1.0f : -1.0f; break;
                case 3: m.value = float(2.0 * p - 1.0); break;
                case 4: m.value = cycleNoise(static_cast<int64_t>(std::floor(position))); break;
                default: m.value = float(std::sin(2.0 * juce::MathConstants<double>::pi * p)); break;
                }
            } else if (type == "envelope") {
                m.value = juce::jlimit(0.0f, 1.0f, m.envelope * modKnob(m, 2));
            } else if (!m.steps.empty()) {
                const int count = static_cast<int>(m.steps.size());
                const double whole = std::floor(m.phase);
                const int index = static_cast<int>(((static_cast<int64_t>(whole) % count) + count) % count);
                const float current = m.steps[size_t(index)];
                const float previous = m.steps[size_t((index + count - 1) % count)];
                const float glide = modKnob(m, 1);
                const float frac = static_cast<float>(m.phase - whole);
                m.value = glide > 0.0f && frac < glide ? previous + (current - previous) * (frac / glide) : current;
            }
            modValues[i] = m.value;
        }

        for (NodeRt *node : nodes)
            node->desc->kind == GraphNode::Kind::Split ? controlSplit(*static_cast<SplitRt *>(node))
                                                       : controlPedal(*static_cast<PedalRt *>(node));
    }

    void controlPedal(PedalRt &pedal)
    {
        const GraphNode &desc = *pedal.desc;
        for (size_t k = 0; k < desc.knobs.size(); ++k) {
            const KnobSpec &spec = pedal.spec->knobs[k];
            float value = paramOr(desc.knobs[k], pedal.literals[k], spec);
            if (!desc.routes[k].empty()) {
                float norm = toNorm(spec, value);
                for (const ModRoute &route : desc.routes[k]) {
                    const float depth = route.depth.param >= 0 ? params[size_t(route.depth.param)] : route.depth.literal;
                    norm += depth * modValues[size_t(route.modulator)];
                }
                value = fromNorm(spec, juce::jlimit(0.0f, 1.0f, norm));
            }
            if (value != pedal.sent[k] && pedal.bindings[k] >= 0) {
                pedal.chain->setParameter(pedal.bindings[k], value);
                pedal.sent[k] = value;
            }
        }
        const float bypass = desc.bypass.param >= 0 ? params[size_t(desc.bypass.param)] : desc.bypass.literal;
        pedal.bypass.setTargetValue(bypass >= 0.5f ? 1.0f : 0.0f);
    }

    void controlSplit(SplitRt &split)
    {
        const GraphNode &desc = *split.desc;
        split.blend.setTargetValue(paramOr(desc.knobs[kSplitBlendKnob], split.literals[kSplitBlendKnob], kBlendKnob));
        if (desc.bands) {
            float hz[kMaxSplitLanes] = {};
            for (size_t j = 1; j < desc.knobs.size(); ++j)
                hz[j - 1] = paramOr(desc.knobs[j], split.literals[j], kCrossoverKnob);
            split.setCrossovers(hz, sampleRate);
        }
    }

    void observe(int slot, const juce::dsp::AudioBlock<float> &block)
    {
        Tap &tap = taps[size_t(slot)];
        if (!tapsEnabled && !peakWanted[size_t(slot)])
            return;
        const int channels = static_cast<int>(block.getNumChannels());
        const int frames = static_cast<int>(block.getNumSamples());
        for (int channel = 0; channel < channels; ++channel) {
            const float *samples = block.getChannelPointer(size_t(channel));
            for (int i = 0; i < frames; ++i) {
                const float magnitude = std::abs(samples[i]);
                tap.slicePeak = std::max(tap.slicePeak, magnitude);
                if (!tapsEnabled)
                    continue;
                tap.peak[channel & 1] = std::max(tap.peak[channel & 1], magnitude);
                tap.sumSquares[channel & 1] += double(samples[i]) * samples[i];
                if (magnitude > std::abs(tap.sliceExtreme))
                    tap.sliceExtreme = samples[i];
            }
        }
        if (tapsEnabled)
            tap.count += frames;
    }

    // Advances modulator state past the slice that just finished.
    void finishSlice()
    {
        const double seconds = kSlice / sampleRate;
        for (ModRt &m : modulators) {
            if (m.desc->type == "envelope") {
                const Tap &source = taps[m.desc->source >= 0 ? size_t(m.desc->source) : inputSlot()];
                const float target = source.slicePeak;
                const float ms = target > m.envelope ? modKnob(m, 0) : modKnob(m, 1);
                const float coeff = static_cast<float>(1.0 - std::exp(-seconds * 1000.0 / ms));
                m.envelope += (target - m.envelope) * coeff;
            } else {
                m.phase += rateOf(m) * seconds;
            }
        }
        for (Tap &tap : taps) {
            tap.slicePeak = 0.0f;
            if (tapsEnabled) {
                tap.scope[tap.scopeWrite] = tap.sliceExtreme;
                tap.scopeWrite = (tap.scopeWrite + 1) % kScopeLength;
                tap.sliceExtreme = 0.0f;
            }
        }
    }

    void seed()
    {
        sliceFrame = 0;
        for (ModRt &m : modulators) {
            m.phase = rateOf(m) * clipTime;
            m.envelope = 0.0f;
        }
        for (Tap &tap : taps)
            tap = Tap{};
        control();
        root.reset();
    }
};

GraphProcessor::GraphProcessor(std::shared_ptr<const AudioGraphDesc> graph)
    : m(std::make_unique<Impl>(std::move(graph)))
{
}

GraphProcessor::~GraphProcessor() = default;

const AudioGraphDesc &GraphProcessor::graph() const
{
    return *m->graph;
}

int GraphProcessor::parameterIndex(std::string_view identifier) const
{
    for (size_t i = 0; i < m->graph->params.size(); ++i) {
        if (m->graph->params[i] == identifier)
            return static_cast<int>(i);
    }
    return -1;
}

void GraphProcessor::setParameter(int index, float value)
{
    if (index >= 0 && size_t(index) < m->params.size())
        m->params[size_t(index)] = value;
}

int GraphProcessor::nodeIndex(std::string_view id) const
{
    for (const Impl::NodeRt *node : m->nodes) {
        if (node->desc->id == id)
            return node->desc->flat;
    }
    return -1;
}

void GraphProcessor::setKnob(int node, int knob, float value)
{
    if (node < 0 || size_t(node) >= m->nodes.size())
        return;
    auto &literals = m->nodes[size_t(node)]->literals;
    if (knob >= 0 && size_t(knob) < literals.size())
        literals[size_t(knob)] = value;
}

int GraphProcessor::modulatorIndex(std::string_view id) const
{
    for (size_t i = 0; i < m->modulators.size(); ++i) {
        if (m->modulators[i].desc->id == id)
            return static_cast<int>(i);
    }
    return -1;
}

void GraphProcessor::setModulatorKnob(int modulator, int knob, float value)
{
    if (modulator < 0 || size_t(modulator) >= m->modulators.size())
        return;
    auto &literals = m->modulators[size_t(modulator)].literals;
    if (knob >= 0 && size_t(knob) < literals.size())
        literals[size_t(knob)] = value;
}

void GraphProcessor::setModulatorStep(int modulator, int step, float value)
{
    if (modulator < 0 || size_t(modulator) >= m->modulators.size())
        return;
    auto &steps = m->modulators[size_t(modulator)].steps;
    if (step >= 0 && size_t(step) < steps.size())
        steps[size_t(step)] = juce::jlimit(0.0f, 1.0f, value);
}

void GraphProcessor::setClipTime(double seconds)
{
    m->clipTime = seconds;
}

void GraphProcessor::enableTaps(bool enabled)
{
    m->tapsEnabled = enabled;
}

const float *GraphProcessor::collectTaps()
{
    float *out = m->tapOut.data();
    for (Impl::Tap &tap : m->taps) {
        out[0] = tap.peak[0];
        out[1] = tap.peak[1];
        const double count = static_cast<double>(std::max<int64_t>(1, tap.count));
        out[2] = static_cast<float>(std::sqrt(tap.sumSquares[0] / count));
        out[3] = static_cast<float>(std::sqrt(tap.sumSquares[1] / count));
        for (int i = 0; i < kScopeLength; ++i)
            out[4 + i] = tap.scope[(tap.scopeWrite + i) % kScopeLength];
        tap.peak[0] = tap.peak[1] = 0.0f;
        tap.sumSquares[0] = tap.sumSquares[1] = 0.0;
        tap.count = 0;
        out += kTapStride;
    }
    return m->tapOut.data();
}

int GraphProcessor::tapSlots() const
{
    return static_cast<int>(m->taps.size());
}

const float *GraphProcessor::modulatorValues() const
{
    return m->modValues.data();
}

void GraphProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m->sampleRate = spec.sampleRate;
    juce::dsp::ProcessSpec stageSpec = spec;
    stageSpec.maximumBlockSize = static_cast<juce::uint32>(kSlice);
    stageSpec.numChannels = 2;
    m->root.prepare(stageSpec);
    m->seed();
}

void GraphProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int frames = static_cast<int>(block.getNumSamples());
    for (int offset = 0; offset < frames;) {
        if (m->sliceFrame == 0)
            m->control();
        const int count = std::min(frames - offset, kSlice - m->sliceFrame);
        auto slice = block.getSubBlock(size_t(offset), size_t(count));
        m->observe(static_cast<int>(m->inputSlot()), slice);
        m->root.process(slice, *m);
        m->observe(static_cast<int>(m->outputSlot()), slice);
        m->sliceFrame += count;
        offset += count;
        if (m->sliceFrame == kSlice) {
            m->finishSlice();
            m->sliceFrame = 0;
        }
    }
}

void GraphProcessor::reset()
{
    m->seed();
}

int GraphProcessor::latencySamples() const
{
    return m->root.latency();
}

} // namespace drift::audiofx
