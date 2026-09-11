#include <QtTest>

#include <QFile>
#include <QThread>
#include <QVector>

#include "core/Project.h"
#include "engine/FrameCompositor.h"
#include "engine/GpuCompositor.h"
#include "engine/VectorClipRenderer.h"
#include "engine/VectorInspect.h"

using namespace drift;

namespace {

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(DRIFT_TEST_DATA_DIR "/vector/") + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

VectorSource slideSource()
{
    VectorSource source;
    source.kind = VectorKind::Lottie;
    source.source = QString::fromUtf8(fixture(QStringLiteral("slide.json")));
    return source;
}

// Straight-alpha view of the renderer's premultiplied output.
QImage straight(const QImage &image)
{
    return image.convertToFormat(QImage::Format_RGBA8888);
}

bool isColor(QRgb p, int r, int g, int b, int tol = 8)
{
    return qAlpha(p) > 200 && qAbs(qRed(p) - r) <= tol && qAbs(qGreen(p) - g) <= tol
        && qAbs(qBlue(p) - b) <= tol;
}

// Bounding box of pixels matching a colour; invalid when none do.
QRect boundsOf(const QImage &image, int r, int g, int b)
{
    int minX = image.width(), minY = image.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!isColor(image.pixel(x, y), r, g, b))
                continue;
            minX = qMin(minX, x);
            minY = qMin(minY, y);
            maxX = qMax(maxX, x);
            maxY = qMax(maxY, y);
        }
    }
    return maxX < 0 ? QRect() : QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

QImage renderAt(const VectorSource &source, double seconds, const QSize &size)
{
    vec::RenderRequest request;
    request.source = source;
    request.animUs = secondsToUs(seconds);
    request.size = size;
    return straight(vec::renderToImage(request));
}

} // namespace

class VectorTest : public QObject
{
    Q_OBJECT

private slots:
    void inspectLottieReportsDocument();
    void inspectReportsExpressions();
    void inspectRejectsGarbage();
    void inspectSvgReportsSizeAndSmil();
    void probeFillsSource();
    void framesFollowTime();
    void slotOverrideRecolours();
    void fitModes();
    void loopModes();
    void svgStillRenders();
    void concurrentRenders();
    void thumbnailIsMidAnimation();
    void compositorRendersVectorClip();
    void compositorFollowsSpeedAndReverse();
};

void VectorTest::inspectLottieReportsDocument()
{
    const vec::InspectReport report = vec::inspectVector(fixture(QStringLiteral("slide.json")), VectorKind::Lottie);
    QVERIFY2(report.ok, qPrintable(report.error));
    QCOMPARE(report.kind, VectorKind::Lottie);
    QCOMPARE(report.version, QStringLiteral("5.12.1"));
    QCOMPARE(report.title, QStringLiteral("Slide"));
    QCOMPARE(report.fps, 30.0);
    QCOMPARE(report.durationUs, secondsToUs(2.0));
    QCOMPARE(report.width, 200);
    QCOMPARE(report.height, 100);
    QCOMPARE(report.layers.size(), 1);
    QCOMPARE(report.layers[0].name, QStringLiteral("Box"));
    QCOMPARE(report.layers[0].outSec, 2.0);
    QCOMPARE(report.slotInfos.size(), 1);
    QCOMPARE(report.slotInfos[0].id, QStringLiteral("accent"));
    QCOMPARE(report.slotInfos[0].type, VectorSlotValue::Type::Color);
    QCOMPARE(report.markers.size(), 2);
    QCOMPARE(report.markers[0].name, QStringLiteral("start"));
    QCOMPARE(report.markers[1].name, QStringLiteral("end"));
    QVERIFY(report.markers[1].t0 > 1.4 && report.markers[1].t0 < 1.6);
    QVERIFY(report.expressions.isEmpty());
    QVERIFY2(report.unsupported.isEmpty(), qPrintable(report.unsupported.join(QLatin1String("; "))));
    bool sawFill = false;
    for (const vec::VectorNamedProperty &p : report.namedProperties)
        sawFill = sawFill || (p.node == QStringLiteral("Fill") && p.type == QStringLiteral("color"));
    QVERIFY(sawFill);

    const QJsonObject json = report.toJson();
    QCOMPARE(json.value(QStringLiteral("durationSec")).toDouble(), 2.0);
    QCOMPARE(json.value(QStringLiteral("slots")).toArray().size(), 1);
    QCOMPARE(json.value(QStringLiteral("layers")).toArray().at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Box"));
}

void VectorTest::inspectReportsExpressions()
{
    QByteArray json = fixture(QStringLiteral("slide.json"));
    json.replace("\"r\": { \"a\": 0, \"k\": 0 },\n        \"p\"",
                 "\"r\": { \"a\": 0, \"k\": 0, \"x\": \"var $bm_rt = time * 90;\" },\n        \"p\"");
    QVERIFY(json.contains("$bm_rt"));
    const vec::InspectReport report = vec::inspectVector(json, VectorKind::Lottie);
    QVERIFY2(report.ok, qPrintable(report.error));
    QCOMPARE(report.expressions.size(), 1);
    QCOMPARE(report.expressions[0], QStringLiteral("layers/Box/ks/r"));
    QVERIFY(!report.hints.isEmpty());
}

void VectorTest::inspectRejectsGarbage()
{
    QVERIFY(!vec::inspectVector(QByteArray("not json at all"), VectorKind::Lottie).ok);
    QVERIFY(!vec::inspectVector(QByteArray("{\"hello\":1}"), VectorKind::Lottie).ok);
    QVERIFY(!vec::inspectVector(QByteArray(), VectorKind::Lottie).ok);
    QVERIFY(!vec::inspectVector(QByteArray("<html><body/></html>"), VectorKind::Svg).ok);

    VectorSource bad;
    bad.source = QStringLiteral("{\"layers\": 3}");
    vec::RenderRequest request;
    request.source = bad;
    request.size = QSize(50, 50);
    QVERIFY(vec::renderToImage(request).isNull());
    QVERIFY(!vec::makePainter(request));

    QCOMPARE(vec::detectVectorKind(QByteArray("  <svg/>")), VectorKind::Svg);
    QCOMPARE(vec::detectVectorKind(QByteArray("{}")), VectorKind::Lottie);
}

void VectorTest::inspectSvgReportsSizeAndSmil()
{
    const vec::InspectReport still = vec::inspectVector(fixture(QStringLiteral("still.svg")), VectorKind::Svg);
    QVERIFY2(still.ok, qPrintable(still.error));
    QCOMPARE(still.width, 200);
    QCOMPARE(still.height, 100);
    QCOMPARE(still.durationUs, TimeUs(0));
    QVERIFY(still.unsupported.isEmpty());

    const QByteArray animated =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 40 20\" font-family=\"Inter\">"
        "<rect width=\"10\" height=\"10\"><animate attributeName=\"x\" from=\"0\" to=\"30\" dur=\"1s\"/></rect>"
        "<circle r=\"3\"><animateTransform attributeName=\"transform\" type=\"rotate\" from=\"0\" to=\"360\" dur=\"2s\"/></circle>"
        "</svg>";
    const vec::InspectReport report = vec::inspectVector(animated, VectorKind::Svg);
    QVERIFY2(report.ok, qPrintable(report.error));
    QCOMPARE(report.width, 40);
    QCOMPARE(report.height, 20);
    QCOMPARE(report.unsupported.size(), 2);
    QVERIFY(report.unsupported.join(QLatin1Char(' ')).contains(QStringLiteral("animateTransform")));
    QCOMPARE(report.fonts, QStringList{QStringLiteral("Inter")});
    QVERIFY(!report.hints.isEmpty());
}

void VectorTest::probeFillsSource()
{
    VectorSource source = slideSource();
    QString error;
    QVERIFY2(vec::probeVectorSource(source, &error), qPrintable(error));
    QCOMPARE(source.hash, vectorSourceHash(source.source.toUtf8()));
    QCOMPARE(source.width, 200);
    QCOMPARE(source.height, 100);
    QCOMPARE(source.fps, 30.0);
    QCOMPARE(source.durationUs, secondsToUs(2.0));
    QCOMPARE(source.title, QStringLiteral("Slide"));

    VectorSource file;
    file.kind = VectorKind::Svg;
    file.path = QStringLiteral(DRIFT_TEST_DATA_DIR "/vector/still.svg");
    QVERIFY2(vec::probeVectorSource(file, &error), qPrintable(error));
    QCOMPARE(file.width, 200);
    QCOMPARE(file.durationUs, TimeUs(0));

    VectorSource missing;
    missing.path = QStringLiteral("/nonexistent/x.json");
    QVERIFY(!vec::probeVectorSource(missing, &error));
    QVERIFY(!error.isEmpty());
}

// The box slides from x=30 to x=170 over two seconds: the red bounds must move right with time
// and stay vertically centred.
void VectorTest::framesFollowTime()
{
    const VectorSource source = slideSource();
    const QImage t0 = renderAt(source, 0.0, QSize(200, 100));
    const QImage t1 = renderAt(source, 1.0, QSize(200, 100));
    QCOMPARE(t0.size(), QSize(200, 100));
    const QRect b0 = boundsOf(t0, 255, 0, 0);
    const QRect b1 = boundsOf(t1, 255, 0, 0);
    QVERIFY2(b0.isValid(), "no red at t=0");
    QVERIFY2(b1.isValid(), "no red at t=1");
    QVERIFY2(qAbs(b0.center().x() - 30) <= 2, qPrintable(QString::number(b0.center().x())));
    QVERIFY2(qAbs(b1.center().x() - 100) <= 2, qPrintable(QString::number(b1.center().x())));
    QVERIFY(qAbs(b0.center().y() - 50) <= 2 && qAbs(b1.center().y() - 50) <= 2);
    QVERIFY(qAbs(b0.width() - 40) <= 2 && qAbs(b0.height() - 40) <= 2);
    QVERIFY(qAlpha(t0.pixel(150, 50)) == 0);
}

void VectorTest::slotOverrideRecolours()
{
    VectorSource source = slideSource();
    source.slotValues.insert(QStringLiteral("accent"), VectorSlotValue::fromColor(QColor(0, 0, 255)));
    const QImage blue = renderAt(source, 0.0, QSize(200, 100));
    QVERIFY(boundsOf(blue, 0, 0, 255).isValid());
    QVERIFY(!boundsOf(blue, 255, 0, 0).isValid());

    // The plain document is a different cache entry and stays red.
    const QImage red = renderAt(slideSource(), 0.0, QSize(200, 100));
    QVERIFY(boundsOf(red, 255, 0, 0).isValid());

    // A mismatched type is ignored, not applied.
    VectorSource wrong = slideSource();
    wrong.slotValues.insert(QStringLiteral("accent"), VectorSlotValue::fromScalar(3.0));
    QVERIFY(boundsOf(renderAt(wrong, 0.0, QSize(200, 100)), 255, 0, 0).isValid());
}

// A 200×100 document into a 200×200 layer. Contain letterboxes to the middle band, cover scales
// ×2 and clips the sides, stretch doubles the height of everything.
void VectorTest::fitModes()
{
    VectorSource source = slideSource();
    source.fit = VectorFit::Contain;
    const QRect contain = boundsOf(renderAt(source, 0.0, QSize(200, 200)), 255, 0, 0);
    QVERIFY(contain.isValid());
    QVERIFY2(qAbs(contain.center().y() - 100) <= 2 && qAbs(contain.width() - 40) <= 2 && qAbs(contain.height() - 40) <= 2,
             qPrintable(QStringLiteral("%1,%2 %3x%4").arg(contain.x()).arg(contain.y()).arg(contain.width()).arg(contain.height())));

    source.fit = VectorFit::Cover;
    // Scale 2 with the document centred, so at t=0 the box (doc x 10..50) is off the left edge;
    // at t=1 its centre (100,50) lands on the layer centre as an 80 px square.
    QVERIFY(!boundsOf(renderAt(source, 0.0, QSize(200, 200)), 255, 0, 0).isValid());
    const QRect cover = boundsOf(renderAt(source, 1.0, QSize(200, 200)), 255, 0, 0);
    QVERIFY(cover.isValid());
    QVERIFY2(qAbs(cover.width() - 80) <= 2 && qAbs(cover.height() - 80) <= 2
                 && qAbs(cover.center().x() - 100) <= 2 && qAbs(cover.center().y() - 100) <= 2,
             qPrintable(QStringLiteral("%1,%2 %3x%4").arg(cover.x()).arg(cover.y()).arg(cover.width()).arg(cover.height())));

    source.fit = VectorFit::Stretch;
    const QRect stretch = boundsOf(renderAt(source, 0.0, QSize(200, 200)), 255, 0, 0);
    QVERIFY(stretch.isValid());
    QVERIFY2(qAbs(stretch.width() - 40) <= 2 && qAbs(stretch.height() - 80) <= 2 && qAbs(stretch.center().x() - 30) <= 2,
             qPrintable(QStringLiteral("%1,%2 %3x%4").arg(stretch.x()).arg(stretch.y()).arg(stretch.width()).arg(stretch.height())));
}

void VectorTest::loopModes()
{
    VectorSource source = slideSource();
    source.loop = VectorLoop::Hold;
    QVERIFY(qAbs(boundsOf(renderAt(source, 5.0, QSize(200, 100)), 255, 0, 0).center().x() - 170) <= 2);
    source.loop = VectorLoop::Loop;
    QVERIFY(qAbs(boundsOf(renderAt(source, 5.0, QSize(200, 100)), 255, 0, 0).center().x() - 100) <= 2);
    source.loop = VectorLoop::PingPong;
    QVERIFY(qAbs(boundsOf(renderAt(source, 3.0, QSize(200, 100)), 255, 0, 0).center().x() - 100) <= 2);
    source.loop = VectorLoop::Hide;
    QVERIFY(renderAt(source, 5.0, QSize(200, 100)).isNull());
    QVERIFY(!renderAt(source, 1.0, QSize(200, 100)).isNull());
    // The offset shifts where the animation starts.
    source.loop = VectorLoop::Hold;
    source.startOffsetUs = secondsToUs(1.0);
    QVERIFY(qAbs(boundsOf(renderAt(source, 0.0, QSize(200, 100)), 255, 0, 0).center().x() - 100) <= 2);
}

void VectorTest::svgStillRenders()
{
    VectorSource source;
    source.kind = VectorKind::Svg;
    source.path = QStringLiteral(DRIFT_TEST_DATA_DIR "/vector/still.svg");
    const QImage image = renderAt(source, 0.0, QSize(200, 100));
    QCOMPARE(image.size(), QSize(200, 100));
    QVERIFY(isColor(image.pixel(50, 50), 0, 255, 0));
    QVERIFY(isColor(image.pixel(150, 50), 0, 0, 255));
    QVERIFY(qAlpha(image.pixel(199, 2)) == 0);
    // Time is irrelevant for a still, and a still painter is cacheable.
    QCOMPARE(renderAt(source, 7.0, QSize(200, 100)), image);
    vec::RenderRequest request;
    request.source = source;
    request.size = QSize(200, 100);
    QVERIFY(vec::makePainter(request)->cacheKey() != 0);
    request.source = slideSource();
    QCOMPARE(vec::makePainter(request)->cacheKey(), quint64(0));

    // Contain into a square: the drawing sits in the middle band.
    source.fit = VectorFit::Contain;
    const QImage square = renderAt(source, 0.0, QSize(100, 100));
    QVERIFY(isColor(square.pixel(25, 50), 0, 255, 0));
    QVERIFY(qAlpha(square.pixel(25, 10)) == 0);
}

// Skottie animations are not thread-safe; the renderer serialises per document. Eight threads
// hammering the same document at different times must all get the frame they asked for.
void VectorTest::concurrentRenders()
{
    const VectorSource source = slideSource();
    const QImage expect0 = renderAt(source, 0.0, QSize(200, 100));
    const QImage expect1 = renderAt(source, 1.0, QSize(200, 100));
    QVector<QThread *> threads;
    std::atomic<int> failures{0};
    for (int i = 0; i < 8; ++i) {
        QThread *thread = QThread::create([&, i] {
            for (int n = 0; n < 20; ++n) {
                const bool odd = (i + n) % 2 == 1;
                const QImage got = renderAt(source, odd ? 1.0 : 0.0, QSize(200, 100));
                if (got != (odd ? expect1 : expect0))
                    ++failures;
            }
        });
        threads.append(thread);
        thread->start();
    }
    for (QThread *thread : threads) {
        thread->wait();
        delete thread;
    }
    QCOMPARE(failures.load(), 0);
}

void VectorTest::thumbnailIsMidAnimation()
{
    const QImage thumb = straight(vec::renderThumbnail(slideSource(), QSize(100, 50), 0.5));
    QCOMPARE(thumb.size(), QSize(100, 50));
    const QRect box = boundsOf(thumb, 255, 0, 0);
    QVERIFY(box.isValid());
    QVERIFY2(qAbs(box.center().x() - 50) <= 2, qPrintable(QString::number(box.center().x())));
}

namespace {

Project vectorProject(VectorSource source, double speed, bool reverse)
{
    Project project;
    project.setResolution(200, 100);
    project.tracks().clear();
    project.tracks().append(Track{.type = TrackType::Shape});
    Clip clip;
    clip.id = QStringLiteral("vec");
    clip.type = ClipType::Vector;
    clip.timelineStart = 0;
    clip.timelineDuration = secondsToUs(2.0);
    clip.srcIn = 0;
    clip.srcOut = static_cast<TimeUs>(secondsToUs(2.0) * speed);
    clip.speed = speed;
    clip.reverse = reverse;
    clip.vector = std::move(source);
    clip.transformX.setKeyframe(0, 0.0);
    clip.transformY.setKeyframe(0, 0.0);
    clip.transformW.setKeyframe(0, 200.0);
    clip.transformH.setKeyframe(0, 100.0);
    project.tracks()[0].clips.append(clip);
    return project;
}

int redCentreX(const QImage &frame)
{
    return boundsOf(frame.convertToFormat(QImage::Format_RGBA8888), 255, 0, 0).center().x();
}

} // namespace

void VectorTest::compositorRendersVectorClip()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");
    // File-backed, so the .json path is on the clip: the decoders must leave it alone.
    VectorSource source;
    source.kind = VectorKind::Lottie;
    source.path = QStringLiteral(DRIFT_TEST_DATA_DIR "/vector/slide.json");
    const Project project = vectorProject(source, 1.0, false);
    FrameCompositor compositor;
    compositor.setProject(&project);
    const QImage t0 = compositor.compositeAt(0);
    const QImage t1 = compositor.compositeAt(secondsToUs(1.0));
    QCOMPARE(t0.size(), QSize(200, 100));
    QVERIFY2(qAbs(redCentreX(t0) - 30) <= 2, qPrintable(QString::number(redCentreX(t0))));
    QVERIFY2(qAbs(redCentreX(t1) - 100) <= 2, qPrintable(QString::number(redCentreX(t1))));
}

void VectorTest::compositorFollowsSpeedAndReverse()
{
    if (!GpuCompositor::isAvailable())
        QSKIP("GL unavailable");
    {
        VectorSource source = slideSource();
        source.loop = VectorLoop::Hold;
        // 2× speed: half a second in reaches the animation's one-second mark.
        const Project fast = vectorProject(source, 2.0, false);
        FrameCompositor compositor;
        compositor.setProject(&fast);
        QVERIFY2(qAbs(redCentreX(compositor.compositeAt(secondsToUs(0.5))) - 100) <= 2,
                 qPrintable(QString::number(redCentreX(compositor.compositeAt(secondsToUs(0.5))))));
    }
    {
        // Reversed: the clip starts at the animation's end.
        const Project back = vectorProject(slideSource(), 1.0, true);
        FrameCompositor compositor;
        compositor.setProject(&back);
        QVERIFY2(qAbs(redCentreX(compositor.compositeAt(0)) - 170) <= 2,
                 qPrintable(QString::number(redCentreX(compositor.compositeAt(0)))));
        QVERIFY2(qAbs(redCentreX(compositor.compositeAt(secondsToUs(1.0))) - 100) <= 2,
                 qPrintable(QString::number(redCentreX(compositor.compositeAt(secondsToUs(1.0))))));
    }
}

QTEST_MAIN(VectorTest)
#include "tst_vector.moc"
