#pragma once

#include "core/Mask.h"

#include <QImage>
#include <QPainterPath>

namespace drift {

// The parametric shape as a path on a canvasWidth x canvasHeight grid, before rotation and
// feather. Media masks have no path — their pixels are the coverage — and return an empty one.
// Shared with the assets-panel thumbnails so a card cannot show a shape the compositor does not
// rasterize.
QPainterPath maskPath(const Mask &mask, int canvasWidth, int canvasHeight);

// Grayscale8 coverage map for the mask, white where the frame shows through.
// The GPU compositor uploads this as a texture and multiplies alpha in a shader,
// so it is built once per (mask, size) rather than per frame.
QImage maskAlphaMap(const Mask &mask, int canvasWidth, int canvasHeight);

// Coverage for a whole stack, folding each contributing entry into the accumulator by its MaskOp.
// Media entries are skipped: their coverage is decoded per frame by the compositor, which is the
// only place that has the frame time. Returns null when nothing contributes.
QImage maskAlphaMap(const QList<Mask> &masks, int canvasWidth, int canvasHeight);

QImage applyMask(const QImage &frame, const Mask &mask, int canvasWidth, int canvasHeight);

QImage applyMask(const QImage &frame, const QList<Mask> &masks, int canvasWidth, int canvasHeight);

} // namespace drift
