#pragma once

#include "TextAnimator.h"
#include "TextParamSpec.h"
#include "VectorSource.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

// Text animation presets: JSON recipes (presets/text-animations/*.json, user files under
// AppData) that expand into animators once their typed params are substituted. The In / Out /
// Loop galleries list them; a TextAnimationSlot keeps the preset id plus the user's overrides.

namespace drift {

struct TextAnimationPreset
{
    QString id;
    QString label;
    QString category;   // gallery chip: basic, character, word, kinetic, light, colour, hold, imported
    QString sampleText; // thumbnail text
    QList<TextAnimSlotKind> slotKinds;
    int order = 100;
    bool builtIn = true;
    QString source;     // file it came from ("" for resources)
    QList<TextAnimParamSpec> params;
    QJsonArray animators; // with "{param}" references
    QJsonObject caret;    // optional, with references
    QJsonObject flags;    // unitLocked, orderLocked, durationLocked, hasDirection, mode ("cycle"|"hold"), mirrorForOut
    QStringList report;   // import warnings (Lottie)

    bool supportsSlot(TextAnimSlotKind kind) const { return slotKinds.contains(kind); }
    const TextAnimParamSpec *param(const QString &id) const;
    QMap<QString, VectorSlotValue> defaultParams() const;
    bool flag(const QString &name, bool fallback = false) const;

    QJsonObject toJson() const;
    // Expands "commonParams" / "defaults" / the "stagger" selector shorthand. Empty on a malformed file.
    static std::optional<TextAnimationPreset> fromJson(const QJsonObject &o, QString *error = nullptr);
};

// The five params every In / Out preset carries so the slot controls always have something to
// drive: duration, stagger, unit, order, ease.
const QList<TextAnimParamSpec> &textAnimationCommonParams();

class TextAnimationPresetCatalog
{
public:
    static TextAnimationPresetCatalog &instance();

    QList<TextAnimationPreset> presets() const;
    QList<TextAnimationPreset> presetsFor(TextAnimSlotKind kind) const;
    std::optional<TextAnimationPreset> presetForId(const QString &id) const;

    // Built-in resources, then $DRIFT_TEXT_ANIMATIONS (':'-separated dirs), then the user dir;
    // later roots override earlier ids.
    void reload();
    static QString userDir();

    // User presets are files in userDir(); ids get the "user:" prefix.
    QString addUserPreset(TextAnimationPreset preset);
    bool removeUserPreset(const QString &id);
    bool renameUserPreset(const QString &id, const QString &label);

private:
    TextAnimationPresetCatalog() = default;
    void ensureLoaded() const;
    void loadDir(const QString &dir, bool builtIn, const QString &idPrefix) const;

    mutable bool m_loaded = false;
    mutable QList<TextAnimationPreset> m_presets;
};

bool isUserTextAnimationPresetId(const QString &id);

// Substitute params into the preset's animators for one slot kind. Fills the caret and the
// slot's delay/duration when the preset declares them.
struct ResolvedPresetSlot
{
    QList<TextAnimator> animators;
    std::optional<TextCaret> caret;
    TimeUs durationUs = 0; // for Curves-only presets: the "duration" param
    TimeUs delayUs = 0;    // e.g. the caret lead
    TimeUs periodUs = 0;   // Loop presets: the "period" param
    bool valid = false;
};
ResolvedPresetSlot resolvePresetSlot(const TextAnimationPreset &preset, const QMap<QString, VectorSlotValue> &params,
                                     TextAnimSlotKind kind);

// The whole set, ready for textanim::evaluateTextAnimation: preset slots expanded, inline slots
// passed through, caret / delays merged. Cached by the set's hash.
struct ResolvedTextAnimation
{
    textanim::ResolvedSlots resolved;
    TextAnimationSet set;
};
ResolvedTextAnimation resolveTextAnimation(const TextAnimationSet &set);
void clearTextAnimationResolveCache();

// The legacy TextAnimKind reveal as a preset slot. `kind` is the old JSON spelling ("slideUp"…);
// "wave" belongs in the loop slot, which `isLoop` reports.
TextAnimationSlot legacyTextAnimationSlot(const QString &kind, TimeUs durationUs, const QString &ease,
                                          const QString &unit, TimeUs staggerUs, const QString &order,
                                          bool *isLoop = nullptr);

} // namespace drift
