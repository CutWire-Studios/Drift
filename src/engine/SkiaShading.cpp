#include "SkiaShading.h"

#include "SkiaPath.h"
#include "SkiaTextEffects.h"

#include <QtMath>

#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImageFilter.h"
#include "include/core/SkShader.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkDiscretePathEffect.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkTrimPathEffect.h"

namespace drift::skia {

SkBlendMode toSkBlendMode(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal:
        return SkBlendMode::kSrcOver;
    case BlendMode::Multiply:
        return SkBlendMode::kMultiply;
    case BlendMode::Screen:
        return SkBlendMode::kScreen;
    case BlendMode::Overlay:
        return SkBlendMode::kOverlay;
    case BlendMode::Add:
        return SkBlendMode::kPlus;
    case BlendMode::Darken:
        return SkBlendMode::kDarken;
    case BlendMode::Lighten:
        return SkBlendMode::kLighten;
    }
    return SkBlendMode::kSrcOver;
}

namespace {

QColor withAlpha(const QColor &c, double factor)
{
    QColor out = c;
    out.setAlphaF(qBound(0.0, c.alphaF() * factor, 1.0));
    return out;
}

} // namespace

void applyShadingPaint(SkPaint &paint, const TextPaint &tp, const QRectF &box, double timeSec, double alpha,
                       double progress)
{
    paint.setShader(nullptr);
    paint.setColorFilter(nullptr);
    switch (tp.kind) {
    case TextPaintKind::Solid:
        paint.setColor(toSkColor(withAlpha(tp.color, alpha)));
        break;
    case TextPaintKind::Gradient:
        paint.setColor(SkColorSetARGB(qRound(alpha * 255.0), 0, 0, 0));
        paint.setShader(gradientShaderFor(tp.gradient, box, timeSec));
        break;
    case TextPaintKind::Texture:
        paint.setColor(SkColorSetARGB(qRound(alpha * 255.0), 0, 0, 0));
        paint.setShader(textureShaderFor(tp.texture, box));
        if (!paint.getShader())
            paint.setColor(toSkColor(withAlpha(tp.color, alpha)));
        break;
    case TextPaintKind::Effect: {
        paint.setColor(SkColorSetARGB(qRound(alpha * 255.0), 0, 0, 0));
        sk_sp<SkShader> base = SkShaders::Color(toSkColor(tp.color));
        paint.setShader(effectShaderFor(tp.effect, std::move(base), box, timeSec, progress));
        if (!paint.getShader())
            paint.setColor(toSkColor(withAlpha(tp.color, alpha)));
        break;
    }
    }
}

namespace {

sk_sp<SkPathEffect> sketchEffect(const TextShadingLayer &layer, double scale)
{
    if (layer.sketchLength <= 0.0 || layer.sketchDeviation <= 0.0)
        return nullptr;
    return SkDiscretePathEffect::Make(float(layer.sketchLength * scale), float(layer.sketchDeviation * scale),
                                      uint32_t(qMax(0, layer.sketchSeed)));
}

} // namespace

sk_sp<SkPathEffect> strokePathEffectFor(const TextShadingLayer &layer, double strokeWidthPx, double scale)
{
    sk_sp<SkPathEffect> effect;
    if (!qFuzzyIsNull(layer.trimStart) || !qFuzzyCompare(layer.trimEnd, 1.0))
        effect = SkTrimPathEffect::Make(float(layer.trimStart), float(layer.trimEnd));
    const QList<double> pattern = strokeDashIntervals(layer.dash);
    if (!pattern.isEmpty() && strokeWidthPx > 0.0) {
        std::vector<SkScalar> intervals;
        intervals.reserve(pattern.size());
        for (double run : pattern)
            intervals.push_back(float(qMax(0.01, run) * strokeWidthPx));
        sk_sp<SkPathEffect> dash = SkDashPathEffect::Make(SkSpan<const SkScalar>(intervals),
                                                          float(layer.dashOffset * strokeWidthPx));
        effect = effect ? SkPathEffect::MakeCompose(std::move(dash), std::move(effect)) : std::move(dash);
    }
    if (sk_sp<SkPathEffect> sketch = sketchEffect(layer, scale))
        effect = effect ? SkPathEffect::MakeCompose(std::move(sketch), std::move(effect)) : std::move(sketch);
    return effect;
}

sk_sp<SkPathEffect> fillPathEffectFor(const TextShadingLayer &layer, double scale)
{
    return sketchEffect(layer, scale);
}

ShadingLayerGroup beginShadingLayer(SkCanvas &canvas, const TextShadingLayer &layer, double scale)
{
    const bool blurred = layer.blur > 0.0 && layer.kind != TextLayerKind::Stroke && layer.kind != TextLayerKind::Extrude;
    const bool needsLayer = blurred || layer.opacity < 1.0 || layer.blend != BlendMode::Normal || layer.spread > 0.0
                            || layer.knockout;
    if (!needsLayer)
        return {};
    SkPaint lp;
    lp.setAlphaf(float(layer.opacity));
    lp.setBlendMode(layer.knockout ? SkBlendMode::kDstOut : toSkBlendMode(layer.blend));
    sk_sp<SkImageFilter> filter;
    if (layer.spread > 0.0) {
        const float r = float(layer.spread * scale);
        filter = SkImageFilters::Dilate(r, r, nullptr);
    }
    if (blurred) {
        // The legacy raster ran a three-pass box blur of radius round(blur·scale); three boxes of
        // radius r have the variance of a gaussian with sigma = r + 0.5.
        const float sigma = float(qRound(layer.blur * scale)) + 0.5f;
        filter = SkImageFilters::Blur(sigma, sigma, std::move(filter));
    }
    lp.setImageFilter(std::move(filter));
    canvas.saveLayer(nullptr, &lp);
    return {true};
}

void endShadingLayer(SkCanvas &canvas, const ShadingLayerGroup &group)
{
    if (group.pushed)
        canvas.restore();
}

} // namespace drift::skia
