#pragma once

#include "engine/gpu/GlRuntime.h"
#include "engine/render2d/skia/VectorPainter.h"

#include <QImage>

#include <memory>

namespace drift::skia {

// Skia's Ganesh context living inside GlRuntime's GL context. There is exactly one, because
// there is exactly one GL context and one GL thread (GlRuntime::exec); every method except
// rasterize() must run inside exec().
//
// Skia never draws into a pooled FBO directly: those have no stencil attachment and Skia cannot
// add one to a framebuffer it did not create. It renders into a surface it owns and the result is
// copied into a pooled target, which keeps Skia's premultiplied pixels (GlTarget::premultiplied) so
// bilinear sampling never blends the colour of fully transparent texels into an antialiased edge.
class SkiaRuntime
{
public:
    ~SkiaRuntime();

    // Creates the runtime on first use and attaches it to rt. GL thread only, context current.
    // Returns nullptr when Skia could not attach to this context (the caller falls back).
    static SkiaRuntime *acquire(gl::GlRuntime &rt);

    // Paint into a fresh pooled, premultiplied target of painter.size() that the caller owns and
    // may mutate. A non-zero cacheKey() is served from a GPU-resident LRU after the first paint, so
    // static text and shapes cost one blit per frame rather than a redraw. Invalid target on failure.
    gl::GlTarget paintToTarget(gl::GlRuntime &rt, QOpenGLExtraFunctions *gl,
                               const VectorPainter &painter);

    // Drop GPU-side caches (Android background). shutdown() additionally abandons the context and
    // must run before GlRuntime deletes its own objects.
    void releaseCaches();
    void shutdown();

    // CPU path, any thread, no GL. Premultiplied ARGB32 image of painter.size().
    static QImage rasterize(const VectorPainter &painter);

    struct Stats
    {
        quint64 paints = 0;     // painter.paint() calls
        quint64 cacheHits = 0;  // paintToTarget served from the LRU
        quint64 cacheMisses = 0;// non-zero cacheKey() that had to be painted
    };
    Stats stats() const;

private:
    struct Impl;
    explicit SkiaRuntime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> d;
};

} // namespace drift::skia
