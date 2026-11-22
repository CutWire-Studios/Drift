#include "core/geom/BezierPath.h"

#include "core/base/Bezier.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace drift::geom {

namespace {

QPointF lerp(const QPointF &a, const QPointF &b, double t)
{
    return a + (b - a) * t;
}

bool isZero(const QPointF &p)
{
    return std::abs(p.x()) < 1e-12 && std::abs(p.y()) < 1e-12;
}

double length(const QPointF &p)
{
    return std::hypot(p.x(), p.y());
}

const ShapeVertex &next(const ShapeContour &c, int seg)
{
    return c.vertices[(seg + 1) % c.vertices.size()];
}

double distSq(const QPointF &a, const QPointF &b)
{
    const QPointF d = a - b;
    return d.x() * d.x() + d.y() * d.y();
}

QPointF snapAngle45(const QPointF &h)
{
    const double len = length(h);
    if (len == 0.0)
        return h;
    const double step = M_PI / 4.0;
    const double a = std::round(std::atan2(h.y(), h.x()) / step) * step;
    return {len * std::cos(a), len * std::sin(a)};
}

} // namespace

int segmentCount(const ShapeContour &c)
{
    const int n = int(c.vertices.size());
    if (n < 2)
        return 0;
    return c.closed ? n : n - 1;
}

QPointF cubicPoint(const ShapeVertex &a, const ShapeVertex &b, double t)
{
    const QPointF p1 = a.p + a.out;
    const QPointF p2 = b.p + b.in;
    return {cubicBezier(a.p.x(), p1.x(), p2.x(), b.p.x(), t),
            cubicBezier(a.p.y(), p1.y(), p2.y(), b.p.y(), t)};
}

QPointF cubicTangent(const ShapeVertex &a, const ShapeVertex &b, double t)
{
    const QPointF p1 = a.p + a.out;
    const QPointF p2 = b.p + b.in;
    const double mt = 1.0 - t;
    return 3.0 * mt * mt * (p1 - a.p) + 6.0 * mt * t * (p2 - p1) + 3.0 * t * t * (b.p - p2);
}

int splitSegment(ShapeContour &c, int seg, double t)
{
    const int n = int(c.vertices.size());
    ShapeVertex &a = c.vertices[seg];
    ShapeVertex &b = c.vertices[(seg + 1) % n];
    const bool curved = !isZero(a.out) || !isZero(b.in);
    const QPointF p0 = a.p;
    const QPointF p1 = a.p + a.out;
    const QPointF p2 = b.p + b.in;
    const QPointF p3 = b.p;
    const QPointF q0 = lerp(p0, p1, t);
    const QPointF q1 = lerp(p1, p2, t);
    const QPointF q2 = lerp(p2, p3, t);
    const QPointF r0 = lerp(q0, q1, t);
    const QPointF r1 = lerp(q1, q2, t);
    const QPointF s = lerp(r0, r1, t);

    a.out = q0 - p0;
    b.in = q2 - p3;
    ShapeVertex nv;
    nv.p = s;
    if (curved) {
        nv.in = r0 - s;
        nv.out = r1 - s;
        nv.mode = TangentMode::Aligned;
    }
    c.vertices.insert(seg + 1, nv);
    return seg + 1;
}

QVector<QPointF> flatten(const ShapeContour &c, int stepsPerSegment)
{
    QVector<QPointF> out;
    const int segs = segmentCount(c);
    if (c.vertices.size() == 1) {
        out.append(c.vertices[0].p);
        return out;
    }
    const int steps = std::max(1, stepsPerSegment);
    for (int s = 0; s < segs; ++s) {
        const ShapeVertex &a = c.vertices[s];
        const ShapeVertex &b = next(c, s);
        for (int i = 0; i < steps; ++i)
            out.append(cubicPoint(a, b, double(i) / steps));
    }
    if (!c.closed && !c.vertices.isEmpty())
        out.append(c.vertices.last().p);
    return out;
}

NearestHit nearestPoint(const ShapeContour &c, const QPointF &p)
{
    NearestHit best;
    const int segs = segmentCount(c);
    if (c.vertices.size() == 1) {
        best.segment = 0;
        best.point = c.vertices[0].p;
        best.distance = length(p - best.point);
        return best;
    }
    constexpr int kSteps = 24;
    double bestSq = 0.0;
    for (int s = 0; s < segs; ++s) {
        const ShapeVertex &a = c.vertices[s];
        const ShapeVertex &b = next(c, s);
        int bi = 0;
        double bd = -1.0;
        for (int i = 0; i <= kSteps; ++i) {
            const double d = distSq(cubicPoint(a, b, double(i) / kSteps), p);
            if (bd < 0.0 || d < bd) {
                bd = d;
                bi = i;
            }
        }
        double lo = std::max(0, bi - 1) / double(kSteps);
        double hi = std::min(kSteps, bi + 1) / double(kSteps);
        for (int i = 0; i < 24; ++i) {
            const double m1 = lo + (hi - lo) / 3.0;
            const double m2 = hi - (hi - lo) / 3.0;
            if (distSq(cubicPoint(a, b, m1), p) < distSq(cubicPoint(a, b, m2), p))
                hi = m2;
            else
                lo = m1;
        }
        const double t = 0.5 * (lo + hi);
        const QPointF pt = cubicPoint(a, b, t);
        const double d = distSq(pt, p);
        if (!best.valid() || d < bestSq) {
            bestSq = d;
            best.segment = s;
            best.t = t;
            best.point = pt;
        }
    }
    best.distance = std::sqrt(bestSq);
    return best;
}

bool removeVertex(ShapeContour &c, int i)
{
    if (i < 0 || i >= c.vertices.size())
        return false;
    c.vertices.remove(i);
    return true;
}

bool removeVertexHeal(ShapeContour &c, int i)
{
    const int n = int(c.vertices.size());
    if (i < 0 || i >= n)
        return false;
    const bool interior = c.closed || (i > 0 && i < n - 1);
    if (!interior || n < 3)
        return removeVertex(c, i);

    const int ia = (i - 1 + n) % n;
    const int ib = (i + 1) % n;
    const ShapeVertex a = c.vertices[ia];
    const ShapeVertex v = c.vertices[i];
    const ShapeVertex b = c.vertices[ib];
    if (isZero(a.out) && isZero(v.in) && isZero(v.out) && isZero(b.in))
        return removeVertex(c, i);

    constexpr int kSamples = 16;
    QVector<QPointF> pts;
    for (int k = 0; k < kSamples; ++k)
        pts.append(cubicPoint(a, v, double(k) / kSamples));
    for (int k = 0; k <= kSamples; ++k)
        pts.append(cubicPoint(v, b, double(k) / kSamples));

    QVector<double> ts(pts.size(), 0.0);
    for (int k = 1; k < pts.size(); ++k)
        ts[k] = ts[k - 1] + length(pts[k] - pts[k - 1]);
    const double total = ts.last();
    if (total <= 0.0)
        return removeVertex(c, i);

    // Least squares for the two inner control points with the end points fixed.
    double m00 = 0, m01 = 0, m11 = 0;
    QPointF r0, r1;
    for (int k = 0; k < pts.size(); ++k) {
        const double t = ts[k] / total;
        const double mt = 1.0 - t;
        const double b0 = mt * mt * mt;
        const double b1 = 3.0 * mt * mt * t;
        const double b2 = 3.0 * mt * t * t;
        const double b3 = t * t * t;
        const QPointF rest = pts[k] - (b0 * a.p + b3 * b.p);
        m00 += b1 * b1;
        m01 += b1 * b2;
        m11 += b2 * b2;
        r0 += b1 * rest;
        r1 += b2 * rest;
    }
    const double det = m00 * m11 - m01 * m01;
    QPointF h1 = a.p + (b.p - a.p) / 3.0;
    QPointF h2 = a.p + (b.p - a.p) * 2.0 / 3.0;
    if (std::abs(det) > 1e-12) {
        h1 = (r0 * m11 - r1 * m01) / det;
        h2 = (r1 * m00 - r0 * m01) / det;
    }

    ShapeVertex &na = c.vertices[ia];
    ShapeVertex &nb = c.vertices[ib];
    na.out = h1 - a.p;
    nb.in = h2 - b.p;
    c.vertices.remove(i);
    return true;
}

ShapeVertex applyTangentDrag(ShapeVertex v, TangentHandle which, const QPointF &newPos, TangentMode mode,
                             bool constrain45)
{
    QPointF h = newPos - v.p;
    if (constrain45)
        h = snapAngle45(h);
    QPointF &dragged = which == TangentHandle::In ? v.in : v.out;
    QPointF &other = which == TangentHandle::In ? v.out : v.in;
    dragged = h;
    switch (mode) {
    case TangentMode::Mirrored:
        other = -h;
        break;
    case TangentMode::Aligned: {
        const double len = length(h);
        if (len > 0.0)
            other = -h / len * length(other);
        break;
    }
    case TangentMode::Corner:
    case TangentMode::Free:
        break;
    }
    v.mode = mode;
    return v;
}

GraphTangents applyGraphTangentDrag(GraphTangents t, TangentHandle which, double dx, double dy,
                                    TangentMode mode, bool horizontalOnly, double spanIn, double spanOut)
{
    const bool out = which == TangentHandle::Out;
    if (horizontalOnly)
        dy = 0.0;
    dx = out ? std::max(0.0, dx) : std::min(0.0, dx);
    double limit = out ? spanOut : spanIn;
    if (mode == TangentMode::Mirrored)
        limit = std::min(spanIn, spanOut);
    limit = std::max(0.0, limit);
    if (std::abs(dx) > limit) {
        const double f = std::abs(dx) > 0.0 ? limit / std::abs(dx) : 0.0;
        dx *= f;
        dy *= f;
    }

    double &dDx = out ? t.outDx : t.inDx;
    double &dDy = out ? t.outDy : t.inDy;
    double &fDx = out ? t.inDx : t.outDx;
    double &fDy = out ? t.inDy : t.outDy;
    dDx = dx;
    dDy = dy;
    switch (mode) {
    case TangentMode::Mirrored:
        fDx = -dx;
        fDy = -dy;
        break;
    case TangentMode::Aligned:
        if (std::abs(dx) > 1e-9)
            fDy = dy / dx * fDx;
        break;
    case TangentMode::Corner:
    case TangentMode::Free:
        break;
    }
    return t;
}

} // namespace drift::geom
