#include "SkiaTextEffects.h"

#include "SkiaPath.h"
#include "SkiaVectorResources.h"

#include <QCache>
#include <QFileInfo>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QtMath>

#include <string>
#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkImage.h"
#include "include/core/SkM44.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkString.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkPerlinNoiseShader.h"
#include "include/effects/SkRuntimeEffect.h"

namespace drift::skia {

namespace {

SkRect toSkRect(const QRectF &r)
{
    return SkRect::MakeXYWH(float(r.x()), float(r.y()), float(r.width()), float(r.height()));
}

// Every effect shares this preamble, so a recipe only writes main().
const char kEffectPreamble[] =
    "uniform shader base;"
    "uniform float2 uOrigin; uniform float2 uSize;"
    "uniform float uTime; uniform float uProgress; uniform float uSeed;"
    "uniform float uSpeed; uniform float uIntensity; uniform float uScale;"
    "uniform float uWidth; uniform float uAngle; uniform float uAmount; uniform float uBlock;"
    "uniform float uBands; uniform float uFlicker; uniform float uEdge;"
    "uniform half4 uColorA; uniform half4 uColorB;"
    "float hash1(float n) { return fract(sin(n) * 43758.5453123); }"
    "float3 hsv2rgb(float h) { float3 k = abs(fract(h + float3(0.0, 2.0/3.0, 1.0/3.0)) * 6.0 - 3.0) - 1.0;"
    "  return clamp(k, 0.0, 1.0); }"
    "half4 premulClamp(half4 c) { c.rgb = min(c.rgb, c.aaa); return c; }";

struct EffectSource
{
    const char *id;
    const char *body;
    bool needsNoise;
};

const EffectSource kEffects[] = {
    {"shine",
     "half4 main(float2 p) {"
     "  half4 c = base.eval(p);"
     "  float2 q = (p - uOrigin) / max(uSize, float2(1.0));"
     "  float2 d = float2(cos(uAngle), sin(uAngle));"
     "  float t = dot(q - 0.5, d) + 0.5;"
     "  float phase = uSpeed > 0.0 ? fract(uTime * uSpeed) : uProgress;"
     "  float centre = phase * (1.0 + 2.0 * uWidth) - uWidth;"
     "  float band = 1.0 - smoothstep(0.0, uWidth, abs(t - centre));"
     "  return premulClamp(c + uColorA * half(band * uIntensity) * c.a);"
     "}",
     false},
    {"shimmer",
     "half4 main(float2 p) {"
     "  half4 c = base.eval(p);"
     "  float2 q = (p - uOrigin) / max(uSize, float2(1.0));"
     "  float h = fract(q.x * uScale + q.y * 0.5 + uTime * uSpeed);"
     "  half3 rgb = half3(hsv2rgb(h));"
     "  return half4(mix(c.rgb, rgb * c.a, half(uIntensity)), c.a);"
     "}",
     false},
    {"neon-pulse",
     "half4 main(float2 p) {"
     "  half4 c = base.eval(p);"
     "  float k = 0.5 + 0.5 * sin(uTime * uSpeed * 6.28318 + uSeed);"
     "  float n = hash1(floor(uTime * 24.0) + uSeed);"
     "  float drop = step(1.0 - uFlicker * 0.12, n);"
     "  half4 r = c * half(1.0 - uIntensity * 0.5 * (1.0 - k)) * half(1.0 - 0.6 * drop)"
     "          + uColorA * half(k * uIntensity * 0.3) * c.a;"
     "  return premulClamp(r);"
     "}",
     false},
    {"glitch",
     "half4 main(float2 p) {"
     "  half4 c = base.eval(p);"
     "  float row = floor((p.y - uOrigin.y) / max(uBlock, 1.0));"
     "  float n = hash1(row * 12.9898 + floor(uTime * uSpeed) * 78.233 + uSeed);"
     "  float shift = n > 0.8 ? (n - 0.9) * uAmount * 10.0 : 0.0;"
     "  half4 r = base.eval(p + float2(shift, 0.0));"
     "  half4 b = base.eval(p - float2(shift, 0.0));"
     "  return premulClamp(half4(r.r, c.g, b.b, c.a));"
     "}",
     false},
    {"chrome",
     "half4 main(float2 p) {"
     "  half4 c = base.eval(p);"
     "  float2 q = (p - uOrigin) / max(uSize, float2(1.0));"
     "  float v = q.y * uBands * 3.14159 + uTime * uSpeed * 6.28318;"
     "  half3 col = mix(uColorB.rgb, uColorA.rgb, half(smoothstep(-0.2, 0.2, sin(v))));"
     "  float hl = pow(max(0.0, sin((q.y + q.x * 0.2) * 6.28318 * uBands)), 8.0);"
     "  return half4(min(col + half(hl), half3(1.0)) * c.a, c.a);"
     "}",
     false},
    {"dissolve",
     "uniform shader noise;"
     "half4 main(float2 p) {"
     "  half4 c = base.eval(p);"
     "  float n = noise.eval(p).r;"
     "  float front = uProgress * (1.0 + 2.0 * uEdge) - uEdge;"
     "  return c * half(1.0 - smoothstep(front - uEdge, front + uEdge, n));"
     "}",
     true},
};

struct CompiledEffect
{
    sk_sp<SkRuntimeEffect> effect;
    QString error;
    bool needsNoise = false;
};

QMutex g_effectMutex;
QHash<QString, CompiledEffect> g_effects;

const CompiledEffect *compiledEffect(const QString &id)
{
    QMutexLocker lock(&g_effectMutex);
    auto it = g_effects.find(id);
    if (it != g_effects.end())
        return &it.value();
    CompiledEffect compiled;
    for (const EffectSource &src : kEffects) {
        if (id != QLatin1String(src.id))
            continue;
        const SkRuntimeEffect::Result result =
            SkRuntimeEffect::MakeForShader(SkString(std::string(kEffectPreamble) + src.body));
        compiled.effect = result.effect;
        compiled.error = QString::fromUtf8(result.errorText.c_str());
        compiled.needsNoise = src.needsNoise;
        break;
    }
    if (!compiled.effect && compiled.error.isEmpty())
        compiled.error = QStringLiteral("unknown text effect '%1'").arg(id);
    return &g_effects.insert(id, compiled).value();
}

QMutex g_textureMutex;
QCache<QString, sk_sp<SkImage>> g_textures(24);

sk_sp<SkImage> cachedTexture(const QString &path)
{
    const QFileInfo info(path);
    const QString key = path + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
    {
        QMutexLocker lock(&g_textureMutex);
        if (const auto *hit = g_textures.object(key))
            return *hit;
    }
    QImage image(path);
    if (image.isNull())
        return nullptr;
    sk_sp<SkImage> sk = imageFromQImage(image.convertToFormat(QImage::Format_ARGB32_Premultiplied));
    QMutexLocker lock(&g_textureMutex);
    g_textures.insert(key, new sk_sp<SkImage>(sk));
    return sk;
}

SkV4 colorV4(const QColor &c)
{
    return SkV4{float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
}

} // namespace

sk_sp<SkShader> gradientShaderFor(const TextGradient &g, const QRectF &box, double timeSec)
{
    if (g.stops.isEmpty() || box.isEmpty())
        return nullptr;
    std::vector<SkColor4f> colors;
    std::vector<float> positions;
    colors.reserve(g.stops.size());
    positions.reserve(g.stops.size());
    float last = 0.0f;
    for (const TextGradientStop &stop : g.stops) {
        colors.push_back(SkColor4f::FromColor(toSkColor(stop.color)));
        last = std::max(last, float(qBound(0.0, stop.pos, 1.0)));
        positions.push_back(last);
    }
    const SkTileMode tile = g.repeat ? SkTileMode::kRepeat : SkTileMode::kClamp;
    const SkGradient::Colors stops{SkSpan<const SkColor4f>(colors), SkSpan<const float>(positions), tile};
    SkGradient::Interpolation interp;
    if (g.oklab)
        interp.fColorSpace = SkGradient::Interpolation::ColorSpace::kOKLab;
    const SkGradient grad{stops, interp};

    // Unit space → the box, rotated about its centre, scaled, then slid along the axis.
    const SkRect unit = SkRect::MakeWH(1.0f, 1.0f);
    SkMatrix local = SkMatrix::RectToRect(unit, toSkRect(box));
    local.preConcat(SkMatrix::RotateDeg(float(g.angle), SkPoint::Make(0.5f, 0.5f)));
    local.preConcat(SkMatrix::Translate(0.5f, 0.5f));
    local.preConcat(SkMatrix::Scale(float(qMax(0.01, g.scale)), float(qMax(0.01, g.scale))));
    local.preConcat(SkMatrix::Translate(-0.5f, -0.5f));
    local.preConcat(SkMatrix::Translate(float(g.offset + g.offsetSpeed * timeSec), 0.0f));

    switch (g.kind) {
    case TextGradientKind::Linear: {
        const SkPoint pts[2] = {SkPoint::Make(0.0f, 0.5f), SkPoint::Make(1.0f, 0.5f)};
        return SkShaders::LinearGradient(pts, grad, &local);
    }
    case TextGradientKind::Radial:
        return SkShaders::RadialGradient(SkPoint::Make(float(g.center.x()), float(g.center.y())), 0.5f, grad, &local);
    case TextGradientKind::Sweep:
        return SkShaders::SweepGradient(SkPoint::Make(float(g.center.x()), float(g.center.y())), 0.0f, 360.0f, grad,
                                        &local);
    }
    return nullptr;
}

sk_sp<SkShader> textureShaderFor(const TextTexture &texture, const QRectF &box)
{
    if (texture.path.isEmpty() || box.isEmpty())
        return nullptr;
    sk_sp<SkImage> image = cachedTexture(texture.path);
    if (!image)
        return nullptr;
    const SkRect imageRect = SkRect::MakeWH(float(image->width()), float(image->height()));
    SkMatrix local;
    if (texture.tile) {
        local = SkMatrix::Translate(float(box.x() + texture.offset.x()), float(box.y() + texture.offset.y()));
        local.preConcat(SkMatrix::Scale(float(qMax(0.01, texture.scale)), float(qMax(0.01, texture.scale))));
    } else {
        local = SkMatrix::RectToRect(imageRect, toSkRect(box), SkMatrix::kFill_ScaleToFit);
        local.postConcat(SkMatrix::Translate(float(texture.offset.x()), float(texture.offset.y())));
        const float sc = float(qMax(0.01, texture.scale));
        local.postConcat(SkMatrix::Translate(-float(box.center().x()), -float(box.center().y())));
        local.postConcat(SkMatrix::Scale(sc, sc));
        local.postConcat(SkMatrix::Translate(float(box.center().x()), float(box.center().y())));
    }
    local.postConcat(SkMatrix::RotateDeg(float(texture.angle), SkPoint::Make(float(box.center().x()), float(box.center().y()))));
    const SkTileMode tile = texture.tile ? SkTileMode::kRepeat : SkTileMode::kClamp;
    return image->makeShader(tile, tile, SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear), &local);
}

bool textEffectIsAnimated(const TextShaderEffect &effect)
{
    const TextEffectSpec *spec = textShaderEffectSpec(effect.id);
    if (!spec || !spec->timeDriven)
        return false;
    QMap<QString, VectorSlotValue> params;
    for (const TextAnimParamSpec &p : spec->params)
        params.insert(p.id, p.defaultValue);
    for (auto it = effect.params.constBegin(); it != effect.params.constEnd(); ++it)
        params.insert(it.key(), *it);
    return scalarParam(params, QStringLiteral("speed"), 0.0) > 0.0;
}

QString textEffectCompileError(const QString &id)
{
    return compiledEffect(id)->error;
}

sk_sp<SkShader> effectShaderFor(const TextShaderEffect &effect, sk_sp<SkShader> base, const QRectF &box,
                                double timeSec, double progress)
{
    const CompiledEffect *compiled = compiledEffect(effect.id);
    if (!compiled->effect || !base)
        return nullptr;
    const TextEffectSpec *spec = textShaderEffectSpec(effect.id);
    QMap<QString, VectorSlotValue> params;
    if (spec)
        for (const TextAnimParamSpec &p : spec->params)
            params.insert(p.id, p.defaultValue);
    for (auto it = effect.params.constBegin(); it != effect.params.constEnd(); ++it)
        params.insert(it.key(), *it);
    const auto scalar = [&](const char *id, double def) { return float(scalarParam(params, QLatin1String(id), def)); };

    SkRuntimeEffectBuilder builder(compiled->effect);
    builder.child("base") = std::move(base);
    builder.uniform("uOrigin") = SkV2{float(box.x()), float(box.y())};
    builder.uniform("uSize") = SkV2{float(box.width()), float(box.height())};
    builder.uniform("uTime") = float(timeSec);
    builder.uniform("uProgress") = float(progress >= 0.0 ? progress : scalar("progress", 1.0));
    builder.uniform("uSeed") = scalar("seed", 1.0);
    builder.uniform("uSpeed") = scalar("speed", 0.0);
    builder.uniform("uIntensity") = scalar("intensity", 1.0);
    builder.uniform("uScale") = scalar("scale", 1.0);
    builder.uniform("uWidth") = scalar("width", 0.25);
    builder.uniform("uAngle") = float(qDegreesToRadians(scalar("angle", 0.0)));
    builder.uniform("uAmount") = scalar("amount", 0.0);
    builder.uniform("uBlock") = scalar("block", 8.0);
    builder.uniform("uBands") = scalar("bands", 2.0);
    builder.uniform("uFlicker") = scalar("flicker", 0.0);
    builder.uniform("uEdge") = scalar("edge", 0.1);
    builder.uniform("uColorA") = colorV4(colorParam(params, QStringLiteral("colorA"), Qt::white));
    builder.uniform("uColorB") = colorV4(colorParam(params, QStringLiteral("colorB"), Qt::black));
    if (compiled->needsNoise) {
        const float freq = scalar("scale", 0.03);
        builder.child("noise") = SkShaders::MakeFractalNoise(freq, freq, 3, scalar("seed", 1.0));
    }
    return builder.makeShader();
}

void clearTextEffectCaches()
{
    QMutexLocker lock(&g_textureMutex);
    g_textures.clear();
}

} // namespace drift::skia
