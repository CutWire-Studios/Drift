#include "engine/audio/AudioGraph.h"

#include "engine/audio/PedalCatalog.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace drift::audiofx {

namespace {

struct Parser
{
    const std::vector<std::string> &params;
    const IrLoader &loadIr;
    std::map<std::string, int> modulatorIds;
    std::map<std::string, int> nodeIds;
    std::vector<std::pair<GraphModulator *, std::string>> envelopeSources;
    std::string error;
    int nextFlat = 0;

    bool fail(std::string message)
    {
        if (error.empty())
            error = std::move(message);
        return false;
    }

    bool knobValue(const juce::var &v, float min, float max, KnobValue *out, const std::string &where)
    {
        if (v.isBool() || v.isInt() || v.isInt64() || v.isDouble()) {
            out->literal = juce::jlimit(min, max, static_cast<float>(static_cast<double>(v)));
            return true;
        }
        if (auto *obj = v.getDynamicObject(); obj && obj->hasProperty("param")) {
            const std::string name = obj->getProperty("param").toString().toStdString();
            const auto it = std::find(params.begin(), params.end(), name);
            if (it == params.end())
                return fail(where + ": unknown parameter '" + name + "'");
            out->param = static_cast<int>(it - params.begin());
            return true;
        }
        return fail(where + ": expected a number or {\"param\": ...}");
    }

    // Fills `knobs` from the spec's defaults, then overrides from a "knobs" object.
    bool knobs(const PedalSpec &spec, const juce::var &obj, std::vector<KnobValue> *out, const std::string &where)
    {
        out->clear();
        for (const KnobSpec &knob : spec.knobs)
            out->push_back({knob.defaultValue, -1});
        if (obj.isVoid())
            return true;
        auto *dyn = obj.getDynamicObject();
        if (!dyn)
            return fail(where + ": \"knobs\" must be an object");
        for (const auto &property : dyn->getProperties()) {
            const std::string name = property.name.toString().toStdString();
            const int index = knobIndex(spec, name);
            if (index < 0)
                return fail(where + ": " + spec.type + " has no knob '" + name + "'");
            const KnobSpec &knob = spec.knobs[size_t(index)];
            if (!knobValue(property.value, knob.min, knob.max, &(*out)[size_t(index)], where + "." + name))
                return false;
        }
        return true;
    }

    bool modulator(const juce::var &v, GraphModulator *out)
    {
        out->id = v.getProperty("id", {}).toString().toStdString();
        out->type = v.getProperty("type", {}).toString().toStdString();
        const std::string where = "modulator '" + out->id + "'";
        if (out->id.empty())
            return fail("modulator without an id");
        if (!modulatorIds.emplace(out->id, int(modulatorIds.size())).second)
            return fail(where + ": duplicate id");
        const PedalSpec *spec = modulatorSpec(out->type);
        if (!spec)
            return fail(where + ": unknown modulator type '" + out->type + "'");
        if (!knobs(*spec, v.getProperty("knobs", {}), &out->knobs, where))
            return false;

        if (out->type == "steps") {
            const juce::var steps = v.getProperty("steps", {});
            if (!steps.isArray() || steps.size() < 1 || steps.size() > kMaxSteps)
                return fail(where + ": \"steps\" must hold 1 to 16 values");
            for (const juce::var &step : *steps.getArray())
                out->steps.push_back(juce::jlimit(0.0f, 1.0f, static_cast<float>(static_cast<double>(step))));
        }
        if (out->type == "envelope") {
            const std::string source = v.getProperty("source", "input").toString().toStdString();
            if (source != "input")
                envelopeSources.emplace_back(out, source);
        }
        return true;
    }

    bool routes(const PedalSpec &spec, const juce::var &mod, GraphNode *node, const std::string &where)
    {
        node->routes.assign(node->knobs.size(), {});
        if (mod.isVoid())
            return true;
        auto *dyn = mod.getDynamicObject();
        if (!dyn)
            return fail(where + ": \"mod\" must be an object");
        for (const auto &property : dyn->getProperties()) {
            const std::string name = property.name.toString().toStdString();
            const int index = knobIndex(spec, name);
            if (index < 0)
                return fail(where + ": " + spec.type + " has no knob '" + name + "'");
            if (!spec.knobs[size_t(index)].continuous())
                return fail(where + "." + name + ": switches cannot be modulated");
            if (!property.value.isArray())
                return fail(where + ".mod." + name + ": expected an array of routes");
            for (const juce::var &r : *property.value.getArray()) {
                const std::string from = r.getProperty("from", {}).toString().toStdString();
                const auto it = modulatorIds.find(from);
                if (it == modulatorIds.end())
                    return fail(where + ".mod." + name + ": unknown modulator '" + from + "'");
                ModRoute route;
                route.modulator = it->second;
                if (!knobValue(r.getProperty("depth", 0.0), -1.0f, 1.0f, &route.depth, where + ".mod." + name))
                    return false;
                node->routes[size_t(index)].push_back(route);
            }
        }
        return true;
    }

    bool chain(const juce::var &v, int depth, std::vector<GraphNode> *out, const std::string &where)
    {
        if (!v.isArray())
            return fail(where + ": \"chain\" must be an array");
        for (const juce::var &item : *v.getArray()) {
            GraphNode node;
            if (!this->node(item, depth, &node))
                return false;
            out->push_back(std::move(node));
        }
        return true;
    }

    bool node(const juce::var &v, int depth, GraphNode *out)
    {
        out->id = v.getProperty("id", {}).toString().toStdString();
        out->type = v.getProperty("type", {}).toString().toStdString();
        const std::string where = "node '" + out->id + "'";
        if (out->id.empty())
            return fail("node without an id");
        out->flat = nextFlat++;
        if (out->flat >= kMaxGraphNodes)
            return fail("graph has more than 64 nodes");
        if (!nodeIds.emplace(out->id, out->flat).second)
            return fail(where + ": duplicate id");
        if (!knobValue(v.getProperty("bypass", false), 0.0f, 1.0f, &out->bypass, where + ".bypass"))
            return false;

        if (out->type == "split")
            return split(v, depth, out, where);

        const PedalSpec *spec = pedalSpec(out->type);
        if (!spec)
            return fail(where + ": unknown pedal type '" + out->type + "'");
        if (!knobs(*spec, v.getProperty("knobs", {}), &out->knobs, where)
            || !routes(*spec, v.getProperty("mod", {}), out, where))
            return false;

        if (out->type == "convolution") {
            const std::string path = v.getProperty("ir", {}).toString().toStdString();
            if (path.empty())
                return fail(where + ": convolution needs an \"ir\"");
            std::string irError;
            out->ir = loadIr ? loadIr(path, &irError) : nullptr;
            if (!out->ir)
                return fail(where + ": " + (irError.empty() ? "cannot load '" + path + "'" : irError));
        }
        return true;
    }

    bool split(const juce::var &v, int depth, GraphNode *out, const std::string &where)
    {
        out->kind = GraphNode::Kind::Split;
        if (depth >= kMaxSplitDepth)
            return fail(where + ": splits nest at most two deep");

        const std::string mode = v.getProperty("mode", "parallel").toString().toStdString();
        if (mode != "parallel" && mode != "bands")
            return fail(where + ": mode must be \"parallel\" or \"bands\"");
        out->bands = mode == "bands";
        out->crossfade = static_cast<bool>(v.getProperty("crossfade", false));

        const juce::var lanes = v.getProperty("lanes", {});
        if (!lanes.isArray() || lanes.size() < 2 || lanes.size() > kMaxSplitLanes)
            return fail(where + ": a split has 2 to 4 lanes");
        const int laneCount = lanes.size();
        if (out->crossfade && (out->bands || laneCount != 2))
            return fail(where + ": crossfade needs a parallel split with two lanes");

        out->knobs.push_back({0.5f, -1});
        if (!knobValue(v.getProperty("blend", 0.5), 0.0f, 1.0f, &out->knobs[kSplitBlendKnob], where + ".blend"))
            return false;

        if (out->bands) {
            // Defaults spread the bands evenly on a log scale between 200 Hz and 5 kHz.
            const juce::var crossovers = v.getProperty("crossovers", {});
            if (!crossovers.isVoid() && (!crossovers.isArray() || crossovers.size() != laneCount - 1))
                return fail(where + ": \"crossovers\" needs one frequency per lane boundary");
            for (int i = 0; i < laneCount - 1; ++i) {
                const float t = laneCount == 2 ? 0.5f : float(i) / float(laneCount - 2);
                KnobValue knob{200.0f * std::pow(25.0f, t), -1};
                if (!crossovers.isVoid()
                    && !knobValue(crossovers[i], kCrossoverMinHz, kCrossoverMaxHz, &knob, where + ".crossovers"))
                    return false;
                out->knobs.push_back(knob);
            }
        }

        for (const juce::var &lane : *lanes.getArray()) {
            out->laneGains.push_back(juce::jlimit(0.0f, 4.0f, static_cast<float>(static_cast<double>(lane.getProperty("gain", 1.0)))));
            out->lanes.emplace_back();
            if (!chain(lane.getProperty("chain", juce::Array<juce::var>()), depth + 1, &out->lanes.back(), where))
                return false;
        }
        return true;
    }
};

int seriesPrerollMs(const std::vector<GraphNode> &nodes)
{
    int total = 0;
    for (const GraphNode &node : nodes) {
        if (node.kind == GraphNode::Kind::Split) {
            int longest = 0;
            for (const auto &lane : node.lanes)
                longest = std::max(longest, seriesPrerollMs(lane));
            total += longest;
            continue;
        }
        total += pedalSpec(node.type)->prerollMs;
        // A convolution tail is as long as its IR, plus whatever pre-delay is dialled in.
        if (node.ir)
            total += int(double(node.ir->frames) * 1000.0 / node.ir->sampleRate) + 200;
    }
    return total;
}

int prerollMs(const AudioGraphDesc &graph)
{
    int modulators = 0;
    for (const GraphModulator &modulator : graph.modulators)
        modulators = std::max(modulators, modulatorSpec(modulator.type)->prerollMs);
    return std::min(kMaxGraphPrerollMs, seriesPrerollMs(graph.chain) + modulators);
}

} // namespace

std::shared_ptr<const AudioGraphDesc> parseAudioGraph(std::string_view graphJson, std::vector<std::string> params,
                                                      std::vector<float> paramDefaults, const IrLoader &loadIr,
                                                      std::string *error)
{
    auto graph = std::make_shared<AudioGraphDesc>();
    graph->params = std::move(params);
    graph->paramDefaults = std::move(paramDefaults);
    graph->paramDefaults.resize(graph->params.size(), 0.0f);

    Parser parser{graph->params, loadIr, {}, {}, {}, {}, 0};
    juce::var root;
    const juce::Result parsed = juce::JSON::parse(juce::String::fromUTF8(graphJson.data(), int(graphJson.size())), root);
    bool ok = parsed.wasOk() || parser.fail("graph is not valid JSON: " + parsed.getErrorMessage().toStdString());

    if (ok && !root.getDynamicObject())
        ok = parser.fail("graph must be an object");
    if (ok && int(root.getProperty("version", 1)) != 1)
        ok = parser.fail("unsupported graph version");

    if (ok) {
        const juce::var modulators = root.getProperty("modulators", juce::Array<juce::var>());
        if (!modulators.isArray()) {
            ok = parser.fail("\"modulators\" must be an array");
        } else {
            graph->modulators.resize(size_t(modulators.size()));
            for (int i = 0; ok && i < modulators.size(); ++i)
                ok = parser.modulator(modulators[i], &graph->modulators[size_t(i)]);
        }
    }
    ok = ok && parser.chain(root.getProperty("chain", {}), 0, &graph->chain, "graph");

    for (auto &[modulator, source] : parser.envelopeSources) {
        const auto it = parser.nodeIds.find(source);
        if (it == parser.nodeIds.end()) {
            ok = ok && parser.fail("modulator '" + modulator->id + "': unknown source '" + source + "'");
            break;
        }
        modulator->source = it->second;
    }

    if (!ok) {
        if (error)
            *error = parser.error;
        return nullptr;
    }
    graph->nodeCount = parser.nextFlat;
    graph->prerollMs = prerollMs(*graph);
    return graph;
}

std::shared_ptr<const AudioGraphDesc> classicGraph(std::string_view processorId, std::vector<std::string> params,
                                                   std::vector<float> paramDefaults)
{
    const PedalSpec *spec = pedalSpec("classic." + std::string(processorId));
    if (!spec)
        return nullptr;

    auto graph = std::make_shared<AudioGraphDesc>();
    graph->params = std::move(params);
    graph->paramDefaults = std::move(paramDefaults);
    graph->paramDefaults.resize(graph->params.size(), 0.0f);

    GraphNode node;
    node.id = "p1";
    node.type = spec->type;
    node.flat = 0;
    for (const KnobSpec &knob : spec->knobs)
        node.knobs.push_back({knob.defaultValue, -1});
    node.routes.assign(node.knobs.size(), {});
    for (size_t p = 0; p < graph->params.size(); ++p) {
        const int index = knobIndex(*spec, graph->params[p]);
        if (index >= 0 && node.knobs[size_t(index)].param < 0)
            node.knobs[size_t(index)].param = int(p);
    }
    graph->chain.push_back(std::move(node));
    graph->nodeCount = 1;
    // The manifest's own prerollMs governs a legacy effect, exactly as before graphs existed.
    graph->prerollMs = 0;
    return graph;
}

} // namespace drift::audiofx
