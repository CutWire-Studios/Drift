#pragma once

#include "core/effects/Effect.h"
#include "core/effects/EffectPreset.h"
#include "engine/gpu/effects/GpuEffectDefinition.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

#include <optional>

// Engine-side preset entry: core metadata plus libavfilter / compositor / GPU wiring.
struct EffectPresetEntry
{
    drift::EffectPresetMeta meta;
    QString filterName;                    // single-filter name, e.g. "eq"
    QString graphTemplate;                 // optional multi-filter template with {{key}} placeholders
    QMap<QString, QVariant> fixedParams;   // always applied, not exposed as sliders
    bool isGpu = false;                    // true for file-based GPU effect packages
    bool isModel3d = false;                // true for "backend": "model3d" face-prop packages
    bool isFaceSwap = false;               // true for "backend": "faceswap" packages
    bool needsFace = false;                // "requires": "face" — engine injects u_face* uniforms
    bool needsDepth = false;               // "requires": "depth" — engine binds the depth map
    bool needsMask = false;                // "requires": "mask" — engine binds the clip's masks
    drift::GpuEffectDefinition gpu;        // valid when isGpu && gpu.valid; packageDir also set for model3d
    int catalogOrder = 0;                  // lower sorts first in the browser catalog
    QString thumbnailPath;                 // absolute path to package thumbnail (optional)
};

// Backward-compatible alias used across the codebase.
using EffectParamDef = drift::EffectParamSpec;
using EffectDef = EffectPresetEntry;

const QList<EffectPresetEntry> &effectCatalog();
const EffectPresetEntry *effectDefForId(const QString &id);
// The Forge's live-preview slot. Never listed in the catalog or its browsers; lookups by this id
// resolve to the draft. A replaced draft is kept alive for a few more frames because the render
// thread may still hold the pointer handed out for it.
inline constexpr QLatin1String kDraftEffectId{"forge.draft.effect"};
void setDraftEffect(std::optional<EffectPresetEntry> entry);


// Merge fixed + instance parameters for a clip effect.
QMap<QString, QVariant> resolvedEffectParameters(const drift::Effect &effect, const EffectPresetEntry &def);

// Build a libavfilter graph fragment for one catalog effect instance (empty when compositor-only / GPU).
QString buildFilterGraphForEffect(const drift::Effect &effect, const EffectPresetEntry *def = nullptr);

// All stable preset ids (for tests and validation).
QStringList effectPresetIds();

// Browser categories in display order (stable id + user-facing label).
QList<QPair<QString, QString>> effectCategories();
QString effectCategoryLabel(const QString &categoryId);

// Reload file-based GPU packages from the given roots (or defaultSearchPaths when empty).
// Built-in libav/compositor presets are always present; GPU packages are appended.
void reloadEffectCatalog(const QStringList &packageRoots = {});
