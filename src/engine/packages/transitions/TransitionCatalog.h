#pragma once

#include "engine/gpu/effects/GpuEffectDefinition.h"
#include "core/effects/EffectPreset.h"
#include "core/project/Transition.h"

#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

// A transition package: transition.json + fragment shaders, mirroring the effect packages.
// Unlike effects, transitions are always GPU (with a built-in CPU crossfade fallback).
struct TransitionPresetEntry
{
    drift::EffectPresetMeta meta;        // id, displayName, category, parameters[]
    drift::GpuEffectDefinition gpu;
    QMap<QString, QVariant> fixedParams; // always applied, not exposed as sliders
    QString audioCurve;                  // "crossfade" (default) | "dip" | "hold"
    QString previewStripPath;            // absolute path to the horizontal sprite-sheet (optional)
    int previewFrames = 0;               // number of cells in the strip
    int catalogOrder = 0;
};

const QList<TransitionPresetEntry> &transitionCatalog();
const TransitionPresetEntry *transitionDefForId(const QString &id);
// The Forge's live-preview slot. Never listed in the catalog or its browsers; lookups by this id
// resolve to the draft. A replaced draft is kept alive for a few more frames because the render
// thread may still hold the pointer handed out for it.
inline constexpr QLatin1String kDraftTransitionId{"forge.draft.transition"};
void setDraftTransition(std::optional<TransitionPresetEntry> entry);


// Merge fixed + default + instance parameters for one transition.
QMap<QString, QVariant> resolvedTransitionParameters(const drift::Transition &transition,
                                                     const TransitionPresetEntry &def);

// All loaded transition ids, in catalog order.
QStringList transitionPresetIds();

// Browser categories in display order (stable id + user-facing label).
QList<QPair<QString, QString>> transitionCategories();

void reloadTransitionCatalog(const QStringList &packageRoots = {});
