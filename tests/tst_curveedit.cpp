#include "ui/color/CurveEdit.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace drift;
using namespace drift::curveedit;

class TstCurveEdit : public QObject
{
    Q_OBJECT

private slots:
    void hitPicksTheNearestPointWithinReach()
    {
        const Points pts = identityCurve(false);
        QCOMPARE(hit(pts, {100, 100}, {2, 98}, 10), 0);
        QCOMPARE(hit(pts, {100, 100}, {97, 3}, 10), 1);
        QCOMPARE(hit(pts, {100, 100}, {50, 50}, 10), -1);
    }

    void insertLandsOnTheDrawnCurve()
    {
        Points pts = parseCurvePoints(
            QStringLiteral(R"([{"x":0,"y":0.5,"ease":"spline"},{"x":0.25,"y":0.9,"ease":"spline"},)"
                           R"({"x":0.8,"y":0.1,"ease":"spline"},{"x":1,"y":0.5,"ease":"spline"}])"),
            true);
        const double expected = evalCurve(pts, 0.5, true);
        const int idx = insertAt(pts, 50, 100, true);
        QCOMPARE(idx, 2);
        QCOMPARE(pts[2].x, 0.5);
        QVERIFY(qAbs(pts[2].y - expected) < 1e-12);
        QCOMPARE(pts[2].ease, QStringLiteral("spline"));
    }

    void insertRefusesCrowdingAndAFullCurve()
    {
        Points pts = identityCurve(false);
        QCOMPARE(insertAt(pts, 0.5, 100, false), -1);
        QCOMPARE(pts.size(), size_t(2));
        for (int i = 1; pts.size() < size_t(kMaxCurvePoints); ++i)
            QVERIFY(insertAt(pts, i * 5.5, 100, false) >= 0);
        QCOMPARE(insertAt(pts, 99, 100, false), -1);
    }

    void dragKeepsNeighbourGapAndClampsY()
    {
        Points pts = identityCurve(false);
        const int idx = insertAt(pts, 50, 100, false);
        QCOMPARE(dragTo(pts, idx, {120, -20}, {100, 100}, false), idx);
        QVERIFY(qAbs(pts[1].x - (1.0 - kMinCurveGap)) < 1e-12);
        QCOMPARE(pts[1].y, 1.0);
    }

    void dragOffRemovesInnerPointsOnly()
    {
        Points pts = identityCurve(false);
        const int idx = insertAt(pts, 50, 100, false);
        QCOMPARE(dragTo(pts, idx, {50, 140}, {100, 100}, false), -1);
        QCOMPARE(pts.size(), size_t(2));
        QCOMPARE(dragTo(pts, 0, {-50, 140}, {100, 100}, false), 0);
        QCOMPARE(pts.size(), size_t(2));
        QCOMPARE(pts[0].x, 0.0);
    }

    void hueEndsStayTied()
    {
        Points pts = identityCurve(true);
        dragTo(pts, 0, {0, 20}, {100, 100}, true);
        QCOMPARE(pts[0].y, 0.8);
        QCOMPARE(pts[1].y, 0.8);
        nudge(pts, 1, 0, -0.1, true);
        QVERIFY(qAbs(pts[0].y - 0.7) < 1e-12);
    }

    void nudgeClampsAndKeepsGap()
    {
        Points pts = identityCurve(false);
        insertAt(pts, 50, 100, false);
        nudge(pts, 1, 5, 5, false);
        QVERIFY(qAbs(pts[1].x - (1.0 - kMinCurveGap)) < 1e-12);
        QCOMPARE(pts[1].y, 1.0);
        nudge(pts, 0, 5, 0, false);
        QCOMPARE(pts[0].x, 0.0);
    }

    void removeKeepsTheEnds()
    {
        Points pts = identityCurve(false);
        insertAt(pts, 50, 100, false);
        QVERIFY(!removeAt(pts, 0));
        QVERIFY(!removeAt(pts, 2));
        QVERIFY(removeAt(pts, 1));
        QCOMPARE(pts.size(), size_t(2));
    }

    void jsonRoundTripsPointsAndEases()
    {
        Points pts = identityCurve(false);
        pts[0].ease = QStringLiteral("hold");
        const Points back = parseCurvePoints(toJson(pts), false);
        QCOMPARE(back.size(), size_t(2));
        QCOMPARE(back[0].ease, QStringLiteral("hold"));
        QCOMPARE(back[1].y, 1.0);
        QCOMPARE(evalCurve(back, 0.5, false), 0.0);
    }
};

QTEST_APPLESS_MAIN(TstCurveEdit)
#include "tst_curveedit.moc"
