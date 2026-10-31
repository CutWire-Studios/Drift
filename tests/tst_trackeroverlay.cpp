#include "ui/preview/TrackerOverlayMath.h"

#include <QtTest>

using namespace drift::trackerview;

class TstTrackerOverlay : public QObject
{
    Q_OBJECT

private slots:
    void tintFollowsConfidence()
    {
        QCOMPARE(tintFor(0.2, 0.5), Tint::Bad);
        QCOMPARE(tintFor(0.5, 0.5), Tint::Warn);
        QCOMPARE(tintFor(0.79, 0.5), Tint::Warn);
        QCOMPARE(tintFor(0.8, 0.5), Tint::Good);
        QCOMPARE(tintFor(0.85, 0.9), Tint::Bad);
    }

    void pointHitHasAMinimumSize()
    {
        const QPointF c(100, 100);
        QVERIFY(pointHit({111, 100}, c, 10, 10));
        QVERIFY(!pointHit({113, 100}, c, 10, 10));
        QVERIFY(pointHit({130, 100}, c, 60, 10));
        QVERIFY(pointHit({100, 111}, c, 60, 10));
        QVERIFY(!pointHit({100, 113}, c, 60, 10));
    }

    void cornerHitReachesPastTheHandle()
    {
        const QPointF c(50, 50);
        QVERIFY(cornerHit({50 + 8 + 8, 50}, c, 16, 8));
        QVERIFY(!cornerHit({50 + 17, 50}, c, 16, 8));
    }

    void stripRunsMergeNeighbours()
    {
        const auto runs = stripRuns({-1, -1, 0.2, 0.3, 0.9, 0.95, 0.6}, 0.5);
        QCOMPARE(runs.size(), 4);
        QCOMPARE(runs[0].tint, StripTint::Empty);
        QCOMPARE(runs[0].count, 2);
        QCOMPARE(runs[1].tint, StripTint::Bad);
        QCOMPARE(runs[1].first, 2);
        QCOMPARE(runs[2].tint, StripTint::Good);
        QCOMPARE(runs[2].count, 2);
        QCOMPARE(runs[3].tint, StripTint::Warn);
    }

    void triangulateCoversAConvexAndAConcavePolygon()
    {
        const QVector<QPointF> square{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        QCOMPARE(triangulate(square).size(), 6);

        const QVector<QPointF> arrow{{0, 0}, {10, 0}, {5, 4}, {10, 10}, {0, 10}};
        const QVector<int> tris = triangulate(arrow);
        QCOMPARE(tris.size(), 9);
        double area = 0;
        for (int i = 0; i < tris.size(); i += 3)
            area += std::abs(signedArea({arrow[tris[i]], arrow[tris[i + 1]], arrow[tris[i + 2]]}));
        QCOMPARE(area, std::abs(signedArea(arrow)));
    }

    void triangulateIgnoresWinding()
    {
        const QVector<QPointF> clockwise{{0, 0}, {0, 10}, {10, 10}, {10, 0}};
        QCOMPARE(triangulate(clockwise).size(), 6);
        QVERIFY(triangulate({{0, 0}, {1, 1}}).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstTrackerOverlay)
#include "tst_trackeroverlay.moc"
