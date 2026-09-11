#pragma once

#include "core/ShapeStyle.h"

#include <QBrush>
#include <QImage>
#include <QPen>
#include <QRectF>

// Shape geometry and paint shared by both renderers: QPainter (rasterizeShape) and Skia
// (SkiaShapePainter). Both take their path, brush and pen from here so they cannot disagree on
// what a shape looks like — only on how the pixels are produced.

namespace drift {

// The layer-space geometry of a shape drawn into a w×h layer. Stroke width and corner radius are
// authored in project pixels and scale with the render, like the layout rect does; `bounds` is the
// layer inset by half the stroke so the outline stays inside it.
struct ShapeLayout
{
    QRectF bounds;
    double strokeWidth = 0.0; // layer px; 0 when the stroke style is None
    ShapeStyle style;         // cornerRadius already scaled
};

ShapeLayout shapeLayout(const ShapeStyle &style, int width, int height, double renderScale);

QBrush shapeBrush(const ShapeStyle &style, const QRectF &bounds);
QPen shapePen(const ShapeStyle &style, double strokeWidth);

// Rasterized at exactly the destination size: the GPU quad is the layout rect and samples this
// texture 0..1, so anything smaller is upscaled and the stroke stretches with it. Straight alpha.
QImage rasterizeShape(const ShapeStyle &style, int width, int height, double renderScale);

// Non-zero hash of everything that changes the pixels of rasterizeShape(style, w, h, scale).
quint64 shapeRasterKey(const ShapeStyle &style, int width, int height, double renderScale);

} // namespace drift
