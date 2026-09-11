#include <QtTest>

#include <QOpenGLExtraFunctions>

#include "engine/GlRuntime.h"
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
    QSize size() const override { return QSize(80, 40); }
    quint64 cacheKey() const override { return 0; }
    void paint(SkCanvas &canvas) const override
    {
        SkPaint opaque;
        opaque.setColor(SK_ColorRED);
        canvas.drawRect(SkRect::MakeXYWH(10, 10, 20, 20), opaque);
        SkPaint half;
        half.setColor(SkColorSetARGB(128, 255, 0, 0));
        canvas.drawRect(SkRect::MakeXYWH(40, 10, 20, 20), half);
    }
};

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

QTEST_MAIN(SkiaTest)
#include "tst_skia.moc"
