#include <QtTest>

#include "core/effects/Mask.h"
#include "core/geom/BezierPath.h"
#include "core/project/Project.h"
#include "core/project/Track.h"
#include "core/shapes/ShapeStyle.h"
#include "engine/compositor/layers/MaskApplier.h"
#include "engine/render3d/editor/ClipGizmo.h"
#include "engine/render3d/scene/ClipTransform3d.h"
#include "ui/manip/ClipFrameMapper.h"
#include "ui/manip/CurveGraphModel.h"
#include "ui/manip/DragSession.h"
#include "ui/manip/ManipModifiers.h"
#include "ui/manip/PathEditCore.h"
#include "ui/manip/SnapService.h"
#include "ui/manip/StrokeSmoother.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QPainterPath>

#include <cmath>
#include <numbers>

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
    v.mode = inX != 0 || inY != 0 || outX != 0 || outY != 0 ? TangentMode::Mirrored : TangentMode::Corner;
    return v;
}

ShapeContour curvy()
{
    ShapeContour c;
    c.closed = false;
    c.vertices = {vtx(0, 0, 0, 0, 0.2, 0.3), vtx(0.5, 0.4, -0.1, 0.2, 0.1, -0.2), vtx(1, 0, 0.2, 0.3, 0, 0)};
    return c;
}

// A clip centred on the canvas, so the eye looks straight down at it and the overlay is canvas px.
drift::gizmo::Pose centredPose()
{
    drift::gizmo::Pose pose;
    pose.canvas = QSizeF(1920, 1080);
    pose.rect = QRectF(760, 390, 400, 300);
    return pose;
}

QPointF gizmoOrigin(const drift::gizmo::Pose &pose)
{
    return drift::gizmo::geometry(pose, drift::gizmo::Tool::Move, drift::gizmo::Orientation::World, 1.0).origin;
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

PathEditCore squareCore(bool closedOnly = false)
{
    PathEditCore core;
    core.setClosedOnly(closedOnly);
    core.setView(QTransform().translate(100, 50).scale(200, 200));
    core.setFrameSize({200, 200});
    ShapeContour c;
    c.closed = true;
    c.vertices = {vtx(0.2, 0.2), vtx(0.8, 0.2), vtx(0.8, 0.8), vtx(0.2, 0.8)};
    core.setContours({c});
    return core;
}

QPointF px(double nx, double ny)
{
    return {100 + nx * 200, 50 + ny * 200};
}


int shiftFlag() { return int(Qt::ShiftModifier); }
int altFlag() { return int(Qt::AltModifier); }
int ctrlFlag() { return int(Qt::ControlModifier); }
int mouse() { return int(DragSession::Device::Mouse); }
int touchDev() { return int(DragSession::Device::Touch); }

QVariantMap gp(double x, double y, double inDx = 0, double inDy = 0, double outDx = 0, double outDy = 0,
                      bool smooth = false)
{
    return {{"x", x}, {"y", y}, {"inDx", inDx}, {"inDy", inDy}, {"outDx", outDx}, {"outDy", outDy}, {"smooth", smooth}};
}

void drag(CurveGraphModel &m, const QPointF &from, const QPointF &to, int mods = 0, int device = 0)
{
    m.press(from, mods, device);
    m.move(from + (to - from) * 0.5, mods);
    m.move(to, mods);
    m.release(to, mods);
}

CurveGraphModel *polyline(QObject *parent, bool snap = false)
{
    auto *m = new CurveGraphModel(parent);
    m->setupPolyline();
    m->setPlot(QRectF(0, 0, 200, 100));
    m->setSnapEnabled(snap);
    m->setPoints({gp(0, 0), gp(0.4, 0.3), gp(0.7, 0.8), gp(1, 1)});
    return m;
}

CurveGraphModel *colorModel(QObject *parent, bool hue = false)
{
    auto *m = new CurveGraphModel(parent);
    m->setupColor(hue, [](double x) { return x; });
    m->setPlot(QRectF(0, 0, 100, 100));
    m->setSnapEnabled(false);
    return m;
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
        QVERIFY(!c.vertices[1].smooth());
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
        QVERIFY(m.smooth());

        ShapeVertex a = applyTangentDrag(v, TangentHandle::Out, target, TangentMode::Aligned, false);
        QCOMPARE(a.out, QPointF(0.0, 0.3));
        QVERIFY(std::abs(a.in.x()) < 1e-12);
        QVERIFY(std::abs(a.in.y() + 0.1) < 1e-12);

        ShapeVertex f = applyTangentDrag(v, TangentHandle::In, target, TangentMode::Free, false);
        QCOMPARE(f.in, QPointF(0.0, 0.3));
        QCOMPARE(f.out, v.out);
        QVERIFY(!f.smooth());

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

    // The overlay used to invert the clip's whole 4x4 pose and divide by w; the mapper inverts its
    // z = 0 plane. A tipped, spun clip must read the same either way.
    void mapperMatchesFullMatrixInverseOnTiltedPose()
    {
        const QSizeF canvas(1920, 1080);
        const QRectF rect(120, 80, 400, 300);
        drift::ClipPose3d pose;
        pose.rotationX = 25.0;
        pose.rotationY = -18.0;
        pose.positionZ = 60.0;
        pose.perspective = 1200.0;
        const QMatrix4x4 layoutToCanvas = drift::clipLocalToCanvas(rect, 12.0, pose, canvas);

        ClipFrameMapper m;
        m.setViewport(canvas, QRectF(0, 0, 1920, 1080));
        m.setClip(QRectF(0, 0, rect.width(), rect.height()), 0.0);
        m.setPose(layoutToCanvas);

        const QMatrix4x4 full = layoutToCanvas.inverted();
        for (const QPointF &layout : {QPointF(0, 0), QPointF(400, 300), QPointF(200, 150), QPointF(37, 281)}) {
            const QPointF onCanvas = m.layoutToCanvas(layout);
            const QVector4D v = full.map(QVector4D(float(onCanvas.x()), float(onCanvas.y()), 0.f, 1.f));
            const QPointF viaFull(v.x() / v.w(), v.y() / v.w());
            QVERIFY(QLineF(m.canvasToLayout(onCanvas), viaFull).length() < 1e-2);
            QVERIFY(QLineF(m.canvasToLayout(onCanvas), layout).length() < 1e-2);
            QVERIFY(std::abs(v.z() / v.w()) < 1e-2);
        }
    }

    void mapperParentMatchesFullMatrixInverse()
    {
        // A homography lifted to 4x4 the way the preview lifts a transform layer's matrix.
        const QTransform t(1.1, 0.08, 0.0004, -0.05, 0.9, 0.0002, 30, -20, 1);
        const QMatrix4x4 lifted(float(t.m11()), float(t.m21()), 0.f, float(t.m31()),
                                float(t.m12()), float(t.m22()), 0.f, float(t.m32()),
                                0.f, 0.f, 1.f, 0.f,
                                float(t.m13()), float(t.m23()), 0.f, float(t.m33()));
        ClipFrameMapper m;
        m.setParent(lifted);
        const QMatrix4x4 full = lifted.inverted();
        for (const QPointF &p : {QPointF(10, 10), QPointF(640, 360), QPointF(300, 90)}) {
            const QVector4D v = full.map(QVector4D(float(p.x()), float(p.y()), 0.f, 1.f));
            QVERIFY(QLineF(m.toParentLocal(p), QPointF(v.x() / v.w(), v.y() / v.w())).length() < 1e-2);
        }
    }

    // ---- format 32: bezier mask vertices ----

    static drift::Project maskProject(const drift::Mask &mask)
    {
        drift::Project project;
        project.tracks().clear();
        project.tracks().append(drift::Track{.type = drift::TrackType::Video});
        drift::Clip clip;
        clip.id = QStringLiteral("mask-clip");
        clip.type = drift::ClipType::Adjustment;
        clip.adjustmentKind = drift::AdjustmentKind::Mask;
        clip.timelineDuration = drift::secondsToUs(4.0);
        clip.mask = mask;
        project.tracks()[0].clips.append(clip);
        return project;
    }

    static QJsonObject &maskJson(QJsonObject &projectJson, QJsonObject &holder)
    {
        holder = projectJson.value(QStringLiteral("tracks")).toArray().at(0).toObject()
                     .value(QStringLiteral("clips")).toArray().at(0).toObject()
                     .value(QStringLiteral("mask")).toObject();
        return holder;
    }

    void format31PolygonMaskLoadsAsCornersAndRendersIdentically()
    {
        const QList<QPointF> poly = {{0.2, 0.2}, {0.8, 0.25}, {0.7, 0.8}, {0.25, 0.7}};
        drift::Mask mask;
        mask.shape = drift::MaskShape::Freeform;
        mask.vertices = drift::cornerVertices(QVector<QPointF>(poly.begin(), poly.end()));
        mask.pathKeys.insert(0, mask.vertices);
        mask.pathKeys.insert(drift::secondsToUs(1.0),
                             drift::cornerVertices({{0.3, 0.2}, {0.9, 0.25}, {0.7, 0.9}, {0.2, 0.7}}));

        // A format 31 file: version 31 and a mask whose points and pathKeys are bare [x, y] pairs.
        QJsonObject json = maskProject(mask).toJson();
        json.insert(QStringLiteral("version"), 31);
        QJsonObject legacyMask;
        maskJson(json, legacyMask);
        for (const QJsonValue &v : legacyMask.value(QStringLiteral("points")).toArray())
            QCOMPARE(v.toArray().size(), 2);
        for (const QJsonValue &key : legacyMask.value(QStringLiteral("pathKeys")).toArray()) {
            for (const QJsonValue &v : key.toArray().at(1).toArray())
                QCOMPARE(v.toArray().size(), 2);
        }

        QString error;
        const drift::Project loaded = drift::Project::fromJson(json, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const drift::Mask &m = loaded.tracks().at(0).clips.at(0).mask;
        QCOMPARE(m.vertices.size(), 4);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(m.vertices.at(i).p, poly.at(i));
            QVERIFY(m.vertices.at(i).in.isNull() && m.vertices.at(i).out.isNull());
            QCOMPARE(m.vertices.at(i).mode, drift::TangentMode::Corner);
        }
        QCOMPARE(m.pathKeys.size(), 2);
        QCOMPARE(m.pathKeys.last().at(1).p, QPointF(0.9, 0.25));

        const int w = 1920;
        const int h = 1080;
        QPainterPath old;
        old.moveTo(poly.at(0).x() * w, poly.at(0).y() * h);
        for (int i = 1; i < poly.size(); ++i)
            old.lineTo(poly.at(i).x() * w, poly.at(i).y() * h);
        old.closeSubpath();

        const QPainterPath now = drift::maskPath(m, w, h);
        QCOMPARE(now.elementCount(), old.elementCount());
        for (int i = 0; i < now.elementCount(); ++i) {
            QCOMPARE(now.elementAt(i).type, old.elementAt(i).type);
            QCOMPARE(now.elementAt(i).x, old.elementAt(i).x);
            QCOMPARE(now.elementAt(i).y, old.elementAt(i).y);
        }

        const drift::Mask halfway = m.resolvedAt(drift::secondsToUs(0.5));
        QVERIFY(std::abs(halfway.vertices.at(0).p.x() - 0.25) < 1e-9);
    }

    void format32MaskRoundTripsHandlesAndModes()
    {
        drift::Mask mask;
        mask.shape = drift::MaskShape::Freeform;
        mask.vertices = {ShapeVertex{{0.2, 0.5}, {-0.1, 0.0}, {0.1, 0.0}, TangentMode::Mirrored},
                         ShapeVertex{{0.5, 0.2}, {-0.05, 0.02}, {0.08, 0.0}, TangentMode::Aligned},
                         ShapeVertex{{0.8, 0.5}, {0.0, -0.1}, {0.0, 0.1}, TangentMode::Free},
                         ShapeVertex{{0.5, 0.8}, {}, {}, TangentMode::Corner}};
        mask.pathKeys.insert(drift::secondsToUs(1.0), mask.vertices);
        QVector<ShapeVertex> later = mask.vertices;
        later[0].out = {0.3, 0.1};
        mask.pathKeys.insert(drift::secondsToUs(2.0), later);

        const QJsonObject json = maskProject(mask).toJson();
        QCOMPARE(json.value(QStringLiteral("version")).toInt(), 32);

        QString error;
        const drift::Project loaded = drift::Project::fromJson(json, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const drift::Mask &m = loaded.tracks().at(0).clips.at(0).mask;
        QCOMPARE(m.vertices, mask.vertices);
        QCOMPARE(m.pathKeys, mask.pathKeys);

        const drift::Mask mid = m.resolvedAt(drift::secondsToUs(1.5));
        QVERIFY(std::abs(mid.vertices.at(0).out.x() - 0.2) < 1e-9);
        QVERIFY(std::abs(mid.vertices.at(0).out.y() - 0.05) < 1e-9);
        QCOMPARE(mid.vertices.at(1).mode, TangentMode::Aligned);

        const QPainterPath path = drift::maskPath(m, 100, 100);
        bool anyCurve = false;
        for (int i = 0; i < path.elementCount(); ++i)
            anyCurve = anyCurve || path.elementAt(i).type == QPainterPath::CurveToElement;
        QVERIFY(anyCurve);
    }

    void shapePathVertexJsonKeepsOldSmoothMapping()
    {
        // A pre-mode file: smooth:true is mirrored, absent is a corner.
        ShapeStyle style;
        style.kind = ShapeKind::Path;
        QJsonObject o = shapeStyleToJson(style);
        o.insert(QStringLiteral("contours"),
                 QJsonArray{QJsonObject{{QStringLiteral("closed"), true},
                                        {QStringLiteral("vertices"),
                                         QJsonArray{QJsonObject{{QStringLiteral("x"), 0.1}, {QStringLiteral("y"), 0.2},
                                                                {QStringLiteral("inX"), -0.1}, {QStringLiteral("outX"), 0.1},
                                                                {QStringLiteral("smooth"), true}},
                                                    QJsonObject{{QStringLiteral("x"), 0.9}, {QStringLiteral("y"), 0.9}}}}}});
        const ShapeStyle old = shapeStyleFromJson(o);
        QCOMPARE(old.contours.size(), 1);
        QCOMPARE(old.contours.at(0).vertices.at(0).mode, TangentMode::Mirrored);
        QCOMPARE(old.contours.at(0).vertices.at(0).out, QPointF(0.1, 0.0));
        QCOMPARE(old.contours.at(0).vertices.at(1).mode, TangentMode::Corner);

        ShapeStyle styled = old;
        styled.contours[0].vertices[1].mode = TangentMode::Aligned;
        styled.contours[0].vertices.append(ShapeVertex{{0.5, 0.5}, {-0.1, 0.0}, {0.1, 0.0}, TangentMode::Free});
        const QJsonObject written = shapeStyleToJson(styled);
        const QJsonArray vertices = written.value(QStringLiteral("contours")).toArray().at(0).toObject()
                                        .value(QStringLiteral("vertices")).toArray();
        QVERIFY(vertices.at(0).toObject().value(QStringLiteral("smooth")).toBool());
        QVERIFY(!vertices.at(0).toObject().contains(QStringLiteral("mode")));
        QVERIFY(vertices.at(1).toObject().value(QStringLiteral("smooth")).toBool());
        QCOMPARE(vertices.at(1).toObject().value(QStringLiteral("mode")).toString(), QStringLiteral("aligned"));
        QVERIFY(!vertices.at(2).toObject().value(QStringLiteral("smooth")).toBool());
        QCOMPARE(shapeStyleFromJson(written).contours, styled.contours);
    }

    // ---- PathEditCore: the geometry behind the pen / path editor ----

    void pathHitTestPrefersHandleThenVertexThenSegmentThenInside()
    {
        PathEditCore core = squareCore();
        const PathEditCore::Tolerances tol;
        QCOMPARE(core.hitTest(px(0.2, 0.2) + QPointF(3, 2), tol).kind, PathEditCore::HitKind::Vertex);
        PathEditCore::Hit seg = core.hitTest(px(0.5, 0.2) + QPointF(0, 4), tol);
        QCOMPARE(seg.kind, PathEditCore::HitKind::Segment);
        QCOMPARE(seg.segment, 0);
        QVERIFY(std::abs(seg.t - 0.5) < 0.05);
        QCOMPARE(core.hitTest(px(0.5, 0.5), tol).kind, PathEditCore::HitKind::Inside);
        QCOMPARE(core.hitTest(px(0.05, 0.05), tol).kind, PathEditCore::HitKind::None);

        // A selected vertex shows its handles, and they win over a vertex or edge beneath them.
        ShapeContour c = core.contours().first();
        c.vertices[0] = vtx(0.2, 0.2, -0.05, 0, 0.1, 0);
        core.setContours({c});
        QVERIFY(core.visibleHandles().isEmpty());
        core.select(0, 0, PathEditCore::SelectMode::Replace);
        QCOMPARE(core.visibleHandles().size(), 2);
        const PathEditCore::Hit h = core.hitTest(px(0.3, 0.2), tol);
        QCOMPARE(h.kind, PathEditCore::HitKind::Handle);
        QCOMPARE(h.handle, geom::TangentHandle::Out);

        // Touch tolerance reaches further than the mouse's.
        PathEditCore::Tolerances touch{24, 24, 20};
        QCOMPARE(core.hitTest(px(0.8, 0.2) + QPointF(15, 0), tol).kind != PathEditCore::HitKind::Vertex, true);
        QCOMPARE(core.hitTest(px(0.8, 0.2) + QPointF(15, 0), touch).kind, PathEditCore::HitKind::Vertex);
    }

    void pathInsertKeepsShapeAndRecordsOp()
    {
        PathEditCore core;
        core.setView(QTransform().scale(100, 100));
        ShapeContour c = curvy();
        core.setContours({c});
        const QVector<QPointF> before = flatten(c, 100);
        const int at = core.insertAt(0, 0, 0.4);
        QCOMPARE(at, 1);
        QCOMPARE(core.contours().first().vertices.size(), 4);
        double worst = 0;
        for (const QPointF &p : flatten(core.contours().first(), 100))
            worst = std::max(worst, distToPoly(before, p));
        QVERIFY(worst < 1e-3);
        QCOMPARE(core.selection().size(), 1);
        const QVector<PathOp> ops = core.takeOps();
        QCOMPARE(ops.size(), 1);
        QVERIFY(core.ops().isEmpty());

        ShapeContour key = curvy();
        QVERIFY(applyPathOp(key, ops.first()));
        QCOMPARE(key.vertices, core.contours().first().vertices);
    }

    void pathDeleteHealsOrNot()
    {
        ShapeContour c;
        c.closed = false;
        c.vertices = {vtx(0, 0, 0, 0, 0.1, 0.2), vtx(0.5, 0.35, -0.1, 0, 0.1, 0), vtx(1, 0, 0, 0.2, 0, 0)};
        const QVector<QPointF> ref = flatten(c, 200);

        PathEditCore healed;
        healed.setView(QTransform().scale(100, 100));
        healed.setContours({c});
        healed.select(0, 1, PathEditCore::SelectMode::Replace);
        QVERIFY(healed.deleteSelected(true));
        PathEditCore plain;
        plain.setView(QTransform().scale(100, 100));
        plain.setContours({c});
        plain.select(0, 1, PathEditCore::SelectMode::Replace);
        QVERIFY(plain.deleteSelected(false));
        double h = 0, p = 0;
        for (const QPointF &pt : flatten(healed.contours().first(), 64))
            h = std::max(h, distToPoly(ref, pt));
        for (const QPointF &pt : flatten(plain.contours().first(), 64))
            p = std::max(p, distToPoly(ref, pt));
        QVERIFY(h < p);
        QVERIFY(healed.ops().first().heal);
        QVERIFY(!plain.ops().first().heal);
        QCOMPARE(healed.selection().size(), 1);

        // A shape path never loses its last contour, and a short one disappears.
        PathEditCore last;
        last.setContours({ShapeContour{{vtx(0, 0), vtx(1, 1)}, false}});
        last.select(0, 0, PathEditCore::SelectMode::Replace);
        QVERIFY(!last.deleteSelected(true));
        PathEditCore two;
        two.setContours({ShapeContour{{vtx(0, 0), vtx(1, 1)}, false}, curvy()});
        two.select(0, 0, PathEditCore::SelectMode::Replace);
        QVERIFY(two.deleteSelected(false));
        QCOMPARE(two.contours().size(), 1);
    }

    void pathMaskKeepsThreeVertices()
    {
        PathEditCore core = squareCore(true);
        core.select(0, 0, PathEditCore::SelectMode::Replace);
        QVERIFY(core.deleteSelected(false));
        QCOMPARE(core.contours().first().vertices.size(), 3);
        core.select(0, 0, PathEditCore::SelectMode::Replace);
        QVERIFY(!core.deleteSelected(false));
        QCOMPARE(core.contours().first().vertices.size(), 3);
        QVERIFY(!core.toggleClosed(0));
    }

    void pathPenCornerSmoothAndClose()
    {
        PathEditCore core;
        core.setView(QTransform().scale(100, 100));
        core.setFrameSize({100, 100});
        ManipModifiers none;
        QCOMPARE(core.penPress({10, 10}, none, 8), PathEditCore::PenResult::Added);
        QCOMPARE(core.penContour(), 0);
        core.endGesture();

        QCOMPARE(core.penPress({60, 10}, none, 8), PathEditCore::PenResult::Added);
        core.beginPenDrag(0, 1, {60, 10});
        QVERIFY(!core.drag({61, 10}, none));
        QVERIFY(core.drag({80, 30}, none));
        core.endGesture();
        const ShapeVertex smooth = core.contours().first().vertices[1];
        QCOMPARE(smooth.mode, TangentMode::Mirrored);
        QVERIFY(std::abs(smooth.out.x() - 0.2) < 1e-9 && std::abs(smooth.out.y() - 0.2) < 1e-9);
        QCOMPARE(smooth.in, -smooth.out);

        QCOMPARE(core.penPress({50, 70}, none, 8), PathEditCore::PenResult::Added);
        core.endGesture();
        QCOMPARE(core.contours().first().vertices[2].mode, TangentMode::Corner);
        QVERIFY(core.penNearFirst({12, 12}, 8));
        QVERIFY(!core.penNearFirst({40, 40}, 8));
        QCOMPARE(core.penPress({11, 11}, none, 8), PathEditCore::PenResult::Closed);
        QVERIFY(core.contours().first().closed);
        QCOMPARE(core.penContour(), -1);
        QCOMPARE(core.contours().first().vertices.size(), 3);

        // Shift holds the next point to 45 degrees from the last.
        PathEditCore snapped;
        snapped.setView(QTransform().scale(100, 100));
        snapped.penPress({0, 0}, none, 8);
        ManipModifiers shift;
        shift.constrain = true;
        snapped.penPress({50, 8}, shift, 8);
        const QPointF p = snapped.contours().first().vertices[1].p;
        QVERIFY(std::abs(p.y()) < 1e-9 && p.x() > 0.4);

        QVERIFY(core.penUndo() == false);
        PathEditCore draft;
        draft.setView(QTransform().scale(100, 100));
        draft.penPress({10, 10}, none, 8);
        draft.penPress({50, 10}, none, 8);
        QVERIFY(draft.penUndo());
        QCOMPARE(draft.contours().first().vertices.size(), 1);
        QVERIFY(draft.penUndo());
        QVERIFY(draft.contours().isEmpty());
    }

    void pathPenContinuesAnOpenEnd()
    {
        PathEditCore core;
        core.setView(QTransform().scale(100, 100));
        core.setContours({ShapeContour{{vtx(0.1, 0.1, 0, 0, 0.1, 0), vtx(0.5, 0.5), vtx(0.9, 0.1)}, false}});
        QVERIFY(core.isOpenEnd(0, 0));
        QVERIFY(core.isOpenEnd(0, 2));
        QVERIFY(!core.isOpenEnd(0, 1));
        QVERIFY(core.penContinue(0, 0));
        const ShapeContour &c = core.contours().first();
        QCOMPARE(c.vertices.last().p, QPointF(0.1, 0.1));
        QCOMPARE(c.vertices.last().in, QPointF(0.1, 0.0));
        QCOMPARE(core.penContour(), 0);
    }

    void pathConvertAndModes()
    {
        PathEditCore core = squareCore();
        QVERIFY(core.toggleSmooth(0, 1));
        const ShapeVertex v = core.contours().first().vertices[1];
        QCOMPARE(v.mode, TangentMode::Mirrored);
        QVERIFY(!v.out.isNull());
        QCOMPARE(v.in, -v.out);
        QVERIFY(core.toggleSmooth(0, 1));
        QCOMPARE(core.contours().first().vertices[1].mode, TangentMode::Corner);
        QVERIFY(core.contours().first().vertices[1].out.isNull());

        core.select(0, 1, PathEditCore::SelectMode::Replace);
        core.select(0, 2, PathEditCore::SelectMode::Add);
        QCOMPARE(core.selectedMode(), int(TangentMode::Corner));
        QVERIFY(core.setSelectedMode(TangentMode::Aligned));
        QCOMPARE(core.selectedMode(), int(TangentMode::Aligned));
        QVERIFY(!core.contours().first().vertices[2].out.isNull());
        core.select(0, 0, PathEditCore::SelectMode::Add);
        QCOMPARE(core.selectedMode(), -2);
        QVERIFY(core.setSelectedMode(TangentMode::Corner));
        QVERIFY(core.contours().first().vertices[2].in.isNull());
    }

    void pathHandleDragBreaksWithAltAndConstrainsWithShift()
    {
        PathEditCore core;
        core.setView(QTransform().scale(100, 100));
        core.setFrameSize({100, 100});
        core.setContours({ShapeContour{{vtx(0.2, 0.5, -0.1, 0, 0.1, 0), vtx(0.9, 0.5)}, false}});
        core.select(0, 0, PathEditCore::SelectMode::Replace);

        ManipModifiers none;
        core.beginHandle(0, 0, geom::TangentHandle::Out, {30, 50}, none);
        QVERIFY(core.drag({30, 80}, none));
        core.endGesture();
        ShapeVertex v = core.contours().first().vertices[0];
        QCOMPARE(v.mode, TangentMode::Mirrored);
        QCOMPARE(v.in, -v.out);

        ManipModifiers alt;
        alt.breakTangent = true;
        core.beginHandle(0, 0, geom::TangentHandle::Out, {30, 80}, alt);
        QVERIFY(core.drag({60, 50}, alt));
        core.endGesture();
        v = core.contours().first().vertices[0];
        QCOMPARE(v.mode, TangentMode::Free);
        QVERIFY(std::abs(v.in.x() + 0.1) < 1e-9 || v.in != -v.out);
        QVERIFY(std::abs(v.out.x() - 0.4) < 1e-9);

        ManipModifiers shift;
        shift.constrain = true;
        core.beginHandle(0, 0, geom::TangentHandle::In, {10, 50}, shift);
        QVERIFY(core.drag({5, 59}, shift));
        core.endGesture();
        const QPointF in = core.contours().first().vertices[0].in;
        QVERIFY(std::abs(std::abs(in.x()) - std::abs(in.y())) < 1e-9 || std::abs(in.y()) < 1e-9
                || std::abs(in.x()) < 1e-9);
    }

    void pathVertexDragMovesGroupLocksAxisAndCancels()
    {
        PathEditCore core = squareCore();
        core.select(0, 0, PathEditCore::SelectMode::Replace);
        core.select(0, 1, PathEditCore::SelectMode::Add);
        ManipModifiers noSnap;
        noSnap.noSnap = true;
        core.beginMove(0, 0, px(0.2, 0.2));
        QVERIFY(core.drag(px(0.2, 0.2) + QPointF(20, 10), noSnap));
        QCOMPARE(core.contours().first().vertices[0].p, QPointF(0.3, 0.25));
        QCOMPARE(core.contours().first().vertices[1].p, QPointF(0.9, 0.25));
        QCOMPARE(core.contours().first().vertices[2].p, QPointF(0.8, 0.8));
        core.cancelGesture();
        QCOMPARE(core.contours().first().vertices[0].p, QPointF(0.2, 0.2));

        ManipModifiers lock;
        lock.constrain = true;
        lock.noSnap = true;
        core.beginMove(0, 0, px(0.2, 0.2));
        QVERIFY(core.drag(px(0.2, 0.2) + QPointF(30, 5), lock));
        QCOMPARE(core.contours().first().vertices[0].p, QPointF(0.35, 0.2));
        core.endGesture();

        // A view change mid-gesture waits for the gesture to end.
        const QTransform before = core.view();
        core.beginMove(0, 0, px(0.35, 0.2));
        core.setView(QTransform().scale(10, 10));
        QCOMPARE(core.view(), before);
        core.endGesture();
        QCOMPARE(core.view(), QTransform().scale(10, 10));
    }

    void pathVertexDragSnapsToFrameUnlessCtrl()
    {
        PathEditCore core = squareCore();
        core.select(0, 0, PathEditCore::SelectMode::Replace);
        ManipModifiers snap;
        core.beginMove(0, 0, px(0.2, 0.2));
        // 0.5 is a frame target; two px away pulls it in.
        QVERIFY(core.drag(px(0.5, 0.2) + QPointF(2, 0), snap));
        QCOMPARE(core.contours().first().vertices[0].p.x(), 0.5);
        QCOMPARE(core.snapGuideX(), 0.5);
        core.cancelGesture();
        ManipModifiers noSnap;
        noSnap.noSnap = true;
        core.beginMove(0, 0, px(0.2, 0.2));
        QVERIFY(core.drag(px(0.5, 0.2) + QPointF(2, 0), noSnap));
        QVERIFY(core.contours().first().vertices[0].p.x() != 0.5);
        QCOMPARE(core.snapGuideX(), -1.0);
        core.cancelGesture();
    }

    void pathBendSegmentKeepsEndpoints()
    {
        PathEditCore core = squareCore();
        ManipModifiers noSnap;
        noSnap.noSnap = true;
        core.beginBend(0, 0, 0.5, px(0.5, 0.2));
        QVERIFY(core.drag(px(0.5, 0.2) + QPointF(0, 30), noSnap));
        core.endGesture();
        const ShapeContour &c = core.contours().first();
        QCOMPARE(c.vertices[0].p, QPointF(0.2, 0.2));
        QCOMPARE(c.vertices[1].p, QPointF(0.8, 0.2));
        const QPointF mid = cubicPoint(c.vertices[0], c.vertices[1], 0.5);
        QVERIFY(std::abs(mid.y() - 0.35) < 1e-6);
        QVERIFY(std::abs(mid.x() - 0.5) < 1e-6);
    }

    void pathMarqueeSelectAndNudge()
    {
        PathEditCore core = squareCore();
        core.beginMarquee();
        core.selectInRect(QRectF(px(0.1, 0.1), px(0.9, 0.5)), PathEditCore::SelectMode::Replace);
        QCOMPARE(core.selection().size(), 2);
        core.beginMarquee();
        core.selectInRect(QRectF(px(0.1, 0.7), px(0.9, 0.9)), PathEditCore::SelectMode::Add);
        QCOMPARE(core.selection().size(), 4);
        core.beginMarquee();
        core.selectInRect(QRectF(px(0.1, 0.1), px(0.9, 0.5)), PathEditCore::SelectMode::Toggle);
        QCOMPARE(core.selection().size(), 2);

        core.clearSelection();
        core.select(0, 2, PathEditCore::SelectMode::Replace);
        QVERIFY(core.nudge(10, -20));
        QCOMPARE(core.contours().first().vertices[2].p, QPointF(0.85, 0.7));
        QCOMPARE(core.contours().first().vertices[0].p, QPointF(0.2, 0.2));
        core.selectAll();
        QCOMPARE(core.selection().size(), 4);
        core.clearSelection();
        QVERIFY(core.nudge(20, 0));
        QCOMPARE(core.contours().first().vertices[0].p, QPointF(0.3, 0.2));
    }

    void pathBodyDragMovesEverythingAndFlagsIt()
    {
        PathEditCore core = squareCore();
        ManipModifiers noSnap;
        noSnap.noSnap = true;
        core.beginBody(px(0.5, 0.5));
        QVERIFY(!core.bodyMoved());
        QVERIFY(core.drag(px(0.5, 0.5) + QPointF(20, 0), noSnap));
        QVERIFY(core.bodyMoved());
        QCOMPARE(core.contours().first().vertices[0].p, QPointF(0.3, 0.2));
        QCOMPARE(core.contours().first().vertices[2].p, QPointF(0.9, 0.8));
    }

    void pathOpsReplayKeepsAnimationKeysInStep()
    {
        ShapeContour a = squareCore().contours().first();
        ShapeContour key = a;
        key.vertices[2].p = {0.9, 0.9};

        PathEditCore core;
        core.setView(QTransform().scale(100, 100));
        core.setContours({a});
        core.insertAt(0, 1, 0.5);
        core.select(0, 0, PathEditCore::SelectMode::Replace);
        QVERIFY(core.deleteSelected(false));
        QCOMPARE(core.contours().first().vertices.size(), 4);
        for (const PathOp &op : core.ops())
            QVERIFY(applyPathOp(key, op));
        QCOMPARE(key.vertices.size(), 4);
        QCOMPARE(key.vertices[2].p, QPointF(0.9, 0.9));
        QVERIFY(!applyPathOp(key, PathOp{PathOp::Kind::Remove, 9, 0.0, false}));
    }

    void strokeSmootherOffFollowsTheCursor()
    {
        StrokeSmoother s;
        s.begin(0, 0);
        QCOMPARE(s.follow(30, 40, 0.0), QPointF(30, 40));
    }

    void strokeSmootherHeadStaysSlackInsideTheString()
    {
        StrokeSmoother s;
        s.begin(0, 0);
        const double length = 0.5 * StrokeSmoother::kMaxStringPx;
        const QPointF head = s.follow(length * 0.5, 0, 0.5);
        QCOMPARE(head, QPointF(0, 0));
    }

    void strokeSmootherPullsTheHeadTautAlongTheCursorLine()
    {
        StrokeSmoother s;
        s.begin(0, 0);
        const double length = 0.5 * StrokeSmoother::kMaxStringPx;
        const QPointF head = s.follow(100, 0, 0.5);
        QVERIFY(qFuzzyCompare(head.x(), 100 - length));
        QVERIFY(qFuzzyIsNull(head.y()));
        const QPointF next = s.follow(100, 100, 0.5);
        QVERIFY(std::abs(std::hypot(next.x() - 100, next.y() - 100) - length) < 1e-9);
    }

    void gizmoSnapStepsRoundAndZeroLeavesFree()
    {
        using drift::gizmo::snapToStep;
        QCOMPARE(snapToStep(47.0, 10.0), 50.0);
        QCOMPARE(snapToStep(-0.04, 0.1), 0.0);
        QVERIFY(std::abs(snapToStep(1.27, 0.1) - 1.3) < 1e-9);
        QCOMPARE(snapToStep(37.0, 15.0), 30.0);
        QCOMPARE(snapToStep(37.5, 0.0), 37.5);
        QCOMPARE(snapToStep(37.5, -5.0), 37.5);
    }

    void gizmoIsFreeUntilSnapIsAsked()
    {
        using namespace drift::gizmo;
        const Pose pose = centredPose();
        const QPointF o = gizmoOrigin(pose);

        const DragResult freeMove = drag(pose, Tool::Move, Orientation::World, QStringLiteral("x"), o,
                                         o + QPointF(47, 0), false, 1.0, {}, 10.0, 15.0);
        QVERIFY(std::abs(freeMove.pose.rect.x() - (pose.rect.x() + 47.0)) < 1e-6);
        const DragResult snapMove = drag(pose, Tool::Move, Orientation::World, QStringLiteral("x"), o,
                                         o + QPointF(47, 0), true, 1.0, {}, 10.0, 15.0);
        QVERIFY(std::abs(snapMove.pose.rect.x() - (pose.rect.x() + 50.0)) < 1e-6);
        QVERIFY(std::abs(snapMove.amount - 50.0) < 1e-6);

        const QPointF from = o + QPointF(100, 0);
        const double radians = 37.0 * std::numbers::pi / 180.0;
        const QPointF to = o + QPointF(100 * std::cos(radians), 100 * std::sin(radians));
        const DragResult freeTurn = drag(pose, Tool::Rotate, Orientation::World, QStringLiteral("z"), from, to,
                                         false, 1.0, {}, 0.0, 15.0);
        QVERIFY2(std::abs(std::abs(freeTurn.pose.rotation) - 37.0) < 0.1, qPrintable(QString::number(freeTurn.pose.rotation)));
        const DragResult snapTurn = drag(pose, Tool::Rotate, Orientation::World, QStringLiteral("z"), from, to,
                                         true, 1.0, {}, 0.0, 15.0);
        QVERIFY2(std::abs(std::abs(snapTurn.pose.rotation) - 30.0) < 0.1, qPrintable(QString::number(snapTurn.pose.rotation)));

        DragExtras step;
        step.scaleStep = 0.1;
        const QPointF tip = o + QPointF(84, 0);
        const DragResult freeScale = drag(pose, Tool::Scale, Orientation::Local, QStringLiteral("x"), tip,
                                          o + QPointF(84 * 1.27, 0), false, 1.0, {}, 0.0, 15.0, step);
        QVERIFY(std::abs(freeScale.pose.rect.width() - 400 * 1.27) < 0.5);
        const DragResult snapScale = drag(pose, Tool::Scale, Orientation::Local, QStringLiteral("x"), tip,
                                          o + QPointF(84 * 1.27, 0), true, 1.0, {}, 0.0, 15.0, step);
        QVERIFY(std::abs(snapScale.pose.rect.width() - 400 * 1.3) < 0.5);
        QVERIFY(std::abs(snapScale.amount - 1.3) < 1e-6);
    }

    void gizmoAxisLockReplacesTheHandlesAxis()
    {
        using namespace drift::gizmo;
        const Pose pose = centredPose();
        const QPointF o = gizmoOrigin(pose);

        DragExtras lockY;
        lockY.lockAxis = 1;
        const DragResult onY = drag(pose, Tool::Move, Orientation::World, QStringLiteral("x"), o,
                                    o + QPointF(30, 40), false, 1.0, {}, 0.0, 15.0, lockY);
        QVERIFY(std::abs(onY.pose.rect.x() - pose.rect.x()) < 1e-6);
        QVERIFY(std::abs(onY.pose.rect.y() - (pose.rect.y() + 40.0)) < 1.0);

        DragExtras planeXY;
        planeXY.lockAxis = 2;
        planeXY.lockPlane = true;
        const DragResult inPlane = drag(pose, Tool::Move, Orientation::World, QStringLiteral("x"), o,
                                        o + QPointF(30, 40), false, 1.0, {}, 0.0, 15.0, planeXY);
        QVERIFY(std::abs(inPlane.pose.rect.x() - (pose.rect.x() + 30.0)) < 1e-6);
        QVERIFY(std::abs(inPlane.pose.rect.y() - (pose.rect.y() + 40.0)) < 1e-6);
        QVERIFY(std::abs(inPlane.pose.pose3d.positionZ) < 1e-6);
    }

    void gizmoPlaneLockIgnoresAnEdgeOnPlane()
    {
        using namespace drift::gizmo;
        const Pose pose = centredPose();
        const QPointF o = gizmoOrigin(pose);
        // The eye looks along z, so the plane without X is seen edge-on.
        DragExtras planeYZ;
        planeYZ.lockAxis = 0;
        planeYZ.lockPlane = true;
        const DragResult r = drag(pose, Tool::Move, Orientation::World, QStringLiteral("y"), o,
                                  o + QPointF(30, 40), false, 1.0, {}, 0.0, 15.0, planeYZ);
        QCOMPARE(r.pose.rect, pose.rect);
        QCOMPARE(r.pose.pose3d.positionZ, pose.pose3d.positionZ);
    }

    void gizmoLockFollowsTheOrientationOverride()
    {
        using namespace drift::gizmo;
        Pose pose = centredPose();
        pose.pose3d.rotationY = 45.0;
        const QPointF o = gizmoOrigin(pose);

        DragExtras world;
        world.lockAxis = 0;
        const DragResult onWorld = drag(pose, Tool::Move, Orientation::World, QStringLiteral("y"), o,
                                        o + QPointF(50, 0), false, 1.0, {}, 0.0, 15.0, world);
        QVERIFY(std::abs(onWorld.pose.pose3d.positionZ) < 1e-6);

        DragExtras local = world;
        local.orientation = Orientation::Local;
        const DragResult onLocal = drag(pose, Tool::Move, Orientation::World, QStringLiteral("y"), o,
                                        o + QPointF(50, 0), false, 1.0, {}, 0.0, 15.0, local);
        QVERIFY(std::abs(onLocal.pose.pose3d.positionZ) > 1.0);
    }

    void gizmoTypedValueReplacesThePointer()
    {
        using namespace drift::gizmo;
        const Pose pose = centredPose();
        const QPointF o = gizmoOrigin(pose);

        DragExtras typed;
        typed.typed = 12.5;
        const DragResult moved = drag(pose, Tool::Move, Orientation::World, QStringLiteral("x"), o, o + QPointF(300, 90),
                                      true, 1.0, {}, 10.0, 15.0, typed);
        QVERIFY(std::abs(moved.pose.rect.x() - (pose.rect.x() + 12.5)) < 1e-6);
        QVERIFY(std::abs(moved.pose.rect.y() - pose.rect.y()) < 1e-6);

        typed.typed = 30.0;
        const DragResult turned = drag(pose, Tool::Rotate, Orientation::World, QStringLiteral("z"), o, o,
                                       false, 1.0, {}, 0.0, 15.0, typed);
        QVERIFY(std::abs(turned.pose.rotation - 30.0) < 1e-6);

        typed.typed = 1.5;
        const DragResult scaled = drag(pose, Tool::Scale, Orientation::Local, QStringLiteral("y"), o, o,
                                       false, 1.0, {}, 0.0, 15.0, typed);
        QVERIFY(std::abs(scaled.pose.rect.height() - 300 * 1.5) < 1e-6);
        QVERIFY(std::abs(scaled.pose.rect.width() - 400) < 1e-6);
    }

    void gizmoHandleAlphaThresholds()
    {
        using namespace drift::gizmo;
        QCOMPARE(handleAlpha(0.0), 1.0);
        QCOMPARE(handleAlpha(kFadeStart), 1.0);
        QCOMPARE(handleAlpha(kFadeEnd), 0.0);
        QCOMPARE(handleAlpha(1.0), 0.0);
        const double mid = handleAlpha((kFadeStart + kFadeEnd) / 2);
        QVERIFY(std::abs(mid - 0.5) < 1e-9);
        QVERIFY(handleAlpha(0.9) > handleAlpha(0.92));
    }

    void gizmoFadesAndStopsPickingAnAxisAlongTheView()
    {
        using namespace drift::gizmo;
        const auto xHandle = [](const Geometry &g) -> const Handle * {
            for (const Handle &h : g.handles) {
                if (h.id == QLatin1String("x"))
                    return &h;
            }
            return nullptr;
        };
        const auto tipOf = [](const Handle &h) { return h.front.first().last(); };
        // Alone, so a neighbouring handle drawn over its tip cannot answer for it.
        const auto pickX = [&](const Geometry &g) {
            Geometry only = g;
            only.handles = {*xHandle(g)};
            return pick(only, tipOf(*xHandle(g)), 8.0);
        };

        Pose pose = centredPose();
        Geometry g = geometry(pose, Tool::Move, Orientation::Local, 1.0);
        QVERIFY(xHandle(g));
        QCOMPARE(xHandle(g)->alpha, 1.0);
        QCOMPARE(pickX(g), QStringLiteral("x"));

        // Local X leans 25 degrees off the view direction: faded, still grabbable.
        pose.pose3d.rotationY = 65.0;
        g = geometry(pose, Tool::Move, Orientation::Local, 1.0);
        QVERIFY(xHandle(g));
        QVERIFY(xHandle(g)->alpha > kPickableAlpha && xHandle(g)->alpha < 1.0);
        QCOMPARE(pickX(g), QStringLiteral("x"));

        // 20 degrees off: nearly gone and no longer pickable.
        pose.pose3d.rotationY = 70.0;
        g = geometry(pose, Tool::Move, Orientation::Local, 1.0);
        QVERIFY(xHandle(g));
        QVERIFY(xHandle(g)->alpha < kPickableAlpha);
        QVERIFY(pickX(g).isEmpty());
    }

    void gizmoLockGuidesFollowTheLock()
    {
        using namespace drift::gizmo;
        const Pose pose = centredPose();
        const QPointF o = gizmoOrigin(pose);
        const QList<LockGuide> axis = lockGuides(pose, Tool::Move, Orientation::World, 1.0, 0, false);
        QCOMPARE(axis.size(), 1);
        QCOMPARE(axis.first().axis, 0);
        QVERIFY(QLineF(o, axis.first().line.p1()).length() < 1e-6);
        QVERIFY(std::abs(axis.first().line.dy()) < 1e-6 && axis.first().line.dx() > 1.0);
        // The plane without X is the Y and Z axes; Z points at the viewer, so only Y is drawn.
        const QList<LockGuide> plane = lockGuides(pose, Tool::Move, Orientation::World, 1.0, 0, true);
        QCOMPARE(plane.size(), 1);
        QCOMPARE(plane.first().axis, 1);
        QVERIFY(lockGuides(pose, Tool::Move, Orientation::World, 1.0, -1, false).isEmpty());
    }

    // --- CurveGraphModel --------------------------------------------------------------------

    void graphMappingRoundTripsAndZooms()
    {
        CurveGraphModel m;
        m.setupCubic(false);
        m.setPlot(QRectF(10, 10, 200, 100));
        QCOMPARE(m.toPx(0, -0.5), QPointF(10, 110));
        QCOMPARE(m.toPx(1, 1.5), QPointF(210, 10));
        const QPointF px = m.toPx(0.25, 0.5);
        QVERIFY(QLineF(m.fromPx(px), QPointF(0.25, 0.5)).length() < 1e-9);
        m.setProperty("zoomX", true);
        m.setProperty("zoomY", true);
        m.zoomAt(110, 60, 2.0);
        QVERIFY(m.zoomed());
        QVERIFY(QLineF(m.fromPx(QPointF(110, 60)), QPointF(0.5, 0.5)).length() < 1e-9);
        m.panBy(1000, 0);
        QVERIFY(m.fromPx(QPointF(10, 60)).x() >= -1e-9);
        m.resetView();
        QVERIFY(!m.zoomed());
    }

    void graphSpeedAxisIsLogarithmic()
    {
        CurveGraphModel m;
        m.setupSpeed(0.1, 100);
        m.setPlot(QRectF(0, 0, 100, 300));
        QVERIFY(qAbs(m.toPx(0, 1).y() - 200.0) < 1e-9);
        QVERIFY(qAbs(m.fromPx(QPointF(0, 200)).y() - 1.0) < 1e-9);
        QVERIFY(qAbs(m.toPx(0, 0.1).y() - 300.0) < 1e-9);
    }

    void graphPointsKeepTheirOrderAndGap()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        QCOMPARE(m->press(m->toPx(0.4, 0.3), 0, mouse()), QStringLiteral("point"));
        m->move(m->toPx(0.9, 0.3), 0);
        m->move(m->toPx(0.95, 0.3), 0);
        m->release(m->toPx(0.95, 0.3), 0);
        const QVariantList pts = m->points();
        QVERIFY(pts[1].toMap()["x"].toDouble() < pts[2].toMap()["x"].toDouble());
        QVERIFY(qAbs(pts[1].toMap()["x"].toDouble() - (0.7 - 0.002)) < 1e-9);
    }

    void graphEndsStayPinned()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        drag(*m, m->toPx(0, 0), m->toPx(0.3, 0.5));
        QCOMPARE(m->points()[0].toMap()["x"].toDouble(), 0.0);
        QCOMPARE(m->points()[0].toMap()["y"].toDouble(), 0.0);

        CurveGraphModel speed;
        speed.setupSpeed(0.1, 100);
        speed.setPlot(QRectF(0, 0, 200, 300));
        speed.setSnapEnabled(false);
        drag(speed, speed.toPx(1, 1), speed.toPx(0.5, 4));
        const QVariantMap end = speed.points().last().toMap();
        QCOMPARE(end["x"].toDouble(), 1.0);
        QVERIFY(qAbs(end["y"].toDouble() - 4.0) < 0.2);
    }

    void graphYIsClampedToTheBackendRange()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        drag(*m, m->toPx(0.4, 0.3), QPointF(m->toPx(0.4, 0.3).x(), -500));
        QCOMPARE(m->points()[1].toMap()["y"].toDouble(), 1.0);

        CurveGraphModel c;
        c.setupCubic(true);
        c.setPlot(QRectF(0, 0, 100, 100));
        c.setSnapEnabled(false);
        drag(c, c.toPx(1.0 / 3.0, 1.0 / 3.0), c.toPx(0.5, 1.4));
        QCOMPARE(c.cubic()[1].toDouble(), 1.0);
        CurveGraphModel o;
        o.setupCubic(false);
        o.setPlot(QRectF(0, 0, 100, 200));
        o.setSnapEnabled(false);
        drag(o, o.toPx(1.0 / 3.0, 1.0 / 3.0), o.toPx(0.5, 1.4));
        QVERIFY(qAbs(o.cubic()[1].toDouble() - 1.4) < 0.02);
    }

    void graphCubicHandleStaysInsideTheXRangeAndShiftFlattensIt()
    {
        CurveGraphModel c;
        c.setupCubic(false);
        c.setPlot(QRectF(0, 0, 100, 100));
        c.setSnapEnabled(false);
        c.setCubic({0.42, 0.0, 0.58, 1.0});
        drag(c, c.toPx(0.42, 0.0), c.toPx(-0.3, 0.4));
        QCOMPARE(c.cubic()[0].toDouble(), 0.0);
        QVERIFY(qAbs(c.cubic()[1].toDouble() - 0.4) < 0.02);
        drag(c, c.toPx(0.58, 1.0), c.toPx(0.9, 0.2), shiftFlag());
        QVERIFY(qAbs(c.cubic()[2].toDouble() - 0.9) < 0.02);
        QCOMPARE(c.cubic()[3].toDouble(), 1.0);
    }

    void graphSpeedTangentsAreMirroredUntilAltBreaksThem()
    {
        CurveGraphModel m;
        m.setupSpeed(0.1, 100);
        m.setPlot(QRectF(0, 0, 400, 300));
        m.setSnapEnabled(false);
        m.setPoints({gp(0, 1), gp(0.5, 2, -0.2, -0.5, 0.2, 0.5, true), gp(1, 1)});
        m.selectOnly(1);
        const QPointF out = m.toPx(0.7, 2.5);
        drag(m, out, m.toPx(0.65, 3.0));
        QVariantMap p = m.points()[1].toMap();
        QVERIFY(qAbs(p["outDx"].toDouble() - 0.15) < 0.01);
        QVERIFY(qAbs(p["inDx"].toDouble() + p["outDx"].toDouble()) < 1e-9);
        QVERIFY(qAbs(p["inDy"].toDouble() + p["outDy"].toDouble()) < 1e-9);
        QVERIFY(p["smooth"].toBool());

        m.selectOnly(1);
        const double inBefore = p["inDy"].toDouble();
        drag(m, m.toPx(0.5 + p["outDx"].toDouble(), 2 + p["outDy"].toDouble()), m.toPx(0.62, 1.2), altFlag());
        p = m.points()[1].toMap();
        QCOMPARE(p["inDy"].toDouble(), inBefore);
        QVERIFY(!p["smooth"].toBool());
    }

    void graphSpeedHandleStopsAtTheNeighbour()
    {
        CurveGraphModel m;
        m.setupSpeed(0.1, 100);
        m.setPlot(QRectF(0, 0, 400, 300));
        m.setSnapEnabled(false);
        m.setPoints({gp(0, 1), gp(0.4, 2, -0.1, 0, 0.1, 0, false), gp(0.6, 2), gp(1, 1)});
        m.selectOnly(1);
        drag(m, m.toPx(0.5, 2), m.toPx(0.95, 3), altFlag());
        const QVariantMap p = m.points()[1].toMap();
        QVERIFY(p["outDx"].toDouble() <= 0.2 + 1e-9);
    }

    void graphClickOnTheCurveAddsAPointWithoutChangingTheShape()
    {
        CurveGraphModel m;
        m.setupSpeed(0.1, 100);
        m.setPlot(QRectF(0, 0, 400, 300));
        m.setSnapEnabled(false);
        m.setPoints({gp(0, 1, 0, 0, 0.3, 2), gp(1, 1, -0.3, 2, 0, 0)});
        const QPointF on = m.curvePath()[m.curvePath().size() / 2];
        QCOMPARE(m.press(on, 0, mouse()), QStringLiteral("curve"));
        QCOMPARE(m.release(on, 0), QStringLiteral("add"));
        QCOMPARE(m.pointCount(), 3);
        QCOMPARE(m.primary(), 1);
        const QList<QPointF> after = m.curvePath();
        QVERIFY(QLineF(after[after.size() / 2], on).length() < 3.0);
    }

    void graphDeleteAndDoubleClickRemoveInnerPointsOnly()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        m->selectOnly(0);
        QVERIFY(!m->deleteSelected());
        QVERIFY(!m->doubleClick(m->toPx(1, 1), mouse()));
        m->selectOnly(1);
        QVERIFY(m->deleteSelected());
        QCOMPARE(m->pointCount(), 3);
        QVERIFY(m->doubleClick(m->toPx(0.7, 0.8), mouse()));
        QCOMPARE(m->pointCount(), 2);
    }

    void graphResetPointAndCurve()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        m->selectOnly(1);
        m->resetSelected();
        QVERIFY(qAbs(m->points()[1].toMap()["y"].toDouble() - 0.4) < 1e-9);
        m->resetCurve();
        QCOMPARE(m->pointCount(), 2);

        CurveGraphModel c;
        c.setupCubic(false);
        c.setCubic({0.1, 0.9, 0.2, 0.3});
        c.resetCurve();
        QVERIFY(qAbs(c.cubic()[0].toDouble() - 1.0 / 3.0) < 1e-9);
        QVERIFY(qAbs(c.cubic()[2].toDouble() - 2.0 / 3.0) < 1e-9);
    }

    void graphMarqueeSelectsAndMovesTogether()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        QCOMPARE(m->press(QPointF(70, 5), 0, mouse()), QStringLiteral("marquee"));
        m->move(QPointF(150, 90), 0);
        QVERIFY(m->marqueeActive());
        m->release(QPointF(150, 90), 0);
        QCOMPARE(m->selectionCount(), 2);
        const double y1 = m->points()[1].toMap()["y"].toDouble();
        const double y2 = m->points()[2].toMap()["y"].toDouble();
        drag(*m, m->toPx(0.4, 0.3), m->toPx(0.4, 0.1));
        QVERIFY(qAbs(m->points()[1].toMap()["y"].toDouble() - (y1 - 0.2)) < 0.02);
        QVERIFY(qAbs(m->points()[2].toMap()["y"].toDouble() - (y2 - 0.2)) < 0.02);
    }

    void graphSnapsToTheGridAndCtrlSuspendsIt()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root, true);
        const QPointF p = m->toPx(0.4, 0.3);
        drag(*m, p, QPointF(m->toPx(0.5, 0.5).x() + 2, m->toPx(0.5, 0.5).y() + 2));
        QCOMPARE(m->points()[1].toMap()["x"].toDouble(), 0.5);
        QCOMPARE(m->points()[1].toMap()["y"].toDouble(), 0.5);
        drag(*m, m->toPx(0.5, 0.5), QPointF(m->toPx(0.5, 0.5).x() + 5, m->toPx(0.5, 0.5).y() + 2), ctrlFlag());
        QVERIFY(m->points()[1].toMap()["x"].toDouble() > 0.5);
        m->setSnapEnabled(false);
        drag(*m, m->toPx(m->points()[1].toMap()["x"].toDouble(), m->points()[1].toMap()["y"].toDouble()),
             QPointF(m->toPx(0.25, 0.25).x() + 2, m->toPx(0.25, 0.25).y() + 2));
        QVERIFY(m->points()[1].toMap()["x"].toDouble() != 0.25);
    }

    void graphOneUndoStepPerDragAndEscRestores()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        DragSession session;
        int begun = 0, committed = 0, cancelled = 0;
        session.setHooks({[&](const QString &) { ++begun; }, [&] { ++committed; }, [&] { ++cancelled; }});
        m->setSession(&session);
        QSignalSpy cancelSpy(m, &CurveGraphModel::gestureCancelled);

        drag(*m, m->toPx(0.4, 0.3), m->toPx(0.45, 0.6));
        QCOMPARE(begun, 1);
        QCOMPARE(committed, 1);

        const QVariantList before = m->points();
        m->press(m->toPx(0.45, 0.6), 0, mouse());
        m->move(m->toPx(0.5, 0.2), 0);
        m->move(m->toPx(0.55, 0.1), 0);
        QVERIFY(m->dragging());
        QVERIFY(m->points() != before);
        m->cancel();
        QVERIFY(!m->dragging());
        QCOMPARE(m->points(), before);
        QCOMPARE(cancelled, 1);
        QCOMPARE(committed, 1);
        QCOMPARE(cancelSpy.size(), 1);

        m->press(m->toPx(0.45, 0.6), 0, mouse());
        m->move(m->toPx(0.5, 0.2), 0);
        m->move(m->toPx(0.55, 0.1), 0);
        session.cancel();
        QCOMPARE(m->points(), before);
        QCOMPARE(cancelSpy.size(), 2);
    }

    void graphBackendsWithoutASessionStillRevertLocally()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        const QVariantList before = m->points();
        QSignalSpy began(m, &CurveGraphModel::gestureBegan);
        QSignalSpy ended(m, &CurveGraphModel::gestureEnded);
        m->press(m->toPx(0.4, 0.3), 0, mouse());
        m->move(m->toPx(0.5, 0.6), 0);
        m->move(m->toPx(0.55, 0.7), 0);
        QCOMPARE(began.size(), 1);
        m->cancel();
        QCOMPARE(m->points(), before);
        QCOMPARE(ended.size(), 0);
    }

    void graphHitReachDependsOnTheDevice()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        const QPointF p = m->toPx(0.4, 0.3);
        QCOMPARE(m->pointAt(p + QPointF(18, 0), mouse()), -1);
        QCOMPARE(m->pointAt(p + QPointF(10, 0), mouse()), 1);
        QCOMPARE(m->pointAt(p + QPointF(20, 0), touchDev()), 1);
        QCOMPARE(m->pointAt(p + QPointF(28, 0), touchDev()), -1);
    }

    void graphNudgeMovesByAPixelOrTen()
    {
        QObject root;
        CurveGraphModel *m = polyline(&root);
        m->selectOnly(1);
        const double y0 = m->points()[1].toMap()["y"].toDouble();
        m->nudge(0, 1, false);
        QVERIFY(qAbs(m->points()[1].toMap()["y"].toDouble() - (y0 + 0.01)) < 1e-9);
        m->nudge(0, -1, true);
        QVERIFY(qAbs(m->points()[1].toMap()["y"].toDouble() - (y0 - 0.09)) < 1e-9);

        DragSession session;
        int begun = 0;
        session.setHooks({[&](const QString &) { ++begun; }, [] {}, [] {}});
        m->setSession(&session);
        m->nudge(1, 0, false);
        m->nudge(1, 0, false);
        m->nudge(1, 0, false);
        QCOMPARE(begun, 1);
    }

    void graphColorClickAddsOnTheDrawnCurveAndRefusesCrowding()
    {
        QObject root;
        CurveGraphModel *m = colorModel(&root);
        QCOMPARE(m->press(QPointF(50, 10), 0, mouse()), QStringLiteral("curve"));
        QCOMPARE(m->release(QPointF(50, 10), 0), QStringLiteral("add"));
        QCOMPARE(m->pointCount(), 3);
        QCOMPARE(m->points()[1].toMap()["x"].toDouble(), 0.5);
        QCOMPARE(m->points()[1].toMap()["y"].toDouble(), 0.5);
        QCOMPARE(m->addPointAtX(0.505), -1);
        while (m->pointCount() < 16)
            QVERIFY(m->addPointAtX(0.06 * m->pointCount()) >= 0 || m->pointCount() >= 16);
        QCOMPARE(m->addPointAtX(0.99), -1);
    }

    void graphColorDragKeepsGapAndClampsY()
    {
        QObject root;
        CurveGraphModel *m = colorModel(&root);
        m->addPointAtX(0.5);
        drag(*m, m->toPx(0.5, 0.5), QPointF(120, 10));
        QVERIFY(qAbs(m->points()[1].toMap()["x"].toDouble() - (1.0 - 0.01)) < 1e-9 || m->pointCount() == 2);
    }

    void graphColorDragOffShowsTheDeleteZoneAndDeletesOnRelease()
    {
        QObject root;
        CurveGraphModel *m = colorModel(&root);
        m->addPointAtX(0.5);
        const QPointF p = m->toPx(0.5, 0.5);
        m->press(p, 0, touchDev());
        m->move(QPointF(50, 30), 0);
        QVERIFY(m->deleteZoneVisible());
        QVERIFY(!m->deleteZoneActive());
        m->move(QPointF(50, 140), 0);
        QVERIFY(m->deleteZoneActive());
        QCOMPARE(m->pointCount(), 3);
        QCOMPARE(m->hudText(), QObject::tr("Release to delete"));
        m->move(QPointF(50, 60), 0);
        QVERIFY(!m->deleteZoneActive());
        m->move(QPointF(50, -40), 0);
        QVERIFY(m->deleteZoneActive());
        QCOMPARE(m->release(QPointF(50, -40), 0), QStringLiteral("delete"));
        QCOMPARE(m->pointCount(), 2);
        QVERIFY(!m->deleteZoneActive());
    }

    void graphColorDeleteZoneBackOutKeepsThePoint()
    {
        QObject root;
        CurveGraphModel *m = colorModel(&root);
        m->addPointAtX(0.5);
        m->press(m->toPx(0.5, 0.5), 0, touchDev());
        m->move(QPointF(50, 140), 0);
        QVERIFY(m->deleteZoneActive());
        m->move(QPointF(60, 40), 0);
        m->release(QPointF(60, 40), 0);
        QCOMPARE(m->pointCount(), 3);
    }

    void graphColorEndsNeverDeleteAndHueEndsStayTied()
    {
        QObject root;
        CurveGraphModel *m = colorModel(&root);
        m->press(m->toPx(0, 0), 0, mouse());
        m->move(QPointF(-50, 300), 0);
        QVERIFY(!m->deleteZoneActive());
        m->release(QPointF(-50, 300), 0);
        QCOMPARE(m->pointCount(), 2);

        CurveGraphModel *h = colorModel(&root, true);
        drag(*h, h->toPx(0, 0.5), h->toPx(0, 0.8));
        QVERIFY(qAbs(h->points()[0].toMap()["y"].toDouble() - 0.8) < 0.02);
        QCOMPARE(h->points()[1].toMap()["y"].toDouble(), h->points()[0].toMap()["y"].toDouble());
        h->selectOnly(1);
        h->nudge(0, -1, true);
        QCOMPARE(h->points()[0].toMap()["y"].toDouble(), h->points()[1].toMap()["y"].toDouble());
    }

    void graphColorNudgeIsOneOver255AndShiftTen()
    {
        QObject root;
        CurveGraphModel *m = colorModel(&root);
        m->addPointAtX(0.5);
        m->selectOnly(1);
        m->nudge(0, 1, false);
        QVERIFY(qAbs(m->points()[1].toMap()["y"].toDouble() - (0.5 + 1.0 / 255.0)) < 1e-12);
        m->nudge(1, 0, true);
        QVERIFY(qAbs(m->points()[1].toMap()["x"].toDouble() - (0.5 + 10.0 / 255.0)) < 1e-12);
    }

    void graphTypedEntryUsesBackendUnits()
    {
        QObject root;
        CurveGraphModel *m = colorModel(&root);
        m->addPointAtX(0.5);
        m->selectOnly(1);
        m->applyTyped(51);
        QVERIFY(qAbs(m->points()[1].toMap()["y"].toDouble() - 0.2) < 1e-12);
        QCOMPARE(m->hudText(), QObject::tr("In %1 \u2192 Out %2").arg(128).arg(51));

        CurveGraphModel s;
        s.setupSpeed(0.1, 100);
        s.setPoints({gp(0, 1), gp(0.5, 1), gp(1, 1)});
        s.selectOnly(1);
        s.applyTyped(2.5);
        QCOMPARE(s.points()[1].toMap()["y"].toDouble(), 2.5);
        QVERIFY(s.hudText().contains(QStringLiteral("2.50")));
    }

    void graphSmoothCornerToggleOnSpeedPoints()
    {
        CurveGraphModel s;
        s.setupSpeed(0.1, 100);
        s.setPoints({gp(0, 1), gp(0.5, 2), gp(1, 1)});
        s.selectOnly(1);
        QVERIFY(s.setSelectedSmooth(true));
        QVariantMap p = s.points()[1].toMap();
        QVERIFY(p["smooth"].toBool());
        QVERIFY(p["outDx"].toDouble() > 0.0);
        QCOMPARE(p["inDx"].toDouble(), -p["outDx"].toDouble());
        QVERIFY(s.setSelectedSmooth(false));
        p = s.points()[1].toMap();
        QCOMPARE(p["outDx"].toDouble(), 0.0);
    }
};

QTEST_MAIN(ManipTest)
#include "tst_manip.moc"
