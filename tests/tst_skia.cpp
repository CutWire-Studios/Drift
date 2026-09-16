#include <QtTest>

#include <functional>

#include <QOpenGLExtraFunctions>

#include "core/Project.h"
#include "engine/FrameCompositor.h"
#include "engine/GlRuntime.h"
#include "engine/GpuCompositor.h"
#include "engine/GpuEffectExecutor.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/SkiaRuntime.h"
#include "engine/SkiaShapePainter.h"
#include "engine/SkiaTextPainter.h"
#include "engine/SkiaTextEffects.h"
#include "engine/TextLayout.h"
#include "core/TextAnimationPreset.h"
#include "core/TextLook.h"
#include "engine/VectorPainter.h"

#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkContourMeasure.h"
#include "include/core/SkM44.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkRect.h"
#include "include/core/SkShader.h"
#include "include/core/SkString.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkRuntimeEffect.h"
#include "include/effects/SkShaderMaskFilter.h"
#include "include/effects/SkTrimPathEffect.h"

using namespace drift;

Q_DECLARE_METATYPE(drift::TextStyle)

// fonts/ is addon content (gitignored; tests.yml stages it from Drift-Addons). A packaging
// build such as the Arch PKGBUILD's check() has none, and then "Inter" resolves to whatever Qt
// falls back to — placeholder boxes in a font-less container — which is not what these tests
// measure. Same guard as tst_engine.
#define SKIP_WITHOUT_FONTS()                                                                        \
    do {                                                                                            \
        if (fontCatalog().isEmpty())                                                                \
            QSKIP("font bundle not present — see recipes/fetch-fonts.py in drift-addons");          \
    } while (false)

namespace {

// An opaque red square at (10,10)-(30,30) and a half-transparent red one at (40,10)-(60,30).
// Straight-alpha readback must give (255,0,0,128) for the second, not the premultiplied
// (128,0,0,128) Skia produces.
class TwoSquares : public skia::VectorPainter
{
public:
    explicit TwoSquares(quint64 key = 0) : m_key(key) {}
    QSize size() const override { return QSize(80, 40); }
    quint64 cacheKey() const override { return m_key; }
    void paint(SkCanvas &canvas) const override
    {
        SkPaint opaque;
        opaque.setColor(SK_ColorRED);
        canvas.drawRect(SkRect::MakeXYWH(10, 10, 20, 20), opaque);
        SkPaint half;
        half.setColor(SkColorSetARGB(128, 255, 0, 0));
        canvas.drawRect(SkRect::MakeXYWH(40, 10, 20, 20), half);
    }

private:
    quint64 m_key;
};


// Every Skia primitive the text rework leans on, drawn side by side on one 320x80 canvas so the
// CPU and Ganesh paths can be checked with the same pixel probes:
//   A (10..70)   SkSL runtime shader over a child shader
//   B (90..150)  shader mask filter: a horizontal alpha ramp
//   C (170..230) stroked line trimmed to its first half
//   D (260..300) outer-blur mask filter (hollow inside, halo outside)
//   E (10..70, rows 2..12) OKLab-interpolated gradient
//   F (90..150, rows 62..78) saveLayer whose paint carries a mask filter (probe only)
class PrimitiveProbe : public skia::VectorPainter
{
public:
    QSize size() const override { return QSize(320, 80); }
    quint64 cacheKey() const override { return 0; }
    void paint(SkCanvas &canvas) const override
    {
        static const char kSksl[] =
            "uniform half4 uColor; uniform shader base;"
            "half4 main(float2 p) { return base.eval(p) * uColor; }";
        const SkRuntimeEffect::Result effect = SkRuntimeEffect::MakeForShader(SkString(kSksl));
        if (effect.effect) {
            SkRuntimeEffectBuilder builder(effect.effect);
            builder.uniform("uColor") = SkV4{0.0f, 1.0f, 0.0f, 1.0f};
            builder.child("base") = SkShaders::Color(SK_ColorWHITE);
            SkPaint p;
            p.setShader(builder.makeShader());
            canvas.drawRect(SkRect::MakeXYWH(10, 20, 60, 40), p);
        }

        const SkPoint rampPts[2] = {{90, 0}, {150, 0}};
        const SkColor4f rampColors[2] = {SkColors::kWhite, SkColors::kTransparent};
        const float rampPos[2] = {0.0f, 1.0f};
        const SkGradient::Colors rampStops{SkSpan<const SkColor4f>(rampColors, 2),
                                           SkSpan<const float>(rampPos, 2), SkTileMode::kClamp};
        const SkGradient ramp{rampStops, SkGradient::Interpolation{}};
        {
            SkPaint p;
            p.setColor(SK_ColorRED);
            p.setMaskFilter(SkShaderMaskFilter::Make(SkShaders::LinearGradient(rampPts, ramp)));
            canvas.drawRect(SkRect::MakeXYWH(90, 20, 60, 40), p);
        }
        {
            SkPathBuilder pb;
            pb.moveTo(170, 40);
            pb.lineTo(230, 40);
            SkPaint p;
            p.setColor(SK_ColorBLUE);
            p.setStyle(SkPaint::kStroke_Style);
            p.setStrokeWidth(8);
            p.setPathEffect(SkTrimPathEffect::Make(0.0f, 0.5f));
            canvas.drawPath(pb.detach(), p);
        }
        {
            SkPaint p;
            p.setColor(SK_ColorWHITE);
            p.setMaskFilter(SkMaskFilter::MakeBlur(kOuter_SkBlurStyle, 4.0f));
            canvas.drawRect(SkRect::MakeXYWH(260, 25, 40, 30), p);
        }
        {
            const SkPoint pts[2] = {{10, 0}, {70, 0}};
            const SkColor4f colors[2] = {SkColors::kRed, SkColors::kBlue};
            const SkGradient::Colors stops{SkSpan<const SkColor4f>(colors, 2), SkSpan<const float>(rampPos, 2),
                                           SkTileMode::kClamp};
            SkGradient::Interpolation interp;
            interp.fColorSpace = SkGradient::Interpolation::ColorSpace::kOKLab;
            const SkGradient grad{stops, interp};
            SkPaint p;
            p.setShader(SkShaders::LinearGradient(pts, grad));
            canvas.drawRect(SkRect::MakeXYWH(10, 2, 60, 10), p);
        }
        {
            SkPaint layer;
            layer.setMaskFilter(SkShaderMaskFilter::Make(SkShaders::LinearGradient(rampPts, ramp)));
            const SkRect bounds = SkRect::MakeXYWH(90, 62, 60, 16);
            canvas.saveLayer(&bounds, &layer);
            SkPaint p;
            p.setColor(SK_ColorWHITE);
            canvas.drawRect(bounds, p);
            canvas.restore();
        }
    }
};

GpuScene sceneWith(std::shared_ptr<const skia::VectorPainter> painter)
{
    GpuScene scene;
    scene.canvasSize = QSize(96, 64);
    scene.backgroundColor = Qt::black;
    GpuItem item;
    item.layer.vector = std::move(painter);
    item.layer.rect = QRectF(8, 8, 80, 40);
    item.layer.valid = true;
    scene.items.push_back(item);
    return scene;
}

// Straight glReadPixels: GlRuntime::readTarget goes through QOpenGLFramebufferObject::toImage,
// which assumes premultiplied pixels and would silently "fix" the alpha under test.
QImage readStraight(QOpenGLExtraFunctions *gl, const gl::GlTarget &target)
{
    QImage out(target.width, target.height, QImage::Format_RGBA8888);
    target.fbo->bind();
    gl->glFinish();
    gl->glReadPixels(0, 0, target.width, target.height, GL_RGBA, GL_UNSIGNED_BYTE, out.bits());
    target.fbo->release();
    return out;
}

bool near(QRgb a, QRgb b, int tol = 2)
{
    return qAbs(qRed(a) - qRed(b)) <= tol && qAbs(qGreen(a) - qGreen(b)) <= tol
        && qAbs(qBlue(a) - qBlue(b)) <= tol && qAbs(qAlpha(a) - qAlpha(b)) <= tol;
}

// Coverage summary of a straight-alpha image: total alpha mass, where that mass sits and its
// mean colour. Coarse enough to survive two antialiasers (Skia's raster AA quantizes edge
// coverage where QPainter's is exact-area), fine enough to catch a missing stroke, a flipped
// gradient or a dash pattern in the wrong units.
struct Coverage
{
    double mass = 0; // sum of alpha / 255
    QPointF centroid;
    double r = 0, g = 0, b = 0;
};

Coverage coverageOf(const QImage &image)
{
    Coverage c;
    double sx = 0, sy = 0;
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const double a = qAlpha(row[x]) / 255.0;
            if (a <= 0.0)
                continue;
            c.mass += a;
            sx += x * a;
            sy += y * a;
            c.r += qRed(row[x]) * a;
            c.g += qGreen(row[x]) * a;
            c.b += qBlue(row[x]) * a;
        }
    }
    if (c.mass > 0) {
        c.centroid = QPointF(sx / c.mass, sy / c.mass);
        c.r /= c.mass;
        c.g /= c.mass;
        c.b /= c.mass;
    }
    return c;
}

const QList<ShapeKind> &allShapeKinds()
{
    static const QList<ShapeKind> kinds = {
        ShapeKind::Rectangle,    ShapeKind::RoundedRectangle, ShapeKind::Square,
        ShapeKind::Ellipse,      ShapeKind::Triangle,         ShapeKind::RightTriangle,
        ShapeKind::Diamond,      ShapeKind::Pentagon,         ShapeKind::Hexagon,
        ShapeKind::Octagon,      ShapeKind::Parallelogram,    ShapeKind::Trapezoid,
        ShapeKind::Arrow,        ShapeKind::DoubleArrow,      ShapeKind::BlockArrow,
        ShapeKind::CurvedArrow,  ShapeKind::Chevron,          ShapeKind::SpeechBubble,
        ShapeKind::SpeechBubbleRect, ShapeKind::ThoughtBubble, ShapeKind::Callout,
        ShapeKind::Star,         ShapeKind::LightningBolt,    ShapeKind::Cloud,
        ShapeKind::Heart,        ShapeKind::Cross,            ShapeKind::Burst,
        ShapeKind::Banner,
    };
    return kinds;
}

Project shapeProject(const ShapeStyle &style)
{
    Project project;
    project.setResolution(160, 120);
    project.tracks().clear();
    project.tracks().append(Track{.type = TrackType::Shape});
    Clip clip;
    clip.id = QStringLiteral("shape");
    clip.type = ClipType::Shape;
    clip.timelineStart = 0;
    clip.timelineDuration = secondsToUs(1.0);
    clip.shapeStyle = style;
    clip.transformX.setKeyframe(0, 20.0);
    clip.transformY.setKeyframe(0, 10.0);
    clip.transformW.setKeyframe(0, 120.0);
    clip.transformH.setKeyframe(0, 100.0);
    project.tracks()[0].clips.append(clip);
    return project;
}

} // namespace

class SkiaTest : public QObject
{
    Q_OBJECT

private slots:
    void grContextAttaches();
    void paintToTargetRoundTrip();
    void restoresGlState();
    void rasterizeMatchesGpu();
    void cacheServesStaticPainters();
    void compositorRendersVectorLayer();
    void sceneUnchangedAfterSkiaUse();
    void allShapeKindsRasterize_data();
    void allShapeKindsRasterize();
    void shapePainterCacheKey();
    void shapeStrokeAlignAndBleed();
    void shapeCornerRadiusRoundsAnyKind();
    void shapeTrimAndDashRemoveMass();
    void shapeShadowOffsetsSilhouette();
    void keyframedShapeStrokeGrowsOverTime();
    void compositorDrawsShapeThroughSkia();
    void textPainterCacheKeys();
    void keyframedTextGrowsOverTime();
    void gradientFillSweepsTheBlock();
    void pathBendArchesTheLine();
    void emojiOutlineDrawsARing();
    void textPrimitivesRender();
    void fragmentAnimationRendersPerCharacter();
    void bleedIsTimeInvariant();
    void shadingLayersCompositeInOrder();
    void gradientOffsetShiftsColour();
    void wipeMaskRevealsBottomUp();
    void caretDrawsAfterLastVisibleFragment();
    void skslEffectsCompileAndRender();
    void textLookRenders();
    void textPacksRender();
};

void SkiaTest::grContextAttaches()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("GL unavailable");
    bool ok = false;
    gl::runtime().exec([&] { ok = skia::SkiaRuntime::acquire(gl::runtime()) != nullptr; });
    QVERIFY(ok);
    QVERIFY(gl::runtime().skia);
}

void SkiaTest::paintToTargetRoundTrip()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("GL unavailable");
    QImage out;
    gl::runtime().exec([&] {
        gl::GlRuntime &rt = gl::runtime();
        auto *sk = skia::SkiaRuntime::acquire(rt);
        QVERIFY(sk);
        gl::GlTarget target = sk->paintToTarget(rt, rt.functions(), TwoSquares());
        QVERIFY(target.isValid());
        out = readStraight(rt.functions(), target);
        rt.releaseTarget(std::move(target));
    });
    QCOMPARE(out.size(), QSize(80, 40));
    // Row 0 is the top: the squares sit in rows 10..30, nowhere near the bottom edge.
    QVERIFY2(near(out.pixel(12, 12), qRgba(255, 0, 0, 255)), qPrintable(QString::number(out.pixel(12, 12), 16)));
    QVERIFY2(near(out.pixel(2, 2), qRgba(0, 0, 0, 0)), qPrintable(QString::number(out.pixel(2, 2), 16)));
    QVERIFY2(near(out.pixel(12, 35), qRgba(0, 0, 0, 0)), qPrintable(QString::number(out.pixel(12, 35), 16)));
    QVERIFY2(near(out.pixel(50, 20), qRgba(255, 0, 0, 128), 3), qPrintable(QString::number(out.pixel(50, 20), 16)));
}

void SkiaTest::restoresGlState()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("GL unavailable");
    gl::runtime().exec([&] {
        gl::GlRuntime &rt = gl::runtime();
        QOpenGLExtraFunctions *gl = rt.functions();
        auto *sk = skia::SkiaRuntime::acquire(rt);
        QVERIFY(sk);
        gl::GlTarget target = sk->paintToTarget(rt, gl, TwoSquares());
        QVERIFY(target.isValid());
        rt.releaseTarget(std::move(target));

        QCOMPARE(gl->glIsEnabled(GL_SCISSOR_TEST), GLboolean(GL_FALSE));
        QCOMPARE(gl->glIsEnabled(GL_STENCIL_TEST), GLboolean(GL_FALSE));
        QCOMPARE(gl->glIsEnabled(GL_DEPTH_TEST), GLboolean(GL_FALSE));
        QCOMPARE(gl->glIsEnabled(GL_BLEND), GLboolean(GL_FALSE));
        GLboolean mask[4] = {};
        gl->glGetBooleanv(GL_COLOR_WRITEMASK, mask);
        QVERIFY(mask[0] && mask[1] && mask[2] && mask[3]);
        GLint v = -1;
        gl->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &v);
        QCOMPARE(v, 0);
        gl->glGetIntegerv(GL_ACTIVE_TEXTURE, &v);
        QCOMPARE(v, GLint(GL_TEXTURE0));
        gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &v);
        QCOMPARE(v, 4);
        gl->glGetIntegerv(GL_UNPACK_ROW_LENGTH, &v);
        QCOMPARE(v, 0);
        gl->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &v);
        QCOMPARE(v, 0);
        gl->glGetIntegerv(GL_CURRENT_PROGRAM, &v);
        QCOMPARE(v, 0);
    });
}

void SkiaTest::rasterizeMatchesGpu()
{
    const QImage cpu = skia::SkiaRuntime::rasterize(TwoSquares()).convertToFormat(QImage::Format_RGBA8888);
    QCOMPARE(cpu.size(), QSize(80, 40));
    QVERIFY(near(cpu.pixel(12, 12), qRgba(255, 0, 0, 255)));
    QVERIFY(near(cpu.pixel(2, 2), qRgba(0, 0, 0, 0)));
    QVERIFY(near(cpu.pixel(50, 20), qRgba(255, 0, 0, 128), 3));

    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("GL unavailable");
    QImage gpu;
    gl::runtime().exec([&] {
        gl::GlRuntime &rt = gl::runtime();
        auto *sk = skia::SkiaRuntime::acquire(rt);
        QVERIFY(sk);
        gl::GlTarget target = sk->paintToTarget(rt, rt.functions(), TwoSquares());
        QVERIFY(target.isValid());
        gpu = readStraight(rt.functions(), target);
        rt.releaseTarget(std::move(target));
    });
    for (int y = 0; y < 40; y += 4)
        for (int x = 0; x < 80; x += 4)
            QVERIFY2(near(cpu.pixel(x, y), gpu.pixel(x, y), 3),
                     qPrintable(QStringLiteral("(%1,%2) cpu %3 gpu %4").arg(x).arg(y)
                                    .arg(cpu.pixel(x, y), 8, 16).arg(gpu.pixel(x, y), 8, 16)));
}

void SkiaTest::cacheServesStaticPainters()
{
    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("GL unavailable");
    gl::runtime().exec([&] {
        gl::GlRuntime &rt = gl::runtime();
        auto *sk = skia::SkiaRuntime::acquire(rt);
        QVERIFY(sk);
        const auto before = sk->stats();

        const TwoSquares cached(0x5ca1ab1e);
        gl::GlTarget a = sk->paintToTarget(rt, rt.functions(), cached);
        gl::GlTarget b = sk->paintToTarget(rt, rt.functions(), cached);
        QVERIFY(a.isValid() && b.isValid());
        // Both are the caller's: distinct targets, identical pixels.
        QVERIFY(a.texture() != b.texture());
        QCOMPARE(readStraight(rt.functions(), a), readStraight(rt.functions(), b));
        rt.releaseTarget(std::move(a));
        rt.releaseTarget(std::move(b));

        auto after = sk->stats();
        QCOMPARE(after.paints - before.paints, quint64(1));
        QCOMPARE(after.cacheMisses - before.cacheMisses, quint64(1));
        QCOMPARE(after.cacheHits - before.cacheHits, quint64(1));

        // Key 0 never touches the cache.
        const TwoSquares live;
        rt.releaseTarget(sk->paintToTarget(rt, rt.functions(), live));
        rt.releaseTarget(sk->paintToTarget(rt, rt.functions(), live));
        const auto end = sk->stats();
        QCOMPARE(end.paints - after.paints, quint64(2));
        QCOMPARE(end.cacheHits, after.cacheHits);
        QCOMPARE(end.cacheMisses, after.cacheMisses);
    });
}

void SkiaTest::compositorRendersVectorLayer()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");
    const QImage out = GpuCompositor::render(sceneWith(std::make_shared<TwoSquares>(0x1))).convertToFormat(QImage::Format_RGB32);
    QCOMPARE(out.size(), QSize(96, 64));
    // Layer placed at (8,8): the opaque square lands at (18..38, 18..38), the half-alpha one at
    // (48..68, 18..38) and composites to half red over black.
    QVERIFY2(near(out.pixel(20, 20) | 0xff000000u, qRgb(255, 0, 0), 3), qPrintable(QString::number(out.pixel(20, 20), 16)));
    QVERIFY2(near(out.pixel(58, 28) | 0xff000000u, qRgb(128, 0, 0), 4), qPrintable(QString::number(out.pixel(58, 28), 16)));
    QVERIFY2(near(out.pixel(4, 4) | 0xff000000u, qRgb(0, 0, 0), 2), qPrintable(QString::number(out.pixel(4, 4), 16)));
    QVERIFY2(near(out.pixel(20, 55) | 0xff000000u, qRgb(0, 0, 0), 2), qPrintable(QString::number(out.pixel(20, 55), 16)));
}

// A plain image scene must render identically before and after Skia has drawn: any GL state
// Skia leaves behind (scissor, stencil, blend, bound VAO) would show up here.
void SkiaTest::sceneUnchangedAfterSkiaUse()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");
    QImage still(40, 30, QImage::Format_RGBA8888);
    still.fill(qRgba(30, 200, 90, 255));
    for (int y = 10; y < 20; ++y)
        for (int x = 10; x < 30; ++x)
            still.setPixel(x, y, qRgba(255, 255, 0, 128));

    GpuScene scene;
    scene.canvasSize = QSize(64, 48);
    scene.backgroundColor = QColor(10, 20, 30);
    GpuItem item;
    item.layer.source = still;
    item.layer.rect = QRectF(5, 5, 40, 30);
    item.layer.rotation = 15.0;
    item.layer.opacity = 0.8;
    item.layer.valid = true;
    scene.items.push_back(item);

    const QImage before = GpuCompositor::render(scene);
    QVERIFY(!before.isNull());
    QVERIFY(!GpuCompositor::render(sceneWith(std::make_shared<TwoSquares>())).isNull());
    const QImage after = GpuCompositor::render(scene);
    QCOMPARE(after, before);
}

namespace {

// A shape with one fill and one stroke layer, the way the catalog builds them.
ShapeStyle shapeStyleWith(ShapeKind kind, const QColor &fill, double strokeWidth = 5.0,
                          const QColor &stroke = QColor(20, 200, 60))
{
    ShapeStyle style;
    style.kind = kind;
    style.layers = defaultShapeLayers(fill, QColor(40, 40, 220), stroke, strokeWidth);
    return style;
}

QImage rasterShape(const ShapeStyle &style, int w, int h, double scale = 1.0, double timeSec = 0.0)
{
    const skia::ShapePainterResult painted = skia::makeShapePainter(style, w, h, scale, timeSec);
    return skia::SkiaRuntime::rasterize(*painted.painter);
}

// Alpha mass inside / outside a rect of the raster.
double massInside(const QImage &image, const QRect &rect)
{
    double mass = 0;
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x)
            if (rect.contains(x, y))
                mass += qAlpha(row[x]) / 255.0;
    }
    return mass;
}

} // namespace

// Every ShapeKind × paint variant rasterizes to something: a solid fill, a gradient fill, a
// dashed stroke and a soft shadow all leave ink, and all of it inside the painter's image.
void SkiaTest::allShapeKindsRasterize_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("variant");
    for (ShapeKind kind : allShapeKinds()) {
        for (const char *variant : {"solid", "gradient", "dash", "shadow"}) {
            const QByteArray name = shapeKindToString(kind).toUtf8() + "/" + variant;
            QTest::newRow(name.constData()) << int(kind) << QString::fromLatin1(variant);
        }
    }
}

void SkiaTest::allShapeKindsRasterize()
{
    QFETCH(int, kind);
    QFETCH(QString, variant);

    ShapeStyle style = shapeStyleWith(ShapeKind(kind), QColor(220, 40, 40));
    style.cornerRadius = 12.0;
    if (variant == QLatin1String("gradient")) {
        style.layers[0].paint.kind = TextPaintKind::Gradient;
        style.layers[0].paint.gradient.angle = 30.0;
        style.layers[1].enabled = false;
    } else if (variant == QLatin1String("dash")) {
        style.layers[0].enabled = false;
        style.layers[1].dash = StrokeDash::Dash;
        style.layers[1].strokeAlign = StrokeAlign::Center;
    } else if (variant == QLatin1String("shadow")) {
        style.layers.prepend(shadowLayer(Qt::black, 6.0, 6.0, 8.0, 0.8));
    }

    const int w = 150;
    const int h = 110;
    const skia::ShapePainterResult painted = skia::makeShapePainter(style, w, h, 0.8);
    QVERIFY(painted.painter);
    QVERIFY(painted.rect.width() > w && painted.rect.height() > h);
    QCOMPARE(painted.painter->size(), painted.rect.size().toSize());
    const QImage image = skia::SkiaRuntime::rasterize(*painted.painter);
    const Coverage c = coverageOf(image);
    QVERIFY2(c.mass > 100, qPrintable(QStringLiteral("mass %1").arg(c.mass)));
    if (variant == QLatin1String("solid"))
        QVERIFY2(c.r > c.g && c.r > c.b, qPrintable(QStringLiteral("rgb %1 %2 %3").arg(c.r).arg(c.g).arg(c.b)));
    if (variant == QLatin1String("gradient")) // red → blue
        QVERIFY2(c.r > c.g && c.b > c.g, qPrintable(QStringLiteral("rgb %1 %2 %3").arg(c.r).arg(c.g).arg(c.b)));
    if (variant == QLatin1String("dash"))
        QVERIFY2(c.g > c.r, qPrintable(QStringLiteral("rgb %1 %2 %3").arg(c.r).arg(c.g).arg(c.b)));
    // Nothing touches the outermost pixel ring: the bleed is wide enough.
    QCOMPARE(massInside(image, QRect(0, 0, image.width(), 1)), 0.0);
    QCOMPARE(massInside(image, QRect(0, image.height() - 1, image.width(), 1)), 0.0);
}

void SkiaTest::shapePainterCacheKey()
{
    ShapeStyle style = shapeStyleWith(ShapeKind::Star, QColor(220, 40, 40));
    const auto a = skia::makeShapePainter(style, 100, 80, 1.0);
    const auto b = skia::makeShapePainter(style, 100, 80, 1.0);
    QVERIFY(a.painter->cacheKey() != 0);
    QCOMPARE(a.painter->cacheKey(), b.painter->cacheKey());
    QCOMPARE(a.rect, b.rect);

    QVERIFY(skia::makeShapePainter(style, 100, 80, 0.5).painter->cacheKey() != a.painter->cacheKey());
    QVERIFY(skia::makeShapePainter(style, 101, 80, 1.0).painter->cacheKey() != a.painter->cacheKey());
    style.points = 7;
    QVERIFY(skia::makeShapePainter(style, 100, 80, 1.0).painter->cacheKey() != a.painter->cacheKey());
    style.layers[0].paint.color = Qt::green;
    const quint64 green = skia::makeShapePainter(style, 100, 80, 1.0).painter->cacheKey();
    style.layers[1].dash = StrokeDash::Dash;
    const quint64 dashed = skia::makeShapePainter(style, 100, 80, 1.0).painter->cacheKey();
    QVERIFY(dashed != green);
    style.cornerRadius = 9.0;
    QVERIFY(skia::makeShapePainter(style, 100, 80, 1.0).painter->cacheKey() != dashed);

    // Anything time-varying redraws every frame instead of filling the GPU cache.
    ShapeStyle moving = style;
    moving.layers[0].paint.kind = TextPaintKind::Gradient;
    moving.layers[0].paint.gradient.offsetSpeed = 0.5;
    QCOMPARE(skia::makeShapePainter(moving, 100, 80, 1.0).painter->cacheKey(), quint64(0));
    ShapeStyle keyed = style;
    keyed.keyframes[QStringLiteral("layer.stroke.width")].setKeyframe(0, 2.0);
    keyed.keyframes[QStringLiteral("layer.stroke.width")].setKeyframe(secondsToUs(1.0), 9.0);
    QCOMPARE(skia::makeShapePainter(keyed, 100, 80, 1.0).painter->cacheKey(), quint64(0));
}

// Inside strokes never leave the layout rect; centred ones straddle it; outside ones sit wholly
// in the bleed ring. The bleed grows with the stroke's reach.
void SkiaTest::shapeStrokeAlignAndBleed()
{
    ShapeStyle style = shapeStyleWith(ShapeKind::Rectangle, QColor(220, 40, 40), 10.0);
    style.layers[0].enabled = false;
    const int w = 100;
    const int h = 80;

    style.layers[1].strokeAlign = StrokeAlign::Inside;
    const double insideBleed = skia::shapeBleedFor(style);
    QVERIFY(insideBleed < 4.0);
    {
        const skia::ShapePainterResult painted = skia::makeShapePainter(style, w, h, 1.0);
        const QImage image = skia::SkiaRuntime::rasterize(*painted.painter);
        const int bleed = qRound(-painted.rect.x());
        const QRect layout(bleed, bleed, w, h);
        const double inside = massInside(image, layout);
        const double total = coverageOf(image).mass;
        QVERIFY2(inside > 1500 && total - inside < 1.0, qPrintable(QStringLiteral("inside %1 total %2").arg(inside).arg(total)));
    }

    style.layers[1].strokeAlign = StrokeAlign::Outside;
    const double outsideBleed = skia::shapeBleedFor(style);
    QVERIFY(outsideBleed >= insideBleed + 10.0);
    {
        const skia::ShapePainterResult painted = skia::makeShapePainter(style, w, h, 1.0);
        const QImage image = skia::SkiaRuntime::rasterize(*painted.painter);
        const int bleed = qRound(-painted.rect.x());
        const QRect layout(bleed, bleed, w, h);
        const double inside = massInside(image, layout);
        const double total = coverageOf(image).mass;
        QVERIFY2(total > 1500 && inside < total * 0.05, qPrintable(QStringLiteral("inside %1 total %2").arg(inside).arg(total)));
    }

    style.layers[1].strokeAlign = StrokeAlign::Center;
    {
        const skia::ShapePainterResult painted = skia::makeShapePainter(style, w, h, 1.0);
        const QImage image = skia::SkiaRuntime::rasterize(*painted.painter);
        const int bleed = qRound(-painted.rect.x());
        const QRect layout(bleed, bleed, w, h);
        const double inside = massInside(image, layout);
        const double total = coverageOf(image).mass;
        QVERIFY2(inside > total * 0.35 && inside < total * 0.65, qPrintable(QStringLiteral("inside %1 total %2").arg(inside).arg(total)));
    }
}

// A triangle with rounded corners loses its three tips: less ink than the sharp one. A star
// rounds its inner corners too (which adds ink back), so there the raster merely has to change.
// An ellipse has no corners and is left alone.
void SkiaTest::shapeCornerRadiusRoundsAnyKind()
{
    ShapeStyle sharp = shapeStyleWith(ShapeKind::Triangle, QColor(220, 40, 40), 0.0);
    ShapeStyle rounded = sharp;
    rounded.cornerRadius = 20.0;
    const double sharpMass = coverageOf(rasterShape(sharp, 160, 160)).mass;
    const double roundedMass = coverageOf(rasterShape(rounded, 160, 160)).mass;
    QVERIFY2(roundedMass < sharpMass * 0.995 && roundedMass > sharpMass * 0.5,
             qPrintable(QStringLiteral("sharp %1 rounded %2").arg(sharpMass).arg(roundedMass)));

    ShapeStyle star = shapeStyleWith(ShapeKind::Star, QColor(220, 40, 40), 0.0);
    ShapeStyle starRounded = star;
    starRounded.cornerRadius = 20.0;
    QVERIFY(rasterShape(starRounded, 160, 160) != rasterShape(star, 160, 160));

    ShapeStyle circle = shapeStyleWith(ShapeKind::Ellipse, QColor(220, 40, 40), 0.0);
    ShapeStyle circleRounded = circle;
    circleRounded.cornerRadius = 20.0;
    QCOMPARE(coverageOf(rasterShape(circleRounded, 160, 160)).mass, coverageOf(rasterShape(circle, 160, 160)).mass);
}

// Trimming a stroke to its first half and dashing it both take ink away from the solid ring.
void SkiaTest::shapeTrimAndDashRemoveMass()
{
    ShapeStyle style = shapeStyleWith(ShapeKind::Ellipse, QColor(220, 40, 40), 6.0);
    style.layers[0].enabled = false;
    style.layers[1].strokeAlign = StrokeAlign::Center;
    const double full = coverageOf(rasterShape(style, 140, 140)).mass;

    ShapeStyle trimmed = style;
    trimmed.layers[1].trimEnd = 0.5;
    const double half = coverageOf(rasterShape(trimmed, 140, 140)).mass;
    QVERIFY2(half > full * 0.4 && half < full * 0.6, qPrintable(QStringLiteral("full %1 half %2").arg(full).arg(half)));

    ShapeStyle dashed = style;
    dashed.layers[1].dash = StrokeDash::Dash;
    const double dashedMass = coverageOf(rasterShape(dashed, 140, 140)).mass;
    // 4-on / 2-off, plus the round caps each dash grows: about five sixths of the ring.
    QVERIFY2(dashedMass > full * 0.5 && dashedMass < full * 0.9,
             qPrintable(QStringLiteral("full %1 dashed %2").arg(full).arg(dashedMass)));

    // Shifting the dash phase moves the runs without changing how much ink there is.
    ShapeStyle shifted = dashed;
    shifted.layers[1].dashOffset = 3.0;
    const QImage a = rasterShape(dashed, 140, 140);
    const QImage b = rasterShape(shifted, 140, 140);
    QVERIFY(a != b);
    QVERIFY(qAbs(coverageOf(b).mass - dashedMass) < dashedMass * 0.1);

    ShapeStyle sketchy = style;
    sketchy.layers[1].sketchLength = 6.0;
    sketchy.layers[1].sketchDeviation = 3.0;
    QVERIFY(rasterShape(sketchy, 140, 140) != rasterShape(style, 140, 140));
}

// A shadow layer behind the fill is the fill's silhouette pushed along its offset: the ink's
// centroid moves that way and the image carries black where the fill does not cover it.
void SkiaTest::shapeShadowOffsetsSilhouette()
{
    ShapeStyle plain = shapeStyleWith(ShapeKind::Rectangle, QColor(255, 0, 0), 0.0);
    ShapeStyle shadowed = plain;
    shadowed.layers.prepend(shadowLayer(Qt::black, 14.0, 10.0, 0.0, 1.0));
    const Coverage a = coverageOf(rasterShape(plain, 120, 90));
    const Coverage b = coverageOf(rasterShape(shadowed, 120, 90));
    QVERIFY2(b.mass > a.mass * 1.15, qPrintable(QStringLiteral("plain %1 shadowed %2").arg(a.mass).arg(b.mass)));
    QVERIFY(b.centroid.x() > a.centroid.x() + 2.0);
    QVERIFY(b.centroid.y() > a.centroid.y() + 1.0);
    QVERIFY(b.r < a.r);
}

// A keyframed stroke width resolves per frame through the compositor: the ring is thicker later.
void SkiaTest::keyframedShapeStrokeGrowsOverTime()
{
    ShapeStyle style = shapeStyleWith(ShapeKind::Ellipse, QColor(255, 0, 0), 2.0, Qt::green);
    style.layers[0].enabled = false;
    style.layers[1].strokeAlign = StrokeAlign::Inside;
    style.keyframes[QStringLiteral("layer.stroke.width")].setKeyframe(0, 2.0);
    style.keyframes[QStringLiteral("layer.stroke.width")].setKeyframe(secondsToUs(1.0), 20.0);
    const ShapeStyle early = style.resolvedAt(0);
    const ShapeStyle late = style.resolvedAt(secondsToUs(1.0));
    QCOMPARE(early.layers[1].width, 2.0);
    QCOMPARE(late.layers[1].width, 20.0);
    const double thin = coverageOf(rasterShape(early, 120, 100)).mass;
    const double thick = coverageOf(rasterShape(late, 120, 100)).mass;
    QVERIFY2(thick > thin * 3.0, qPrintable(QStringLiteral("thin %1 thick %2").arg(thin).arg(thick)));

    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");
    const Project project = shapeProject(style);
    FrameCompositor compositor;
    compositor.setProject(&project);
    auto greenPixels = [](const QImage &img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qGreen(img.pixel(x, y)) > 150 && qRed(img.pixel(x, y)) < 100)
                    ++n;
        return n;
    };
    const int first = greenPixels(compositor.compositeAt(0));
    const int last = greenPixels(compositor.compositeAt(secondsToUs(1.0) - 1));
    QVERIFY2(first > 50 && last > first * 3, qPrintable(QStringLiteral("first %1 last %2").arg(first).arg(last)));
}

// A static shape paints once and is served from the GPU cache afterwards; the composited fill
// lands inside the clip's layout rect.
void SkiaTest::compositorDrawsShapeThroughSkia()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");

    ShapeStyle style = shapeStyleWith(ShapeKind::Heart, QColor(200, 30, 90), 4.0, Qt::white);
    const Project project = shapeProject(style);
    FrameCompositor compositor;
    compositor.setProject(&project);

    auto paints = [] {
        quint64 n = 0;
        gl::runtime().exec([&] {
            if (auto *sk = skia::SkiaRuntime::acquire(gl::runtime()))
                n = sk->stats().paints;
        });
        return n;
    };

    const quint64 before = paints();
    const QImage frame = compositor.compositeAt(0);
    QCOMPARE(paints(), before + 1);
    // Same painter key: the second frame comes from the GPU cache, not a repaint.
    QVERIFY(!compositor.compositeAt(0).isNull());
    QCOMPARE(paints(), before + 1);

    int inside = 0, outside = 0;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            if (!near(frame.pixel(x, y) | 0xff000000u, qRgb(200, 30, 90), 12))
                continue;
            if (QRect(20, 10, 120, 100).contains(x, y))
                ++inside;
            else
                ++outside;
        }
    }
    QVERIFY2(inside > 500 && outside == 0, qPrintable(QStringLiteral("inside %1 outside %2").arg(inside).arg(outside)));
}

namespace {

// Lit-pixel bounding box, count and alpha-weighted mean colour of a straight-alpha image.
struct Ink
{
    QRect bbox;
    int count = 0;
    double r = 0, g = 0, b = 0;
};

Ink inkOf(const QImage &image)
{
    Ink ink;
    int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
    double mass = 0;
    const QImage img = image.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < img.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(row[x]);
            if (a < 16)
                continue;
            ++ink.count;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
            mass += a;
            ink.r += qRed(row[x]) * a;
            ink.g += qGreen(row[x]) * a;
            ink.b += qBlue(row[x]) * a;
        }
    }
    if (ink.count > 0) {
        ink.bbox = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
        ink.r /= mass;
        ink.g /= mass;
        ink.b /= mass;
    }
    return ink;
}

QString describe(const Ink &a, const Ink &b)
{
    return QStringLiteral("qt n=%1 bbox=%2,%3 %4x%5 rgb=(%6,%7,%8) | skia n=%9 bbox=%10,%11 %12x%13 rgb=(%14,%15,%16)")
        .arg(a.count).arg(a.bbox.x()).arg(a.bbox.y()).arg(a.bbox.width()).arg(a.bbox.height())
        .arg(qRound(a.r)).arg(qRound(a.g)).arg(qRound(a.b))
        .arg(b.count).arg(b.bbox.x()).arg(b.bbox.y()).arg(b.bbox.width()).arg(b.bbox.height())
        .arg(qRound(b.r)).arg(qRound(b.g)).arg(qRound(b.b));
}

// Tolerances are wide enough for two antialiasers and a gaussian standing in for a triple box
// blur, and narrow enough that a missing outline, a shifted block or a wrong colour fails.
bool inkClose(const Ink &a, const Ink &b, QString *why)
{
    *why = describe(a, b);
    if (a.count == 0 || b.count == 0)
        return a.count == b.count;
    const QRect da = a.bbox;
    const QRect db = b.bbox;
    if (qAbs(da.left() - db.left()) > 3 || qAbs(da.top() - db.top()) > 3
        || qAbs(da.right() - db.right()) > 3 || qAbs(da.bottom() - db.bottom()) > 3)
        return false;
    if (qAbs(a.count - b.count) > qMax(40, int(a.count * 0.12)))
        return false;
    return qAbs(a.r - b.r) <= 16 && qAbs(a.g - b.g) <= 16 && qAbs(a.b - b.b) <= 16;
}

Clip textClip(const TextStyle &style, const QString &text)
{
    Clip clip;
    clip.type = ClipType::Text;
    clip.textContent = text;
    clip.textStyle = style;
    return clip;
}

QImage skiaText(const Clip &clip, const QString &text, const QRectF &rect, double scale, int word,
                QRectF *outRect)
{
    const skia::TextPainterResult painted = skia::makeTextPainter(clip, text, rect, scale, word);
    if (outRect)
        *outRect = painted.rect;
    return painted.painter ? skia::SkiaRuntime::rasterize(*painted.painter) : QImage();
}

} // namespace

void SkiaTest::textPainterCacheKeys()
{
    TextStyle style;
    style.pixelSize = 40;
    const Clip clip = textClip(style, QStringLiteral("cache me"));
    const QRectF layout(0, 0, 300, 100);
    const auto a = skia::makeTextPainter(clip, QStringLiteral("cache me"), layout, 1.0);
    const auto b = skia::makeTextPainter(clip, QStringLiteral("cache me"), layout, 1.0);
    QVERIFY(a.painter && b.painter);
    QVERIFY(a.painter->cacheKey() != 0);
    QCOMPARE(a.painter->cacheKey(), b.painter->cacheKey());
    QVERIFY(skia::makeTextPainter(clip, QStringLiteral("cache you"), layout, 1.0).painter->cacheKey() != a.painter->cacheKey());
    QVERIFY(skia::makeTextPainter(clip, QStringLiteral("cache me"), layout, 0.5).painter->cacheKey() != a.painter->cacheKey());
    QVERIFY(skia::makeTextPainter(clip, QStringLiteral("cache me"), layout, 1.0, 0).painter->cacheKey() != a.painter->cacheKey());
    Clip other = clip;
    setSolidFill(other.textStyle, Qt::red);
    QVERIFY(skia::makeTextPainter(other, QStringLiteral("cache me"), layout, 1.0).painter->cacheKey() != a.painter->cacheKey());
    // A moving gradient is time-driven: never cached.
    Clip flowing = clip;
    TextShadingLayer *fill = firstTextLayerOfKind(flowing.textStyle.layers, TextLayerKind::Fill, false);
    fill->paint.kind = TextPaintKind::Gradient;
    fill->paint.gradient.offsetSpeed = 0.5;
    QCOMPARE(skia::makeTextPainter(flowing, QStringLiteral("cache me"), layout, 1.0).painter->cacheKey(), quint64(0));
}

// A keyframed pixelSize is baked per frame: the painter stops caching and the rendered block is
// taller later in the clip.
void SkiaTest::keyframedTextGrowsOverTime()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    Clip clip;
    clip.type = ClipType::Text;
    clip.textContent = QStringLiteral("Grow");
    clip.textStyle.pixelSize = 24;
    setSolidFill(clip.textStyle, QColor(255, 0, 0));
    clip.textStyle.keyframes[QStringLiteral("pixelSize")].setKeyframe(0, 24.0);
    clip.textStyle.keyframes[QStringLiteral("pixelSize")].setKeyframe(secondsToUs(2.0), 72.0);
    const QRectF layout(0, 0, 400, 160);

    clip.timelineDuration = secondsToUs(3.0);
    const skia::TextPainterResult a = skia::makeTextPainter(clip, clip.textContent, layout, 1.0, -1, 0);
    const skia::TextPainterResult b = skia::makeTextPainter(clip, clip.textContent, layout, 1.0, -1, secondsToUs(2.0));
    QVERIFY(a.painter && b.painter);
    QCOMPARE(a.painter->cacheKey(), quint64(0));
    QCOMPARE(b.painter->cacheKey(), quint64(0));
    const Ink small = inkOf(skia::SkiaRuntime::rasterize(*a.painter));
    const Ink big = inkOf(skia::SkiaRuntime::rasterize(*b.painter));
    QVERIFY2(big.bbox.height() > small.bbox.height() * 2, qPrintable(describe(small, big)));

    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");
    Project project;
    project.setResolution(400, 160);
    project.tracks().clear();
    project.tracks().append(Track{.type = TrackType::Text});
    clip.id = QStringLiteral("g");
    clip.timelineStart = 0;
    clip.timelineDuration = secondsToUs(3.0);
    clip.transformX.setKeyframe(0, 0.0);
    clip.transformY.setKeyframe(0, 0.0);
    clip.transformW.setKeyframe(0, 400.0);
    clip.transformH.setKeyframe(0, 160.0);
    project.tracks()[0].clips.append(clip);
    FrameCompositor compositor;
    compositor.setProject(&project);
    auto redHeight = [](const QImage &img) {
        int minY = img.height(), maxY = -1;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qRed(img.pixel(x, y)) > 150 && qGreen(img.pixel(x, y)) < 100) {
                    minY = qMin(minY, y);
                    maxY = qMax(maxY, y);
                }
        return maxY - minY;
    };
    const int early = redHeight(compositor.compositeAt(0));
    const int late = redHeight(compositor.compositeAt(secondsToUs(2.0)));
    QVERIFY2(early > 5 && late > early * 2, qPrintable(QStringLiteral("early %1 late %2").arg(early).arg(late)));
}

// A top→bottom gradient from red to blue: the upper rows of ink are red, the lower rows blue, and
// the solid-fill raster of the same style stays all red.
void SkiaTest::gradientFillSweepsTheBlock()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 90;
    TextShadingLayer *gradientFill = firstTextLayerOfKind(style.layers, TextLayerKind::Fill, false);
    gradientFill->paint.kind = TextPaintKind::Gradient;
    gradientFill->paint.gradient.stops = {{0.0, QColor(255, 0, 0)}, {1.0, QColor(0, 0, 255)}};
    gradientFill->paint.gradient.angle = 90.0;
    const QString text = QStringLiteral("HIGH");
    const Clip clip = textClip(style, text);
    const QRectF layout(0, 0, 400, 140);
    const QImage image = skiaText(clip, text, layout, 1.0, -1, nullptr).convertToFormat(QImage::Format_RGBA8888);
    QVERIFY(!image.isNull());
    const Ink ink = inkOf(image);
    QVERIFY(ink.count > 500);
    auto meanOf = [&](int y0, int y1) {
        double r = 0, b = 0, n = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QRgb p = image.pixel(x, y);
                if (qAlpha(p) < 200)
                    continue;
                r += qRed(p);
                b += qBlue(p);
                n += 1;
            }
        return QPointF(n > 0 ? r / n : 0, n > 0 ? b / n : 0);
    };
    const int mid = ink.bbox.center().y();
    const QPointF top = meanOf(ink.bbox.top(), ink.bbox.top() + (mid - ink.bbox.top()) / 2);
    const QPointF bottom = meanOf(mid + (ink.bbox.bottom() - mid) / 2, ink.bbox.bottom() + 1);
    QVERIFY2(top.x() > 150 && top.y() < 110, qPrintable(QStringLiteral("top r=%1 b=%2").arg(top.x()).arg(top.y())));
    QVERIFY2(bottom.y() > 150 && bottom.x() < 110, qPrintable(QStringLiteral("bottom r=%1 b=%2").arg(bottom.x()).arg(bottom.y())));

    // The image size and placement are unchanged by the fill, so the layer still lands where the
    // solid block would.
    Clip solid = clip;
    setSolidFill(solid.textStyle, QColor(255, 0, 0));
    const QImage plain = skiaText(solid, text, layout, 1.0, -1, nullptr);
    QCOMPARE(image.size(), plain.size());
    QString why;
    Ink a = inkOf(plain);
    Ink b = ink;
    a.r = a.g = a.b = b.r = b.g = b.b = 0; // colour deliberately differs
    QVERIFY2(inkClose(a, b, &why), qPrintable(why));
}

// A bent line rises above where the straight one sits, keeps every glyph, and reserves the rise
// in the bleed so nothing is clipped.
void SkiaTest::pathBendArchesTheLine()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 40;
    setSolidFill(style, Qt::white);
    style.wordWrap = false;
    const QString text = QStringLiteral("curve this line");
    const QRectF layout(0, 0, 420, 120);
    const Clip flat = textClip(style, text);
    QRectF flatRect;
    const Ink straight = inkOf(skiaText(flat, text, layout, 1.0, -1, &flatRect));

    Clip bent = flat;
    bent.textStyle.pathBend = 60.0;
    QRectF bentRect;
    const QImage bentImage = skiaText(bent, text, layout, 1.0, -1, &bentRect);
    const Ink arched = inkOf(bentImage);
    QVERIFY(arched.count > straight.count * 0.8);
    // The bleed grew by the rise (|60|/100 × 2 em = 48 px), on every side.
    QVERIFY2(bentRect.top() < flatRect.top() - 40, qPrintable(QStringLiteral("%1 vs %2").arg(bentRect.top()).arg(flatRect.top())));
    // The middle of the line is higher than its ends: sample the ink's top edge per column.
    auto topAt = [&](int x0, int x1) {
        int top = bentImage.height();
        for (int x = x0; x < x1; ++x)
            for (int y = 0; y < bentImage.height(); ++y)
                if (qAlpha(bentImage.pixel(x, y)) > 60) {
                    top = qMin(top, y);
                    break;
                }
        return top;
    };
    const int leftTop = topAt(arched.bbox.left(), arched.bbox.left() + 30);
    const int midTop = topAt(arched.bbox.center().x() - 15, arched.bbox.center().x() + 15);
    const int rightTop = topAt(arched.bbox.right() - 30, arched.bbox.right() + 1);
    QVERIFY2(midTop < leftTop - 15 && midTop < rightTop - 15,
             qPrintable(QStringLiteral("left %1 mid %2 right %3").arg(leftTop).arg(midTop).arg(rightTop)));
    QVERIFY(arched.bbox.top() >= 1 && arched.bbox.bottom() < bentImage.height() - 1);

    // A downward bend mirrors it; a multi-line block ignores the bend.
    bent.textStyle.pathBend = -60.0;
    const Ink dipped = inkOf(skiaText(bent, text, layout, 1.0, -1, nullptr));
    QVERIFY(qAbs(dipped.count - arched.count) < arched.count * 0.15);
    Clip wrapped = bent;
    wrapped.textStyle.wordWrap = true;
    const QString two = QStringLiteral("first line\nsecond line");
    const Ink multi = inkOf(skiaText(wrapped, two, layout, 1.0, -1, nullptr));
    Clip wrappedFlat = wrapped;
    wrappedFlat.textStyle.pathBend = 0.0;
    QRectF r1, r2;
    skiaText(wrapped, two, layout, 1.0, -1, &r1);
    const Ink multiFlat = inkOf(skiaText(wrappedFlat, two, layout, 1.0, -1, &r2));
    QVERIFY(multi.count > 0);
    // Same glyphs at the same relative place (the bleed differs, so compare bbox size).
    QVERIFY(qAbs(multi.bbox.height() - multiFlat.bbox.height()) <= 2);
}

// With the emoji-font addon, an outlined block draws a tinted ring around every colour emoji and
// the shadow pass carries its silhouette.
void SkiaTest::emojiOutlineDrawsARing()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    reloadEmojiCatalog({QString::fromUtf8(DRIFT_TEST_EMOJI_FONT_DIR)});
    if (emojiFontFamily().isEmpty())
        QSKIP("No emoji font available");
    TextStyle style;
    style.pixelSize = 72;
    setSolidFill(style, Qt::white);
    const QString text = QString::fromUtf8("\xF0\x9F\x98\x80");
    const QRectF layout(0, 0, 200, 120);
    const Ink plain = inkOf(skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr));
    QVERIFY(plain.count > 300);

    style.layers = {strokeLayer(6.0, QColor(0, 255, 0)), solidFillLayer(Qt::white)};
    const QImage ringed = skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr).convertToFormat(QImage::Format_RGBA8888);
    const Ink ring = inkOf(ringed);
    QVERIFY2(ring.count > plain.count * 1.25, qPrintable(describe(plain, ring)));
    QVERIFY(ring.bbox.width() >= plain.bbox.width() + 8);
    // Just outside the emoji's own edge the ring is the outline colour.
    int green = 0;
    for (int x = ring.bbox.left(); x < ring.bbox.left() + 4; ++x)
        for (int y = ring.bbox.top(); y <= ring.bbox.bottom(); ++y) {
            const QRgb p = ringed.pixel(x, y);
            if (qAlpha(p) > 100 && qGreen(p) > 180 && qRed(p) < 80)
                ++green;
        }
    QVERIFY2(green > 5, qPrintable(QString::number(green)));

    style.layers = {shadowLayer(QColor(0, 0, 255), 14.0, 0.0, 0.0, 1.0), solidFillLayer(Qt::white)};
    const QImage shadowed = skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr).convertToFormat(QImage::Format_RGBA8888);
    const Ink shade = inkOf(shadowed);
    QVERIFY2(shade.bbox.right() >= plain.bbox.right() + 10, qPrintable(describe(plain, shade)));
    int blue = 0;
    for (int x = shade.bbox.right() - 6; x <= shade.bbox.right(); ++x)
        for (int y = shade.bbox.top(); y <= shade.bbox.bottom(); ++y) {
            const QRgb p = shadowed.pixel(x, y);
            if (qAlpha(p) > 100 && qBlue(p) > 180 && qRed(p) < 80)
                ++blue;
        }
    QVERIFY2(blue > 5, qPrintable(QString::number(blue)));
}


namespace {

// The probes PrimitiveProbe is checked against, shared by the CPU and GPU paths.
void checkPrimitives(const QImage &img, const char *path)
{
    const auto at = [&](int x, int y) { return img.pixel(x, y); };
    const auto info = [&](int x, int y) {
        return qPrintable(QStringLiteral("%1 (%2,%3) = %4").arg(QLatin1String(path)).arg(x).arg(y)
                              .arg(QString::number(at(x, y), 16)));
    };
    // A: the SkSL shader tinted its white child green.
    QVERIFY2(qGreen(at(40, 40)) > 200 && qRed(at(40, 40)) < 20 && qAlpha(at(40, 40)) > 200, info(40, 40));
    // B: the shader mask ramps alpha from opaque on the left to clear on the right.
    QVERIFY2(qAlpha(at(93, 40)) > 200, info(93, 40));
    QVERIFY2(qAlpha(at(147, 40)) < 70, info(147, 40));
    QVERIFY2(qAlpha(at(93, 40)) > qAlpha(at(120, 40)) && qAlpha(at(120, 40)) > qAlpha(at(147, 40)), info(120, 40));
    // C: the trim kept only the first half of the stroke.
    QVERIFY2(qAlpha(at(180, 40)) > 200 && qBlue(at(180, 40)) > 200, info(180, 40));
    QVERIFY2(qAlpha(at(220, 40)) < 16, info(220, 40));
    // D: an outer blur is hollow inside the rect and glows just outside it.
    QVERIFY2(qAlpha(at(280, 40)) < 16, info(280, 40));
    QVERIFY2(qAlpha(at(257, 40)) > 10, info(257, 40));
    // E: OKLab red→blue passes through a purple, not a dark desaturated grey.
    QVERIFY2(qRed(at(40, 7)) > 60 && qBlue(at(40, 7)) > 60, info(40, 7));
}

} // namespace

void SkiaTest::textPrimitivesRender()
{
    const QImage cpu = skia::SkiaRuntime::rasterize(PrimitiveProbe()).convertToFormat(QImage::Format_RGBA8888);
    QCOMPARE(cpu.size(), QSize(320, 80));
    checkPrimitives(cpu, "cpu");
    // F is a probe, not a contract: Skia documents saveLayer as ignoring the paint's mask filter.
    qInfo("saveLayer mask filter honoured on CPU: %s (alpha right edge %d)",
          qAlpha(cpu.pixel(147, 70)) < 70 ? "yes" : "no", qAlpha(cpu.pixel(147, 70)));

    // Text on a path: the contour measure of an arc places the midpoint at the apex, tangent flat.
    SkPathBuilder pb;
    pb.addArc(SkRect::MakeXYWH(0, 0, 100, 100), 180, 180);
    SkContourMeasureIter iter(pb.detach(), false);
    sk_sp<SkContourMeasure> arc = iter.next();
    QVERIFY(arc);
    SkMatrix m;
    QVERIFY(arc->getMatrix(arc->length() / 2, &m));
    QVERIFY2(qAbs(m.getTranslateX() - 50.0f) < 1.0f && qAbs(m.getTranslateY()) < 1.0f,
             qPrintable(QStringLiteral("%1,%2").arg(m.getTranslateX()).arg(m.getTranslateY())));
    QVERIFY(qAbs(qAbs(m.getScaleX()) - 1.0f) < 0.05f && qAbs(m.getSkewY()) < 0.05f);

    if (!GpuEffectExecutor::instance().isAvailable())
        QSKIP("GL unavailable");
    QImage gpu;
    gl::runtime().exec([&] {
        gl::GlRuntime &rt = gl::runtime();
        auto *sk = skia::SkiaRuntime::acquire(rt);
        QVERIFY(sk);
        gl::GlTarget target = sk->paintToTarget(rt, rt.functions(), PrimitiveProbe());
        QVERIFY(target.isValid());
        gpu = readStraight(rt.functions(), target);
        rt.releaseTarget(std::move(target));
    });
    QCOMPARE(gpu.size(), QSize(320, 80));
    checkPrimitives(gpu, "gpu");
    qInfo("saveLayer mask filter honoured on GPU: %s (alpha right edge %d)",
          qAlpha(gpu.pixel(147, 70)) < 70 ? "yes" : "no", qAlpha(gpu.pixel(147, 70)));
}


namespace {

QImage skiaTextAt(const Clip &clip, const QString &text, const QRectF &rect, double scale, TimeUs clipTimeUs,
                  QRectF *outRect = nullptr, quint64 *key = nullptr)
{
    const skia::TextPainterResult painted = skia::makeTextPainter(clip, text, rect, scale, -1, clipTimeUs);
    if (outRect)
        *outRect = painted.rect;
    if (key)
        *key = painted.painter ? painted.painter->cacheKey() : 0;
    return painted.painter ? skia::SkiaRuntime::rasterize(*painted.painter) : QImage();
}

double centroidY(const QImage &image)
{
    double sum = 0, mass = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
            const int a = qAlpha(image.pixel(x, y));
            sum += a * y;
            mass += a;
        }
    return mass > 0 ? sum / mass : 0.0;
}

double centroidXOf(const QImage &image, const std::function<bool(QRgb)> &pick)
{
    double sum = 0, n = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (pick(image.pixel(x, y))) {
                sum += x;
                n += 1;
            }
    return n > 0 ? sum / n : -1.0;
}

Clip animatedClip(const TextStyle &style, const QString &text, TimeUs durationUs = secondsToUs(2.0))
{
    Clip clip = textClip(style, text);
    clip.timelineDuration = durationUs;
    return clip;
}

} // namespace

// A staggered character reveal shows more glyphs as time goes on; a word rise starts below its
// resting place and settles.
void SkiaTest::fragmentAnimationRendersPerCharacter()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 48;
    style.animation.in = legacyTextAnimationSlot(QStringLiteral("typewriter"), 0, QStringLiteral("linear"),
                                                 QStringLiteral("character"), 100000, QStringLiteral("forward"));
    const QString text = QStringLiteral("abcdef");
    const QRectF layout(0, 0, 400, 120);
    const Clip clip = animatedClip(style, text);
    const int atStart = inkOf(skiaTextAt(clip, text, layout, 1.0, 0)).count;
    const int atQuarter = inkOf(skiaTextAt(clip, text, layout, 1.0, 250000)).count;
    const int atEnd = inkOf(skiaTextAt(clip, text, layout, 1.0, secondsToUs(1.5))).count;
    QVERIFY2(atStart > 0 && atStart < atQuarter && atQuarter < atEnd,
             qPrintable(QStringLiteral("%1 %2 %3").arg(atStart).arg(atQuarter).arg(atEnd)));

    TextStyle rise = style;
    rise.animation.in = legacyTextAnimationSlot(QStringLiteral("rise"), 400000, QStringLiteral("linear"),
                                                QStringLiteral("word"), 200000, QStringLiteral("forward"));
    const Clip rising = animatedClip(rise, QStringLiteral("one two"));
    const QImage early = skiaTextAt(rising, QStringLiteral("one two"), layout, 1.0, 100000);
    const QImage settled = skiaTextAt(rising, QStringLiteral("one two"), layout, 1.0, secondsToUs(1.5));
    QVERIFY(inkOf(early).count > 0);
    QVERIFY2(centroidY(early) > centroidY(settled) + 5.0,
             qPrintable(QStringLiteral("%1 vs %2").arg(centroidY(early)).arg(centroidY(settled))));
    // Per-fragment motion changes the pixels, not the layer: both frames are the same image size.
    QCOMPARE(early.size(), settled.size());
}

// The painter's image is sized for the whole animation up front, so its rect never moves while
// the animation plays, and the held pose is cached while the moving frames are not.
void SkiaTest::bleedIsTimeInvariant()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 48;
    style.animation.in = legacyTextAnimationSlot(QStringLiteral("rise"), 400000, QStringLiteral("easeOut"),
                                                 QStringLiteral("word"), 100000, QStringLiteral("forward"));
    const QString text = QStringLiteral("hold still");
    const QRectF layout(0, 0, 400, 120);
    const Clip clip = animatedClip(style, text, secondsToUs(3.0));
    QRectF r0, r1, r2;
    quint64 k0 = 0, k1 = 0, k2 = 0;
    const QImage a = skiaTextAt(clip, text, layout, 1.0, 0, &r0, &k0);
    const QImage b = skiaTextAt(clip, text, layout, 1.0, 200000, &r1, &k1);
    const QImage c = skiaTextAt(clip, text, layout, 1.0, secondsToUs(2.0), &r2, &k2);
    QCOMPARE(r0, r1);
    QCOMPARE(r1, r2);
    QCOMPARE(a.size(), c.size());
    QCOMPARE(k0, quint64(0));
    QCOMPARE(k1, quint64(0));
    QVERIFY(k2 != 0);
    // The rise reserved room: the image is taller than the plain block's.
    Clip plain = clip;
    plain.textStyle.animation = TextAnimationSet{};
    QRectF plainRect;
    skiaTextAt(plain, text, layout, 1.0, 0, &plainRect);
    QVERIFY2(r0.height() > plainRect.height() + 10, qPrintable(QStringLiteral("%1 vs %2").arg(r0.height()).arg(plainRect.height())));
}

// An offset shadow layer under a white fill shows its colour on the far edge; a stroke layer
// adds ink around every glyph.
void SkiaTest::shadingLayersCompositeInOrder()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 64;
    const QString text = QStringLiteral("HELLO");
    const QRectF layout(0, 0, 400, 120);
    const Ink plain = inkOf(skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr));
    QVERIFY(plain.count > 500);

    style.layers = {shadowLayer(QColor(0, 0, 255), 14.0, 0.0, 0.0, 1.0), solidFillLayer(Qt::white)};
    const QImage shadowed = skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr).convertToFormat(QImage::Format_RGBA8888);
    const Ink shade = inkOf(shadowed);
    QVERIFY2(shade.bbox.right() >= plain.bbox.right() + 10, qPrintable(describe(plain, shade)));
    int blue = 0;
    for (int x = shade.bbox.right() - 6; x <= shade.bbox.right(); ++x)
        for (int y = shade.bbox.top(); y <= shade.bbox.bottom(); ++y) {
            const QRgb p = shadowed.pixel(x, y);
            if (qAlpha(p) > 100 && qBlue(p) > 180 && qRed(p) < 80)
                ++blue;
        }
    QVERIFY2(blue > 5, qPrintable(QString::number(blue)));

    style.layers = {strokeLayer(6.0, QColor(0, 255, 0)), solidFillLayer(Qt::white)};
    const Ink stroked = inkOf(skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr));
    QVERIFY2(stroked.count > plain.count * 1.2, qPrintable(describe(plain, stroked)));
    QVERIFY(stroked.bbox.width() >= plain.bbox.width() + 8);

    // Layer order matters: the same stroke on top of the fill covers it (green-heavy ink).
    style.layers = {solidFillLayer(Qt::white), strokeLayer(6.0, QColor(0, 255, 0))};
    const Ink over = inkOf(skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr));
    QVERIFY2(over.r < stroked.r - 20 && over.b < stroked.b - 20, qPrintable(describe(stroked, over)));

    // A hollow (knockout) fill leaves only the stroke.
    TextShadingLayer hollow = solidFillLayer(Qt::white);
    hollow.knockout = true;
    style.layers = {strokeLayer(6.0, QColor(0, 255, 0)), hollow};
    const Ink hollowed = inkOf(skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr));
    QVERIFY2(hollowed.count < stroked.count * 0.8 && hollowed.count > 0, qPrintable(describe(stroked, hollowed)));
}

// A repeating left→right gradient slides with its offset, and maps per word when asked.
void SkiaTest::gradientOffsetShiftsColour()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 72;
    TextShadingLayer *fill = firstTextLayerOfKind(style.layers, TextLayerKind::Fill, false);
    fill->paint.kind = TextPaintKind::Gradient;
    fill->paint.gradient.stops = {{0.0, QColor(255, 0, 0)}, {1.0, QColor(0, 0, 255)}};
    fill->paint.gradient.angle = 0.0;
    fill->paint.gradient.repeat = true;
    const QString text = QStringLiteral("WWWWWWWW");
    const QRectF layout(0, 0, 600, 140);
    const auto redX = [](const QImage &img) {
        return centroidXOf(img, [](QRgb p) { return qAlpha(p) > 200 && qRed(p) > 150 && qBlue(p) < 100; });
    };
    const QImage at0 = skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr).convertToFormat(QImage::Format_RGBA8888);
    fill->paint.gradient.offset = 0.3;
    const QImage at05 = skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr).convertToFormat(QImage::Format_RGBA8888);
    QVERIFY(redX(at0) >= 0 && redX(at05) >= 0);
    QVERIFY2(qAbs(redX(at0) - redX(at05)) > 10.0, qPrintable(QStringLiteral("%1 vs %2").arg(redX(at0)).arg(redX(at05))));

    // The same gradient driven by offsetSpeed moves with time and is never cached.
    fill->paint.gradient.offset = 0.0;
    fill->paint.gradient.offsetSpeed = 0.5;
    const Clip flowing = animatedClip(style, text);
    quint64 key = 1;
    const QImage t0 = skiaTextAt(flowing, text, layout, 1.0, 0, nullptr, &key).convertToFormat(QImage::Format_RGBA8888);
    QCOMPARE(key, quint64(0));
    const QImage t1 = skiaTextAt(flowing, text, layout, 1.0, 600000).convertToFormat(QImage::Format_RGBA8888);
    QVERIFY(qAbs(redX(t0) - redX(t1)) > 10.0);

    // Mapped per word, every word starts red on its left edge.
    fill->paint.gradient.offsetSpeed = 0.0;
    fill->paint.gradient.repeat = false;
    fill->paint.gradient.space = TextGradientSpace::Word;
    const QString words = QStringLiteral("WW WW");
    const QImage perWord = skiaText(textClip(style, words), words, layout, 1.0, -1, nullptr).convertToFormat(QImage::Format_RGBA8888);
    const Ink ink = inkOf(perWord);
    // Two red runs: one at the block's left edge and one past the middle.
    int redLeft = 0, redRight = 0;
    for (int y = ink.bbox.top(); y <= ink.bbox.bottom(); ++y)
        for (int x = ink.bbox.left(); x <= ink.bbox.right(); ++x) {
            const QRgb p = perWord.pixel(x, y);
            if (qAlpha(p) > 200 && qRed(p) > 150 && qBlue(p) < 100)
                (x < ink.bbox.center().x() ? redLeft : redRight)++;
        }
    QVERIFY2(redLeft > 20 && redRight > 20, qPrintable(QStringLiteral("%1 %2").arg(redLeft).arg(redRight)));
}

// An inline wipe animator reveals the block through a soft mask travelling upward.
void SkiaTest::wipeMaskRevealsBottomUp()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 72;
    TextAnimator wipe;
    TextRangeSelector sel;
    sel.driver = TextSelectorDriver::Stagger;
    sel.domain = TextSelectorDomain::All;
    sel.staggerUs = 0;
    sel.durationUs = secondsToUs(1.0);
    sel.ease.kind = TextEaseKind::Linear;
    wipe.selectors = {sel};
    wipe.props.wipe.enabled = true;
    wipe.props.wipe.angleDeg = -90.0;
    wipe.props.wipe.softness = 0.1;
    style.animation.in.animators = {wipe};
    const QString text = QStringLiteral("WIPE");
    const QRectF layout(0, 0, 400, 140);
    const Clip clip = animatedClip(style, text, secondsToUs(3.0));
    const QImage half = skiaTextAt(clip, text, layout, 1.0, secondsToUs(0.5));
    const QImage full = skiaTextAt(clip, text, layout, 1.0, secondsToUs(2.0));
    const Ink h = inkOf(half), f = inkOf(full);
    QVERIFY2(h.count > f.count * 0.2 && h.count < f.count * 0.8, qPrintable(describe(h, f)));
    // Bottom revealed first: the half-revealed ink sits lower than the whole block's.
    QVERIFY2(centroidY(half) > centroidY(full) + 5.0, qPrintable(QStringLiteral("%1 vs %2").arg(centroidY(half)).arg(centroidY(full))));
}

// The typewriter caret appears after the last typed character and blinks.
void SkiaTest::caretDrawsAfterLastVisibleFragment()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 48;
    style.animation.in = legacyTextAnimationSlot(QStringLiteral("typewriter"), 0, QStringLiteral("linear"),
                                                 QStringLiteral("character"), 100000, QStringLiteral("forward"));
    style.animation.in.params.insert(QStringLiteral("caret"), VectorSlotValue::fromScalar(1.0));
    style.animation.in.params.insert(QStringLiteral("lead"), VectorSlotValue::fromScalar(0.0));
    const QString text = QStringLiteral("type");
    const QRectF layout(0, 0, 400, 120);
    const Clip clip = animatedClip(style, text, secondsToUs(3.0));
    TextStyle bare = style;
    bare.animation.in.params.insert(QStringLiteral("caret"), VectorSlotValue::fromScalar(0.0));
    const Clip noCaret = animatedClip(bare, text, secondsToUs(3.0));
    // Blink phase 0 → on: the caret adds ink to the right of the first glyph.
    const Ink withCaret = inkOf(skiaTextAt(clip, text, layout, 1.0, 0));
    const Ink without = inkOf(skiaTextAt(noCaret, text, layout, 1.0, 0));
    QVERIFY2(withCaret.count > without.count + 20, qPrintable(describe(without, withCaret)));
    QVERIFY(withCaret.bbox.right() > without.bbox.right() + 2);
    // 250 ms in the caret is off (200 on / 300 off).
    const Ink off = inkOf(skiaTextAt(clip, text, layout, 1.0, 250000));
    const Ink offBare = inkOf(skiaTextAt(noCaret, text, layout, 1.0, 250000));
    QVERIFY2(qAbs(off.count - offBare.count) < 10, qPrintable(describe(offBare, off)));
}

// Every SkSL effect compiles, and a shine layer renders as ink over its base.
void SkiaTest::skslEffectsCompileAndRender()
{
    for (const TextEffectSpec &spec : textShaderEffectSpecs())
        QVERIFY2(skia::textEffectCompileError(spec.id).isEmpty(),
                 qPrintable(spec.id + QLatin1String(": ") + skia::textEffectCompileError(spec.id)));
    QVERIFY(!skia::textEffectCompileError(QStringLiteral("nope")).isEmpty());

    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 72;
    const QString text = QStringLiteral("SHINE");
    const QRectF layout(0, 0, 400, 140);
    const Ink plain = inkOf(skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr));
    for (const TextEffectSpec &spec : textShaderEffectSpecs()) {
        TextStyle fx = style;
        TextShadingLayer *fill = firstTextLayerOfKind(fx.layers, TextLayerKind::Fill, false);
        fill->paint.kind = TextPaintKind::Effect;
        fill->paint.effect.id = spec.id;
        const Ink ink = inkOf(skiaText(textClip(fx, text), text, layout, 1.0, -1, nullptr));
        QVERIFY2(ink.count > plain.count * 0.5, qPrintable(spec.id + QLatin1String(": ") + describe(plain, ink)));
    }
    // An animated effect is time-driven, so the painter opts out of the cache.
    TextStyle animated = style;
    TextShadingLayer *fill = firstTextLayerOfKind(animated.layers, TextLayerKind::Fill, false);
    fill->paint.kind = TextPaintKind::Effect;
    fill->paint.effect.id = QStringLiteral("shine");
    QCOMPARE(skia::makeTextPainter(textClip(animated, text), text, layout, 1.0).painter->cacheKey(), quint64(0));
}

// Every look renders something, and Neon glows outside the glyphs.
void SkiaTest::textLookRenders()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 64;
    const QString text = QStringLiteral("Look");
    const QRectF layout(0, 0, 400, 140);
    const Ink plain = inkOf(skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr));
    for (const TextLook &look : textLooks()) {
        TextStyle styled = style;
        QVERIFY(applyTextLook(styled, look.id, {}));
        const Ink ink = inkOf(skiaText(textClip(styled, text), text, layout, 1.0, -1, nullptr));
        QVERIFY2(ink.count > 100, qPrintable(look.id));
        if (look.id == QLatin1String("neon"))
            QVERIFY2(ink.bbox.width() > plain.bbox.width() + 20, qPrintable(describe(plain, ink)));
    }
}

// Every built-in style pack rasterises its sample text once the In animation has settled: the
// gradient, effect, extrude and arc layers all produce ink through the shared painter.
void SkiaTest::textPacksRender()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    SKIP_WITHOUT_FONTS();
    const QRectF layout(0, 0, 900, 300);
    for (const TextPreset &preset : textPresets()) {
        TextStyle style = preset.style;
        style.pixelSize = 48;
        const Clip clip = animatedClip(style, preset.sampleText, secondsToUs(4.0));
        const Ink ink = inkOf(skiaTextAt(clip, preset.sampleText, layout, 1.0, secondsToUs(2.0)));
        QVERIFY2(ink.count > 100, qPrintable(preset.id + QStringLiteral(": %1").arg(ink.count)));
    }
}

QTEST_MAIN(SkiaTest)
#include "tst_skia.moc"
