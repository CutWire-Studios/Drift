#pragma once

#include "TextParamSpec.h"
#include "TextStyle.h"

#include <QList>
#include <QMap>
#include <QString>

// Looks: one-click recipes (Canva's vocabulary — Shadow, Lift, Hollow, Neon…) that rewrite a
// style's shading stack from a handful of sliders. The style remembers the look id and its
// params so the sliders keep regenerating it; a hand edit of a layer clears the id.

namespace drift {

struct TextLook
{
    QString id;
    QString label;
    QList<TextAnimParamSpec> params;
};

const QList<TextLook> &textLooks();
const TextLook *textLookForId(const QString &id);

// Rewrites style.layers (and the box / bend for the decoration looks) from the recipe, keeping
// the style's primary colour. Unknown ids leave the style alone and return false.
bool applyTextLook(TextStyle &style, const QString &id, const QMap<QString, VectorSlotValue> &params);

// Canned gradients for the paint editor's preset strip.
struct TextGradientPreset
{
    QString id;
    QString label;
    TextGradient gradient;
};
const QList<TextGradientPreset> &textGradientPresets();

} // namespace drift
