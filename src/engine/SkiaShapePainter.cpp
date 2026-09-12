#include "SkiaShapePainter.h"

#include "SkiaPath.h"
#include "SkiaShading.h"
#include "core/ShapePath.h"

#include <QtMath>

#include <cmath>

#include "include/core/SkCanvas.h"
#include "include/core/SkClipOp.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkStrokeRec.h"
#include "include/effects/SkCornerPathEffect.h"

namespace drift::skia {

namespace {

// The rect family rounds its own corners in shapePath(); every other kind gets the same radius
// as a corner path effect over its polygon. An ellipse has no corners to round.
bool nativeCornerKind(ShapeKind kind)
{
    switch (kind) {
    case ShapeKind::RoundedRectangle:
    case ShapeKind::SpeechBubbleRect:
    case ShapeKind::Callout:
    case ShapeKind::Ellipse:
        return true;
    default:
        return false;
    }
}

// How far a stroke layer reaches outside the outline, as a multiple of its width.
double strokeReachFactor(StrokeAlign align)
{
    switch (align) {
    case StrokeAlign::Center:
        return 0.5;
    case StrokeAlign::Outside:
        return 1.0;
    case StrokeAlign::Inside:
        return 0.0;
    }
    return 0.5;
}

double widestOutwardStroke(const ShapeStyle &style)
{
    double widest = 0.0;
    for (const TextShadingLayer &layer : style.layers) {
        if (layer.enabled && layer.kind == TextLayerKind::Stroke)
            widest = qMax(widest, layer.width * strokeReachFactor(layer.strokeAlign));
    }
    return widest;
}

class ShapePainter final : public VectorPainter
{
public:
    ShapePainter(const ShapeStyle &style, int width, int height, double renderScale, double timeSec, double bleedPx)
        : m_style(style), m_scale(renderScale), m_timeSec(timeSec), m_bleed(bleedPx),
          m_size(qMax(1, qRound(width + bleedPx * 2.0)), qMax(1, qRound(height + bleedPx * 2.0)))
    {
        m_style.keyframes.clear();
        const bool cacheable = !style.isAnimated() && !shapeTimeDrivenPaint(style);
        m_key = cacheable ? (qHashMulti(shapeStyleHash(style), width, height, qRound(renderScale * 1000.0)) | 1) : 0;

        ShapeStyle scaled = m_style;
        scaled.cornerRadius = m_style.cornerRadius * m_scale;
        const SkPath outline = toSkPath(shapePath(scaled, QRectF(0, 0, width, height)));
        if (!nativeCornerKind(scaled.kind) && scaled.cornerRadius > 0.0) {
            SkPathBuilder rounded;
            SkStrokeRec rec(SkStrokeRec::kFill_InitStyle);
            if (SkCornerPathEffect::Make(float(scaled.cornerRadius))->filterPath(&rounded, outline, &rec))
                m_path = rounded.detach();
            else
                m_path = outline;
        } else {
            m_path = outline;
        }
        m_silhouetteStrokePx = widestOutwardStroke(style) * m_scale * 2.0;
    }

    QSize size() const override { return m_size; }
    quint64 cacheKey() const override { return m_key; }

    void paint(SkCanvas &canvas) const override
    {
        canvas.translate(float(m_bleed), float(m_bleed));
        const SkRect bounds = m_path.getBounds();
        const QRectF box(bounds.left(), bounds.top(), bounds.width(), bounds.height());
        for (const TextShadingLayer &layer : m_style.layers) {
            if (!layer.enabled || layer.opacity <= 0.0)
                continue;
            const ShadingLayerGroup group = beginShadingLayer(canvas, layer, m_scale);
            canvas.save();
            canvas.translate(float(layer.offsetX * m_scale), float(layer.offsetY * m_scale));
            drawLayer(canvas, layer, box);
            canvas.restore();
            endShadingLayer(canvas, group);
        }
    }

private:
    void drawLayer(SkCanvas &canvas, const TextShadingLayer &layer, const QRectF &box) const
    {
        SkPaint paint;
        paint.setAntiAlias(true);
        switch (layer.kind) {
        case TextLayerKind::Fill:
            applyShadingPaint(paint, layer.paint, box, m_timeSec, 1.0);
            paint.setPathEffect(fillPathEffectFor(layer, m_scale));
            canvas.drawPath(m_path, paint);
            break;
        case TextLayerKind::Stroke: {
            const double width = layer.width * m_scale;
            if (width <= 0.0)
                break;
            applyShadingPaint(paint, layer.paint, box, m_timeSec, 1.0);
            paint.setStyle(SkPaint::kStroke_Style);
            paint.setStrokeJoin(SkPaint::kRound_Join);
            paint.setStrokeCap(SkPaint::kRound_Cap);
            paint.setPathEffect(strokePathEffectFor(layer, width, m_scale));
            if (layer.strokeAlign == StrokeAlign::Center) {
                paint.setStrokeWidth(float(width));
                canvas.drawPath(m_path, paint);
                break;
            }
            // A centred stroke of twice the width, clipped to one side of the outline, is the
            // half that grows purely outward or purely inward.
            paint.setStrokeWidth(float(width * 2.0));
            canvas.save();
            canvas.clipPath(m_path, layer.strokeAlign == StrokeAlign::Inside ? SkClipOp::kIntersect : SkClipOp::kDifference,
                            true);
            canvas.drawPath(m_path, paint);
            canvas.restore();
            break;
        }
        case TextLayerKind::Shadow:
        case TextLayerKind::Glow:
            // Silhouettes: the outline grown by the widest outward stroke, in the layer's colour.
            paint.setColor(toSkColor(layer.paint.color));
            canvas.drawPath(m_path, paint);
            if (m_silhouetteStrokePx > 0.0) {
                paint.setStyle(SkPaint::kStroke_Style);
                paint.setStrokeWidth(float(m_silhouetteStrokePx));
                paint.setStrokeJoin(SkPaint::kRound_Join);
                canvas.drawPath(m_path, paint);
            }
            break;
        case TextLayerKind::Extrude: {
            const int steps = qMax(1, layer.extrudeSteps);
            const double rad = qDegreesToRadians(layer.extrudeAngle);
            const double depth = layer.width * m_scale;
            for (int k = steps; k >= 1; --k) {
                const double f = double(k) / steps;
                paint.setColor(toSkColor(layer.paint.color.darker(100 + int(layer.extrudeDarken * 100.0 * f))));
                canvas.save();
                canvas.translate(float(std::cos(rad) * depth * f), float(std::sin(rad) * depth * f));
                canvas.drawPath(m_path, paint);
                canvas.restore();
            }
            break;
        }
        }
    }

    ShapeStyle m_style;
    double m_scale;
    double m_timeSec;
    double m_bleed;
    QSize m_size;
    quint64 m_key = 0;
    SkPath m_path;
    double m_silhouetteStrokePx = 0.0;
};

} // namespace

double shapeBleedFor(const ShapeStyle &style)
{
    const double silhouette = widestOutwardStroke(style);
    double widest = 0.0;
    for (const TextShadingLayer &layer : style.layers) {
        if (!layer.enabled)
            continue;
        // A blur's kernel runs to three sigma (sigma ≈ blur·scale), so that is the margin it needs.
        double reach = qMax(std::abs(layer.offsetX), std::abs(layer.offsetY)) + layer.blur * 3.0 + qMax(0.0, layer.spread)
                       + layer.sketchDeviation;
        switch (layer.kind) {
        case TextLayerKind::Stroke:
            reach += layer.width * strokeReachFactor(layer.strokeAlign);
            break;
        case TextLayerKind::Extrude:
            reach += layer.width;
            break;
        case TextLayerKind::Shadow:
        case TextLayerKind::Glow:
            reach += silhouette;
            break;
        case TextLayerKind::Fill:
            break;
        }
        widest = qMax(widest, reach);
    }
    return widest + 2.0;
}

ShapePainterResult makeShapePainter(const ShapePaintRequest &request)
{
    ShapePainterResult result;
    const double bleed = std::ceil(shapeBleedFor(request.style) * request.renderScale);
    const int w = qMax(1, qRound(request.layoutRect.width()));
    const int h = qMax(1, qRound(request.layoutRect.height()));
    auto painter = std::make_shared<ShapePainter>(request.style, w, h, request.renderScale, request.timeSec, bleed);
    result.rect = QRectF(request.layoutRect.x() - bleed, request.layoutRect.y() - bleed, painter->size().width(),
                         painter->size().height());
    result.painter = std::move(painter);
    return result;
}

ShapePainterResult makeShapePainter(const ShapeStyle &style, int width, int height, double renderScale, double timeSec)
{
    ShapePaintRequest request;
    request.style = style;
    request.layoutRect = QRectF(0, 0, width, height);
    request.renderScale = renderScale;
    request.timeSec = timeSec;
    return makeShapePainter(request);
}

} // namespace drift::skia
