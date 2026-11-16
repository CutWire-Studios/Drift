#include <QtTest>

#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <cmath>

// Routing of wheel and touchpad scrolls on the timeline (TimelineWheel.js), run in a bare
// QJSEngine so it needs no window.
class TimelineWheelTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void route_data();
    void route();
    void input_data();
    void input();
    void delta_data();
    void delta();
    void oneAxis_data();
    void oneAxis();
    void notches_data();
    void notches();

private:
    QJSEngine m_engine;
    QJSValue m_route;
    QJSValue m_input;
    QJSValue m_delta;
    QJSValue m_oneAxis;
    QJSValue m_notches;
    QJSValue m_platform;
};

void TimelineWheelTest::initTestCase()
{
    const QString path = QStringLiteral(DRIFT_TIMELINE_WHEEL_JS);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString source = QString::fromUtf8(file.readAll());
    source.remove(QStringLiteral(".pragma library"));
    // The script reads Qt.* constants and Qt.platform.pluginName, which QML provides.
    QJSValue qt = m_engine.newObject();
    qt.setProperty(QStringLiteral("NoScrollPhase"), 0);
    qt.setProperty(QStringLiteral("ShiftModifier"), 0x02000000);
    qt.setProperty(QStringLiteral("ControlModifier"), 0x04000000);
    qt.setProperty(QStringLiteral("AltModifier"), 0x08000000);
    m_platform = m_engine.newObject();
    m_platform.setProperty(QStringLiteral("pluginName"), QStringLiteral("xcb"));
    qt.setProperty(QStringLiteral("platform"), m_platform);
    m_engine.globalObject().setProperty(QStringLiteral("Qt"), qt);
    const QJSValue result = m_engine.evaluate(source, path);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    m_route = m_engine.globalObject().property(QStringLiteral("route"));
    QVERIFY(m_route.isCallable());
    m_input = m_engine.globalObject().property(QStringLiteral("input"));
    m_delta = m_engine.globalObject().property(QStringLiteral("delta"));
    m_oneAxis = m_engine.globalObject().property(QStringLiteral("oneAxis"));
    m_notches = m_engine.globalObject().property(QStringLiteral("notches"));
    QVERIFY(m_input.isCallable());
    QVERIFY(m_delta.isCallable());
    QVERIFY(m_oneAxis.isCallable());
    QVERIFY(m_notches.isCallable());
}

static void addRow(const char *name, bool touchpad, bool ctrl, bool shift, double pixelX,
                   double pixelY, double angleX, double angleY, bool wheelPansTime, bool overflow,
                   double expZoom, double expX, double expY)
{
    QTest::newRow(name) << touchpad << ctrl << shift << pixelX << pixelY << angleX << angleY
                        << wheelPansTime << overflow << expZoom << expX << expY;
}

void TimelineWheelTest::route_data()
{
    QTest::addColumn<bool>("touchpad");
    QTest::addColumn<bool>("ctrl");
    QTest::addColumn<bool>("shift");
    QTest::addColumn<double>("pixelX");
    QTest::addColumn<double>("pixelY");
    QTest::addColumn<double>("angleX");
    QTest::addColumn<double>("angleY");
    QTest::addColumn<bool>("wheelPansTime");
    QTest::addColumn<bool>("overflow");
    QTest::addColumn<double>("expZoom");
    QTest::addColumn<double>("expX");
    QTest::addColumn<double>("expY");

    //      name                      tp     ctrl   shift  pxX pxY  anX  anY  time   ovfl   zoom  x     y
    addRow("x11MouseFits",            false, false, false, 0,  120, 0,   120, false, false, 1,    120,  0);
    addRow("x11MouseOverflow",        false, false, false, 0,  120, 0,   120, false, true,  1,    0,    120);
    addRow("shiftX11Mouse",           false, false, true,  0,  120, 0,   120, false, true,  1,    120,  0);
    addRow("macShiftWheel",           false, false, true,  20, 0,   120, 0,   false, true,  1,    120,  0);
    addRow("timeSettingMouse",        false, false, false, 0,  120, 0,   120, true,  true,  1,    120,  0);
    addRow("timeShiftOverflow",       false, false, true,  0,  120, 0,   120, true,  true,  1,    0,    120);
    addRow("timeShiftFits",           false, false, true,  0,  120, 0,   120, true,  false, 1,    0,    120);
    addRow("timeSettingMouseFits",    false, false, false, 0,  120, 0,   120, true,  false, 1,    120,  0);
    addRow("timeSettingTilt",         false, false, false, 0,  0,   120, 0,   true,  true,  1,    120,  0);
    addRow("diagonalTieDyWins",       false, false, false, 0,  0,   60,  -60, false, false, 1,    -60,  0);
    addRow("touchpadShiftTime",       true,  false, true,  1,  6,   0,   0,   true,  true,  1,    6,    0);
    addRow("wheelTilt",               false, false, false, 0,  0,   120, 0,   false, true,  1,    120,  0);
    addRow("touchpadOverflow",        true,  false, false, 5,  -7,  40,  -56, false, true,  1,    5,    -7);
    addRow("touchpadFitsVertical",    true,  false, false, 2,  -9,  0,   0,   false, false, 1,    2,    0);
    addRow("touchpadFitsHorizontal",  true,  false, false, 9,  2,   0,   0,   false, false, 1,    9,    0);
    addRow("touchpadFitsOnlyVertical", true,  false, false, 0,  -9,  0,   0,   false, false, 1,    0,    0);
    addRow("touchpadShift",           true,  false, true,  1,  6,   0,   0,   false, true,  1,    6,    0);
    addRow("touchpadIgnoresTime",     true,  false, false, 3,  8,   0,   0,   true,  true,  1,    3,    8);
    addRow("touchpadTimeFits",        true,  false, false, 3,  8,   0,   0,   true,  false, 1,    3,    0);
    addRow("touchpadNoPixels",        true,  false, false, 0,  0,   0,   24,  false, true,  1,    0,    24);
    addRow("allZeros",                false, false, false, 0,  0,   0,   0,   false, true,  1,    0,    0);
    addRow("ctrlWheelUp",             false, true,  false, 0,  120, 0,   120, false, true,  1.15, 0,    0);
    addRow("ctrlWheelDown",           false, true,  false, 0,  -120, 0,  -120, false, true, 1.0 / 1.15, 0, 0);
    addRow("ctrlPartialNotch",        false, true,  false, 0,  0,   0,   15,  false, true,  std::pow(1.15, 1.0 / 8), 0, 0);
    addRow("ctrlTilt",                false, true,  false, 0,  0,   120, 0,   false, true,  1,    0,    0);
    addRow("ctrlTouchpad",            true,  true,  false, 0,  40,  0,   0,   false, true,  std::pow(1.0015, 40), 0, 0);
    addRow("ctrlShiftZoomWins",       false, true,  true,  0,  120, 0,   120, false, true,  1.15, 0,    0);
    addRow("windowsPrecisionTouchpad", false, false, false, 0, 0,   0,   12,  false, true,  1,    0,    12);
}

void TimelineWheelTest::route()
{
    QFETCH(bool, touchpad);
    QFETCH(bool, ctrl);
    QFETCH(bool, shift);
    QFETCH(double, pixelX);
    QFETCH(double, pixelY);
    QFETCH(double, angleX);
    QFETCH(double, angleY);
    QFETCH(bool, wheelPansTime);
    QFETCH(bool, overflow);
    QFETCH(double, expZoom);
    QFETCH(double, expX);
    QFETCH(double, expY);

    QJSValue e = m_engine.newObject();
    e.setProperty(QStringLiteral("touchpad"), touchpad);
    e.setProperty(QStringLiteral("ctrl"), ctrl);
    e.setProperty(QStringLiteral("shift"), shift);
    e.setProperty(QStringLiteral("pixelX"), pixelX);
    e.setProperty(QStringLiteral("pixelY"), pixelY);
    e.setProperty(QStringLiteral("angleX"), angleX);
    e.setProperty(QStringLiteral("angleY"), angleY);

    const QJSValue r = m_route.call({e, wheelPansTime, overflow});
    QVERIFY2(!r.isError(), qPrintable(r.toString()));
    QVERIFY(qFuzzyCompare(r.property(QStringLiteral("zoom")).toNumber(), expZoom));
    QCOMPARE(r.property(QStringLiteral("x")).toNumber(), expX);
    QCOMPARE(r.property(QStringLiteral("y")).toNumber(), expY);
}

namespace {
constexpr int kTouchPad = 4;
constexpr int kShift = 0x02000000;
constexpr int kCtrl = 0x04000000;
constexpr int kAlt = 0x08000000;
}

void TimelineWheelTest::input_data()
{
    QTest::addColumn<int>("phase");
    QTest::addColumn<QString>("plugin");
    QTest::addColumn<int>("deviceType");   // -1: null device
    QTest::addColumn<int>("modifiers");
    QTest::addColumn<bool>("expTouchpad");
    QTest::addColumn<bool>("expCtrl");
    QTest::addColumn<bool>("expShift");
    QTest::addColumn<bool>("expAlt");

    QTest::newRow("waylandPhased") << 1 << "wayland" << 0 << 0 << true << false << false << false;
    QTest::newRow("macPhased") << 2 << "cocoa" << 0 << 0 << true << false << false << false;
    QTest::newRow("macRetypedMouse") << 0 << "cocoa" << kTouchPad << 0 << false << false << false << false;
    QTest::newRow("x11TouchPadType") << 0 << "xcb" << kTouchPad << 0 << true << false << false << false;
    QTest::newRow("x11Mouse") << 0 << "xcb" << 0 << 0 << false << false << false << false;
    QTest::newRow("x11NullDevice") << 0 << "xcb" << -1 << 0 << false << false << false << false;
    QTest::newRow("altShiftMods") << 0 << "wayland" << 0 << (kAlt | kShift) << false << false << true << true;
    QTest::newRow("ctrlMod") << 0 << "wayland" << 0 << kCtrl << false << true << false << false;
}

void TimelineWheelTest::input()
{
    QFETCH(int, phase);
    QFETCH(QString, plugin);
    QFETCH(int, deviceType);
    QFETCH(int, modifiers);
    QFETCH(bool, expTouchpad);
    QFETCH(bool, expCtrl);
    QFETCH(bool, expShift);
    QFETCH(bool, expAlt);

    m_platform.setProperty(QStringLiteral("pluginName"), plugin);
    QJSValue wheel = m_engine.newObject();
    wheel.setProperty(QStringLiteral("phase"), phase);
    wheel.setProperty(QStringLiteral("modifiers"), modifiers);
    if (deviceType < 0) {
        wheel.setProperty(QStringLiteral("device"), QJSValue(QJSValue::NullValue));
    } else {
        QJSValue device = m_engine.newObject();
        device.setProperty(QStringLiteral("type"), deviceType);
        wheel.setProperty(QStringLiteral("device"), device);
    }
    QJSValue pixel = m_engine.newObject();
    pixel.setProperty(QStringLiteral("x"), 3);
    pixel.setProperty(QStringLiteral("y"), 4);
    wheel.setProperty(QStringLiteral("pixelDelta"), pixel);
    QJSValue angle = m_engine.newObject();
    angle.setProperty(QStringLiteral("x"), 5);
    angle.setProperty(QStringLiteral("y"), 6);
    wheel.setProperty(QStringLiteral("angleDelta"), angle);

    const QJSValue r = m_input.call({wheel, kTouchPad});
    QVERIFY2(!r.isError(), qPrintable(r.toString()));
    QCOMPARE(r.property(QStringLiteral("touchpad")).toBool(), expTouchpad);
    QCOMPARE(r.property(QStringLiteral("ctrl")).toBool(), expCtrl);
    QCOMPARE(r.property(QStringLiteral("shift")).toBool(), expShift);
    QCOMPARE(r.property(QStringLiteral("alt")).toBool(), expAlt);
    QCOMPARE(r.property(QStringLiteral("pixelX")).toNumber(), 3.0);
    QCOMPARE(r.property(QStringLiteral("pixelY")).toNumber(), 4.0);
    QCOMPARE(r.property(QStringLiteral("angleX")).toNumber(), 5.0);
    QCOMPARE(r.property(QStringLiteral("angleY")).toNumber(), 6.0);
}

void TimelineWheelTest::delta_data()
{
    QTest::addColumn<bool>("touchpad");
    QTest::addColumn<double>("pixelX");
    QTest::addColumn<double>("pixelY");
    QTest::addColumn<double>("angleX");
    QTest::addColumn<double>("angleY");
    QTest::addColumn<double>("scale");
    QTest::addColumn<double>("expX");
    QTest::addColumn<double>("expY");

    QTest::newRow("touchpadPixels") << true << 7.0 << -9.0 << 0.0 << 120.0 << 0.5 << 7.0 << -9.0;
    QTest::newRow("touchpadNoPixels") << true << 0.0 << 0.0 << 120.0 << -240.0 << 0.5 << 60.0 << -120.0;
    QTest::newRow("x11Mouse") << false << 0.0 << 120.0 << 0.0 << 120.0 << 0.5 << 0.0 << 60.0;
    QTest::newRow("macMouse") << false << 0.0 << 20.0 << 0.0 << 120.0 << 0.5 << 0.0 << 60.0;
}

void TimelineWheelTest::delta()
{
    QFETCH(bool, touchpad);
    QFETCH(double, pixelX);
    QFETCH(double, pixelY);
    QFETCH(double, angleX);
    QFETCH(double, angleY);
    QFETCH(double, scale);
    QFETCH(double, expX);
    QFETCH(double, expY);

    QJSValue e = m_engine.newObject();
    e.setProperty(QStringLiteral("touchpad"), touchpad);
    e.setProperty(QStringLiteral("pixelX"), pixelX);
    e.setProperty(QStringLiteral("pixelY"), pixelY);
    e.setProperty(QStringLiteral("angleX"), angleX);
    e.setProperty(QStringLiteral("angleY"), angleY);
    const QJSValue r = m_delta.call({e, scale});
    QVERIFY2(!r.isError(), qPrintable(r.toString()));
    QCOMPARE(r.property(QStringLiteral("x")).toNumber(), expX);
    QCOMPARE(r.property(QStringLiteral("y")).toNumber(), expY);
}

void TimelineWheelTest::oneAxis_data()
{
    QTest::addColumn<double>("angleX");
    QTest::addColumn<double>("angleY");
    QTest::addColumn<double>("expected");

    QTest::newRow("vertical") << 0.0 << 120.0 << 120.0;
    QTest::newRow("altSwapped") << 120.0 << 0.0 << 120.0;
    QTest::newRow("verticalWins") << 30.0 << -60.0 << -60.0;
}

void TimelineWheelTest::oneAxis()
{
    QFETCH(double, angleX);
    QFETCH(double, angleY);
    QFETCH(double, expected);
    const QJSValue r = m_oneAxis.call({angleX, angleY});
    QVERIFY2(!r.isError(), qPrintable(r.toString()));
    QCOMPARE(r.toNumber(), expected);
}

void TimelineWheelTest::notches_data()
{
    QTest::addColumn<QList<double>>("deltas");
    QTest::addColumn<int>("expSteps");
    QTest::addColumn<double>("expRest");

    QTest::newRow("eightSmallEvents") << QList<double>(8, 15.0) << 1 << 0.0;
    QTest::newRow("sevenSmallEvents") << QList<double>(7, 15.0) << 0 << 105.0;
    QTest::newRow("oneNotchDown") << QList<double>{-120.0} << -1 << 0.0;
    QTest::newRow("twoAndABit") << QList<double>{250.0} << 2 << 10.0;
    QTest::newRow("reversal") << QList<double>{60.0, -120.0} << -1 << 0.0;
    QTest::newRow("eightSmallEventsDown") << QList<double>(8, -15.0) << -1 << 0.0;
}

void TimelineWheelTest::notches()
{
    QFETCH(QList<double>, deltas);
    QFETCH(int, expSteps);
    QFETCH(double, expRest);

    // Steps add up over the events, the way the QML MouseAreas carry the remainder.
    double rest = 0;
    int steps = 0;
    for (double d : deltas) {
        const QJSValue r = m_notches.call({rest, d});
        QVERIFY2(!r.isError(), qPrintable(r.toString()));
        steps += r.property(QStringLiteral("steps")).toInt();
        rest = r.property(QStringLiteral("rest")).toNumber();
    }
    QCOMPARE(steps, expSteps);
    QCOMPARE(rest, expRest);
}

QTEST_GUILESS_MAIN(TimelineWheelTest)
#include "tst_timelinewheel.moc"
