#pragma once

#include "app/forge/doc/ForgeDoc.h"

namespace drift::forge {

enum class ParamTexture { None, Asset, Track, Lookup };

struct ParamTypeSpec
{
    // Only the next Drift reads it.
    bool next = false;
    ParamTexture texture = ParamTexture::None;
    // Whether the manifest carries a defaultValue.
    bool exportsDefault = true;
};

const ParamTypeSpec &paramSpec(ParamType t);
inline const ParamTypeSpec &paramSpec(const ParamDef &p) { return paramSpec(p.type); }

QString paramGlslType(const ParamDef &p);
// The literal an input falls back to when it stops being a slider.
Literal paramLiteral(const ParamDef &p);
// The value in the shape Drift's JSON uses for this type.
QJsonValue paramDriftValue(const ForgeDoc &doc, const ParamDef &p, const QJsonValue &v);
// Type-specific keys of the parameter's manifest entry.
QJsonObject paramManifestFields(const ParamDef &p);

QString mimeExt(const QString &mime);
// File an image parameter's default picture is written to inside the package; null if the asset is missing.
QString imageParamFile(const ForgeDoc &doc, const ParamDef &p);

bool isSamplerParam(const ParamDef &p);
// A sampler bound to a timeline track in Drift (a video track, a track matte or a track's audio).
bool isTrackParam(const ParamDef &p);

}
