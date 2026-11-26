#pragma once

#include "core/base/Time.h"
#include "core/effects/EffectPreset.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

namespace drift {

// The built-in pictures a ClipType::Generator can be. A kind is only a parameter schema: the clip
// keeps its values (and their keyframes) in Clip::procedural exactly like a procedural 3D clip, with
// packageId holding the kind id, so the keyframe stack and the parameter inspector need no second path.
struct GeneratorKind
{
    QString id; // "gen.solid", "gen.particles", ...
    QString label;
    QString category; // "background", "pattern", "leader", "particles"
    QString description;
    double defaultDurationSec = 5.0;
    // Redraws every frame whatever the parameters say (the picture depends on the clip time).
    bool timeDriven = false;
    QList<EffectParamSpec> parameters;
};

// A kind plus parameter overrides, offered next to the plain kinds ("Confetti burst").
struct GeneratorPreset
{
    QString id; // "particles.confetti"
    QString label;
    QString kindId;
    QString category;
    double durationSec = 0.0; // 0 = the kind's default
    QMap<QString, QVariant> params;
};

constexpr int kGeneratorSpriteImage = 7; // index of the "Image / emoji" entry of the particle sprite enum

const QList<GeneratorKind> &generatorKinds();
const GeneratorKind *generatorKind(const QString &id);
const QList<GeneratorPreset> &generatorPresets();
const GeneratorPreset *generatorPreset(const QString &id);

// A fresh parameter map: every spec's default, then `overrides` on top.
QMap<QString, QVariant> generatorDefaultParameters(const GeneratorKind &kind,
                                                   const QMap<QString, QVariant> &overrides = {});

} // namespace drift
