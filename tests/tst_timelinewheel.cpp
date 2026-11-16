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

private:
    QJSEngine m_engine;
    QJSValue m_route;
};

void TimelineWheelTest::initTestCase()
{
    const QString path = QStringLiteral(DRIFT_TIMELINE_WHEEL_JS);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QString source = QString::fromUtf8(file.readAll());
    source.remove(QStringLiteral(".pragma library"));
    const QJSValue result = m_engine.evaluate(source, path);
    QVERIFY2(!result.isError(), qPrintable(result.toString()));
    m_route = m_engine.globalObject().property(QStringLiteral("route"));
    QVERIFY(m_route.isCallable());
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

QTEST_GUILESS_MAIN(TimelineWheelTest)
#include "tst_timelinewheel.moc"
