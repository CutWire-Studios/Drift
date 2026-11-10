#include "engine/render2d/skia/SkiaShading.h"

#include "engine/render2d/skia/SkiaPath.h"
#include "engine/render2d/text/SkiaTextEffects.h"

#include <QtMath>

#include <string>
#include <vector>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImageFilter.h"
#include "include/core/SkString.h"
#include "include/core/SkShader.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkDiscretePathEffect.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/effects/SkTrimPathEffect.h"

namespace drift::skia {

namespace {

constexpr const char *kBlenderPreamble = R"(
half lum(half3 c) { return dot(c, half3(0.3, 0.59, 0.11)); }
half sat(half3 c) { return max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b)); }
half3 clipColor(half3 c) {
    half l = lum(c);
    half n = min(c.r, min(c.g, c.b));
    half x = max(c.r, max(c.g, c.b));
    if (n < 0.0) c = l + (c - l) * l / max(l - n, 0.0001);
    if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 0.0001);
    return c;
}
half3 setLum(half3 c, half l) { return clipColor(c + (l - lum(c))); }
half3 setSat(half3 c, half s) {
    half mn = min(c.r, min(c.g, c.b));
    half mx = max(c.r, max(c.g, c.b));
    if (mx <= mn) return half3(0.0);
    return (c - mn) * s / (mx - mn);
}
half3 colorBurn(half3 b, half3 s) {
    half3 r = 1.0 - min(half3(1.0), (1.0 - b) / max(s, half3(0.0001)));
    r = mix(r, half3(0.0), step(s, half3(0.0)));
    return mix(r, half3(1.0), step(half3(1.0), b));
}
half3 colorDodge(half3 b, half3 s) {
    half3 r = min(half3(1.0), b / max(1.0 - s, half3(0.0001)));
    r = mix(r, half3(1.0), step(half3(1.0), s));
    return mix(r, half3(0.0), step(b, half3(0.0)));
}
)";

constexpr const char *kBlenderMain = R"(
half4 main(half4 src, half4 dst) {
    half sa = src.a;
    half da = dst.a;
    half3 s = sa > 0.0 ? src.rgb / sa : half3(0.0);
    half3 b = da > 0.0 ? dst.rgb / da : half3(0.0);
    half3 mixed = clamp(blend(b, s), 0.0, 1.0);
    half3 cr = (1.0 - da) * s + da * mixed;
    return half4(sa * cr + da * (1.0 - sa) * b, sa + da * (1.0 - sa));
}
)";

sk_sp<SkBlender> makeBlender(const char *blendBody)
{
    const std::string sksl = std::string(kBlenderPreamble) + "half3 blend(half3 b, half3 s) {\n" + blendBody
                             + "\n}\n" + kBlenderMain;
    auto result = SkRuntimeEffect::MakeForBlender(SkString(sksl));
    if (!result.effect)
        return SkBlender::Mode(SkBlendMode::kSrcOver);
    return result.effect->makeBlender(nullptr);
}

} // namespace

sk_sp<SkBlender> toSkBlender(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal:
        break;
    case BlendMode::Multiply:
        return SkBlender::Mode(SkBlendMode::kMultiply);
    case BlendMode::Screen:
        return SkBlender::Mode(SkBlendMode::kScreen);
    case BlendMode::Overlay:
        return SkBlender::Mode(SkBlendMode::kOverlay);
    case BlendMode::Add:
        return SkBlender::Mode(SkBlendMode::kPlus);
    case BlendMode::Darken:
        return SkBlender::Mode(SkBlendMode::kDarken);
    case BlendMode::Lighten:
        return SkBlender::Mode(SkBlendMode::kLighten);
    case BlendMode::ColorBurn:
        return SkBlender::Mode(SkBlendMode::kColorBurn);
    case BlendMode::ColorDodge:
        return SkBlender::Mode(SkBlendMode::kColorDodge);
    case BlendMode::SoftLight:
        return SkBlender::Mode(SkBlendMode::kSoftLight);
    case BlendMode::HardLight:
        return SkBlender::Mode(SkBlendMode::kHardLight);
    case BlendMode::Difference:
        return SkBlender::Mode(SkBlendMode::kDifference);
    case BlendMode::Exclusion:
        return SkBlender::Mode(SkBlendMode::kExclusion);
    case BlendMode::Hue:
        return SkBlender::Mode(SkBlendMode::kHue);
    case BlendMode::Saturation:
        return SkBlender::Mode(SkBlendMode::kSaturation);
    case BlendMode::Color:
        return SkBlender::Mode(SkBlendMode::kColor);
    case BlendMode::Luminosity:
        return SkBlender::Mode(SkBlendMode::kLuminosity);
    case BlendMode::LinearBurn: {
        static const sk_sp<SkBlender> blender = makeBlender("return b + s - 1.0;");
        return blender;
    }
    case BlendMode::DarkerColor: {
        static const sk_sp<SkBlender> blender = makeBlender("return lum(s) < lum(b) ? s : b;");
        return blender;
    }
    case BlendMode::LighterColor: {
        static const sk_sp<SkBlender> blender = makeBlender("return lum(s) > lum(b) ? s : b;");
        return blender;
    }
    case BlendMode::VividLight: {
        static const sk_sp<SkBlender> blender = makeBlender(
            "return mix(colorDodge(b, 2.0 * (s - 0.5)), colorBurn(b, 2.0 * s), step(s, half3(0.5)));");
        return blender;
    }
    case BlendMode::LinearLight: {
        static const sk_sp<SkBlender> blender = makeBlender("return b + 2.0 * s - 1.0;");
        return blender;
    }
    case BlendMode::PinLight: {
        static const sk_sp<SkBlender> blender = makeBlender(
            "return mix(max(b, 2.0 * s - 1.0), min(b, 2.0 * s), step(s, half3(0.5)));");
        return blender;
    }
    case BlendMode::HardMix: {
        static const sk_sp<SkBlender> blender = makeBlender("return step(half3(1.0), b + s);");
        return blender;
    }
    case BlendMode::Subtract: {
        static const sk_sp<SkBlender> blender = makeBlender("return b - s;");
        return blender;
    }
    case BlendMode::Divide: {
        static const sk_sp<SkBlender> blender = makeBlender(
            "return mix(b / max(s, half3(0.0001)), step(half3(0.0001), b), step(s, half3(0.0)));");
        return blender;
    }
    }
    return SkBlender::Mode(SkBlendMode::kSrcOver);
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
                       double progress, double frameRate)
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
        paint.setShader(effectShaderFor(tp.effect, std::move(base), box, timeSec, progress, frameRate));
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

void applyStrokeJoin(SkPaint &paint, StrokeJoin join, double miterLimit)
{
    switch (join) {
    case StrokeJoin::Miter:
        paint.setStrokeJoin(SkPaint::kMiter_Join);
        paint.setStrokeMiter(float(miterLimit));
        break;
    case StrokeJoin::Round:
        paint.setStrokeJoin(SkPaint::kRound_Join);
        break;
    case StrokeJoin::Bevel:
        paint.setStrokeJoin(SkPaint::kBevel_Join);
        break;
    }
}

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
    lp.setBlender(layer.knockout ? SkBlender::Mode(SkBlendMode::kDstOut) : toSkBlender(layer.blend));
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
