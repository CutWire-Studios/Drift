#include <QtTest>

#include "core/geom/BezierPath.h"
#include "ui/manip/ClipFrameMapper.h"
#include "ui/manip/DragSession.h"
#include "ui/manip/ManipModifiers.h"
#include "ui/manip/SnapService.h"

#include <cmath>

using namespace drift;
using namespace drift::geom;
using namespace drift::manip;

namespace {

ShapeVertex vtx(double x, double y, double inX = 0, double inY = 0, double outX = 0, double outY = 0)
{
    ShapeVertex v;
    v.p = {x, y};
    v.in = {inX, inY};
    v.out = {outX, outY};
    v.smooth = inX != 0 || inY != 0 || outX != 0 || outY != 0;
    return v;
}

ShapeContour curvy()
{
    ShapeContour c;
    c.closed = false;
    c.vertices = {vtx(0, 0, 0, 0, 0.2, 0.3), vtx(0.5, 0.4, -0.1, 0.2, 0.1, -0.2), vtx(1, 0, 0.2, 0.3, 0, 0)};
    return c;
}

// Distance from p to the polyline.
double distToPoly(const QVector<QPointF> &poly, const QPointF &p)
{
    double best = 1e9;
    for (int i = 0; i + 1 < poly.size(); ++i) {
        const QPointF a = poly[i];
        const QPointF d = poly[i + 1] - a;
        const double l2 = d.x() * d.x() + d.y() * d.y();
        const double s = l2 > 0 ? std::clamp(QPointF::dotProduct(p - a, d) / l2, 0.0, 1.0) : 0.0;
        best = std::min(best, std::hypot((a + d * s - p).x(), (a + d * s - p).y()));
    }
    return best;
}

} // namespace

class ManipTest : public QObject
{
    Q_OBJECT

private slots:
    void splitPreservesShape()
    {
        const ShapeContour before = curvy();
        for (const double t : {0.25, 0.5, 0.8}) {
            ShapeContour after = before;
            const int idx = splitSegment(after, 1, t);
            QCOMPARE(idx, 2);
            QCOMPARE(after.vertices.size(), 4);
            const QVector<QPointF> ref = flatten(before, 200);
            for (const QPointF &p : flatten(after, 64))
                QVERIFY2(distToPoly(ref, p) < 1e-3, "split moved the outline");
            const QPointF s = cubicPoint(before.vertices[1], before.vertices[2], t);
            QVERIFY(QLineF(after.vertices[idx].p, s).length() < 1e-9);
        }
    }

    void splitStraightSegmentStaysCorner()
    {
        ShapeContour c;
        c.vertices = {vtx(0, 0), vtx(1, 0), vtx(1, 1)};
        splitSegment(c, 0, 0.5);
        QVERIFY(!c.vertices[1].smooth);
        QCOMPARE(c.vertices[1].p, QPointF(0.5, 0));
    }

    void splitClosingSegmentAppends()
    {
        ShapeContour c;
        c.vertices = {vtx(0, 0), vtx(1, 0), vtx(1, 1)};
        QCOMPARE(splitSegment(c, 2, 0.5), 3);
        QCOMPARE(c.vertices[3].p, QPointF(0.5, 0.5));
    }

    void nearestPoint()
    {
        const ShapeContour c = curvy();
        const QPointF on = cubicPoint(c.vertices[0], c.vertices[1], 0.37);
        const NearestHit h = geom::nearestPoint(c, on + QPointF(0.0, 0.0));
        QCOMPARE(h.segment, 0);
        QVERIFY(std::abs(h.t - 0.37) < 1e-3);
        QVERIFY(h.distance < 1e-4);

        const QPointF off = on + QPointF(0.01, 0.0);
        const NearestHit h2 = geom::nearestPoint(c, off);
        QVERIFY(h2.distance <= 0.0101);
        QVERIFY(h2.valid());

        const NearestHit h3 = geom::nearestPoint(c, QPointF(0.5, 0.4) + QPointF(0.0, 0.0));
        QVERIFY(h3.distance < 1e-3);
    }

    void removeVertexIsPlain()
    {
        ShapeContour c = curvy();
        QVERIFY(removeVertex(c, 1));
        QCOMPARE(c.vertices.size(), 2);
        QVERIFY(!removeVertex(c, 5));
    }

    void healApproximatesOriginal()
    {
        ShapeContour c;
        c.closed = false;
        c.vertices = {vtx(0, 0, 0, 0, 0.1, 0.2), vtx(0.5, 0.35, -0.1, 0, 0.1, 0), vtx(1, 0, 0, 0.2, 0, 0)};
        const QVector<QPointF> ref = flatten(c, 200);
        ShapeContour healed = c;
        QVERIFY(removeVertexHeal(healed, 1));
        QCOMPARE(healed.vertices.size(), 2);
        double worst = 0;
        for (const QPointF &p : flatten(healed, 64))
            worst = std::max(worst, distToPoly(ref, p));
        QVERIFY2(worst < 0.03, qPrintable(QString::number(worst)));

        ShapeContour plain = c;
        removeVertex(plain, 1);
        double plainWorst = 0;
        for (const QPointF &p : flatten(plain, 64))
            plainWorst = std::max(plainWorst, distToPoly(ref, p));
        QVERIFY(worst < plainWorst);
    }

    void healStraightSpanDropsToPlain()
    {
        ShapeContour c;
        c.vertices = {vtx(0, 0), vtx(1, 0), vtx(1, 1), vtx(0, 1)};
        QVERIFY(removeVertexHeal(c, 1));
        QCOMPARE(c.vertices.size(), 3);
        QCOMPARE(c.vertices[0].out, QPointF());
    }

    void healKeepsOpenEnds()
    {
        ShapeContour c = curvy();
        QVERIFY(removeVertexHeal(c, 0));
        QCOMPARE(c.vertices.size(), 2);
    }

    void tangentModes()
    {
        const ShapeVertex v = vtx(0.5, 0.5, -0.1, 0.0, 0.2, 0.0);
        const QPointF target(0.5 + 0.0, 0.5 + 0.3);

        ShapeVertex m = applyTangentDrag(v, TangentHandle::Out, target, TangentMode::Mirrored, false);
        QCOMPARE(m.out, QPointF(0.0, 0.3));
        QCOMPARE(m.in, QPointF(0.0, -0.3));
        QVERIFY(m.smooth);

        ShapeVertex a = applyTangentDrag(v, TangentHandle::Out, target, TangentMode::Aligned, false);
        QCOMPARE(a.out, QPointF(0.0, 0.3));
        QVERIFY(std::abs(a.in.x()) < 1e-12);
        QVERIFY(std::abs(a.in.y() + 0.1) < 1e-12);

        ShapeVertex f = applyTangentDrag(v, TangentHandle::In, target, TangentMode::Free, false);
        QCOMPARE(f.in, QPointF(0.0, 0.3));
        QCOMPARE(f.out, v.out);
        QVERIFY(!f.smooth);

        ShapeVertex c = applyTangentDrag(v, TangentHandle::Out, target, TangentMode::Corner, false);
        QCOMPARE(c.in, v.in);
        QCOMPARE(c.out, QPointF(0.0, 0.3));
    }

    void tangentConstrain45()
    {
        const ShapeVertex v = vtx(0, 0);
        const ShapeVertex r = applyTangentDrag(v, TangentHandle::Out, {0.30, 0.02}, TangentMode::Free, true);
        QVERIFY(std::abs(r.out.y()) < 1e-12);
        const ShapeVertex d = applyTangentDrag(v, TangentHandle::Out, {0.2, 0.25}, TangentMode::Free, true);
        QVERIFY(std::abs(d.out.x() - d.out.y()) < 1e-12);
    }

    void smoothFlagMapsToMirrored()
    {
        QCOMPARE(tangentModeOf(vtx(0, 0, 0.1, 0, -0.1, 0)), TangentMode::Mirrored);
        QCOMPARE(tangentModeOf(vtx(0, 0)), TangentMode::Corner);
    }

    void graphTangentRules()
    {
        GraphTangents t;
        t.inDx = -0.2;
        t.inDy = 0.1;
        t.outDx = 0.3;
        t.outDy = 0.5;

        GraphTangents m = applyGraphTangentDrag(t, TangentHandle::Out, 0.4, 0.2, TangentMode::Mirrored, false, 1.0, 1.0);
        QCOMPARE(m.outDx, 0.4);
        QCOMPARE(m.inDx, -0.4);
        QCOMPARE(m.inDy, -0.2);

        GraphTangents a = applyGraphTangentDrag(t, TangentHandle::Out, 0.4, 0.2, TangentMode::Aligned, false, 1.0, 1.0);
        QCOMPARE(a.inDx, -0.2);
        QVERIFY(std::abs(a.inDy + 0.1) < 1e-12);

        GraphTangents f = applyGraphTangentDrag(t, TangentHandle::Out, 0.4, 0.2, TangentMode::Free, false, 1.0, 1.0);
        QCOMPARE(f.inDx, t.inDx);
        QCOMPARE(f.inDy, t.inDy);
    }

    void graphTangentConstrainAndClamp()
    {
        GraphTangents t;
        GraphTangents h = applyGraphTangentDrag(t, TangentHandle::Out, 0.3, 0.9, TangentMode::Free, true, 1.0, 1.0);
        QCOMPARE(h.outDy, 0.0);
        QCOMPARE(h.outDx, 0.3);

        GraphTangents wrongSign = applyGraphTangentDrag(t, TangentHandle::Out, -0.5, 0.1, TangentMode::Free, false, 1.0, 1.0);
        QCOMPARE(wrongSign.outDx, 0.0);
        GraphTangents wrongIn = applyGraphTangentDrag(t, TangentHandle::In, 0.5, 0.1, TangentMode::Free, false, 1.0, 1.0);
        QCOMPARE(wrongIn.inDx, 0.0);

        GraphTangents c = applyGraphTangentDrag(t, TangentHandle::Out, 2.0, 1.0, TangentMode::Free, false, 1.0, 0.5);
        QCOMPARE(c.outDx, 0.5);
        QCOMPARE(c.outDy, 0.25);

        GraphTangents cm = applyGraphTangentDrag(t, TangentHandle::Out, 2.0, 0.0, TangentMode::Mirrored, false, 0.25, 0.5);
        QCOMPARE(cm.outDx, 0.25);
        QCOMPARE(cm.inDx, -0.25);
    }

    void snapTargetsAndAxis()
    {
        const snap::Targets t = canvasTargets({1920, 1080}, {100.0}, {200.0}, {QRectF(500, 300, 200, 100)}, 0);
        QVERIFY(t.x.contains(0.0) && t.x.contains(960.0) && t.x.contains(1920.0));
        QVERIFY(t.x.contains(100.0) && t.x.contains(500.0) && t.x.contains(600.0) && t.x.contains(700.0));
        QVERIFY(t.y.contains(540.0) && t.y.contains(200.0) && t.y.contains(350.0));

        const AxisHit hit = snapAxis({955.0}, t.x, 8.0);
        QVERIFY(hit.snapped);
        QCOMPARE(hit.delta, 5.0);
        QCOMPARE(hit.target, 960.0);
        QVERIFY(!snapAxis({940.0}, {960.0}, 8.0).snapped);
    }

    void snapGrid()
    {
        const snap::Targets t = canvasTargets({100, 100}, {}, {}, {}, 25.0);
        QVERIFY(t.x.contains(25.0) && t.x.contains(75.0));
    }

    void snapRectMoveAndBypass()
    {
        const snap::Targets t = canvasTargets({1000, 1000}, {}, {}, {QRectF(620, 0, 100, 100)});
        const QRectF r(100, 100, 100, 100);
        // Right edge of the moved rect at 618 -> other clip's left edge 620.
        const RectSnap s = snapRectMove(r, QPointF(418, 0), t, 8.0, false);
        QCOMPARE(s.guideX, 620.0);
        QCOMPARE(s.delta.x(), 420.0);

        const RectSnap off = snapRectMove(r, QPointF(418, 0), t, 8.0, true);
        QCOMPARE(off.delta, QPointF(418, 0));
        QCOMPARE(off.guideX, -1.0);
    }

    void snapClipFrameTargets()
    {
        const snap::Targets t = clipFrameTargets({0.25});
        QCOMPARE(t.x, (QList<double>{0.0, 0.5, 1.0, 0.25}));
        QVERIFY(snapAxis({0.51}, t.x, 0.02).snapped);
    }

    void snapPlacedClipHonoursBypass()
    {
        snap::PlacedClip clip;
        clip.local.translate(100, 0);
        clip.ex = QVector4D(1, 0, 0, 0);
        clip.ey = QVector4D(0, 1, 0, 0);
        clip.size = QSizeF(200, 100);
        snap::Targets t;
        t.x = {303.0};
        const snap::MoveSnap on = snapClipMove(clip, QPointF(0, 0), t, 8.0, false);
        QCOMPARE(on.guideX, 303.0);
        QCOMPARE(on.delta.x(), 3.0);
        const snap::MoveSnap off = snapClipMove(clip, QPointF(0, 0), t, 8.0, true);
        QCOMPARE(off.delta, QPointF(0, 0));
        QCOMPARE(off.guideX, -1.0);
    }

    void snapAngleStep()
    {
        QCOMPARE(constrainAngle(37.0), 30.0);
        QCOMPARE(constrainAngle(38.0), 45.0);
        QCOMPARE(constrainAngle(-97.0), -90.0);
        const AxisHit h = snapAngle(88.0, 15.0, 3.0, false);
        QVERIFY(h.snapped);
        QCOMPARE(h.target, 90.0);
        QVERIFY(!snapAngle(82.0, 15.0, 3.0, false).snapped);
        QVERIFY(!snapAngle(88.0, 15.0, 3.0, true).snapped);
    }

    void pointConstraints()
    {
        QCOMPARE(lockAxis({5, 2}), QPointF(5, 0));
        QCOMPARE(lockAxis({1, -4}), QPointF(0, -4));
        const QPointF p = constrain45({10, 10}, {20, 11});
        QVERIFY(std::abs(p.y() - 10.0) < 1e-9);
        QVERIFY(std::abs(p.x() - (10.0 + std::hypot(10.0, 1.0))) < 1e-9);
    }

    void snapToleranceHasOneSource()
    {
        QCOMPARE(snapTolerancePx(), kDefaultSnapTolerancePx);
        setSnapTolerancePx(12.0);
        QCOMPARE(toleranceInUnits(2.0), 6.0);
        setSnapTolerancePx(kDefaultSnapTolerancePx);
    }

    void modifiersMapping()
    {
        const ManipModifiers s = ManipModifiers::fromQt(Qt::ShiftModifier);
        QVERIFY(s.constrain && !s.fromCenter && !s.noSnap);
        const ManipModifiers a = ManipModifiers::fromQt(Qt::AltModifier);
        QVERIFY(a.fromCenter && a.breakTangent && a.exclude && !a.constrain);
        const ManipModifiers c = ManipModifiers::fromQt(Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(c.noSnap && c.constrain);
        QVERIFY(!ManipModifiers::fromQt({}).noSnap);

        ManipModifiers chips;
        chips.noSnap = true;
        const ManipModifiers merged = s.merged(chips);
        QVERIFY(merged.constrain && merged.noSnap);
        QCOMPARE(ManipModifiers::fromQtFlags(int(Qt::ShiftModifier)).constrain, true);
    }

    void dragCancelRestoresAndMakesNoUndo()
    {
        int value = 5;
        int snapshot = 0;
        int undoEntries = 0;
        DragSession s;
        DragHooks h;
        h.begin = [&](const QString &) { snapshot = value; };
        h.commit = [&] { ++undoEntries; };
        h.cancel = [&] { value = snapshot; };
        s.setHooks(h);

        QSignalSpy cancelled(&s, &DragSession::cancelled);
        s.begin(QStringLiteral("move"), DragSession::Device::Touch);
        QVERIFY(s.interacting());
        QCOMPARE(s.device(), DragSession::Device::Touch);
        value = 99;
        s.update(QStringLiteral("X 12"));
        QCOMPARE(s.hudText(), QStringLiteral("X 12"));
        s.cancel();
        QCOMPARE(value, 5);
        QCOMPARE(undoEntries, 0);
        QVERIFY(!s.interacting());
        QVERIFY(s.hudText().isEmpty());
        QCOMPARE(cancelled.count(), 1);

        s.cancel();
        QCOMPARE(cancelled.count(), 1);
    }

    void dragCommitIsOneUndoStep()
    {
        int begins = 0, commits = 0;
        DragSession s;
        DragHooks h;
        h.begin = [&](const QString &) { ++begins; };
        h.commit = [&] { ++commits; };
        s.setHooks(h);
        s.begin(QStringLiteral("a"));
        s.update(QStringLiteral("1"));
        s.update(QStringLiteral("2"));
        s.commit();
        s.commit();
        QCOMPARE(begins, 1);
        QCOMPARE(commits, 1);
        s.begin(QStringLiteral("b"));
        s.begin(QStringLiteral("c"));
        QCOMPARE(commits, 2);
        s.commit();
        QCOMPARE(commits, 3);
    }

    void nudgesCoalesce()
    {
        int begins = 0, commits = 0, cancels = 0;
        DragSession s;
        DragHooks h;
        h.begin = [&](const QString &) { ++begins; };
        h.commit = [&] { ++commits; };
        h.cancel = [&] { ++cancels; };
        s.setHooks(h);
        s.setNudgeWindowMs(60);

        s.nudge();
        QTest::qWait(25);
        s.nudge();
        QTest::qWait(25);
        s.nudge();
        QCOMPARE(begins, 1);
        QCOMPARE(commits, 0);
        QVERIFY(s.interacting());
        QTRY_COMPARE_WITH_TIMEOUT(commits, 1, 1000);
        QVERIFY(!s.interacting());
        QCOMPARE(begins, 1);

        s.nudge();
        QCOMPARE(begins, 2);
        s.cancel();
        QCOMPARE(cancels, 1);
        QTest::qWait(100);
        QCOMPARE(commits, 1);
    }

    void dragAfterNudgeCommitsNudgeFirst()
    {
        int begins = 0, commits = 0;
        DragSession s;
        DragHooks h;
        h.begin = [&](const QString &) { ++begins; };
        h.commit = [&] { ++commits; };
        s.setHooks(h);
        s.nudge();
        s.begin(QStringLiteral("move"));
        QCOMPARE(commits, 1);
        QCOMPARE(begins, 2);
        s.commit();
        QCOMPARE(commits, 2);
    }

    void mapperRoundTripsWithRotation()
    {
        ClipFrameMapper m;
        m.setViewport({1920, 1080}, QRectF(40, 20, 960, 540));
        m.setClip(QRectF(300, 200, 640, 360), 33.0);

        for (const QPointF &n : {QPointF(0, 0), QPointF(1, 1), QPointF(0.3, 0.8), QPointF(-0.2, 1.4)}) {
            const QPointF v = m.clipToViewport(n);
            const QPointF back = m.viewportToClip(v);
            QVERIFY(QLineF(n, back).length() < 1e-4);
            const QPointF c = m.clipToCanvas(n);
            QVERIFY(QLineF(m.viewportToCanvas(m.canvasToViewport(c)), c).length() < 1e-3);
        }
        QVERIFY(QLineF(m.clipToCanvas({0.5, 0.5}), QPointF(620, 380)).length() < 1e-3);
        QVERIFY(QLineF(m.canvasToViewport({0, 0}), QPointF(40, 20)).length() < 1e-9);
        QCOMPARE(m.viewScale(), 0.5);
    }

    void mapperRotatesAboutCentreAndMatchesTrig()
    {
        ClipFrameMapper m;
        m.setViewport({1000, 1000}, QRectF(0, 0, 1000, 1000));
        m.setClip(QRectF(100, 100, 200, 100), 90.0);
        // Top-left corner of a 90 degree clip about its centre (200,150) lands at (250,50).
        const QPointF tl = m.clipToCanvas({0, 0});
        QVERIFY(QLineF(tl, QPointF(250, 50)).length() < 1e-3);
        const QPointF layout = m.canvasToLayout(tl);
        QVERIFY(QLineF(layout, QPointF(0, 0)).length() < 1e-3);
    }

    void mapperPivotAndParent()
    {
        ClipFrameMapper m;
        m.setViewport({100, 100}, QRectF(0, 0, 100, 100));
        m.setClip(QRectF(10, 10, 20, 20), 90.0);
        m.setPivot({0, 0});
        QVERIFY(QLineF(m.layoutToCanvas({20, 0}), QPointF(10, 30)).length() < 1e-3);

        QCOMPARE(m.toParentLocal({5, 5}), QPointF(5, 5));
        QMatrix4x4 parent;
        parent.translate(10, 0);
        parent.scale(2, 2);
        m.setParent(parent);
        QVERIFY(QLineF(m.toParentLocal({30, 8}), QPointF(10, 4)).length() < 1e-4);
    }

    void mapperPoseOverride()
    {
        ClipFrameMapper m;
        m.setViewport({100, 100}, QRectF(0, 0, 100, 100));
        m.setClip(QRectF(0, 0, 50, 50), 0.0);
        QMatrix4x4 pose;
        pose.translate(20, 30);
        pose.rotate(20.0f, 0, 1, 0);
        pose.rotate(15.0f, 0, 0, 1);
        m.setPose(pose);
        const QPointF n(0.6, 0.4);
        QVERIFY(QLineF(m.canvasToClip(m.clipToCanvas(n)), n).length() < 1e-4);
    }
};

QTEST_MAIN(ManipTest)
#include "tst_manip.moc"
