#pragma once

#include "VectorPainter.h"
#include "core/Clip.h"

#include <QList>
#include <QRectF>
#include <QString>

#include <memory>

// The Skia twin of rasterizeText / rasterizeTextSpans. Layout is TextLayout's (QTextLayout, the
// same pieces, bleed and cache keys as the QPainter raster); only the drawing differs — glyph
// paths, outlines, pills and underlines go straight to the canvas, shadow and glow are image
// filters on a layer, and colour emoji are the bitmap face pre-drawn by Qt and blitted.
// Skia-free header.

namespace drift::skia {

struct TextPainterResult
{
    std::shared_ptr<const VectorPainter> painter; // null when there is nothing to draw
    QRectF rect;                                  // destination in canvas px, bleed included
};

TextPainterResult makeTextPainter(const Clip &clip, const QString &text, const QRectF &layoutRect,
                                  double renderScale, int activeWordIndex = -1);

struct TextSpanPainter
{
    std::shared_ptr<const VectorPainter> painter;
    QRectF rect;
    int index = 0; // reading-order index; -1 marks the static box background
    int count = 0; // number of glyph spans (excludes the box)
};

// Empty for TextAnimUnit::Block — callers use makeTextPainter for the whole-layer path.
QList<TextSpanPainter> makeTextSpanPainters(const Clip &clip, const QString &text,
                                            const QRectF &layoutRect, double renderScale,
                                            TextAnimUnit unit, int activeWordIndex = -1);

} // namespace drift::skia
