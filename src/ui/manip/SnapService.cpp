#include "ui/manip/SnapService.h"

#include <QtMath>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace drift::manip {

namespace {
std::atomic<double> g_tolerancePx{kDefaultSnapTolerancePx};
}

double snapTolerancePx()
{
    return g_tolerancePx.load();
}

void setSnapTolerancePx(double px)
{
    g_tolerancePx.store(std::max(0.0, px));
}

double toleranceInUnits(double screenPerUnit)
{
    return screenPerUnit > 0.0 ? snapTolerancePx() / screenPerUnit : 0.0;
}

AxisHit snapAxis(const QList<double> &candidates, const QList<double> &targets, double tolerance)
{
    AxisHit hit;
    double best = tolerance;
    for (const double c : candidates) {
        for (const double t : targets) {
            const double d = t - c;
            if (std::abs(d) < best) {
                best = std::abs(d);
                hit.delta = d;
                hit.target = t;
                hit.snapped = true;
            }
        }
    }
    return hit;
}

snap::Targets canvasTargets(const QSizeF &canvas, const QList<double> &guidesX,
                            const QList<double> &guidesY, const QList<QRectF> &otherClips,
                            double gridStep)
{
    snap::Targets t;
    t.x = {0.0, canvas.width() / 2.0, canvas.width()};
    t.y = {0.0, canvas.height() / 2.0, canvas.height()};
    t.x += guidesX;
    t.y += guidesY;
    for (const QRectF &r : otherClips) {
        t.x << r.left() << r.center().x() << r.right();
        t.y << r.top() << r.center().y() << r.bottom();
    }
    if (gridStep > 0.0) {
        for (double x = gridStep; x < canvas.width(); x += gridStep)
            t.x << x;
        for (double y = gridStep; y < canvas.height(); y += gridStep)
            t.y << y;
    }
    return t;
}

snap::Targets clipFrameTargets(const QList<double> &guidesX, const QList<double> &guidesY)
{
    snap::Targets t;
    t.x = {0.0, 0.5, 1.0};
    t.y = {0.0, 0.5, 1.0};
    t.x += guidesX;
    t.y += guidesY;
    return t;
}

RectSnap snapRectMove(const QRectF &rect, const QPointF &delta, const snap::Targets &targets,
                      double tolerance, bool noSnap)
{
    RectSnap out;
    out.delta = delta;
    if (noSnap)
        return out;
    const QRectF moved = rect.translated(delta);
    const AxisHit x = snapAxis({moved.left(), moved.center().x(), moved.right()}, targets.x, tolerance);
    const AxisHit y = snapAxis({moved.top(), moved.center().y(), moved.bottom()}, targets.y, tolerance);
    out.delta += QPointF(x.delta, y.delta);
    out.guideX = x.snapped ? x.target : -1.0;
    out.guideY = y.snapped ? y.target : -1.0;
    return out;
}

snap::MoveSnap snapClipMove(const snap::PlacedClip &clip, const QPointF &delta,
                            const snap::Targets &targets, double tolerance, bool noSnap)
{
    if (noSnap)
        return {delta, -1.0, -1.0};
    return snap::snapMove(clip, delta, targets, tolerance);
}

QPointF lockAxis(const QPointF &delta)
{
    return std::abs(delta.x()) >= std::abs(delta.y()) ? QPointF(delta.x(), 0.0) : QPointF(0.0, delta.y());
}

QPointF constrain45(const QPointF &origin, const QPointF &p)
{
    const QPointF d = p - origin;
    const double len = std::hypot(d.x(), d.y());
    if (len == 0.0)
        return p;
    const double step = M_PI / 4.0;
    const double a = std::round(std::atan2(d.y(), d.x()) / step) * step;
    return origin + QPointF(len * std::cos(a), len * std::sin(a));
}

double constrainAngle(double degrees, double stepDeg)
{
    return stepDeg > 0.0 ? std::round(degrees / stepDeg) * stepDeg : degrees;
}

AxisHit snapAngle(double degrees, double stepDeg, double toleranceDeg, bool noSnap)
{
    AxisHit hit;
    if (noSnap || stepDeg <= 0.0)
        return hit;
    const double target = constrainAngle(degrees, stepDeg);
    if (std::abs(target - degrees) <= toleranceDeg) {
        hit.delta = target - degrees;
        hit.target = target;
        hit.snapped = true;
    }
    return hit;
}

} // namespace drift::manip
