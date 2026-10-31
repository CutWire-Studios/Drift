#include <QtTest>

#include "app/editor/sessions/PolylineDraft.h"

class PolylineDraftTest : public QObject
{
    Q_OBJECT

private slots:
    void addsPointsAndUndoes();
    void ignoresADoubleClickDuplicate();
    void constrainsToFortyFiveDegrees();
    void closesOnlyWithEnoughPoints();
    void hoverNearFirstFollowsTheHover();
    void normalizesAndClamps();
};

void PolylineDraftTest::addsPointsAndUndoes()
{
    PolylineDraft draft;
    QSignalSpy spy(&draft, &PolylineDraft::pointsChanged);
    QVERIFY(draft.add({10, 10}, false));
    QVERIFY(draft.add({50, 10}, false));
    QCOMPARE(draft.count(), 2);
    QCOMPARE(draft.first(), QPointF(10, 10));
    QCOMPARE(draft.last(), QPointF(50, 10));
    draft.undo();
    QCOMPARE(draft.count(), 1);
    draft.undo();
    draft.undo();
    QCOMPARE(draft.count(), 0);
    QCOMPARE(draft.last(), QPointF());
    draft.add({1, 1}, false);
    draft.clear();
    QCOMPARE(draft.count(), 0);
    QVERIFY(spy.count() >= 5);
}

void PolylineDraftTest::ignoresADoubleClickDuplicate()
{
    PolylineDraft draft;
    QVERIFY(draft.add({10, 10}, false));
    QVERIFY(!draft.add({11, 11}, false));
    QCOMPARE(draft.count(), 1);
    QVERIFY(draft.add({20, 10}, false));
}

void PolylineDraftTest::constrainsToFortyFiveDegrees()
{
    PolylineDraft draft;
    QCOMPARE(draft.constrained({30, 17}, true), QPointF(30, 17));
    draft.add({0, 0}, false);
    const QPointF flat = draft.constrained({100, 10}, true);
    QVERIFY(qAbs(flat.y()) < 1e-9);
    QVERIFY(qAbs(flat.x() - std::hypot(100.0, 10.0)) < 1e-9);
    const QPointF diag = draft.constrained({50, 40}, true);
    QVERIFY(qAbs(diag.x() - diag.y()) < 1e-9);
    QCOMPARE(draft.constrained({50, 40}, false), QPointF(50, 40));
    QVERIFY(draft.add({100, 10}, true));
    QVERIFY(qAbs(draft.last().y()) < 1e-9);
}

void PolylineDraftTest::closesOnlyWithEnoughPoints()
{
    PolylineDraft draft;
    draft.setMinPoints(4);
    draft.add({0, 0}, false);
    draft.add({50, 0}, false);
    draft.add({50, 50}, false);
    QVERIFY(!draft.nearFirst({2, 2}, 8));
    draft.add({0, 50}, false);
    QVERIFY(draft.nearFirst({2, 2}, 8));
    QVERIFY(!draft.nearFirst({20, 20}, 8));
}

void PolylineDraftTest::hoverNearFirstFollowsTheHover()
{
    PolylineDraft draft;
    draft.add({0, 0}, false);
    draft.add({50, 0}, false);
    draft.add({50, 50}, false);
    draft.moveHover({3, 3}, false);
    QVERIFY(draft.hoverNearFirst());
    draft.moveHover({30, 30}, false);
    QVERIFY(!draft.hoverNearFirst());
    QCOMPARE(draft.hover(), QPointF(30, 30));
}

void PolylineDraftTest::normalizesAndClamps()
{
    PolylineDraft draft;
    draft.add({50, 25}, false);
    draft.add({250, -20}, false);
    QVariantList n = draft.normalized(100, 50);
    QCOMPARE(n.at(0).toMap().value("x").toDouble(), 0.5);
    QCOMPARE(n.at(0).toMap().value("y").toDouble(), 0.5);
    QCOMPARE(n.at(1).toMap().value("x").toDouble(), 2.5);
    n = draft.normalized(100, 50, true);
    QCOMPARE(n.at(1).toMap().value("x").toDouble(), 1.0);
    QCOMPARE(n.at(1).toMap().value("y").toDouble(), 0.0);
}

QTEST_APPLESS_MAIN(PolylineDraftTest)
#include "tst_polylinedraft.moc"
