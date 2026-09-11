#pragma once

#include "TextAnimationPreset.h"

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

// After Effects text animators as Lottie exports them (a text layer's "t.a" array) turned into
// a Drift animation preset: range selectors → Curves selectors, animated props → animator props
// with their keyframes re-based onto the slot's progress. Pure QJson; nothing here needs Skottie.

namespace drift::lottie {

struct TextLayerSummary
{
    QString name;
    int animatorCount = 0;
    QStringList unsupported;
};

struct TextImportReport
{
    bool ok = false;
    QString error;
    QList<TextLayerSummary> layers; // every text layer in the document
};

TextImportReport inspectLottieText(const QByteArray &json);

struct ImportOptions
{
    int layerIndex = -1;   // -1 = the first text layer that has animators
    QString category;      // "in" | "out" | "loop"; empty = guessed from the motion
    QString id;            // preset id; empty = derived from the layer name
    QString label;
};

// Empty when the document has no usable text animator; `warnings` lists what was skipped
// (expression selectors, 3D rotation, anchor animation, …).
std::optional<TextAnimationPreset> importLottieTextPreset(const QByteArray &json, const ImportOptions &options,
                                                          QStringList *warnings = nullptr, QString *error = nullptr);

} // namespace drift::lottie
