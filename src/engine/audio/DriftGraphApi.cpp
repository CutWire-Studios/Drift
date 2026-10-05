#include "engine/audio/DriftGraphApi.h"

#include "engine/audio/AudioGraph.h"
#include "engine/audio/GraphProcessor.h"
#include "engine/audio/PedalCatalog.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr int kIoFrames = 4096;

std::map<std::string, std::vector<uint8_t>> &stagedFiles()
{
    static std::map<std::string, std::vector<uint8_t>> files;
    return files;
}

std::string &lastError()
{
    static std::string error;
    return error;
}

DriftGraph *fail(std::string message)
{
    lastError() = std::move(message);
    return nullptr;
}

} // namespace

struct DriftGraph
{
    std::unique_ptr<drift::audiofx::GraphProcessor> processor;
    juce::AudioBuffer<float> buffer{2, kIoFrames};
    float io[kIoFrames * 2] = {};
    int prerollMs = 0;
    double sampleRate = 48000.0;
};

extern "C" {

void dg_stage_file(const char *path, const uint8_t *data, int size)
{
    stagedFiles()[path] = std::vector<uint8_t>(data, data + size);
}

void dg_clear_files(void)
{
    stagedFiles().clear();
}

DriftGraph *dg_create(const char *manifestJson, int sampleRate)
{
    juce::var root;
    const juce::Result parsed = juce::JSON::parse(juce::String::fromUTF8(manifestJson), root);
    if (parsed.failed() || !root.getDynamicObject())
        return fail("manifest is not a JSON object");
    if (sampleRate < 8000 || sampleRate > 384000)
        return fail("unsupported sample rate");

    // The two spellings Drift's parameter parser accepts (GpuPackageParse::parseParameters).
    std::vector<std::string> ids;
    std::vector<float> defaults;
    if (const juce::Array<juce::var> *params = root.getProperty("parameters", {}).getArray()) {
        for (const juce::var &p : *params) {
            juce::var id = p.getProperty("identifier", {});
            if (id.isVoid())
                id = p.getProperty("key", {});
            juce::var def = p.getProperty("defaultValue", {});
            if (def.isVoid())
                def = p.getProperty("default", 0.0);
            ids.push_back(id.toString().toStdString());
            defaults.push_back(def.isBool() ? (static_cast<bool>(def) ? 1.0f : 0.0f) : static_cast<float>(static_cast<double>(def)));
        }
    }

    const std::string processor = root.getProperty("processor", {}).toString().toStdString();
    std::shared_ptr<const drift::audiofx::AudioGraphDesc> graph;
    if (processor == "graph") {
        const juce::String graphJson = juce::JSON::toString(root.getProperty("graph", {}), true);
        std::string error;
        graph = drift::audiofx::parseAudioGraph(
            graphJson.toStdString(), std::move(ids), std::move(defaults),
            [](const std::string &path, std::string *err) -> std::shared_ptr<const drift::audiofx::IrData> {
                const auto it = stagedFiles().find(path);
                if (it == stagedFiles().end()) {
                    *err = "impulse response '" + path + "' was not staged";
                    return nullptr;
                }
                auto ir = std::make_shared<drift::audiofx::IrData>();
                if (!drift::audiofx::readWav(it->second.data(), it->second.size(), ir.get(), err))
                    return nullptr;
                return ir;
            },
            &error);
        if (!graph)
            return fail("graph: " + error);
    } else {
        graph = drift::audiofx::classicGraph(processor, std::move(ids), std::move(defaults));
        if (!graph)
            return fail("unknown processor '" + processor + "'");
    }

    auto *g = new DriftGraph;
    g->processor = std::make_unique<drift::audiofx::GraphProcessor>(graph);
    g->prerollMs = std::max(static_cast<int>(root.getProperty("prerollMs", 0)), graph->prerollMs);
    g->sampleRate = sampleRate;
    g->processor->prepare({static_cast<double>(sampleRate), static_cast<juce::uint32>(kIoFrames), 2});
    return g;
}

const char *dg_last_error(void)
{
    return lastError().c_str();
}

void dg_destroy(DriftGraph *graph)
{
    delete graph;
}

int dg_param_index(DriftGraph *graph, const char *identifier)
{
    return graph->processor->parameterIndex(identifier);
}

void dg_set_param(DriftGraph *graph, int index, float value)
{
    graph->processor->setParameter(index, value);
}

int dg_node_index(DriftGraph *graph, const char *nodeId)
{
    return graph->processor->nodeIndex(nodeId);
}

void dg_set_knob(DriftGraph *graph, int node, int knob, float value)
{
    graph->processor->setKnob(node, knob, value);
}

void dg_set_bypass(DriftGraph *graph, int node, int bypassed)
{
    graph->processor->setBypass(node, bypassed != 0);
}

void dg_set_route_depth(DriftGraph *graph, int node, int knob, int modulator, float depth)
{
    graph->processor->setRouteDepth(node, knob, modulator, depth);
}

void dg_set_lane_gain(DriftGraph *graph, int node, int lane, float gain)
{
    graph->processor->setLaneGain(node, lane, gain);
}

int dg_modulator_index(DriftGraph *graph, const char *modulatorId)
{
    return graph->processor->modulatorIndex(modulatorId);
}

int dg_modulator_count(DriftGraph *graph)
{
    return static_cast<int>(graph->processor->graph().modulators.size());
}

void dg_set_modulator_knob(DriftGraph *graph, int modulator, int knob, float value)
{
    graph->processor->setModulatorKnob(modulator, knob, value);
}

void dg_set_modulator_step(DriftGraph *graph, int modulator, int step, float value)
{
    graph->processor->setModulatorStep(modulator, step, value);
}

const float *dg_modulator_values(DriftGraph *graph)
{
    return graph->processor->modulatorValues();
}

void dg_reset(DriftGraph *graph, double clipSeconds)
{
    graph->processor->setClipTime(clipSeconds);
    graph->processor->reset();
}

int dg_latency(DriftGraph *graph)
{
    return graph->processor->latencySamples();
}

int dg_prime_frames(DriftGraph *graph)
{
    const int preroll = static_cast<int>(static_cast<int64_t>(graph->prerollMs) * static_cast<int64_t>(graph->sampleRate) / 1000);
    return std::max(graph->processor->latencySamples(), preroll);
}

float *dg_io(DriftGraph *graph)
{
    return graph->io;
}

int dg_io_capacity(void)
{
    return kIoFrames;
}

void dg_process(DriftGraph *graph, int frames)
{
    frames = juce::jlimit(0, kIoFrames, frames);
    float *left = graph->buffer.getWritePointer(0);
    float *right = graph->buffer.getWritePointer(1);
    for (int i = 0; i < frames; ++i) {
        left[i] = graph->io[i * 2];
        right[i] = graph->io[i * 2 + 1];
    }
    float *channels[2] = {left, right};
    juce::dsp::AudioBlock<float> block(channels, 2, static_cast<size_t>(frames));
    graph->processor->process(block);
    for (int i = 0; i < frames; ++i) {
        graph->io[i * 2] = left[i];
        graph->io[i * 2 + 1] = right[i];
    }
}

void dg_enable_taps(DriftGraph *graph, int enabled)
{
    graph->processor->enableTaps(enabled != 0);
}

int dg_tap_slots(DriftGraph *graph)
{
    return graph->processor->tapSlots();
}

int dg_tap_stride(void)
{
    return drift::audiofx::GraphProcessor::kTapStride;
}

const float *dg_collect_taps(DriftGraph *graph)
{
    return graph->processor->collectTaps();
}

const char *dg_pedal_catalog(void)
{
    static const std::string json = drift::audiofx::pedalCatalogJson();
    return json.c_str();
}

} // extern "C"
