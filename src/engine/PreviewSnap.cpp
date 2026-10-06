#include "engine/PreviewSnap.h"

#include <QtMath>

#include <cmath>

namespace drift::snap {

namespace {

// How far a projected edge may lean off the screen axis and still count as lined up with a
// target: tan(3°).
constexpr double kAxisSlack = 0.0524;
constexpr double kMinW = 1e-6;

QVector4D at(const QMatrix4x4 &m, double x, double y)
{
    return m.map(QVector4D(float(x), float(y), 0.f, 1.f));
}

bool screenPoint(const QVector4D &h, QPointF *out)
{
    if (h.w() <= kMinW)
        return false;
    *out = QPointF(h.x() / h.w(), h.y() / h.w());
    return true;
}

// The local segment a -> b, moved by `delta`, runs along the screen's y axis (`vertical`) or x axis.
bool alongAxis(const PlacedClip &clip, const QPointF &a, const QPointF &b, const QPointF &delta,
               bool vertical)
{
    const QVector4D shift = clip.ex * float(delta.x()) + clip.ey * float(delta.y());
    QPointF pa;
    QPointF pb;
    if (!screenPoint(at(clip.local, a.x(), a.y()) + shift, &pa)
        || !screenPoint(at(clip.local, b.x(), b.y()) + shift, &pb))
        return false;
    const double along = vertical ? std::abs(pb.y() - pa.y()) : std::abs(pb.x() - pa.x());
    const double across = vertical ? std::abs(pb.x() - pa.x()) : std::abs(pb.y() - pa.y());
    return along > 1e-6 && across <= along * kAxisSlack;
}

// The value along `step` that puts the homogeneous point `base + s * step` on the screen line
// `target` (x when `xAxis`), or nothing when the move cannot reach it.
std::optional<double> solveOnto(const QVector4D &base, const QVector4D &step, double target, bool xAxis)
{
    const double num = target * base.w() - (xAxis ? base.x() : base.y());
    const double den = (xAxis ? step.x() : step.y()) - target * step.w();
    if (std::abs(den) < 1e-12)
        return std::nullopt;
    const double s = num / den;
    if (base.w() + s * step.w() <= kMinW)
        return std::nullopt;
    return s;
}

struct AxisPick
{
    double value = 0.0;
    double guide = -1.0;
};

// One axis of snapMove: the candidate lines are at 0, half and the full extent of the rect.
AxisPick snapAxis(const PlacedClip &clip, QPointF delta, const QList<double> &targets,
                  double tolerance, bool xAxis)
{
    const double w = clip.size.width();
    const double h = clip.size.height();
    const QVector4D step = xAxis ? clip.ex : clip.ey;
    // Everything about the point except the axis being solved.
    const QVector4D fixed = xAxis ? clip.ey * float(delta.y()) : clip.ex * float(delta.x());
    const double current = xAxis ? delta.x() : delta.y();
    AxisPick best{current, -1.0};
    double bestDistance = tolerance;
    for (const double u : {0.0, 0.5, 1.0}) {
        const QPointF a = xAxis ? QPointF(u * w, 0) : QPointF(0, u * h);
        const QPointF b = xAxis ? QPointF(u * w, h) : QPointF(w, u * h);
        if (!alongAxis(clip, a, b, delta, xAxis))
            continue;
        const QPointF mid = (a + b) / 2.0;
        const QVector4D base = at(clip.local, mid.x(), mid.y()) + fixed;
        QPointF now;
        if (!screenPoint(base + step * float(current), &now))
            continue;
        const double screen = xAxis ? now.x() : now.y();
        for (const double t : targets) {
            const double distance = std::abs(t - screen);
            if (distance >= bestDistance)
                continue;
            if (const std::optional<double> s = solveOnto(base, step, t, xAxis)) {
                bestDistance = distance;
                best = {*s, t};
            }
        }
    }
    return best;
}

} // namespace

std::optional<QPointF> dragDelta(const PlacedClip &clip, const QPointF &press, const QPointF &now)
{
    bool invertible = false;
    const QMatrix4x4 inverse = clip.local.inverted(&invertible);
    if (!invertible)
        return std::nullopt;
    const QVector4D grabbed = inverse.map(QVector4D(float(press.x()), float(press.y()), 0.f, 1.f));
    if (std::abs(grabbed.w()) <= kMinW)
        return std::nullopt;
    const QVector4D h0 = at(clip.local, grabbed.x() / grabbed.w(), grabbed.y() / grabbed.w());
    if (h0.w() <= kMinW)
        return std::nullopt;

    // (h0 + dx·ex + dy·ey) divided through by its w lands on `now`: two linear equations.
    const double nx = now.x();
    const double ny = now.y();
    const double a = clip.ex.x() - nx * clip.ex.w();
    const double b = clip.ey.x() - nx * clip.ey.w();
    const double c = clip.ex.y() - ny * clip.ex.w();
    const double d = clip.ey.y() - ny * clip.ey.w();
    const double rx = nx * h0.w() - h0.x();
    const double ry = ny * h0.w() - h0.y();
    const double det = a * d - b * c;
    // Relative to the size of the terms, so the test means "edge-on" at any canvas scale.
    const double scale = std::max({std::abs(a * d), std::abs(b * c), 1e-30});
    if (std::abs(det) < 1e-6 * scale)
        return std::nullopt;
    const double dx = (rx * d - b * ry) / det;
    const double dy = (a * ry - rx * c) / det;
    if (h0.w() + dx * clip.ex.w() + dy * clip.ey.w() <= kMinW)
        return std::nullopt;
    return QPointF(dx, dy);
}

MoveSnap snapMove(const PlacedClip &clip, const QPointF &delta, const Targets &targets,
                  double tolerance)
{
    MoveSnap out;
    out.delta = delta;
    const AxisPick x = snapAxis(clip, out.delta, targets.x, tolerance, true);
    out.delta.setX(x.value);
    const AxisPick y = snapAxis(clip, out.delta, targets.y, tolerance, false);
    out.delta.setY(y.value);
    out.guideY = y.guide;
    // Under a roll or a tilt the y pull nudges the screen x too; settle x once more on the same
    // target so the engaged edge stays exactly on its line.
    if (x.guide >= 0.0) {
        const AxisPick again = snapAxis(clip, out.delta, {x.guide}, tolerance, true);
        out.delta.setX(again.value);
        out.guideX = again.guide;
    }
    return out;
}

std::optional<EdgeSnap> snapEdge(const PlacedClip &clip, bool horizontal, double anchor,
                                 double scale, double size, const Targets &targets,
                                 double tolerance)
{
    if (qFuzzyIsNull(scale))
        return std::nullopt;
    const double edge = anchor + scale * size;
    const double span = horizontal ? clip.size.height() : clip.size.width();
    const QPointF a = horizontal ? QPointF(edge, 0) : QPointF(0, edge);
    const QPointF b = horizontal ? QPointF(edge, span) : QPointF(span, edge);
    if (!alongAxis(clip, a, b, {}, horizontal))
        return std::nullopt;
    // The edge's midpoint as a function of its position along the axis.
    const QVector4D base = horizontal ? at(clip.local, 0, span / 2) : at(clip.local, span / 2, 0);
    const QVector4D step = horizontal ? clip.local.column(0) : clip.local.column(1);
    QPointF now;
    if (!screenPoint(base + step * float(edge), &now))
        return std::nullopt;
    const double screen = horizontal ? now.x() : now.y();
    std::optional<EdgeSnap> best;
    double bestDistance = tolerance;
    for (const double t : horizontal ? targets.x : targets.y) {
        const double distance = std::abs(t - screen);
        if (distance >= bestDistance)
            continue;
        const std::optional<double> e = solveOnto(base, step, t, horizontal);
        if (!e)
            continue;
        const double snapped = (*e - anchor) / scale;
        if (snapped < 1.0)
            continue;
        bestDistance = distance;
        best = EdgeSnap{snapped, t, distance};
    }
    return best;
}

} // namespace drift::snap
