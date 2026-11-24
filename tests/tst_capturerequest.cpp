#include <QtTest>

#include "engine/capture/CaptureRequest.h"

using namespace drift;

class CaptureRequestTest : public QObject
{
    Q_OBJECT

private slots:
    void sourceRoundTrips();
    void fpsIsClamped();
    void regionIsClippedToTheScreen();
    void regionDimensionsBecomeEven();
    void regionRoundsDownAtTheScreenEdge();
    void tinyRegionIsRefused();
    void offscreenRegionIsRefused();
    void invalidInputsAreRefused();
    void problemNamesWhatIsMissing();
};

void CaptureRequestTest::sourceRoundTrips()
{
    for (auto source : {CaptureSource::Screen, CaptureSource::Window, CaptureSource::Region,
                        CaptureSource::Camera}) {
        const QString text = captureSourceToString(source);
        QVERIFY(!text.isEmpty());
        QCOMPARE(captureSourceFromString(text), std::optional<CaptureSource>(source));
    }
    QVERIFY(!captureSourceFromString(QStringLiteral("display")).has_value());
    QVERIFY(!captureSourceFromString(QString()).has_value());
}

void CaptureRequestTest::fpsIsClamped()
{
    QCOMPARE(clampCaptureFps(30), 30);
    QCOMPARE(clampCaptureFps(0), 1);
    QCOMPARE(clampCaptureFps(-5), 1);
    QCOMPARE(clampCaptureFps(500), 120);
}

void CaptureRequestTest::regionIsClippedToTheScreen()
{
    // A drag that ran off the right edge asks for pixels that do not exist.
    const auto region = normalizeCaptureRegion(QRect(1800, 100, 400, 200), QSize(1920, 1080));
    QVERIFY(region.has_value());
    QCOMPARE(region->left(), 1800);
    QCOMPARE(region->width(), 120);
    QCOMPARE(region->height(), 200);
}

void CaptureRequestTest::regionDimensionsBecomeEven()
{
    // H.264 chroma is half resolution, so an odd width or height is rejected by the encoder.
    const auto region = normalizeCaptureRegion(QRect(10, 20, 101, 203), QSize(1920, 1080));
    QVERIFY(region.has_value());
    QCOMPARE(region->width(), 100);
    QCOMPARE(region->height(), 202);
    // The corner does not move: only the size is adjusted, so what the user framed stays framed.
    QCOMPARE(region->topLeft(), QPoint(10, 20));
}

void CaptureRequestTest::regionRoundsDownAtTheScreenEdge()
{
    // Rounding an odd width *up* here would push the rectangle back past the screen edge that
    // clipping had just brought it inside.
    const auto region = normalizeCaptureRegion(QRect(0, 0, 1920, 1080), QSize(1921, 1081));
    QVERIFY(region.has_value());
    QCOMPARE(region->width(), 1920);
    QCOMPARE(region->height(), 1080);

    const auto odd = normalizeCaptureRegion(QRect(0, 0, 1921, 1081), QSize(1921, 1081));
    QVERIFY(odd.has_value());
    QCOMPARE(odd->width(), 1920);
    QCOMPARE(odd->height(), 1080);
    QVERIFY(odd->right() < 1921);
}

void CaptureRequestTest::tinyRegionIsRefused()
{
    // A stray click rather than a drag. Recording a 1x2 sliver is worse than refusing.
    QVERIFY(!normalizeCaptureRegion(QRect(5, 5, 1, 2), QSize(1920, 1080)).has_value());
    QVERIFY(!normalizeCaptureRegion(QRect(5, 5, 15, 400), QSize(1920, 1080)).has_value());
    // Exactly at the floor is fine.
    QVERIFY(normalizeCaptureRegion(QRect(5, 5, 16, 16), QSize(1920, 1080)).has_value());
    // A caller may lower the floor, but never below what an encoder can take.
    const auto small = normalizeCaptureRegion(QRect(0, 0, 4, 4), QSize(100, 100), 1);
    QVERIFY(small.has_value());
    QCOMPARE(small->width(), 4);
}

void CaptureRequestTest::offscreenRegionIsRefused()
{
    QVERIFY(!normalizeCaptureRegion(QRect(2000, 100, 400, 200), QSize(1920, 1080)).has_value());
    QVERIFY(!normalizeCaptureRegion(QRect(-500, 0, 400, 200), QSize(1920, 1080)).has_value());
}

void CaptureRequestTest::invalidInputsAreRefused()
{
    QVERIFY(!normalizeCaptureRegion(QRect(), QSize(1920, 1080)).has_value());
    QVERIFY(!normalizeCaptureRegion(QRect(0, 0, 100, 100), QSize()).has_value());
    QVERIFY(!normalizeCaptureRegion(QRect(0, 0, -100, 100), QSize(1920, 1080)).has_value());
}

void CaptureRequestTest::problemNamesWhatIsMissing()
{
    CaptureRequest request;
    // No output path is the first thing checked: nothing else matters if there is nowhere to write.
    QVERIFY(!captureRequestProblem(request).isEmpty());

    request.outputPath = QStringLiteral("/tmp/out.mp4");
    QVERIFY(captureRequestProblem(request).isEmpty()); // a whole-screen capture needs nothing more

    request.source = CaptureSource::Window;
    QVERIFY(!captureRequestProblem(request).isEmpty());
    // Either half identifies a window: the index alone is what a picker supplies, and the title
    // alone is enough to find it again after the list has shifted.
    request.windowIndex = 2;
    QVERIFY(captureRequestProblem(request).isEmpty());
    request.windowIndex = -1;
    request.windowTitle = QStringLiteral("Terminal");
    QVERIFY(captureRequestProblem(request).isEmpty());

    request.source = CaptureSource::Region;
    QVERIFY(!captureRequestProblem(request).isEmpty());
    request.region = QRect(0, 0, 320, 240);
    QVERIFY(captureRequestProblem(request).isEmpty());

    // A camera with no id is the default camera, not an error.
    request.source = CaptureSource::Camera;
    QVERIFY(captureRequestProblem(request).isEmpty());
}

QTEST_MAIN(CaptureRequestTest)
#include "tst_capturerequest.moc"
