#include "engine/ClipGizmo.h"

#include <QLineF>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace drift::gizmo {

namespace {

// On-screen sizes, in overlay px at size 1.
constexpr double kArrowLength = 84.0;
constexpr double kRingRadius = 72.0;
constexpr double kHeadLength = 12.0;
constexpr double kHeadHalfWidth = 5.5;
constexpr double kSquareHalf = 5.0;
constexpr double kDollyRadius = 16.0;
// Nearest the eye a point may get, as a fraction of the perspective distance (as the renderer).
constexpr double kNear = 0.02;
constexpr int kRingSegments = 72;

// World space is canvas px about the canvas centre: y down, z toward the viewer, eye at (0, 0, d).
struct Space
{
    double d = 2000.0;
    QSizeF canvas;
    double scale = 1.0;

    double w(const QVector3D &p) const { return 1.0 - double(p.z()) / d; }

    // Overlay px; false at or behind the eye.
    bool project(const QVector3D &p, QPointF *out) const
    {
        const double pw = w(p);
        if (pw <= kNear)
            return false;
        *out = QPointF((canvas.width() * 0.5 + p.x() / pw) * scale,
                       (canvas.height() * 0.5 + p.y() / pw) * scale);
        return true;
    }

    QVector3D eye() const { return QVector3D(0.f, 0.f, float(d)); }

    // Direction of the ray from the eye through an overlay point.
    QVector3D ray(QPointF overlay) const
    {
        const QVector3D onCanvas(float(overlay.x() / scale - canvas.width() * 0.5),
                                 float(overlay.y() / scale - canvas.height() * 0.5), 0.f);
        return onCanvas - eye();
    }
};

Space spaceFor(const Pose &pose, double scale)
{
    Space s;
    s.d = std::max(1.0, pose.pose3d.perspective);
    s.canvas = pose.canvas;
    s.scale = scale;
    return s;
}

QVector3D originOf(const Pose &pose)
{
    const QPointF c = pose.rect.center();
    return QVector3D(float(c.x() - pose.canvas.width() * 0.5),
                     float(c.y() - pose.canvas.height() * 0.5), float(pose.pose3d.positionZ));
}

// The same order the renderer applies: X, then Y, then the in-plane spin.
QMatrix4x4 rotationOf(const Pose &pose)
{
    QMatrix4x4 m;
    m.rotate(float(pose.pose3d.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(pose.pose3d.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(pose.rotation), 0.f, 0.f, 1.f);
    return m;
}

QVector3D basis(int axis)
{
    return QVector3D(axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f);
}

QVector3D axisFor(const Pose &pose, Tool tool, Orientation orientation, int axis)
{
    // Scaling is always along the clip's own edges: a flat clip has nothing to stretch along a
    // world axis without shearing.
    if (orientation == Orientation::Global && tool != Tool::Scale)
        return basis(axis);
    return rotationOf(pose).mapVector(basis(axis)).normalized();
}

QString idFor(int axis)
{
    return axis == 0 ? QStringLiteral("x") : axis == 1 ? QStringLiteral("y") : QStringLiteral("z");
}

int axisFromId(const QString &id)
{
    return id == QLatin1String("x") ? 0 : id == QLatin1String("y") ? 1 : id == QLatin1String("z") ? 2 : -1;
}

QPointF unit(QPointF v)
{
    const double len = std::hypot(v.x(), v.y());
    return len > 1e-9 ? v / len : QPointF(1, 0);
}

QPolygonF arrowHead(QPointF tip, QPointF dir, double size)
{
    const QPointF perp(-dir.y(), dir.x());
    return QPolygonF({tip + dir * kHeadLength * size, tip + perp * kHeadHalfWidth * size,
                      tip - perp * kHeadHalfWidth * size, tip + dir * kHeadLength * size});
}

QPolygonF square(QPointF centre, QPointF dir, double size)
{
    const QPointF a = dir * kSquareHalf * size;
    const QPointF b = QPointF(-dir.y(), dir.x()) * kSquareHalf * size;
    return QPolygonF({centre + a + b, centre - a + b, centre - a - b, centre + a - b, centre + a + b});
}

QPolygonF circle(QPointF centre, double radius)
{
    QPolygonF out;
    for (int i = 0; i <= 48; ++i) {
        const double t = 2.0 * std::numbers::pi * i / 48;
        out << centre + QPointF(std::cos(t), std::sin(t)) * radius;
    }
    return out;
}

// Closest point on the line origin + t*axis to the eye ray through `overlay`, as t. False when
// the two are nearly parallel, so the drag cannot be read off the line.
bool lineParam(const Space &s, const QVector3D &origin, const QVector3D &axis, QPointF overlay,
               double *t)
{
    const QVector3D r = s.ray(overlay);
    const QVector3D w0 = origin - s.eye();
    const double a = QVector3D::dotProduct(axis, axis);
    const double b = QVector3D::dotProduct(axis, r);
    const double c = QVector3D::dotProduct(r, r);
    const double dd = QVector3D::dotProduct(axis, w0);
    const double e = QVector3D::dotProduct(r, w0);
    const double denom = a * c - b * b;
    if (denom < 1e-4 * a * c)
        return false;
    *t = (b * e - c * dd) / denom;
    return true;
}

double wrapNear(double v, double nearest)
{
    return v + 360.0 * std::round((nearest - v) / 360.0);
}

double segmentDistance(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0.0;
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

double polylineDistance(QPointF p, const QPolygonF &line)
{
    double best = std::numeric_limits<double>::infinity();
    for (int i = 1; i < line.size(); ++i)
        best = std::min(best, segmentDistance(p, line.at(i - 1), line.at(i)));
    return best;
}

} // namespace

Tool toolFromString(const QString &name)
{
    if (name == QLatin1String("rotate"))
        return Tool::Rotate;
    if (name == QLatin1String("scale"))
        return Tool::Scale;
    return Tool::Move;
}

Orientation orientationFromString(const QString &name)
{
    return name == QLatin1String("local") ? Orientation::Local : Orientation::Global;
}

Geometry geometry(const Pose &pose, Tool tool, Orientation orientation, double scale, double size)
{
    Geometry g;
    const Space s = spaceFor(pose, scale);
    const QVector3D origin = originOf(pose);
    if (!s.project(origin, &g.origin))
        return g;
    g.valid = true;
    // Overlay px per world px at the origin, so handles keep their on-screen size at any depth.
    const double magnify = scale / s.w(origin);

    if (tool == Tool::Rotate) {
        const double radius = kRingRadius * size / magnify;
        const QVector3D toEye = (s.eye() - origin).normalized();
        for (int axis = 0; axis < 3; ++axis) {
            const QVector3D a = axisFor(pose, tool, orientation, axis);
            QVector3D u = QVector3D::crossProduct(a, std::abs(a.x()) < 0.9f ? basis(0) : basis(1));
            u.normalize();
            const QVector3D v = QVector3D::crossProduct(a, u);
            Handle h;
            h.id = idFor(axis);
            h.axis = axis;
            h.kind = HandleKind::Ring;
            QPolygonF run;
            bool runFront = true;
            for (int i = 0; i <= kRingSegments; ++i) {
                const double t = 2.0 * std::numbers::pi * i / kRingSegments;
                const QVector3D offset = (u * float(std::cos(t)) + v * float(std::sin(t))) * float(radius);
                QPointF p;
                const bool visible = s.project(origin + offset, &p);
                const bool front = QVector3D::dotProduct(offset, toEye) >= 0.f;
                if (!visible || (front != runFront && !run.isEmpty())) {
                    if (visible)
                        run << p; // share the joint so the two halves meet
                    if (run.size() > 1)
                        (runFront ? h.front : h.back) << run;
                    run.clear();
                }
                if (visible) {
                    run << p;
                    runFront = front;
                }
            }
            if (run.size() > 1)
                (runFront ? h.front : h.back) << run;
            g.handles << h;
        }
        return g;
    }

    const double length = kArrowLength * size / magnify;
    const int axes = tool == Tool::Scale ? 2 : 3;
    for (int axis = 0; axis < axes; ++axis) {
        const QVector3D a = axisFor(pose, tool, orientation, axis);
        Handle h;
        h.id = idFor(axis);
        h.axis = axis;
        QPointF tip;
        const bool visible = s.project(origin + a * float(length), &tip);
        const double screenLength = visible ? QLineF(g.origin, tip).length() : 0.0;
        if (screenLength < kArrowLength * size * 0.3) {
            // Pointing (nearly) at the viewer: nothing to follow on screen.
            if (tool == Tool::Scale)
                continue;
            h.kind = HandleKind::Dolly;
            h.front << circle(g.origin, kDollyRadius * size);
            g.handles << h;
            continue;
        }
        const QPointF dir = unit(tip - g.origin);
        h.kind = tool == Tool::Scale ? HandleKind::Scale : HandleKind::Arrow;
        h.front << QPolygonF({g.origin, tip});
        h.head = tool == Tool::Scale ? square(tip, dir, size) : arrowHead(tip, dir, size);
        g.handles << h;
    }

    if (tool == Tool::Scale) {
        const QVector3D diag =
            (axisFor(pose, tool, orientation, 0) + axisFor(pose, tool, orientation, 1)).normalized();
        QPointF p;
        if (s.project(origin + diag * float(length * 0.55), &p)) {
            Handle h;
            h.id = QStringLiteral("xy");
            h.kind = HandleKind::Uniform;
            h.head = square(p, unit(p - g.origin), size * 1.2);
            g.handles << h;
        }
    }
    return g;
}

QString pick(const Geometry &geometry, QPointF point, double tolerance)
{
    QString best;
    double bestDistance = tolerance;
    for (const Handle &h : geometry.handles) {
        double distance = std::numeric_limits<double>::infinity();
        for (const QPolygonF &line : h.front)
            distance = std::min(distance, polylineDistance(point, line));
        for (const QPolygonF &line : h.back)
            distance = std::min(distance, polylineDistance(point, line));
        if (!h.head.isEmpty()) {
            distance = h.head.containsPoint(point, Qt::OddEvenFill)
                           ? 0.0
                           : std::min(distance, polylineDistance(point, h.head));
        }
        if (h.kind == HandleKind::Dolly && !h.front.isEmpty()
            && h.front.first().containsPoint(point, Qt::OddEvenFill)) {
            distance = 0.0;
        }
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = h.id;
        }
    }
    return best;
}

void eulerFromMatrix(const QMatrix4x4 &r, double nearest[3], double out[3])
{
    constexpr double toDeg = 180.0 / std::numbers::pi;
    const double sb = std::clamp(double(r(0, 2)), -1.0, 1.0);
    double cand[2][3];
    if (std::abs(sb) > 0.99999) {
        // Gimbal lock: X and the spin turn about the same axis. Keep X, fold the rest into the spin.
        const double a = nearest[0];
        const double b = sb > 0 ? 90.0 : -90.0;
        const double sum = std::atan2(double(r(1, 0)), double(r(1, 1))) * toDeg;
        const double c = sb > 0 ? sum - a : sum + a;
        cand[0][0] = cand[1][0] = a;
        cand[0][1] = cand[1][1] = b;
        cand[0][2] = cand[1][2] = c;
    } else {
        const double a = std::atan2(-double(r(1, 2)), double(r(2, 2))) * toDeg;
        const double b = std::asin(sb) * toDeg;
        const double c = std::atan2(-double(r(0, 1)), double(r(0, 0))) * toDeg;
        cand[0][0] = a;
        cand[0][1] = b;
        cand[0][2] = c;
        // The same rotation reached the other way round.
        cand[1][0] = a + 180.0;
        cand[1][1] = 180.0 - b;
        cand[1][2] = c + 180.0;
    }
    double bestCost = std::numeric_limits<double>::infinity();
    for (auto &c : cand) {
        double cost = 0.0;
        for (int i = 0; i < 3; ++i) {
            c[i] = wrapNear(c[i], nearest[i]);
            cost += std::abs(c[i] - nearest[i]);
        }
        if (cost < bestCost) {
            bestCost = cost;
            std::copy(c, c + 3, out);
        }
    }
}

DragResult drag(const Pose &start, Tool tool, Orientation orientation, const QString &handle,
                QPointF press, QPointF now, bool snap, double scale)
{
    DragResult result;
    result.pose = start;
    const Space s = spaceFor(start, scale);
    const QVector3D origin = originOf(start);
    const double d = s.d;

    if (tool == Tool::Scale) {
        const bool uniform = handle == QLatin1String("xy");
        const int axis = axisFromId(handle);
        if (!uniform && axis < 0)
            return result;
        const QVector3D a = uniform ? (axisFor(start, tool, orientation, 0)
                                       + axisFor(start, tool, orientation, 1)).normalized()
                                    : axisFor(start, tool, orientation, axis);
        double t0 = 0.0;
        double t1 = 0.0;
        double factor = 1.0;
        if (lineParam(s, origin, a, press, &t0) && lineParam(s, origin, a, now, &t1)
            && std::abs(t0) > 1e-3) {
            factor = t1 / t0;
        } else {
            factor = 1.0 - (now.y() - press.y()) / 100.0;
        }
        factor = std::max(0.01, factor);
        QSizeF size = start.rect.size();
        if (uniform || axis == 0)
            size.setWidth(std::max(1.0, size.width() * factor));
        if (uniform || axis == 1)
            size.setHeight(std::max(1.0, size.height() * factor));
        const QPointF c = start.rect.center();
        result.pose.rect = QRectF(c.x() - size.width() * 0.5, c.y() - size.height() * 0.5,
                                  size.width(), size.height());
        result.uniformFactor = uniform ? factor : 1.0;
        return result;
    }

    const int axis = axisFromId(handle);
    if (axis < 0)
        return result;
    const QVector3D a = axisFor(start, tool, orientation, axis);

    if (tool == Tool::Move) {
        double delta = 0.0;
        double t0 = 0.0;
        double t1 = 0.0;
        const Geometry g = geometry(start, tool, orientation, scale);
        const bool dolly = std::any_of(g.handles.cbegin(), g.handles.cend(), [&](const Handle &h) {
            return h.id == handle && h.kind == HandleKind::Dolly;
        });
        if (!dolly && lineParam(s, origin, a, press, &t0) && lineParam(s, origin, a, now, &t1)) {
            delta = t1 - t0;
        } else {
            // 300 px of vertical drag moves one perspective distance; down comes toward the viewer.
            delta = (now.y() - press.y()) * d / 300.0 * (a.z() >= 0.f ? 1.0 : -1.0);
        }
        QVector3D moved = origin + a * float(delta);
        // Stop short of the eye, where the clip would vanish.
        const double maxZ = d * 0.8;
        if (moved.z() > maxZ && std::abs(a.z()) > 1e-6f) {
            const double back = (moved.z() - maxZ) / a.z();
            moved -= a * float(back);
        }
        const QSizeF size = start.rect.size();
        result.pose.rect = QRectF(moved.x() + start.canvas.width() * 0.5 - size.width() * 0.5,
                                  moved.y() + start.canvas.height() * 0.5 - size.height() * 0.5,
                                  size.width(), size.height());
        result.pose.pose3d.positionZ = moved.z();
        return result;
    }

    // Rotate about `a` through the clip centre, by the angle the pointer sweeps on the ring's
    // plane. Global and local both come to Rot(a)·R0: a local axis is R0 applied to a basis axis.
    double degrees = 0.0;
    const QVector3D r0 = s.ray(press);
    const QVector3D r1 = s.ray(now);
    const double facing = std::abs(QVector3D::dotProduct(r0.normalized(), a));
    if (facing < 0.08) {
        // The ring is edge-on: its plane gives no angle, so read it off the horizontal drag.
        degrees = (now.x() - press.x()) * 0.5;
    } else {
        const auto hit = [&](const QVector3D &r) {
            const double t = QVector3D::dotProduct(origin - s.eye(), a) / QVector3D::dotProduct(r, a);
            return s.eye() + r * float(t) - origin;
        };
        const QVector3D v0 = hit(r0);
        const QVector3D v1 = hit(r1);
        degrees = std::atan2(double(QVector3D::dotProduct(a, QVector3D::crossProduct(v0, v1))),
                             double(QVector3D::dotProduct(v0, v1)))
                  * 180.0 / std::numbers::pi;
    }
    if (snap)
        degrees = std::round(degrees / 15.0) * 15.0;
    QMatrix4x4 turn;
    turn.rotate(float(degrees), a);
    const QMatrix4x4 rotated = turn * rotationOf(start);
    double nearest[3] = {start.pose3d.rotationX, start.pose3d.rotationY, start.rotation};
    double angles[3];
    eulerFromMatrix(rotated, nearest, angles);
    result.pose.pose3d.rotationX = angles[0];
    result.pose.pose3d.rotationY = angles[1];
    result.pose.rotation = angles[2];
    return result;
}

} // namespace drift::gizmo
