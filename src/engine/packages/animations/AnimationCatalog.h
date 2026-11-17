#pragma once

#include "engine/gpu/effects/GpuEffectDefinition.h"
#include "core/effects/EffectPreset.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

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
// The Forge's live-preview slot. Never listed in the catalog or its browsers; lookups by this id
// resolve to the draft. A replaced draft is kept alive for a few more frames because the render
// thread may still hold the pointer handed out for it.
inline constexpr QLatin1String kDraftAnimationId{"forge.draft.animation"};
void setDraftAnimation(std::optional<AnimationPresetEntry> entry);


// Merge fixed + default + instance parameters for one animation.
QMap<QString, QVariant> resolvedAnimationParameters(const QMap<QString, QVariant> &instance,
                                                    const AnimationPresetEntry &def);

void reloadAnimationCatalog(const QStringList &packageRoots = {});
