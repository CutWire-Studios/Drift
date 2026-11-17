#pragma once

#include "engine/gpu/effects/GpuEffectDefinition.h"
#include "core/effects/EffectPreset.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

// A shader-based clip in/out animation package: animation.json + fragment shaders. The pipeline
// has a single source, the clip's isolated layer.
struct AnimationPresetEntry
{
    drift::EffectPresetMeta meta;        // id, displayName, category, parameters[]
    drift::GpuEffectDefinition gpu;
    QMap<QString, QVariant> fixedParams; // always applied, not exposed as sliders
    QStringList directions;              // subset of "in", "out"
    int defaultDurationMs = 500;
    QString previewStripPath;            // absolute path to the preview image (optional)
    int catalogOrder = 0;
};

const QList<AnimationPresetEntry> &animationCatalog();
const AnimationPresetEntry *animationDefForId(const QString &id);

// Merge fixed + default + instance parameters for one animation.
QMap<QString, QVariant> resolvedAnimationParameters(const QMap<QString, QVariant> &instance,
                                                    const AnimationPresetEntry &def);

void reloadAnimationCatalog(const QStringList &packageRoots = {});
