#pragma once

#include "app/forge/audio/Rack.h"

namespace drift::forge {

// Where an impulse response lives inside the package: ir/<name>.wav.
QString irPath(const ForgeDoc &doc, const QString &ir);

// The "graph" object of audio-effect.json, as Drift's AudioGraph parser reads it.
QJsonObject rackGraphJson(const ForgeDoc &doc);

}
