#pragma once

#include "app/forge/compiler/Compiler.h"

namespace drift::forge {

// First Drift that reads the parameter types in `nextFeatures` (point, choice, int, colour alpha).
inline constexpr const char *kMinAppVersion = "0.7.0";
inline constexpr const char *kNextAppVersion = "0.7.1";
// First Drift that reads track, track mask and audio inputs and the depth/face blocks.
inline constexpr const char *kNext2AppVersion = "0.9.0";
// First Drift that loads animation packages.
inline constexpr const char *kAnimationAppVersion = "0.9.3";

// One entry of the manifest's "parameters" array.
QJsonObject paramManifestEntry(const ForgeDoc &doc, const ParamDef &p);
// The manifest's "presets" array; empty when the document has none.
QJsonArray presetsManifest(const ForgeDoc &doc);

// Features of `doc` only the next Drift understands, sorted.
QStringList nextFeatures(const ForgeDoc &doc, const CompileResult &compiled);

// The oldest Drift that can load `doc`: 0.7.0 unless it uses something only the next Drift reads.
QString minAppVersion(const ForgeDoc &doc, const CompileResult &compiled);

// effect.json / transition.json / animation.json in the shape Drift's GpuPackageParse reads; audio-effect.json for audio documents.
QJsonObject packageManifest(const ForgeDoc &doc, const CompileResult &compiled);

QString packageManifestName(const ForgeDoc &doc);

}
