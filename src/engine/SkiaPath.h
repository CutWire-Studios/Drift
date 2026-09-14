#pragma once

#include <QBrush>
#include <QPainterPath>
#include <QPen>

#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"

// Qt geometry and paint expressed for Skia. Every renderer keeps building QPainterPath / QBrush /
// QPen — that is what the shape catalog, masks and text styles already speak — and converts here,
// so the two backends share one definition of a dash, a join or a gradient axis.

namespace drift::skia {

SkPath toSkPath(const QPainterPath &path);
SkMatrix toSkMatrix(const QTransform &t);
SkColor toSkColor(const QColor &c);

// Fill paint for the brush: solid colour, or a linear/radial gradient shader. Returns false for
// Qt::NoBrush (draw nothing). Other pattern styles fall back to the brush colour.
bool applyBrush(SkPaint &paint, const QBrush &brush);

// Stroke paint for the pen: width, cap, join, dash pattern (pen-width units, like Qt) and colour.
// Returns false for Qt::NoPen.
bool applyPen(SkPaint &paint, const QPen &pen);

} // namespace drift::skia
