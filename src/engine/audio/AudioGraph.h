#pragma once

#include "engine/audio/WavReader.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace drift::audiofx {

// The "graph" object of an audio-effect.json with processor "graph", parsed and validated. Nothing
// here runs audio: GraphProcessor builds the stages from it. Free of JUCE and Qt so the catalog can
// hold one per entry and share it between every clip using the effect.
//
//   "graph": {
//     "version": 1,
//     "modulators": [ { "id": "m1", "type": "lfo", "knobs": { "rate": { "param": "wobble" } } } ],
//     "chain": [
//       { "id": "p1", "type": "filter", "knobs": { "cutoff": 900 },
//         "mod": { "cutoff": [ { "from": "m1", "depth": 0.3 } ] } },
//       { "id": "s1", "type": "split", "mode": "parallel", "crossfade": true, "blend": { "param": "space" },
//         "lanes": [ { "chain": [] },
//                    { "chain": [ { "id": "p2", "type": "convolution", "ir": "ir/plate.wav" } ] } ] }
//     ]
//   }
//
// A knob is a literal or { "param": "<identifier>" }, bound to one of the manifest's parameters.
// Route depth is a fraction of the target knob's range, so 0.25 sweeps a quarter of it.

// A literal, or (param >= 0) an index into AudioGraphDesc::params.
struct KnobValue
{
    float literal = 0.0f;
    int param = -1;
};

struct ModRoute
{
    int modulator = -1;
    KnobValue depth;
};

struct GraphNode
{
    enum class Kind { Pedal, Split };

    Kind kind = Kind::Pedal;
    std::string id;
    std::string type;
    // Depth-first position among every node, splits included: what taps and live edits index by.
    int flat = -1;
    // Pedals: one per PedalSpec knob, in spec order. Splits: blend, then the crossovers.
    std::vector<KnobValue> knobs;
    std::vector<std::vector<ModRoute>> routes; // pedals only, parallel to knobs
    KnobValue bypass;                          // >= 0.5 bypasses
    std::shared_ptr<const IrData> ir;          // convolution only

    // Splits only.
    bool bands = false;
    bool crossfade = false;
    std::vector<float> laneGains;
    std::vector<std::vector<GraphNode>> lanes;
};

struct GraphModulator
{
    std::string id;
    std::string type;
    std::vector<KnobValue> knobs; // modulator spec order
    std::vector<float> steps;     // step sequencer only, each 0..1
    int source = -1;              // envelope only: flat node index, or -1 for the graph input
};

struct AudioGraphDesc
{
    std::vector<std::string> params;
    std::vector<float> paramDefaults;
    std::vector<GraphNode> chain;
    std::vector<GraphModulator> modulators;
    int nodeCount = 0;
    // Lookback a correct block needs: echo and reverb tails, IR length, follower settle time.
    int prerollMs = 0;
};

inline constexpr int kMaxSplitLanes = 4;
inline constexpr int kMaxSplitDepth = 2;
inline constexpr int kMaxGraphNodes = 64;
inline constexpr int kMaxSteps = 16;
inline constexpr int kMaxGraphPrerollMs = 4000;
inline constexpr int kSplitBlendKnob = 0; // crossovers follow at 1..lanes-1
inline constexpr float kCrossoverMinHz = 20.0f;
inline constexpr float kCrossoverMaxHz = 20000.0f;

// Resolves an "ir" path from the manifest to decoded audio; null with *error set on failure.
using IrLoader = std::function<std::shared_ptr<const IrData>(const std::string &path, std::string *error)>;

// `params` are the manifest's parameter identifiers in order, `paramDefaults` their defaults.
std::shared_ptr<const AudioGraphDesc> parseAudioGraph(std::string_view graphJson, std::vector<std::string> params,
                                                      std::vector<float> paramDefaults, const IrLoader &loadIr,
                                                      std::string *error);

// A legacy manifest ("processor": "echo") as a one-pedal graph: the classic pedal with every knob a
// parameter shares an identifier (or alias) with bound to it. Null for an unknown processor.
std::shared_ptr<const AudioGraphDesc> classicGraph(std::string_view processorId, std::vector<std::string> params,
                                                   std::vector<float> paramDefaults);

} // namespace drift::audiofx
