#include <QtTest>

#include <QOpenGLExtraFunctions>

#include "engine/GlRuntime.h"
#include "engine/GpuCompositor.h"
#include "engine/GpuEffectExecutor.h"
#include "engine/SkiaRuntime.h"
#include "engine/VectorPainter.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

using namespace drift;

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

QTEST_MAIN(SkiaTest)
#include "tst_skia.moc"
