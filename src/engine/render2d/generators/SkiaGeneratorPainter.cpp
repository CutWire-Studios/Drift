#include "engine/render2d/generators/SkiaGeneratorPainter.h"

#include "core/project/sources/GeneratorKinds.h"
#include "engine/render2d/generators/ParticleSim.h"
#include "engine/render2d/skia/SkiaPath.h"
#include "engine/render2d/skia/SkiaShading.h"
#include "engine/render2d/text/SkiaTextEffects.h"
#include "engine/render2d/text/SkiaTextPainter.h"
#include "engine/render2d/vector/SkiaVectorResources.h"

#include <QCache>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <functional>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkSamplingOptions.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkRuntimeEffect.h"

namespace drift::skia {

namespace {

constexpr double kPi = 3.14159265358979323846;

double num(const ProceduralSource &s, const char *key, double fallback)
{
    const QVariant v = s.parameters.value(QString::fromLatin1(key));
    return v.isValid() ? v.toDouble() : fallback;
}

bool flag(const ProceduralSource &s, const char *key, bool fallback)
{
    const QVariant v = s.parameters.value(QString::fromLatin1(key));
    return v.isValid() ? v.toDouble() > 0.5 : fallback;
}

// Stored colours are "#rrggbb" or "#rrggbbaa", which is not the order Qt's own parser reads.
QColor color(const ProceduralSource &s, const char *key, const QColor &fallback)
{
    const QVariant v = s.parameters.value(QString::fromLatin1(key));
    if (!v.isValid())
        return fallback;
    const QString hex = v.toString();
    QColor c(hex.left(7));
    if (!c.isValid())
        return fallback;
    if (hex.size() == 9)
        c.setAlpha(hex.right(2).toInt(nullptr, 16));
    return c;
}

SkColor sk(const QColor &c, double opacity = 1.0)
{
    return SkColorSetARGB(qBound(0, qRound(c.alpha() * opacity), 255), c.red(), c.green(), c.blue());
}

class FunctionPainter final : public VectorPainter
{
public:
    FunctionPainter(QSize size, quint64 key, std::function<void(SkCanvas &)> draw)
        : m_size(size), m_key(key), m_draw(std::move(draw))
    {
    }
    QSize size() const override { return m_size; }
    quint64 cacheKey() const override { return m_key; }
    void paint(SkCanvas &canvas) const override { m_draw(canvas); }

private:
    QSize m_size;
    quint64 m_key;
    std::function<void(SkCanvas &)> m_draw;
};

// --- noise / checker: SkSL ------------------------------------------------------------------

constexpr const char *kNoiseSksl = R"(
uniform float2 uSize;
uniform float uScale;
uniform float uTime;
uniform float2 uOffset;
uniform float uContrast;
uniform float uGain;
uniform float uOctaves;
uniform float uType;
uniform float uSeed;
uniform float4 uColorA;
uniform float4 uColorB;

float hash13(float3 p3) {
    p3 = fract(p3 * 0.1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}
float3 hash33(float3 p3) {
    p3 = fract(p3 * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return fract((p3.xxy + p3.yxx) * p3.zyx);
}
float vnoise(float3 p) {
    float3 i = floor(p);
    float3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = mix(mix(hash13(i), hash13(i + float3(1, 0, 0)), f.x),
                  mix(hash13(i + float3(0, 1, 0)), hash13(i + float3(1, 1, 0)), f.x), f.y);
    float b = mix(mix(hash13(i + float3(0, 0, 1)), hash13(i + float3(1, 0, 1)), f.x),
                  mix(hash13(i + float3(0, 1, 1)), hash13(i + float3(1, 1, 1)), f.x), f.y);
    return mix(a, b, f.z);
}
float gcorner(float3 i, float3 f, float3 c) {
    float3 g = normalize(hash33(i + c) * 2.0 - 1.0 + 0.0001);
    return dot(g, f - c);
}
float pnoise(float3 p) {
    float3 i = floor(p);
    float3 f = fract(p);
    float3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = mix(mix(gcorner(i, f, float3(0, 0, 0)), gcorner(i, f, float3(1, 0, 0)), u.x),
                  mix(gcorner(i, f, float3(0, 1, 0)), gcorner(i, f, float3(1, 1, 0)), u.x), u.y);
    float b = mix(mix(gcorner(i, f, float3(0, 0, 1)), gcorner(i, f, float3(1, 0, 1)), u.x),
                  mix(gcorner(i, f, float3(0, 1, 1)), gcorner(i, f, float3(1, 1, 1)), u.x), u.y);
    return mix(a, b, u.z) * 0.9 + 0.5;
}
float cells(float3 p) {
    float2 ip = floor(p.xy);
    float2 fp = fract(p.xy);
    float d = 8.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            float2 g = float2(float(x), float(y));
            float3 h = hash33(float3(ip + g, 3.0));
            float2 o = 0.5 + 0.5 * sin(p.z * 1.7 + 6.2831 * h.xy);
            float2 r = g + o - fp;
            d = min(d, dot(r, r));
        }
    }
    return clamp(sqrt(d), 0.0, 1.0);
}
float4 main(float2 xy) {
    float3 p = float3(xy / uSize.y * uScale + uOffset + float2(uSeed * 13.37, uSeed * 7.13), uTime);
    float v = 0.0;
    if (uType < 0.5) {
        v = vnoise(p);
    } else if (uType < 1.5) {
        v = pnoise(p);
    } else if (uType < 2.5) {
        float amp = 0.5;
        float sum = 0.0;
        float norm = 0.0;
        float3 q = p;
        for (int i = 0; i < 8; i++) {
            float on = step(float(i) + 0.5, uOctaves);
            sum += on * amp * vnoise(q);
            norm += on * amp;
            amp *= uGain;
            q = q * 2.0 + float3(17.1, 9.2, 3.3);
        }
        v = sum / max(norm, 0.0001);
    } else {
        v = cells(p);
    }
    v = clamp((v - 0.5) * uContrast + 0.5, 0.0, 1.0);
    float4 c = mix(uColorA, uColorB, v);
    return float4(c.rgb * c.a, c.a);
}
)";

constexpr const char *kCheckerSksl = R"(
uniform float2 uOffset;
uniform float uSize;
uniform float4 uColorA;
uniform float4 uColorB;
float4 main(float2 xy) {
    float2 c = floor((xy - uOffset) / uSize);
    float m = mod(c.x + c.y, 2.0);
    float4 col = mix(uColorA, uColorB, m);
    return float4(col.rgb * col.a, col.a);
}
)";

sk_sp<SkRuntimeEffect> compile(const char *source)
{
    static QMutex mutex;
    static QHash<const char *, sk_sp<SkRuntimeEffect>> cache;
    QMutexLocker lock(&mutex);
    auto it = cache.find(source);
    if (it != cache.end())
        return it.value();
    sk_sp<SkRuntimeEffect> effect = SkRuntimeEffect::MakeForShader(SkString(source)).effect;
    cache.insert(source, effect);
    return effect;
}

SkV4 v4(const QColor &c)
{
    return SkV4{float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
}

// --- kinds ----------------------------------------------------------------------------------

void paintSolid(SkCanvas &canvas, const ProceduralSource &s)
{
    canvas.drawColor(sk(color(s, "color", QColor(59, 130, 246))));
}

void paintGradient(SkCanvas &canvas, QSize size, const ProceduralSource &s, double timeSec)
{
    TextGradient g;
    g.kind = TextGradientKind(std::clamp(int(num(s, "type", 0)), 0, 2));
    g.angle = num(s, "angle", 90);
    g.offset = num(s, "offset", 0);
    g.offsetSpeed = num(s, "offsetSpeed", 0);
    g.scale = num(s, "scale", 1);
    g.center = QPointF(num(s, "center.x", 0.5), num(s, "center.y", 0.5));
    g.repeat = flag(s, "repeat", false);
    g.oklab = flag(s, "oklab", false);
    const int n = std::clamp(int(num(s, "stops", 3)), 2, 5);
    static const QColor defaults[5] = {QColor(0xff, 0x51, 0x2f), QColor(0xdd, 0x24, 0x76), QColor(0x7f, 0x00, 0xff),
                                       QColor(0x00, 0xc6, 0xff), QColor(0x00, 0xff, 0x95)};
    g.stops.clear();
    double last = 0.0;
    for (int i = 0; i < n; ++i) {
        double pos = 0.0;
        if (i == n - 1)
            pos = 1.0;
        else if (i > 0)
            pos = num(s, QByteArray("pos" + QByteArray::number(i + 1)).constData(), 0.5);
        pos = std::max(pos, last);
        last = pos;
        g.stops.append({pos, color(s, QByteArray("color" + QByteArray::number(i + 1)).constData(), defaults[i])});
    }
    SkPaint paint;
    paint.setShader(gradientShaderFor(g, QRectF(0, 0, size.width(), size.height()), timeSec));
    canvas.drawRect(SkRect::MakeWH(size.width(), size.height()), paint);
}

void paintNoise(SkCanvas &canvas, QSize size, const ProceduralSource &s, double timeSec, int seed)
{
    sk_sp<SkRuntimeEffect> effect = compile(kNoiseSksl);
    if (!effect)
        return;
    SkRuntimeEffectBuilder builder(effect);
    builder.uniform("uSize") = SkV2{float(size.width()), float(size.height())};
    builder.uniform("uScale") = float(num(s, "scale", 5));
    builder.uniform("uTime") = float(timeSec * num(s, "speed", 0.25));
    builder.uniform("uOffset") = SkV2{float(num(s, "offset.x", 0)), float(num(s, "offset.y", 0))};
    builder.uniform("uContrast") = float(num(s, "contrast", 1));
    builder.uniform("uGain") = float(num(s, "gain", 0.5));
    builder.uniform("uOctaves") = float(num(s, "octaves", 4));
    builder.uniform("uType") = float(int(num(s, "type", 2)));
    builder.uniform("uSeed") = float(seed % 4096);
    builder.uniform("uColorA") = v4(color(s, "colorA", QColor(0x0b, 0x13, 0x2b)));
    builder.uniform("uColorB") = v4(color(s, "colorB", QColor(0x5b, 0xc0, 0xbe)));
    SkPaint paint;
    paint.setShader(builder.makeShader());
    canvas.drawRect(SkRect::MakeWH(size.width(), size.height()), paint);
}

void fillRectColor(SkCanvas &canvas, double x0, double y0, double x1, double y1, SkColor c)
{
    SkPaint paint;
    paint.setColor(c);
    canvas.drawRect(SkRect::MakeLTRB(x0, y0, x1, y1), paint);
}

SkColor scaled(int r, int g, int b, double k)
{
    const auto ch = [k](int v) { return qBound(0, qRound(v * k), 255); };
    return SkColorSetRGB(ch(r), ch(g), ch(b));
}

void paintBars(SkCanvas &canvas, QSize size, const ProceduralSource &s)
{
    const double w = size.width(), h = size.height();
    const double k = num(s, "brightness", 1.0);
    const int style = int(num(s, "style", 0));
    if (style == 2) {
        const int steps = 11;
        for (int i = 0; i < steps; ++i) {
            const int v = qRound(255.0 * i / (steps - 1));
            fillRectColor(canvas, w * i / steps, 0, w * (i + 1) / steps, h * 0.8, scaled(v, v, v, k));
        }
        SkPaint paint;
        const SkColor4f ends[2] = {SkColor4f::FromColor(SK_ColorBLACK), SkColor4f::FromColor(scaled(255, 255, 255, k))};
        const SkPoint pts[2] = {{0, 0}, {float(w), 0}};
        paint.setShader(SkShaders::LinearGradient(
            pts, SkGradient(SkGradient::Colors(SkSpan<const SkColor4f>(ends), SkTileMode::kClamp), {})));
        canvas.drawRect(SkRect::MakeLTRB(0, h * 0.8, w, h), paint);
        return;
    }
    static const int smpte[7][3] = {{191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0},
                                    {191, 0, 191},   {191, 0, 0},   {0, 0, 191}};
    static const int full[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                                   {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0}};
    if (style == 1) {
        for (int i = 0; i < 8; ++i)
            fillRectColor(canvas, w * i / 8, 0, w * (i + 1) / 8, h, scaled(full[i][0], full[i][1], full[i][2], k));
        return;
    }
    const double top = h * 0.667, mid = h * 0.75;
    for (int i = 0; i < 7; ++i)
        fillRectColor(canvas, w * i / 7, 0, w * (i + 1) / 7, top, scaled(smpte[i][0], smpte[i][1], smpte[i][2], k));
    static const int castle[7][3] = {{0, 0, 191}, {19, 19, 19}, {191, 0, 191}, {19, 19, 19},
                                     {0, 191, 191}, {19, 19, 19}, {191, 191, 191}};
    for (int i = 0; i < 7; ++i)
        fillRectColor(canvas, w * i / 7, top, w * (i + 1) / 7, mid, scaled(castle[i][0], castle[i][1], castle[i][2], k));
    struct Seg { double width; int r, g, b; };
    const Seg bottom[] = {{1.25, 0, 33, 76}, {1.25, 255, 255, 255}, {1.25, 50, 0, 106}, {1.25, 19, 19, 19},
                          {1.0 / 3, 8, 8, 8}, {1.0 / 3, 19, 19, 19}, {1.0 / 3, 29, 29, 29}, {1.0, 19, 19, 19}};
    double x = 0;
    for (const Seg &seg : bottom) {
        const double x1 = x + seg.width * w / 7;
        fillRectColor(canvas, x, mid, x1, h, scaled(seg.r, seg.g, seg.b, k));
        x = x1;
    }
}

void paintGrid(SkCanvas &canvas, QSize size, const ProceduralSource &s, double scale, double timeSec)
{
    const double w = size.width(), h = size.height();
    const double cell = std::max(2.0, num(s, "size", 120) * scale);
    const double lineW = std::max(0.5, num(s, "lineWidth", 2) * scale);
    const int major = std::max(1, int(num(s, "major", 4)));
    canvas.drawColor(sk(color(s, "background", QColor(0x14, 0x16, 0x1c))));
    SkPaint minor;
    minor.setColor(sk(color(s, "lineColor", QColor(0x3a, 0x3f, 0x4b))));
    minor.setStrokeWidth(float(lineW));
    SkPaint accent;
    accent.setColor(sk(color(s, "accent", QColor(0x7c, 0x8a, 0xa5))));
    accent.setStrokeWidth(float(lineW * 1.6));
    const double shift = std::fmod(num(s, "scroll", 0) * scale * timeSec, cell * major);
    const double cx = w / 2 + shift, cy = h / 2;
    const int kx = int(std::ceil(w / cell)) + major + 2;
    const int ky = int(std::ceil(h / cell)) + major + 2;
    for (int i = -kx; i <= kx; ++i) {
        const double x = cx + i * cell;
        if (x < -lineW || x > w + lineW)
            continue;
        canvas.drawLine(float(x), 0, float(x), float(h), (i % major == 0) ? accent : minor);
    }
    for (int j = -ky; j <= ky; ++j) {
        const double y = cy + j * cell;
        if (y < -lineW || y > h + lineW)
            continue;
        canvas.drawLine(0, float(y), float(w), float(y), (j % major == 0) ? accent : minor);
    }
}

void paintChecker(SkCanvas &canvas, QSize size, const ProceduralSource &s, double scale, double timeSec)
{
    sk_sp<SkRuntimeEffect> effect = compile(kCheckerSksl);
    if (!effect)
        return;
    const double cell = std::max(2.0, num(s, "size", 120) * scale);
    SkRuntimeEffectBuilder builder(effect);
    builder.uniform("uOffset") = SkV2{float(num(s, "scrollX", 0) * scale * timeSec + size.width() / 2.0),
                                      float(num(s, "scrollY", 0) * scale * timeSec + size.height() / 2.0)};
    builder.uniform("uSize") = float(cell);
    builder.uniform("uColorA") = v4(color(s, "colorA", Qt::white));
    builder.uniform("uColorB") = v4(color(s, "colorB", QColor(0x1b, 0x1b, 0x1b)));
    SkPaint paint;
    paint.setShader(builder.makeShader());
    canvas.drawRect(SkRect::MakeWH(size.width(), size.height()), paint);
}

struct CountdownFrame
{
    int number = 0;
    double fraction = 0.0; // how far through the current count
};

CountdownFrame countdownAt(const ProceduralSource &s, double timeSec)
{
    const int from = std::max(1, int(num(s, "from", 10)));
    const double interval = std::max(0.05, num(s, "interval", 1.0));
    const double ticks = std::max(0.0, timeSec) / interval;
    const int elapsed = int(std::floor(ticks));
    CountdownFrame f;
    f.number = std::max(1, from - elapsed);
    f.fraction = elapsed >= from ? 1.0 : ticks - elapsed;
    return f;
}

void paintCountdown(SkCanvas &canvas, QSize size, const ProceduralSource &s, double scale, const CountdownFrame &f,
                    const std::shared_ptr<const VectorPainter> &number, QPointF numberOrigin)
{
    const double w = size.width(), h = size.height();
    canvas.drawColor(sk(color(s, "background", QColor(0x11, 0x11, 0x11))));
    const double cx = w / 2, cy = h / 2;
    const double radius = std::min(w, h) * 0.42;
    const QColor lineColor = color(s, "line", QColor(0xd9, 0xd9, 0xd9));
    if (flag(s, "sweep", true)) {
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(sk(color(s, "sweepColor", QColor(255, 255, 255, 255)), 0.28));
        canvas.drawArc(SkRect::MakeLTRB(cx - radius, cy - radius, cx + radius, cy + radius), -90.0f,
                       float(360.0 * f.fraction), true, paint);
    }
    if (flag(s, "cross", true)) {
        SkPaint line;
        line.setAntiAlias(true);
        line.setStyle(SkPaint::kStroke_Style);
        line.setColor(sk(lineColor));
        line.setStrokeWidth(float(std::max(1.0, 3.0 * scale)));
        canvas.drawCircle(float(cx), float(cy), float(radius), line);
        canvas.drawCircle(float(cx), float(cy), float(radius * 0.88), line);
        line.setStrokeWidth(float(std::max(1.0, 2.0 * scale)));
        canvas.drawLine(0, float(cy), float(w), float(cy), line);
        canvas.drawLine(float(cx), 0, float(cx), float(h), line);
    }
    if (number) {
        canvas.save();
        canvas.translate(float(numberOrigin.x()), float(numberOrigin.y()));
        number->paint(canvas);
        canvas.restore();
    }
}

// --- particles ------------------------------------------------------------------------------

constexpr int kSpritePx = 128;

sk_sp<SkImage> shapeSprite(int kind)
{
    static QMutex mutex;
    static sk_sp<SkImage> cache[8];
    QMutexLocker lock(&mutex);
    if (cache[kind])
        return cache[kind];
    SkBitmap bitmap;
    bitmap.allocPixels(SkImageInfo::MakeA8(kSpritePx, kSpritePx));
    bitmap.eraseColor(SK_ColorTRANSPARENT);
    SkCanvas canvas(bitmap);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorWHITE);
    const float c = kSpritePx / 2.0f;
    const float r = kSpritePx * 0.46f;
    switch (kind) {
    case 0:
        canvas.drawCircle(c, c, r, paint);
        break;
    case 1: {
        const SkPoint centre{c, c};
        const SkColor4f colors[2] = {SkColor4f::FromColor(SK_ColorWHITE),
                                     SkColor4f::FromColor(SkColorSetARGB(0, 255, 255, 255))};
        paint.setShader(SkShaders::RadialGradient(
            centre, float(kSpritePx) * 0.5f,
            SkGradient(SkGradient::Colors(SkSpan<const SkColor4f>(colors), SkTileMode::kClamp), {})));
        canvas.drawCircle(c, c, kSpritePx * 0.5f, paint);
        break;
    }
    case 2: {
        paint.setStyle(SkPaint::kStroke_Style);
        paint.setStrokeWidth(kSpritePx * 0.07f);
        canvas.drawCircle(c, c, r - kSpritePx * 0.04f, paint);
        paint.setStyle(SkPaint::kFill_Style);
        paint.setColor(SkColorSetARGB(40, 255, 255, 255));
        canvas.drawCircle(c, c, r - kSpritePx * 0.07f, paint);
        paint.setColor(SkColorSetARGB(230, 255, 255, 255));
        canvas.drawCircle(c - r * 0.42f, c - r * 0.42f, kSpritePx * 0.07f, paint);
        break;
    }
    case 3:
        canvas.drawRect(SkRect::MakeLTRB(c - r * 0.8f, c - r * 0.8f, c + r * 0.8f, c + r * 0.8f), paint);
        break;
    case 4: {
        SkPathBuilder star;
        for (int i = 0; i < 10; ++i) {
            const float a = float(-kPi / 2 + i * kPi / 5);
            const float rr = (i % 2 == 0) ? r : r * 0.45f;
            const SkPoint p{c + std::cos(a) * rr, c + std::sin(a) * rr};
            i == 0 ? star.moveTo(p) : star.lineTo(p);
        }
        star.close();
        canvas.drawPath(star.detach(), paint);
        break;
    }
    case 5: {
        SkPathBuilder spark;
        spark.moveTo(kSpritePx * 0.02f, c);
        spark.lineTo(c, c - kSpritePx * 0.16f);
        spark.lineTo(kSpritePx * 0.98f, c);
        spark.lineTo(c, c + kSpritePx * 0.16f);
        spark.close();
        canvas.drawPath(spark.detach(), paint);
        break;
    }
    default:
        canvas.drawRect(SkRect::MakeLTRB(kSpritePx * 0.06f, kSpritePx * 0.2f, kSpritePx * 0.94f, kSpritePx * 0.8f), paint);
        break;
    }
    cache[kind] = SkImages::RasterFromPixmapCopy(bitmap.pixmap());
    return cache[kind];
}

sk_sp<SkImage> fileSprite(const QString &path)
{
    static QMutex mutex;
    static QCache<QString, sk_sp<SkImage>> cache(16);
    const QFileInfo info(path);
    const QString key = path + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
    {
        QMutexLocker lock(&mutex);
        if (const auto *hit = cache.object(key))
            return *hit;
    }
    QImage image(path);
    sk_sp<SkImage> out = image.isNull() ? nullptr : imageFromQImage(image);
    QMutexLocker lock(&mutex);
    cache.insert(key, new sk_sp<SkImage>(out));
    return out;
}

QColor lerpColor(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

void paintParticles(SkCanvas &canvas, QSize size, const particles::Params &p, const particles::Frame &frame,
                    const QString &spriteImagePath)
{
    const double k = size.height() / particles::kUnitsHigh;
    sk_sp<SkImage> image;
    bool tint = true;
    if (p.sprite == kGeneratorSpriteImage) {
        image = fileSprite(spriteImagePath);
        tint = false;
    }
    const bool confetti = p.sprite == 6;
    if (!image) {
        tint = true;
        image = shapeSprite(std::clamp(p.sprite == kGeneratorSpriteImage ? 0 : p.sprite, 0, 6));
    }
    if (!image)
        return;
    const SkSamplingOptions sampling(SkFilterMode::kLinear);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setBlendMode(p.additive ? SkBlendMode::kPlus : SkBlendMode::kSrcOver);
    for (const particles::Particle &q : frame.particles) {
        const double life = std::clamp(double(q.age) / double(q.life), 0.0, 1.0);
        const double opacity = std::clamp(p.opacity + (p.opacityEnd - p.opacity) * life, 0.0, 1.0);
        if (opacity <= 0.002)
            continue;
        QColor c;
        if (p.rainbow)
            c = QColor::fromHsvF(q.hue, 0.85, 1.0);
        else
            c = life < 0.5 ? lerpColor(p.colorStart, p.colorMid, life * 2.0) : lerpColor(p.colorMid, p.colorEnd, (life - 0.5) * 2.0);
        paint.setColor(tint ? sk(c, opacity) : SkColorSetARGB(qRound(opacity * 255), 255, 255, 255));
        if (!tint && p.additive)
            paint.setColor(SkColorSetARGB(qRound(opacity * 255), 255, 255, 255));

        const double sz = p.size * q.sizeK * (1.0 + (p.sizeEnd - 1.0) * life) * k;
        if (sz < 0.3)
            continue;
        double sx = sz, sy = sz;
        double rotation = q.rot;
        const double speed = std::hypot(double(q.vx), double(q.vy));
        if (p.alignVelocity) {
            rotation = std::atan2(double(q.vy), double(q.vx)) * 180.0 / kPi;
            sx = sz * (1.0 + p.stretch * speed / 500.0);
        }
        if (confetti)
            sy = sz * std::abs(std::cos(double(q.age) * 5.0 + q.hue * 6.2831853));
        canvas.save();
        canvas.translate(float(q.x * k), float(q.y * k));
        canvas.rotate(float(rotation));
        canvas.drawImageRect(image.get(), SkRect::MakeLTRB(float(-sx / 2), float(-sy / 2), float(sx / 2), float(sy / 2)),
                             sampling, &paint);
        canvas.restore();
    }
}

bool animatedParams(const ProceduralSource &source, const QString &kind, const ProceduralSource &s)
{
    if (source.isAnimated())
        return true;
    if (kind == QLatin1String("gen.noise"))
        return num(s, "speed", 0.25) != 0.0;
    if (kind == QLatin1String("gen.grid"))
        return num(s, "scroll", 0) != 0.0;
    if (kind == QLatin1String("gen.checker"))
        return num(s, "scrollX", 0) != 0.0 || num(s, "scrollY", 0) != 0.0;
    if (kind == QLatin1String("gen.gradient"))
        return num(s, "offsetSpeed", 0) != 0.0;
    return kind == QLatin1String("gen.countdown") || kind == QLatin1String("gen.particles");
}

quint64 hashRequest(const GeneratorPaintRequest &r, QSize size)
{
    const QByteArray bytes = QJsonDocument(r.resolved.toJson()).toJson(QJsonDocument::Compact)
                             + QByteArray::number(size.width()) + 'x' + QByteArray::number(size.height())
                             + QByteArray::number(r.renderScale, 'g', 8);
    return quint64(qHash(bytes, 0x6e65)) | 1u;
}

} // namespace

bool generatorIsTimeDriven(const GeneratorPaintRequest &request)
{
    return animatedParams(request.source, request.resolved.packageId, request.resolved);
}

GeneratorPainterResult makeGeneratorPainter(const GeneratorPaintRequest &r)
{
    const QSize size(std::max(1, qRound(r.layoutRect.width())), std::max(1, qRound(r.layoutRect.height())));
    const QString kind = r.resolved.packageId;
    const double timeSec = usToSeconds(std::max<TimeUs>(0, r.clipTimeUs));
    const bool driven = generatorIsTimeDriven(r);
    const quint64 key = driven ? 0 : hashRequest(r, size);
    const ProceduralSource s = r.resolved;
    const double scale = r.renderScale;
    const int seed = r.source.seed;

    std::function<void(SkCanvas &)> draw;
    if (kind == QLatin1String("gen.solid")) {
        draw = [s](SkCanvas &c) { paintSolid(c, s); };
    } else if (kind == QLatin1String("gen.gradient")) {
        draw = [s, size, timeSec](SkCanvas &c) { paintGradient(c, size, s, timeSec); };
    } else if (kind == QLatin1String("gen.noise")) {
        draw = [s, size, timeSec, seed](SkCanvas &c) { paintNoise(c, size, s, timeSec, seed); };
    } else if (kind == QLatin1String("gen.bars")) {
        draw = [s, size](SkCanvas &c) { paintBars(c, size, s); };
    } else if (kind == QLatin1String("gen.grid")) {
        draw = [s, size, scale, timeSec](SkCanvas &c) { paintGrid(c, size, s, scale, timeSec); };
    } else if (kind == QLatin1String("gen.checker")) {
        draw = [s, size, scale, timeSec](SkCanvas &c) { paintChecker(c, size, s, scale, timeSec); };
    } else if (kind == QLatin1String("gen.countdown")) {
        const CountdownFrame f = countdownAt(s, timeSec);
        const double interval = std::max(0.05, num(s, "interval", 1.0));
        std::shared_ptr<const VectorPainter> number;
        QPointF origin;
        {
            Clip tmp;
            tmp.type = ClipType::Text;
            tmp.textStyle = r.textStyle;
            tmp.timelineDuration = secondsToUs(interval);
            const TextPainterResult painted = makeTextPainter(tmp, QString::number(f.number), QRectF(0, 0, size.width(), size.height()),
                                                              scale, -1, secondsToUs(f.fraction * interval));
            number = painted.painter;
            origin = painted.rect.topLeft();
        }
        draw = [s, size, scale, f, number, origin](SkCanvas &c) { paintCountdown(c, size, s, scale, f, number, origin); };
    } else if (kind == QLatin1String("gen.particles")) {
        const particles::Params p = particles::Params::read(s, r.clipTimeUs);
        const double aspect = r.layoutRect.width() / std::max(1.0, r.layoutRect.height());
        auto frame = std::make_shared<particles::Frame>(particles::simulate(r.clipId, r.source, aspect, r.clipTimeUs));
        const QString sprite = r.spriteImagePath;
        draw = [p, frame, size, sprite](SkCanvas &c) { paintParticles(c, size, p, *frame, sprite); };
    } else {
        return {};
    }
    GeneratorPainterResult out;
    out.painter = std::make_shared<FunctionPainter>(size, key, std::move(draw));
    out.rect = r.layoutRect;
    return out;
}

} // namespace drift::skia
