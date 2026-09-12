#pragma once

#include "VectorPainter.h"
#include "core/ShapeStyle.h"

#include <QRectF>

#include <memory>

// Skia-free header: FrameCompositor builds these on the scene thread without seeing Skia.

namespace drift::skia {

struct ShapePaintRequest
{
    ShapeStyle style; // already resolvedAt() the instant
    QRectF layoutRect;
    double renderScale = 1.0;
    double timeSec = 0.0; // since the clip start; drives time-based paints
};

struct ShapePainterResult
{
    std::shared_ptr<const VectorPainter> painter;
    QRectF rect; // destination in canvas px, bleed included
};

// The outline fills the layout rect exactly; strokes, shadows and glows spill into a bleed margin
// around it, so the image is larger than the layout and `rect` says where it lands. Static
// styles carry a cache key and are drawn once; a keyframed or time-driven style redraws every
// frame.
ShapePainterResult makeShapePainter(const ShapePaintRequest &request);
// Layout at the origin: tests and thumbnails.
ShapePainterResult makeShapePainter(const ShapeStyle &style, int width, int height, double renderScale,
                                    double timeSec = 0.0);

// How far the enabled layers can reach outside the outline, in project px.
double shapeBleedFor(const ShapeStyle &style);

} // namespace drift::skia
