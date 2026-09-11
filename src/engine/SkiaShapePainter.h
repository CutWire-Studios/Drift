#pragma once

#include "VectorPainter.h"
#include "core/ShapeStyle.h"

#include <memory>

// Skia-free header: FrameCompositor builds these on the scene thread without seeing Skia.

namespace drift::skia {

// A shape clip drawn into a width×height layer, the Skia twin of drift::rasterizeShape. Shapes
// are static, so the painter always carries a cache key and is drawn once per style/size.
std::shared_ptr<const VectorPainter> makeShapePainter(const ShapeStyle &style, int width,
                                                      int height, double renderScale);

} // namespace drift::skia
