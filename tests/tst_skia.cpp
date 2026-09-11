#include <QtTest>

#include <QOpenGLExtraFunctions>

#include "core/Project.h"
#include "engine/FrameCompositor.h"
#include "engine/GlRuntime.h"
#include "engine/GpuCompositor.h"
#include "engine/GpuEffectExecutor.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/RenderBackend.h"
#include "engine/ShapeRaster.h"
#include "engine/SkiaRuntime.h"
#include "engine/SkiaShapePainter.h"
#include "engine/SkiaTextPainter.h"
#include "engine/TextRaster.h"
#include "engine/VectorPainter.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

using namespace drift;

Q_DECLARE_METATYPE(drift::TextStyle)

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
    const QImage img = image.convertToFormat(QImage::Format_RGBA8888);
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
    void shapePainterMatchesQPainter_data();
    void shapePainterMatchesQPainter();
    void shapePainterCacheKey();
    void backendSwitchSelectsShapeRenderer();
    void textPainterMatchesQPainter_data();
    void textPainterMatchesQPainter();
    void textSpanPaintersMatchRasterSpans();
    void textPainterCacheKeys();
    void emojiTextMatchesQPainter();
    void backendSwitchSelectsTextRenderer();
    void keyframedTextGrowsOverTime();
    void gradientFillSweepsTheBlock();
    void pathBendArchesTheLine();
    void emojiOutlineDrawsARing();
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

// Every ShapeKind × fill × stroke, Skia's CPU raster against the QPainter reference. The two
// rasterizers differ per pixel — Skia quantizes edge coverage where QPainter is exact-area, and
// Qt flattens curved stroke outlines to chords — so the comparison is on coverage: alpha mass,
// centroid and mean colour. Fills agree to 0.1 %; thin stroke-only rings are where the noise
// peaks (about 5 % of mass, 0.9 px of centroid). A wrong dash unit, cap, gradient axis or path
// fill rule moves those well past the tolerances.
void SkiaTest::shapePainterMatchesQPainter_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("fill");
    QTest::addColumn<int>("stroke");
    const ShapeFillKind fills[] = {ShapeFillKind::None, ShapeFillKind::Solid,
                                   ShapeFillKind::LinearGradient, ShapeFillKind::RadialGradient};
    const ShapeStrokeStyle strokes[] = {ShapeStrokeStyle::None, ShapeStrokeStyle::Solid,
                                        ShapeStrokeStyle::Dash, ShapeStrokeStyle::Dot,
                                        ShapeStrokeStyle::DashDot};
    for (ShapeKind kind : allShapeKinds()) {
        for (ShapeFillKind fill : fills) {
            for (ShapeStrokeStyle stroke : strokes) {
                if (fill == ShapeFillKind::None && stroke == ShapeStrokeStyle::None)
                    continue;
                const QByteArray name = shapeKindToString(kind).toUtf8() + "/"
                    + shapeFillKindToString(fill).toUtf8() + "/"
                    + shapeStrokeStyleToString(stroke).toUtf8();
                QTest::newRow(name.constData()) << int(kind) << int(fill) << int(stroke);
            }
        }
    }
}

void SkiaTest::shapePainterMatchesQPainter()
{
    QFETCH(int, kind);
    QFETCH(int, fill);
    QFETCH(int, stroke);

    ShapeStyle style;
    style.kind = ShapeKind(kind);
    style.fillKind = ShapeFillKind(fill);
    style.strokeStyle = ShapeStrokeStyle(stroke);
    style.fill = QColor(220, 40, 40);
    style.fillSecondary = QColor(40, 40, 220);
    style.gradientAngle = 30.0;
    style.stroke = QColor(20, 200, 60);
    style.strokeWidth = 5.0;
    style.cornerRadius = 12.0;

    const int w = 150;
    const int h = 110;
    const double scale = 0.8;
    const QImage reference = rasterizeShape(style, w, h, scale);
    const QImage skiaImage = skia::SkiaRuntime::rasterize(*skia::makeShapePainter(style, w, h, scale));
    QCOMPARE(skiaImage.size(), reference.size());

    const Coverage a = coverageOf(reference);
    const Coverage b = coverageOf(skiaImage);
    QVERIFY(a.mass > 100);
    const QString info = QStringLiteral("qt mass %1 centroid (%2,%3) rgb (%4,%5,%6) | skia mass %7 "
                                        "centroid (%8,%9) rgb (%10,%11,%12)")
                             .arg(a.mass).arg(a.centroid.x()).arg(a.centroid.y())
                             .arg(a.r).arg(a.g).arg(a.b)
                             .arg(b.mass).arg(b.centroid.x()).arg(b.centroid.y())
                             .arg(b.r).arg(b.g).arg(b.b);
    QVERIFY2(qAbs(a.mass - b.mass) <= a.mass * 0.06, qPrintable(info));
    QVERIFY2(qAbs(a.centroid.x() - b.centroid.x()) <= 1.5, qPrintable(info));
    QVERIFY2(qAbs(a.centroid.y() - b.centroid.y()) <= 1.5, qPrintable(info));
    QVERIFY2(qAbs(a.r - b.r) <= 6 && qAbs(a.g - b.g) <= 6 && qAbs(a.b - b.b) <= 6, qPrintable(info));
}

void SkiaTest::shapePainterCacheKey()
{
    ShapeStyle style;
    style.kind = ShapeKind::Star;
    const auto a = skia::makeShapePainter(style, 100, 80, 1.0);
    const auto b = skia::makeShapePainter(style, 100, 80, 1.0);
    QVERIFY(a->cacheKey() != 0);
    QCOMPARE(a->cacheKey(), b->cacheKey());
    QCOMPARE(a->size(), QSize(100, 80));

    QVERIFY(skia::makeShapePainter(style, 100, 80, 0.5)->cacheKey() != a->cacheKey());
    QVERIFY(skia::makeShapePainter(style, 101, 80, 1.0)->cacheKey() != a->cacheKey());
    style.points = 7;
    QVERIFY(skia::makeShapePainter(style, 100, 80, 1.0)->cacheKey() != a->cacheKey());
    style.fill = Qt::green;
    const quint64 green = skia::makeShapePainter(style, 100, 80, 1.0)->cacheKey();
    style.strokeStyle = ShapeStrokeStyle::Dash;
    QVERIFY(skia::makeShapePainter(style, 100, 80, 1.0)->cacheKey() != green);
}

// DRIFT_VECTOR_RENDERER picks which backend builds the shape layer. Both must produce the same
// composited frame (coverage-wise), and only the Skia one touches the Skia paint counter.
void SkiaTest::backendSwitchSelectsShapeRenderer()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");

    ShapeStyle style;
    style.kind = ShapeKind::Heart;
    style.fill = QColor(200, 30, 90);
    style.stroke = Qt::white;
    style.strokeWidth = 4.0;
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

    qputenv("DRIFT_VECTOR_RENDERER", "qt");
    QCOMPARE(vectorBackend(), VectorBackend::Qt);
    const quint64 before = paints();
    const QImage qtFrame = compositor.compositeAt(0);
    QCOMPARE(paints(), before);

    qputenv("DRIFT_VECTOR_RENDERER", "skia");
    QCOMPARE(vectorBackend(), VectorBackend::Skia);
    const QImage skiaFrame = compositor.compositeAt(0);
    QCOMPARE(paints(), before + 1);
    // Same painter key: the second frame comes from the GPU cache, not a repaint.
    QVERIFY(!compositor.compositeAt(0).isNull());
    QCOMPARE(paints(), before + 1);
    qunsetenv("DRIFT_VECTOR_RENDERER");

    QCOMPARE(qtFrame.size(), skiaFrame.size());
    // Composited over a background, so compare the shape's colour footprint rather than alpha:
    // count pixels that are clearly the fill colour.
    auto fillPixels = [](const QImage &img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (near(img.pixel(x, y) | 0xff000000u, qRgb(200, 30, 90), 12))
                    ++n;
        return n;
    };
    const int qtCount = fillPixels(qtFrame);
    const int skiaCount = fillPixels(skiaFrame);
    QVERIFY(qtCount > 500);
    QVERIFY2(qAbs(qtCount - skiaCount) <= qtCount * 0.04,
             qPrintable(QStringLiteral("qt %1 skia %2").arg(qtCount).arg(skiaCount)));
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

void SkiaTest::textPainterMatchesQPainter_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<TextStyle>("style");
    QTest::addColumn<int>("word");

    TextStyle plain;
    plain.fontFamily = QStringLiteral("Inter");
    plain.pixelSize = 56;
    plain.color = Qt::white;
    QTest::newRow("plain") << QStringLiteral("Hello world") << plain << -1;

    TextStyle outlined = plain;
    outlined.color = QColor(250, 220, 40);
    outlined.outlineEnabled = true;
    outlined.outlineWidth = 4.0;
    outlined.outlineColor = QColor(20, 20, 120);
    outlined.shadowEnabled = true;
    outlined.shadowOffsetX = 6.0;
    outlined.shadowOffsetY = 6.0;
    outlined.shadowBlur = 5.0;
    outlined.shadowOpacity = 0.8;
    outlined.shadowColor = Qt::black;
    QTest::newRow("outline+shadow") << QStringLiteral("Outline shadow") << outlined << -1;

    TextStyle glowing = plain;
    glowing.glowEnabled = true;
    glowing.glowColor = QColor(255, 60, 60);
    glowing.glowRadius = 8.0;
    glowing.glowOpacity = 0.9;
    glowing.boxEnabled = true;
    glowing.boxColor = QColor(0, 0, 0, 160);
    glowing.boxPadding = 12.0;
    glowing.boxRadius = 8.0;
    QTest::newRow("glow+box") << QStringLiteral("Glow box") << glowing << -1;

    TextStyle ruled = plain;
    ruled.wordHighlight.enabled = true;
    ruled.wordHighlight.color = QColor(40, 120, 220);
    ruled.wordHighlight.padding = 6.0;
    ruled.wordHighlight.radius = 4.0;
    ruled.underlineEnabled = true;
    ruled.underlineColor = QColor(255, 255, 0);
    ruled.underlineWidth = 4.0;
    ruled.underlineOffset = 8.0;
    QTest::newRow("pills+underline") << QStringLiteral("Pills and rules") << ruled << -1;

    TextStyle accented = plain;
    accented.accent.rule = WordAccentRule::EveryNth;
    accented.accent.n = 2;
    accented.accent.colorEnabled = true;
    accented.accent.color = QColor(255, 120, 0);
    accented.accent.sizeScale = 1.3;
    accented.accent.outlineEnabled = true;
    accented.accent.outlineWidth = 2.0;
    accented.accent.outlineColor = Qt::black;
    QTest::newRow("accent") << QStringLiteral("one two three four") << accented << -1;

    TextStyle karaoke = plain;
    karaoke.accent.rule = WordAccentRule::Karaoke;
    karaoke.accent.colorEnabled = true;
    karaoke.accent.color = QColor(0, 255, 0);
    karaoke.accent.highlight.enabled = true;
    karaoke.accent.highlight.color = QColor(120, 0, 200);
    QTest::newRow("karaoke") << QStringLiteral("sing along now") << karaoke << 1;

    TextStyle wrapped = plain;
    wrapped.wordWrap = true;
    wrapped.align = TextAlign::Right;
    wrapped.valign = TextVAlign::Bottom;
    wrapped.lineHeight = 1.2;
    wrapped.letterSpacing = 2.0;
    wrapped.italic = true;
    QTest::newRow("wrap+right+bottom") << QStringLiteral("the quick brown fox jumps over the lazy dog again") << wrapped << -1;
}

// Same layout, two painters: the block must land in the same place with the same amount of ink
// and the same overall colour. Antialiasing and the gaussian-vs-box blur keep this from being a
// pixel comparison.
void SkiaTest::textPainterMatchesQPainter()
{
    QFETCH(QString, text);
    QFETCH(TextStyle, style);
    QFETCH(int, word);
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});

    const Clip clip = textClip(style, text);
    const QRectF layout(0, 0, 420, 160);
    const double scale = 0.75;
    const TextRasterResult qt = rasterizeText(clip, text, layout, scale, word);
    QRectF skiaRect;
    const QImage sk = skiaText(clip, text, layout, scale, word, &skiaRect);
    QVERIFY(!qt.image.isNull());
    QVERIFY(!sk.isNull());
    QCOMPARE(sk.size(), qt.image.size());
    QCOMPARE(skiaRect, qt.rect);

    QString why;
    QVERIFY2(inkClose(inkOf(qt.image), inkOf(sk), &why), qPrintable(why));
}

void SkiaTest::textSpanPaintersMatchRasterSpans()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 48;
    style.color = Qt::white;
    style.outlineEnabled = true;
    style.outlineWidth = 3.0;
    style.boxEnabled = true;
    style.boxColor = QColor(0, 0, 0, 180);
    style.boxPadding = 10.0;
    style.animIn.kind = TextAnimKind::SlideUp;
    style.animIn.unit = TextAnimUnit::Word;
    const QString text = QStringLiteral("stagger these words");
    const Clip clip = textClip(style, text);
    const QRectF layout(30, 20, 400, 120);

    for (TextAnimUnit unit : {TextAnimUnit::Word, TextAnimUnit::Character, TextAnimUnit::Line}) {
        const QList<TextSpanRaster> qt = rasterizeTextSpans(clip, text, layout, 1.0, unit);
        const QList<skia::TextSpanPainter> sk = skia::makeTextSpanPainters(clip, text, layout, 1.0, unit);
        QCOMPARE(sk.size(), qt.size());
        for (int i = 0; i < qt.size(); ++i) {
            QCOMPARE(sk[i].index, qt[i].index);
            QCOMPARE(sk[i].count, qt[i].count);
            QVERIFY2(qAbs(sk[i].rect.x() - qt[i].rect.x()) <= 2 && qAbs(sk[i].rect.y() - qt[i].rect.y()) <= 2
                         && qAbs(sk[i].rect.width() - qt[i].rect.width()) <= 2
                         && qAbs(sk[i].rect.height() - qt[i].rect.height()) <= 2,
                     qPrintable(QStringLiteral("span %1: qt %2,%3 %4x%5 skia %6,%7 %8x%9").arg(i)
                                    .arg(qt[i].rect.x()).arg(qt[i].rect.y()).arg(qt[i].rect.width()).arg(qt[i].rect.height())
                                    .arg(sk[i].rect.x()).arg(sk[i].rect.y()).arg(sk[i].rect.width()).arg(sk[i].rect.height())));
            const QImage image = skia::SkiaRuntime::rasterize(*sk[i].painter);
            QCOMPARE(image.size(), qt[i].image.size());
            QString why;
            QVERIFY2(inkClose(inkOf(qt[i].image), inkOf(image), &why), qPrintable(QStringLiteral("span %1: %2").arg(i).arg(why)));
        }
    }
    QVERIFY(skia::makeTextSpanPainters(clip, text, layout, 1.0, TextAnimUnit::Block).isEmpty());
}

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
    other.textStyle.color = Qt::red;
    QVERIFY(skia::makeTextPainter(other, QStringLiteral("cache me"), layout, 1.0).painter->cacheKey() != a.painter->cacheKey());

    const QList<skia::TextSpanPainter> spans =
        skia::makeTextSpanPainters(clip, QStringLiteral("cache me"), layout, 1.0, TextAnimUnit::Word);
    QCOMPARE(spans.size(), 2);
    QVERIFY(spans[0].painter->cacheKey() != spans[1].painter->cacheKey());
    QVERIFY(spans[0].painter->cacheKey() != 0);
}

void SkiaTest::emojiTextMatchesQPainter()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    reloadEmojiCatalog({QString::fromUtf8(DRIFT_TEST_EMOJI_FONT_DIR)});
    if (emojiFontFamily().isEmpty())
        QSKIP("No emoji font available");
    // Plain style: with an outline or shadow Skia treats emoji like glyphs (ring, silhouette in
    // the shadow — see emojiOutlineDrawsARing) where QPainter leaves them bare.
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 56;
    style.color = Qt::white;
    const QString text = QString::fromUtf8("Party \xF0\x9F\x8E\x89 time \xF0\x9F\x98\x80");
    const Clip clip = textClip(style, text);
    const QRectF layout(0, 0, 420, 120);
    const TextRasterResult qt = rasterizeText(clip, text, layout, 1.0);
    const QImage sk = skiaText(clip, text, layout, 1.0, -1, nullptr);
    QVERIFY(!qt.image.isNull() && !sk.isNull());
    QString why;
    QVERIFY2(inkClose(inkOf(qt.image), inkOf(sk), &why), qPrintable(why));
    QVERIFY(inkOf(sk).count > 500);
}

// DRIFT_VECTOR_RENDERER selects the text backend too; both composite to the same frame footprint.
void SkiaTest::backendSwitchSelectsTextRenderer()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    Project project;
    project.setResolution(320, 120);
    project.tracks().clear();
    project.tracks().append(Track{.type = TrackType::Text});
    Clip clip;
    clip.id = QStringLiteral("t");
    clip.type = ClipType::Text;
    clip.textContent = QStringLiteral("Switch");
    clip.textStyle.pixelSize = 64;
    clip.textStyle.color = QColor(255, 0, 0);
    clip.timelineStart = 0;
    clip.timelineDuration = secondsToUs(2.0);
    clip.transformX.setKeyframe(0, 0.0);
    clip.transformY.setKeyframe(0, 0.0);
    clip.transformW.setKeyframe(0, 320.0);
    clip.transformH.setKeyframe(0, 120.0);
    project.tracks()[0].clips.append(clip);
    FrameCompositor compositor;
    compositor.setProject(&project);

    auto redCount = [](const QImage &img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qRed(img.pixel(x, y)) > 180 && qGreen(img.pixel(x, y)) < 90 && qBlue(img.pixel(x, y)) < 90)
                    ++n;
        return n;
    };
    qputenv("DRIFT_VECTOR_RENDERER", "qt");
    const int qt = redCount(compositor.compositeAt(secondsToUs(0.5)));
    qputenv("DRIFT_VECTOR_RENDERER", "skia");
    const int sk = redCount(compositor.compositeAt(secondsToUs(0.5)));
    qunsetenv("DRIFT_VECTOR_RENDERER");
    QVERIFY(qt > 300);
    QVERIFY2(qAbs(qt - sk) <= qMax(40, int(qt * 0.12)), qPrintable(QStringLiteral("qt %1 skia %2").arg(qt).arg(sk)));
}

// A keyframed pixelSize is baked per frame: the painter stops caching and the rendered block is
// taller later in the clip.
void SkiaTest::keyframedTextGrowsOverTime()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    Clip clip;
    clip.type = ClipType::Text;
    clip.textContent = QStringLiteral("Grow");
    clip.textStyle.pixelSize = 24;
    clip.textStyle.color = QColor(255, 0, 0);
    clip.textStyle.keyframes[QStringLiteral("pixelSize")].setKeyframe(0, 24.0);
    clip.textStyle.keyframes[QStringLiteral("pixelSize")].setKeyframe(secondsToUs(2.0), 72.0);
    const QRectF layout(0, 0, 400, 160);

    Clip t0 = clip;
    t0.textStyle = clip.textStyle.resolvedAt(0);
    Clip t2 = clip;
    t2.textStyle = clip.textStyle.resolvedAt(secondsToUs(2.0));
    const skia::TextPainterResult a = skia::makeTextPainter(t0, clip.textContent, layout, 1.0);
    const skia::TextPainterResult b = skia::makeTextPainter(t2, clip.textContent, layout, 1.0);
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
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 90;
    style.color = QColor(255, 0, 0);
    style.colorSecondary = QColor(0, 0, 255);
    style.fillKind = TextFillKind::LinearGradient;
    style.gradientAngle = 90.0;
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
    // solid raster would.
    Clip solid = clip;
    solid.textStyle.fillKind = TextFillKind::Solid;
    const TextRasterResult qt = rasterizeText(solid, text, layout, 1.0);
    QCOMPARE(image.size(), qt.image.size());
    QString why;
    Ink a = inkOf(qt.image);
    Ink b = ink;
    a.r = a.g = a.b = b.r = b.g = b.b = 0; // colour deliberately differs
    QVERIFY2(inkClose(a, b, &why), qPrintable(why));
}

// A bent line rises above where the straight one sits, keeps every glyph, and reserves the rise
// in the bleed so nothing is clipped.
void SkiaTest::pathBendArchesTheLine()
{
    reloadFontCatalog({QString::fromUtf8(DRIFT_TEST_FONTS_DIR)});
    TextStyle style;
    style.fontFamily = QStringLiteral("Inter");
    style.pixelSize = 40;
    style.color = Qt::white;
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
    reloadEmojiCatalog({QString::fromUtf8(DRIFT_TEST_EMOJI_FONT_DIR)});
    if (emojiFontFamily().isEmpty())
        QSKIP("No emoji font available");
    TextStyle style;
    style.pixelSize = 72;
    style.color = Qt::white;
    const QString text = QString::fromUtf8("\xF0\x9F\x98\x80");
    const QRectF layout(0, 0, 200, 120);
    const Ink plain = inkOf(skiaText(textClip(style, text), text, layout, 1.0, -1, nullptr));
    QVERIFY(plain.count > 300);

    style.outlineEnabled = true;
    style.outlineWidth = 6.0;
    style.outlineColor = QColor(0, 255, 0);
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

    style.outlineEnabled = false;
    style.shadowEnabled = true;
    style.shadowOffsetX = 14.0;
    style.shadowOffsetY = 0.0;
    style.shadowBlur = 0.0;
    style.shadowOpacity = 1.0;
    style.shadowColor = QColor(0, 0, 255);
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

QTEST_MAIN(SkiaTest)
#include "tst_skia.moc"
