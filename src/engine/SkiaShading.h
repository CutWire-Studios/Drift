#pragma once

#include "core/BlendMode.h"
#include "core/TextShading.h"

#include <QRectF>

#include "include/core/SkBlendMode.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkRefCnt.h"

class SkCanvas;

// The shading stack in Skia form, shared by the text and shape painters so a stroke, a shadow
// blur or a gradient reads the same on both. Skia-including header: only Skia*.cpp may include it.

namespace drift::skia {

SkBlendMode toSkBlendMode(BlendMode mode);

// The layer's own paint source — solid, gradient over `box`, texture or shader effect — into
// `paint`, with `alpha` folded into the colour. A texture or effect that fails to build falls
// back to the tint. `progress` feeds progress-driven effects (a text wipe); -1 when there is none.
void applyShadingPaint(SkPaint &paint, const TextPaint &source, const QRectF &box, double timeSec, double alpha,
                       double progress = -1.0);

// Trim → dash → sketch, composed inner-to-outer so the write-on trims the original outline, the
// dash runs along what is left and the jitter is applied last. Null when none applies.
// `strokeWidthPx` scales the dash pattern and offset; `scale` the sketch lengths.
sk_sp<SkPathEffect> strokePathEffectFor(const TextShadingLayer &layer, double strokeWidthPx, double scale);
// The sketch jitter on a fill outline; null when off.
sk_sp<SkPathEffect> fillPathEffectFor(const TextShadingLayer &layer, double scale);

// Wraps the layer's draws in a saveLayer when its opacity, blend, knockout, spread or blur asks
// for one. Blur is left out for strokes and extrudes, which draw sharp. Every begin is matched by
// an end, which restores only when a layer was pushed.
struct ShadingLayerGroup
{
    bool pushed = false;
};
ShadingLayerGroup beginShadingLayer(SkCanvas &canvas, const TextShadingLayer &layer, double scale);
void endShadingLayer(SkCanvas &canvas, const ShadingLayerGroup &group);

} // namespace drift::skia
