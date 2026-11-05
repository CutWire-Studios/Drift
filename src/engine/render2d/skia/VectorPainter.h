#pragma once

#include <QSize>
#include <QtGlobal>

#include <memory>

class SkCanvas;

namespace drift::skia {

// Something that draws itself with Skia onto a layer-sized canvas: a text block, a shape, a
// Lottie frame. This header is deliberately Skia-free so GpuLayer can carry one without pulling
// Skia's include root and compile defines into the whole engine.
//
// Painters are built on the scene-build thread and painted on the GL thread (or a raster thread
// for thumbnails), so they must capture everything they need by value — no project pointers.
class VectorPainter
{
public:
    virtual ~VectorPainter() = default;

    // Layer target size in device pixels.
    virtual QSize size() const = 0;

    // A stable hash of everything that changes pixels, so static content is drawn once and then
    // served from a GPU-resident cache. 0 means "redraw every frame" — the right answer for
    // anything animated, which would otherwise thrash the cache with one-off entries.
    virtual quint64 cacheKey() const = 0;

    // The canvas is premultiplied RGBA, cleared to transparent, origin at the layer's top-left,
    // one unit per device pixel.
    virtual void paint(SkCanvas &canvas) const = 0;
};

// `inner` rasterized `scale` (> 1) times larger than its size() says, for a layer the GPU will draw
// bigger than it was laid out. Draws the same picture through a scaled canvas, so geometry,
// strokes, blurs and glyphs are all re-rendered at the finer grid rather than resampled.
// Defined with the Skia sources; only callers built with DRIFT_WITH_SKIA may use it.
std::shared_ptr<const VectorPainter> scaledPainter(std::shared_ptr<const VectorPainter> inner,
                                                   double scale);

} // namespace drift::skia
