#pragma once

#include "TextLayout.h"
#include "VectorPainter.h"
#include "core/Clip.h"
#include "core/TextAnimator.h"

#include <QList>
#include <QRectF>
#include <QString>

#include <memory>

// The Skia text painter. Layout is TextLayout's (QTextLayout pieces, bleed and cache keys); the
// animator engine's frame says where every fragment sits at this instant; the shading stack says
// how each is painted. Skia-free header.

namespace drift::skia {

struct TextPainterResult
{
    std::shared_ptr<const VectorPainter> painter; // null when there is nothing to draw
    QRectF rect;                                  // destination in canvas px, bleed included
};

struct TextPaintRequest
{
    std::shared_ptr<const text::FragmentSet> set;
    textanim::Frame frame;
    TextStyle style; // already resolvedAt() the instant
    QRectF layoutRect;
    double renderScale = 1.0;
    double timeSec = 0.0; // since the window start; drives time-based paints
    textanim::Bounds envelope;
    TextAnchorGrouping anchorGrouping = TextAnchorGrouping::Character;
    QPointF anchorAlignment;
};

TextPainterResult makeTextPainter(const TextPaintRequest &request);

// Lays the text out and evaluates its animation at clipTimeUs inside the clip's own window (3 s
// when the clip has no duration); -1 picks the middle of the hold. For thumbnails and tests.
TextPainterResult makeTextPainter(const Clip &clip, const QString &text, const QRectF &layoutRect,
                                  double renderScale, int activeWordIndex = -1, TimeUs clipTimeUs = -1);

void clearTextGeometryCache();

} // namespace drift::skia
