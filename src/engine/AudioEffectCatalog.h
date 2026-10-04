#pragma once

#include "core/Effect.h"
#include "core/EffectPreset.h"
#include "engine/audio/AudioEffectRack.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

// File-based audio effect packages: audio-effects/<name>/audio-effect.json.
//
// An audio effect is a chain of JUCE DSP stages applied to a clip's audio in the mixer. Unlike the
// GPU effect catalog there is no built-in baseline — the whole catalog comes from packages, shipped
// as the "audio-effects" addon kind (src/models/AddonManager) or found under a local override dir.
//
// The DSP itself is compiled in (src/engine/audio), so a manifest names a processor rather than
// describing one. That means an addon can add a preset or relabel an effect but cannot contribute
// new DSP — the trade taken when these moved off libavfilter chain strings.

// One parsed manifest. `processorId` selects a builder from the audio effect factory; `prerollMs`
// is the lookback a correct block needs from an arbitrary timeline position (0 for stateless
// stages, larger for echo tails).
struct AudioEffectEntry
{
    QString id;
    QString displayName;
    QString category; // stable slug: "voice", "transmission", "texture", "space"
    int order = 0;
    QString processorId;
    int prerollMs = 0;
    QList<drift::EffectParamSpec> parameters;
    QString packageDir; // where it was loaded from; traces the entry back to its addon
    // Lucide file name (no extension) for the Audio FX browser card / inspector chip.
    QString icon;
    // Absolute path to package thumbnail.png (or an explicit "thumbnail" asset), if present.
    QString thumbnailPath;
};

// Parse one package directory without touching the catalog; nullopt with errorOut set on failure.
std::optional<AudioEffectEntry> loadAudioEffectPackage(const QString &packageDir, QString *errorOut);

const QList<AudioEffectEntry> &audioEffectCatalog();
const AudioEffectEntry *audioEffectDefForId(const QString &id);

// Merge each parameter's current instance value (or its default) into a name->value map.
QMap<QString, QVariant> resolvedAudioEffectParameters(const drift::Effect &effect,
                                                      const AudioEffectEntry &def);

// Reduce a clip's effect list to what AudioEffectRack needs. Effects whose catalogId is not in the
// catalog are dropped, which is what makes an uninstalled addon a clean passthrough rather than a
// dropout.
QVector<drift::AudioEffectSpec> audioEffectSpecsFor(const QList<drift::Effect> &effects);

QStringList audioEffectPresetIds();

// Browser categories in display order (stable id + user-facing label), derived from the catalog.
QList<QPair<QString, QString>> audioEffectCategories();
QString audioEffectCategoryLabel(const QString &categoryId);

// Reload from the given roots, or defaultAudioEffectSearchPaths() when empty.
void reloadAudioEffectCatalog(const QStringList &packageRoots = {});

// Default roots: DRIFT_AUDIO_EFFECTS_DIR, installed "audio-effects" addons, <appDir>/audio-effects
// and <AppDataLocation>/audio-effects — same precedence GPU effects use.
QStringList defaultAudioEffectSearchPaths();
