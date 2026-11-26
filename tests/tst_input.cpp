#include <QtTest>

#include "app/editor/keymap/Keymap.h"
#include "app/editor/keymap/KeymapPresets.h"
#include "app/platform/Gamepad.h"
#include "core/animation/KeyframeSimplify.h"

class InputTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void padChordsNormalize();
    void padDefaultsReachEveryPreset();
    void padChordsStayOutOfShortcuts();
    void sticksAreDeadZonedAndCurved();
    void shouldersActAsChordModifiers();
    void triggersPressWithHysteresis();
    void simplifyKeepsPeaksAndRangeEnds();
    void simplifyLeavesKeysOutsideTheRange();
};

void InputTest::padChordsNormalize()
{
    using drift::keymap::normalizePadChord;
    QCOMPARE(normalizePadChord(QStringLiteral("Pad+A")), QStringLiteral("Pad+A"));
    QCOMPARE(normalizePadChord(QStringLiteral("Pad+RB+LB+A")), QStringLiteral("Pad+LB+RB+A"));
    QCOMPARE(normalizePadChord(QStringLiteral("Pad+LB+LB")), QStringLiteral("Pad+LB"));
    QVERIFY(normalizePadChord(QStringLiteral("Pad+Z")).isEmpty());
    QVERIFY(normalizePadChord(QStringLiteral("Pad+X+A")).isEmpty());
    QVERIFY(normalizePadChord(QStringLiteral("Ctrl+A")).isEmpty());
    QCOMPARE(drift::Keymap::normalizeChord(QStringLiteral("Pad+DpadLeft")), QStringLiteral("Pad+DpadLeft"));
    QCOMPARE(drift::Keymap::normalizeChord(QStringLiteral("Ctrl+S")), QStringLiteral("Ctrl+S"));
}

void InputTest::padDefaultsReachEveryPreset()
{
    for (const drift::keymap::Preset &preset : drift::keymap::presets()) {
        const QStringList play = drift::keymap::presetChords(preset, QStringLiteral("playPause"), QStringLiteral("global"));
        QVERIFY2(play.contains(QStringLiteral("Pad+X")), qPrintable(preset.id));
        const QStringList up = drift::keymap::presetChords(preset, QStringLiteral("fly3d.up"), QStringLiteral("fly3d"));
        QVERIFY2(up.contains(QStringLiteral("Pad+RB")), qPrintable(preset.id));
    }
    const drift::keymap::Preset *premiere = drift::keymap::findPreset(QStringLiteral("premiere"));
    QVERIFY(premiere);
    QVERIFY(drift::keymap::presetChords(*premiere, QStringLiteral("playPause"), QStringLiteral("global"))
                .contains(QStringLiteral("Space")));
}

void InputTest::padChordsStayOutOfShortcuts()
{
    drift::Keymap keymap;
    for (const QString &chord : keymap.chords())
        QVERIFY2(!chord.startsWith(QLatin1String("Pad+")), qPrintable(chord));
    QCOMPARE(keymap.actionFor(QStringLiteral("global"), QStringLiteral("Pad+X")), QStringLiteral("playPause"));
    QCOMPARE(keymap.actionFor(QStringLiteral("fly3d"), QStringLiteral("Pad+B")), QStringLiteral("fly3d.land"));
    QCOMPARE(keymap.addBinding(QStringLiteral("stop"), QStringLiteral("Pad+Y")), QString());
    QVERIFY(keymap.bindings(QStringLiteral("stop")).contains(QStringLiteral("Pad+Y")));
    keymap.resetAll();
}

void InputTest::sticksAreDeadZonedAndCurved()
{
    drift::Gamepad pad;
    pad.setDeadZone(0.2);
    pad.setCurve(1.0);
    pad.ingestAxes(0.1, 0.1, 0, 0, 0, 0);
    QCOMPARE(pad.leftX(), 0.0);
    QCOMPARE(pad.leftY(), 0.0);
    pad.ingestAxes(1.0, 0.0, 0, 0, 0, 0);
    QCOMPARE(pad.leftX(), 1.0);
    // Screen-down is positive on the wire; the service reports up as positive.
    pad.ingestAxes(0, -1.0, 0, 1.0, 0, 0);
    QCOMPARE(pad.leftY(), 1.0);
    QCOMPARE(pad.rightY(), -1.0);
    pad.setCurve(2.0);
    pad.ingestAxes(0.6, 0.0, 0, 0, 0, 0);
    QVERIFY(qFuzzyCompare(pad.leftX(), 0.5 * 0.5));
}

void InputTest::shouldersActAsChordModifiers()
{
    drift::Gamepad pad;
    QSignalSpy pressed(&pad, &drift::Gamepad::buttonPressed);
    pad.ingestButton(QStringLiteral("A"), true);
    pad.ingestButton(QStringLiteral("A"), false);
    pad.ingestButton(QStringLiteral("LB"), true);
    pad.ingestButton(QStringLiteral("A"), true);
    QVERIFY(pad.chordHeld(QStringLiteral("Pad+LB+A")));
    QVERIFY(!pad.chordHeld(QStringLiteral("Pad+RB")));
    QCOMPARE(pressed.size(), 3);
    QCOMPARE(pressed.at(0).at(0).toString(), QStringLiteral("Pad+A"));
    QCOMPARE(pressed.at(1).at(0).toString(), QStringLiteral("Pad+LB"));
    QCOMPARE(pressed.at(2).at(0).toString(), QStringLiteral("Pad+LB+A"));
}

void InputTest::triggersPressWithHysteresis()
{
    drift::Gamepad pad;
    QSignalSpy pressed(&pad, &drift::Gamepad::buttonPressed);
    pad.ingestAxes(0, 0, 0, 0, 0.4, 0);
    QVERIFY(!pad.buttonDown(QStringLiteral("LT")));
    pad.ingestAxes(0, 0, 0, 0, 0.6, 0);
    QVERIFY(pad.buttonDown(QStringLiteral("LT")));
    pad.ingestAxes(0, 0, 0, 0, 0.4, 0);
    QVERIFY(pad.buttonDown(QStringLiteral("LT")));
    pad.ingestAxes(0, 0, 0, 0, 0.2, 0);
    QVERIFY(!pad.buttonDown(QStringLiteral("LT")));
    QCOMPARE(pressed.size(), 1);
}

void InputTest::simplifyKeepsPeaksAndRangeEnds()
{
    drift::KeyframeTrack<double> track;
    for (int i = 0; i <= 100; ++i) {
        const double v = i <= 50 ? i : 100 - i;
        track.setKeyframe(i * 10000, v);
    }
    drift::simplifyKeyframes(track, 0, 1000000, 0.5, false);
    QCOMPARE(track.keyframes().size(), 3);
    QVERIFY(track.keyframes().contains(0));
    QVERIFY(track.keyframes().contains(500000));
    QVERIFY(track.keyframes().contains(1000000));
}

void InputTest::simplifyLeavesKeysOutsideTheRange()
{
    drift::KeyframeTrack<double> track;
    for (int i = 0; i <= 20; ++i)
        track.setKeyframe(i * 1000, 5.0);
    drift::simplifyKeyframes(track, 5000, 15000, 0.1, false);
    QCOMPARE(track.keyframes().size(), 5 + 2 + 5);
    drift::removeKeyframesBetween(track, 5000, 15000);
    QCOMPARE(track.keyframes().size(), 5 + 2 + 5);
}

QTEST_MAIN(InputTest)
#include "tst_input.moc"
