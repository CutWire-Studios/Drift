#pragma once

#include "app/forge/audio/Rack.h"

namespace drift::forge {

// First Drift that loads a single classic pedal written as a built-in processor.
inline constexpr const char *kAudioClassicAppVersion = "0.7.1";
// First Drift that runs audio graphs (processor "graph") and keyframes audio parameters.
inline constexpr const char *kAudioGraphAppVersion = "0.8.0";

// The legacy processor `doc` is when it is exactly one classic pedal with every knob a slider of the
// same name (what schema-1 documents migrate to); empty otherwise. Those keep the old format.
QString legacyProcessorFor(const ForgeDoc &doc);

QString audioMinAppVersion(const ForgeDoc &doc);

// audio-effect.json: the classic processor shortcut, or processor "graph".
QJsonObject audioManifest(const ForgeDoc &doc);

// The same manifest with every literal knob, bypass, blend, crossover, modulator knob and route
// depth turned into a hidden parameter "__k_<itemId>_<knob>" (route depth: "__k_<routeId>_depth"),
// so dragging a knob only changes Effect.parameters and never rebuilds the graph. `hoisted` maps
// each name to the value it holds now.
QJsonObject audioPreviewManifest(const ForgeDoc &doc, QJsonObject *hoisted = nullptr);

}
