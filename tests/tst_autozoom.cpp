#include <QtTest>

#include "core/animation/AutoZoom.h"
#include "core/input/InputLog.h"

#include <QTemporaryDir>

using namespace drift;

class AutoZoomTest : public QObject
{
    Q_OBJECT

private slots:
    // Input log
    void logRoundTrips();
    void logSortsOutOfOrderEvents();
    void logRejectsAFutureVersion();
    void logPathFollowsTheVideo();
    void pressesExcludeReleasesAndKeys();
    // Auto zoom
    void noClicksYieldsNothing();
    void zoomOfOneYieldsNothing();
    void aSingleClickPushesInAndBackOut();
    void theClickedPointEndsUpCentred();
    void zoomArrivesBeforeTheClick();
    void nearbyClicksBecomeOneZoomThatPans();
    void distantClicksGetSeparateZooms();
    void clicksOutsideTheClipAreIgnored();
    void aClickAtTheStartOpensZoomed();
    void keysNeverRunPastTheClip();
};

namespace {

InputEvent press(double seconds, double x, double y)
{
    InputEvent event;
    event.kind = InputEventKind::Press;
    event.timeUs = secondsToUs(seconds);
    event.position = QPointF(x, y);
    event.button = 1;
    return event;
}

AutoZoomOptions optionsFor(double zoom = 2.0)
{
    AutoZoomOptions options;
    options.zoom = zoom;
    options.frame = QSizeF(1920, 1080);
    options.restPosition = QPointF(960, 540); // a full-frame clip on a 1920x1080 canvas
    options.rampUs = secondsToUs(0.4);
    options.holdUs = secondsToUs(0.9);
    options.groupWithinUs = secondsToUs(1.5);
    return options;
}

// Where the frame point `click` actually lands on the canvas, given the layout contract:
// position + scale * (point - anchor), with the anchor at the frame centre.
QPointF canvasPointOf(const AutoZoomTracks &tracks, TimeUs at, const QPointF &click,
                      const QSizeF &frame)
{
    const double scale = tracks.scaleX.evaluateAt(at);
    const QPointF centre(frame.width() / 2.0, frame.height() / 2.0);
    const QPointF point(click.x() * frame.width(), click.y() * frame.height());
    return QPointF(tracks.positionX.evaluateAt(at), tracks.positionY.evaluateAt(at))
        + (point - centre) * scale;
}

} // namespace

void AutoZoomTest::logRoundTrips()
{
    InputLog log;
    log.events.append(press(1.0, 0.25, 0.5));
    InputEvent release;
    release.kind = InputEventKind::Release;
    release.timeUs = secondsToUs(1.1);
    release.position = QPointF(0.25, 0.5);
    release.button = 1;
    log.events.append(release);
    InputEvent key;
    key.kind = InputEventKind::Key;
    key.timeUs = secondsToUs(2.0);
    key.label = QStringLiteral("Ctrl+S");
    log.events.append(key);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("r.input.json"));
    QString error;
    QVERIFY2(writeInputLog(path, log, &error), qPrintable(error));

    InputLog read;
    QVERIFY2(readInputLog(path, &read, &error), qPrintable(error));
    QCOMPARE(read.events.size(), 3);
    QCOMPARE(read.events.at(0).kind, InputEventKind::Press);
    QCOMPARE(read.events.at(0).button, 1);
    QVERIFY(qFuzzyCompare(read.events.at(0).position.x(), 0.25));
    QCOMPARE(read.events.at(2).kind, InputEventKind::Key);
    // The label is what the overlay draws, so it has to survive verbatim rather than as a code.
    QCOMPARE(read.events.at(2).label, QStringLiteral("Ctrl+S"));
}

void AutoZoomTest::logSortsOutOfOrderEvents()
{
    // A platform that batches events can hand over a pair the wrong way round, and everything
    // downstream assumes time order.
    InputLog log;
    log.events.append(press(2.0, 0.5, 0.5));
    log.events.append(press(1.0, 0.2, 0.2));
    const InputLog read = inputLogFromJson(inputLogToJson(log));
    QCOMPARE(read.events.size(), 2);
    QCOMPARE(read.events.at(0).timeUs, secondsToUs(1.0));
    QCOMPARE(read.events.at(1).timeUs, secondsToUs(2.0));
}

void AutoZoomTest::logRejectsAFutureVersion()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("future.input.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("{\"version\":99,\"events\":[]}");
    file.close();

    InputLog out;
    QString error;
    // Reject the future: a newer writer may have changed what a field means, and guessing would
    // put the overlay in the wrong places.
    QVERIFY(!readInputLog(path, &out, &error));
    QVERIFY(!error.isEmpty());
}

void AutoZoomTest::logPathFollowsTheVideo()
{
    QCOMPARE(inputLogPathFor(QStringLiteral("/tmp/Screen 2026-01-01 10-00-00.mp4")),
             QStringLiteral("/tmp/Screen 2026-01-01 10-00-00.input.json"));
    QVERIFY(inputLogPathFor(QString()).isEmpty());
}

void AutoZoomTest::pressesExcludeReleasesAndKeys()
{
    InputLog log;
    log.events.append(press(1.0, 0.5, 0.5));
    InputEvent release;
    release.kind = InputEventKind::Release;
    release.timeUs = secondsToUs(1.1);
    log.events.append(release);
    InputEvent key;
    key.kind = InputEventKind::Key;
    key.timeUs = secondsToUs(1.2);
    log.events.append(key);
    // A zoom follows presses: a release is where the mouse stopped being held, not where
    // attention went.
    QCOMPARE(log.presses().size(), 1);
}

void AutoZoomTest::noClicksYieldsNothing()
{
    QVERIFY(autoZoomFromClicks({}, secondsToUs(10), optionsFor()).isEmpty());
}

void AutoZoomTest::zoomOfOneYieldsNothing()
{
    // Flat keys would clutter the graph editor for no effect.
    AutoZoomOptions options = optionsFor(1.0);
    QVERIFY(autoZoomFromClicks({press(2.0, 0.5, 0.5)}, secondsToUs(10), options).isEmpty());
}

void AutoZoomTest::aSingleClickPushesInAndBackOut()
{
    const AutoZoomOptions options = optionsFor(2.0);
    const AutoZoomTracks tracks =
        autoZoomFromClicks({press(3.0, 0.25, 0.25)}, secondsToUs(10), options);
    QVERIFY(!tracks.isEmpty());

    // Resting before, zoomed at the click, resting again after the hold and the pull-out.
    QVERIFY(qFuzzyCompare(tracks.scaleX.evaluateAt(secondsToUs(3.0)), 2.0));
    QCOMPARE(tracks.scaleX.evaluateAt(secondsToUs(2.6)), 1.0);
    QVERIFY(qFuzzyCompare(tracks.scaleX.evaluateAt(secondsToUs(3.9)), 2.0)); // still held
    QCOMPARE(tracks.scaleX.evaluateAt(secondsToUs(4.3)), 1.0);              // pulled out
    // Both axes move together: a recording zoomed on one axis is a stretched recording.
    QCOMPARE(tracks.scaleX.keyframes().size(), tracks.scaleY.keyframes().size());
}

void AutoZoomTest::theClickedPointEndsUpCentred()
{
    const AutoZoomOptions options = optionsFor(2.0);
    const QPointF click(0.25, 0.75);
    const AutoZoomTracks tracks =
        autoZoomFromClicks({press(3.0, click.x(), click.y())}, secondsToUs(10), options);

    // The whole point of the feature: at the peak the clicked pixel sits in the middle of the
    // canvas. This is the check that catches a sign error in the anchor maths, which would send
    // the zoom to the opposite corner.
    const QPointF landed = canvasPointOf(tracks, secondsToUs(3.0), click, options.frame);
    QVERIFY2(qAbs(landed.x() - options.restPosition.x()) < 0.5,
             qPrintable(QStringLiteral("x landed at %1").arg(landed.x())));
    QVERIFY2(qAbs(landed.y() - options.restPosition.y()) < 0.5,
             qPrintable(QStringLiteral("y landed at %1").arg(landed.y())));

    // And at rest the frame is not displaced at all.
    QCOMPARE(tracks.positionX.evaluateAt(secondsToUs(2.6)), options.restPosition.x());
    QCOMPARE(tracks.positionY.evaluateAt(secondsToUs(2.6)), options.restPosition.y());
}

void AutoZoomTest::zoomArrivesBeforeTheClick()
{
    const AutoZoomOptions options = optionsFor(2.0);
    const AutoZoomTracks tracks =
        autoZoomFromClicks({press(3.0, 0.5, 0.5)}, secondsToUs(10), options);
    // Fully in by the moment of the click, not still travelling: landing mid-push means the
    // viewer's eye is still moving when the thing they should see happens.
    QVERIFY(qFuzzyCompare(tracks.scaleX.evaluateAt(secondsToUs(3.0)), 2.0));
    // Halfway through the ramp it is genuinely between the two, so it is a push and not a cut.
    const double mid = tracks.scaleX.evaluateAt(secondsToUs(2.8));
    QVERIFY2(mid > 1.01 && mid < 1.99, qPrintable(QString::number(mid)));
}

void AutoZoomTest::nearbyClicksBecomeOneZoomThatPans()
{
    const AutoZoomOptions options = optionsFor(2.0);
    // Three clicks a second apart: one sustained zoom, not three. Zooming out and back in over a
    // burst is the "drunk camera" that makes auto-zoom unwatchable.
    const AutoZoomTracks tracks = autoZoomFromClicks(
        {press(3.0, 0.2, 0.2), press(4.0, 0.8, 0.2), press(5.0, 0.8, 0.8)}, secondsToUs(12),
        options);

    for (double t : {3.0, 3.5, 4.0, 4.5, 5.0}) {
        QVERIFY2(qAbs(tracks.scaleX.evaluateAt(secondsToUs(t)) - 2.0) < 0.001,
                 qPrintable(QStringLiteral("dipped out of zoom at %1s").arg(t)));
    }
    // It pans: each click gets centred in turn.
    for (const auto &click : {QPointF(0.2, 0.2), QPointF(0.8, 0.2), QPointF(0.8, 0.8)}) {
        const double at = click == QPointF(0.2, 0.2) ? 3.0 : (click == QPointF(0.8, 0.2) ? 4.0 : 5.0);
        const QPointF landed = canvasPointOf(tracks, secondsToUs(at), click, options.frame);
        QVERIFY(qAbs(landed.x() - options.restPosition.x()) < 0.5);
        QVERIFY(qAbs(landed.y() - options.restPosition.y()) < 0.5);
    }
}

void AutoZoomTest::distantClicksGetSeparateZooms()
{
    const AutoZoomOptions options = optionsFor(2.0);
    const AutoZoomTracks tracks =
        autoZoomFromClicks({press(2.0, 0.2, 0.2), press(8.0, 0.8, 0.8)}, secondsToUs(12), options);
    // Six seconds apart, so it comes back out in between.
    QCOMPARE(tracks.scaleX.evaluateAt(secondsToUs(5.0)), 1.0);
    QVERIFY(qFuzzyCompare(tracks.scaleX.evaluateAt(secondsToUs(2.0)), 2.0));
    QVERIFY(qFuzzyCompare(tracks.scaleX.evaluateAt(secondsToUs(8.0)), 2.0));
}

void AutoZoomTest::clicksOutsideTheClipAreIgnored()
{
    const AutoZoomOptions options = optionsFor(2.0);
    // A whole recording's log handed to a clip whose head was trimmed off.
    const AutoZoomTracks tracks = autoZoomFromClicks(
        {press(-5.0, 0.5, 0.5), press(99.0, 0.5, 0.5)}, secondsToUs(10), options);
    QVERIFY(tracks.isEmpty());
}

void AutoZoomTest::aClickAtTheStartOpensZoomed()
{
    const AutoZoomOptions options = optionsFor(2.0);
    // No room for a push-in before it, so it simply opens zoomed rather than keying a ramp at a
    // negative time.
    const AutoZoomTracks tracks =
        autoZoomFromClicks({press(0.0, 0.5, 0.5)}, secondsToUs(10), options);
    QVERIFY(!tracks.isEmpty());
    QVERIFY(qFuzzyCompare(tracks.scaleX.evaluateAt(0), 2.0));
    for (const auto &key : tracks.scaleX.keyframes().keys())
        QVERIFY(key >= 0);
}

void AutoZoomTest::keysNeverRunPastTheClip()
{
    const AutoZoomOptions options = optionsFor(2.0);
    const TimeUs duration = secondsToUs(4);
    // A click near the end: the hold and pull-out would both land past the clip.
    const AutoZoomTracks tracks = autoZoomFromClicks({press(3.9, 0.5, 0.5)}, duration, options);
    QVERIFY(!tracks.isEmpty());
    for (const auto &track : {tracks.scaleX, tracks.scaleY, tracks.positionX, tracks.positionY}) {
        for (const auto &key : track.keyframes().keys()) {
            QVERIFY2(key >= 0 && key < duration,
                     qPrintable(QStringLiteral("key at %1 outside the clip").arg(key)));
        }
    }
}

QTEST_MAIN(AutoZoomTest)
#include "tst_autozoom.moc"
