#include "core/shapes/ShapePath.h"

#include <QPainterPathStroker>
#include <QPointF>
#include <QTransform>
#include <QtMath>

#include <initializer_list>

namespace drift {
namespace {

// Paths below are authored on a 0..1 grid and mapped onto the bounds, so every shape stretches
// with the clip's layout rect instead of carrying a baked aspect ratio.
struct Grid
{
    QRectF b;
    double x(double fx) const { return b.left() + fx * b.width(); }
    double y(double fy) const { return b.top() + fy * b.height(); }
    QPointF p(double fx, double fy) const { return {x(fx), y(fy)}; }
};

QPainterPath polygonPath(const Grid &g, std::initializer_list<QPointF> normalized)
{
    QPainterPath path;
    bool first = true;
    for (const QPointF &pt : normalized) {
        const QPointF mapped = g.p(pt.x(), pt.y());
        if (first) {
            path.moveTo(mapped);
            first = false;
        } else {
            path.lineTo(mapped);
        }
    }
    path.closeSubpath();
    return path;
}

// Scales and shifts `path` so its tight bounds are exactly `bounds`.
QPainterPath fillBounds(const QPainterPath &path, const QRectF &bounds)
{
    const QRectF box = path.boundingRect();
    if (box.width() <= 0.0 || box.height() <= 0.0)
        return path;
    QTransform t;
    t.translate(bounds.left(), bounds.top());
    t.scale(bounds.width() / box.width(), bounds.height() / box.height());
    t.translate(-box.left(), -box.top());
    return t.map(path);
}

// A hole: wound against addEllipse()/addRect() so the default winding fill leaves it empty and a
// path effect or clip that drops the fill rule cannot fill it in.
void addEllipseHole(QPainterPath &path, const QRectF &rect)
{
    path.moveTo(rect.right(), rect.center().y());
    path.arcTo(rect, 0.0, 360.0);
    path.closeSubpath();
}

void addPolygonHole(QPainterPath &path, const QVector<QPointF> &pts)
{
    path.moveTo(pts.last());
    for (int i = pts.size() - 2; i >= 0; --i)
        path.lineTo(pts.at(i));
    path.closeSubpath();
}

QPainterPath starPath(const QRectF &bounds, int points, double innerRatio, bool alternateOuter)
{
    const int spikes = qBound(3, points, 60);
    const double inner = qBound(0.05, innerRatio, 0.95);
    const QPointF center = bounds.center();
    const double rx = bounds.width() / 2.0;
    const double ry = bounds.height() / 2.0;

    QPainterPath path;
    for (int i = 0; i < spikes * 2; ++i) {
        const bool outer = (i % 2) == 0;
        double radius = outer ? 1.0 : inner;
        // A burst reads as an explosion rather than a star because its spikes are uneven.
        if (outer && alternateOuter && ((i / 2) % 2) == 1)
            radius = 0.82;
        const double angle = -M_PI_2 + i * M_PI / spikes;
        const QPointF pt(center.x() + rx * radius * qCos(angle),
                         center.y() + ry * radius * qSin(angle));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    return path;
}

// Body plus tail merged into one outline, so the stroke does not run through the join.
QPainterPath bubblePath(const QRectF &bounds, const ShapeStyle &style, bool rounded, bool thought,
                        bool sharpTail)
{
    const double tailSize = qBound(0.05, style.tailSize, 0.5);
    const QRectF body(bounds.left(), bounds.top(), bounds.width(),
                      bounds.height() * (1.0 - tailSize));
    const Grid g{bounds};

    QPainterPath path;
    if (rounded) {
        const double radius =
            qBound(0.0, style.cornerRadius, qMin(body.width(), body.height()) / 2.0);
        path.addRoundedRect(body, radius, radius);
    } else {
        path.addEllipse(body);
    }

    const double tx = qBound(0.08, style.tailX, 0.92);
    if (thought) {
        // Two shrinking puffs trailing away from the body instead of a pointer. The radii are
        // clamped against the box so a wide tail cannot push them outside the layout rect.
        const double r1 = qMin(bounds.height() * tailSize * 0.45, bounds.width() * 0.06);
        const double r2 = r1 * 0.58;
        const double cx1 = qBound(bounds.left() + r1, g.x(tx), bounds.right() - r1);
        const double cx2 = qBound(bounds.left() + r2, g.x(tx) - bounds.width() * 0.1,
                                  bounds.right() - r2);
        QPainterPath puffs;
        puffs.setFillRule(Qt::WindingFill);
        puffs.addEllipse(QPointF(cx1, qMin(g.y(1.0 - tailSize * 0.55), bounds.bottom() - r1)), r1,
                         r1);
        puffs.addEllipse(QPointF(cx2, bounds.bottom() - r2), r2, r2);
        return path.united(puffs);
    }

    const double halfWidth = sharpTail ? 0.035 : 0.09;
    QPainterPath tail;
    tail.moveTo(g.p(tx - halfWidth, 1.0 - tailSize - 0.02));
    tail.lineTo(g.p(tx + halfWidth, 1.0 - tailSize - 0.02));
    tail.lineTo(g.p(qMax(0.0, tx - halfWidth * 1.6), 1.0));
    tail.closeSubpath();
    return path.united(tail);
}

// Authored in a 1.6 x 1 design space, then fitted to the bounds. Each puff is united on its own:
// ellipses added to one path would use the odd-even rule and leave a hole where two overlap.
QPainterPath cloudPath(const QRectF &bounds)
{
    struct Puff
    {
        double cx, cy, r;
    };
    constexpr Puff puffs[] = {{0.26, 0.66, 0.22}, {0.50, 0.42, 0.26}, {0.86, 0.36, 0.30}, {1.22, 0.62, 0.26}};

    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    path.addRoundedRect(QRectF(0.26, 0.56, 0.96, 0.32), 0.16, 0.16);
    for (const Puff &puff : puffs) {
        QPainterPath circle;
        circle.addEllipse(QPointF(puff.cx, puff.cy), puff.r, puff.r);
        path = path.united(circle);
    }
    return fillBounds(path, bounds);
}

QPainterPath curvedArrowPath(const QRectF &bounds, const ShapeStyle &style)
{
    const Grid g{bounds};
    const double t = qBound(0.12, style.thickness, 0.6);
    const double innerLeft = 0.04 + t * 0.55;

    QPainterPath path;
    path.moveTo(g.p(0.04, 0.98));
    path.cubicTo(g.p(0.04, 0.32), g.p(0.34, 0.06), g.p(0.68, 0.15));
    path.lineTo(g.p(0.68, 0.00));
    path.lineTo(g.p(1.00, 0.28));
    path.lineTo(g.p(0.68, 0.56));
    path.lineTo(g.p(0.68, 0.41));
    path.cubicTo(g.p(0.46, 0.35), g.p(innerLeft, 0.46), g.p(innerLeft, 0.98));
    path.closeSubpath();
    return path;
}

QPainterPath ringPath(const QRectF &bounds, double thickness)
{
    const double inner = 1.0 - qBound(0.05, thickness, 0.95);
    QPainterPath path;
    path.addEllipse(bounds);
    const QPointF c = bounds.center();
    const double rx = bounds.width() / 2.0 * inner;
    const double ry = bounds.height() / 2.0 * inner;
    addEllipseHole(path, QRectF(c.x() - rx, c.y() - ry, rx * 2.0, ry * 2.0));
    return path;
}

QPainterPath framePath(const QRectF &bounds, double thickness)
{
    const double inset = qBound(0.05, thickness, 0.95) * qMin(bounds.width(), bounds.height()) / 2.0;
    const QRectF r = bounds.adjusted(inset, inset, -inset, -inset);
    QPainterPath path;
    path.addRect(bounds);
    addPolygonHole(path, {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()});
    return path;
}

// Both start at the top and run clockwise, as a pie chart does.
QPainterPath piePath(const QRectF &bounds, double sweep)
{
    QPainterPath path;
    path.moveTo(bounds.center());
    path.arcTo(bounds, 90.0, -qBound(1.0, sweep, 359.0));
    path.closeSubpath();
    return path;
}

// A band of the ring centred on the top, so a half sweep reads as a rainbow.
QPainterPath arcPath(const QRectF &bounds, double thickness, double sweepDeg)
{
    const double sweep = qBound(1.0, sweepDeg, 359.0);
    const double inner = 1.0 - qBound(0.05, thickness, 0.95);
    const QPointF c = bounds.center();
    const double irx = bounds.width() / 2.0 * inner;
    const double iry = bounds.height() / 2.0 * inner;
    const QRectF innerRect(c.x() - irx, c.y() - iry, irx * 2.0, iry * 2.0);
    const double start = 90.0 + sweep / 2.0;
    const double end = qDegreesToRadians(start - sweep);

    QPainterPath path;
    path.arcMoveTo(bounds, start);
    path.arcTo(bounds, start, -sweep);
    path.lineTo(c.x() + irx * qCos(end), c.y() - iry * qSin(end));
    path.arcTo(innerRect, start - sweep, sweep);
    path.closeSubpath();
    return path;
}

// Outer circle minus an offset circle, built as two arcs between the intersection points so the
// outline stays a clean pair of curves.
QPainterPath crescentPath(const QRectF &bounds)
{
    constexpr double r0 = 0.5, r1 = 0.42, d = 0.2;
    const double x = (d * d + r0 * r0 - r1 * r1) / (2.0 * d);
    const double y = qSqrt(r0 * r0 - x * x);
    const double outerTip = qRadiansToDegrees(qAtan2(y, x));
    const double innerTip = qRadiansToDegrees(qAtan2(y, x - d));

    QPainterPath path;
    path.arcMoveTo(QRectF(0.0, 0.0, 1.0, 1.0), outerTip);
    path.arcTo(QRectF(0.0, 0.0, 1.0, 1.0), outerTip, 360.0 - 2.0 * outerTip);
    path.arcTo(QRectF(0.5 + d - r1, 0.5 - r1, 2.0 * r1, 2.0 * r1), -innerTip, -(360.0 - 2.0 * innerTip));
    path.closeSubpath();
    return fillBounds(path, bounds);
}

// Trapezoidal teeth on a root circle, with a hole in the middle.
QPainterPath gearPath(const QRectF &bounds, int teeth, double rootRatio)
{
    const int n = qBound(3, teeth, 60);
    const double root = qBound(0.3, rootRatio, 0.95);
    const QPointF c = bounds.center();
    const double rx = bounds.width() / 2.0;
    const double ry = bounds.height() / 2.0;
    const double pitch = 2.0 * M_PI / n;
    const auto at = [&](double radius, double angle) {
        return QPointF(c.x() + rx * radius * qCos(angle), c.y() + ry * radius * qSin(angle));
    };

    QPainterPath path;
    for (int i = 0; i < n; ++i) {
        const double a = -M_PI_2 + i * pitch;
        const QPointF pts[] = {at(root, a - 0.32 * pitch), at(1.0, a - 0.18 * pitch), at(1.0, a + 0.18 * pitch),
                               at(root, a + 0.32 * pitch)};
        for (const QPointF &pt : pts) {
            if (path.elementCount() == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
    }
    path.closeSubpath();
    const double hole = root * 0.45;
    addEllipseHole(path, QRectF(c.x() - rx * hole, c.y() - ry * hole, rx * hole * 2.0, ry * hole * 2.0));
    return path;
}

// A circle whose edge bulges out between `scallops` valleys; the valley radius is `valleyRatio`.
QPainterPath sealPath(const QRectF &bounds, int scallops, double valleyRatio)
{
    const int n = qBound(3, scallops, 60);
    const double valley = qBound(0.5, valleyRatio, 0.99);
    const int steps = n * 12;
    const QPointF c = bounds.center();
    QPainterPath path;
    for (int i = 0; i < steps; ++i) {
        const double a = -M_PI_2 + i * 2.0 * M_PI / steps;
        const double r = valley + (1.0 - valley) * qAbs(qCos(n * (a + M_PI_2) / 2.0));
        const QPointF pt(c.x() + bounds.width() / 2.0 * r * qCos(a), c.y() + bounds.height() / 2.0 * r * qSin(a));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    return path;
}

QPainterPath shieldPath(const QRectF &bounds)
{
    const Grid g{bounds};
    QPainterPath path;
    path.moveTo(g.p(0.50, 0.00));
    path.cubicTo(g.p(0.66, 0.11), g.p(0.84, 0.15), g.p(1.00, 0.15));
    path.lineTo(g.p(1.00, 0.50));
    path.cubicTo(g.p(1.00, 0.78), g.p(0.72, 0.92), g.p(0.50, 1.00));
    path.cubicTo(g.p(0.28, 0.92), g.p(0.00, 0.78), g.p(0.00, 0.50));
    path.lineTo(g.p(0.00, 0.15));
    path.cubicTo(g.p(0.16, 0.15), g.p(0.34, 0.11), g.p(0.50, 0.00));
    path.closeSubpath();
    return path;
}

QPainterPath teardropPath(const QRectF &bounds)
{
    constexpr double k = 0.5523;
    const Grid g{bounds};
    QPainterPath path;
    path.moveTo(g.p(0.50, 0.00));
    path.cubicTo(g.p(0.62, 0.22), g.p(1.00, 0.42), g.p(1.00, 0.66));
    path.cubicTo(g.p(1.00, 0.66 + 0.34 * k), g.p(0.5 + 0.5 * k, 1.00), g.p(0.50, 1.00));
    path.cubicTo(g.p(0.5 - 0.5 * k, 1.00), g.p(0.00, 0.66 + 0.34 * k), g.p(0.00, 0.66));
    path.cubicTo(g.p(0.00, 0.42), g.p(0.38, 0.22), g.p(0.50, 0.00));
    path.closeSubpath();
    return path;
}

QPainterPath locationPinPath(const QRectF &bounds)
{
    constexpr double k = 0.5523;
    const Grid g{bounds};
    QPainterPath path;
    path.moveTo(g.p(0.50, 1.00));
    path.cubicTo(g.p(0.40, 0.80), g.p(0.00, 0.62), g.p(0.00, 0.36));
    path.cubicTo(g.p(0.00, 0.36 * (1.0 - k)), g.p(0.5 * (1.0 - k), 0.00), g.p(0.50, 0.00));
    path.cubicTo(g.p(0.5 + 0.5 * k, 0.00), g.p(1.00, 0.36 * (1.0 - k)), g.p(1.00, 0.36));
    path.cubicTo(g.p(1.00, 0.62), g.p(0.60, 0.80), g.p(0.50, 1.00));
    path.closeSubpath();
    addEllipseHole(path, QRectF(g.p(0.31, 0.22), g.p(0.69, 0.50)));
    return path;
}

QPainterPath tagPath(const QRectF &bounds)
{
    const Grid g{bounds};
    QPainterPath path = polygonPath(g, {{0.28, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.28, 1.0}, {0.0, 0.5}});
    addEllipseHole(path, QRectF(g.p(0.20, 0.416), g.p(0.32, 0.584)));
    return path;
}

// A thick polyline outlined into a closed polygon, authored in a unit square and fitted.
QPainterPath checkmarkPath(const QRectF &bounds, double thickness)
{
    QPainterPath line;
    line.moveTo(0.05, 0.55);
    line.lineTo(0.38, 0.90);
    line.lineTo(0.95, 0.10);
    QPainterPathStroker stroker;
    stroker.setWidth(qBound(0.05, thickness, 1.0) * 0.5);
    stroker.setJoinStyle(Qt::MiterJoin);
    stroker.setCapStyle(Qt::FlatCap);
    QPainterPath outline = stroker.createStroke(line);
    outline.setFillRule(Qt::WindingFill);
    return fillBounds(outline, bounds);
}

QPainterPath xMarkPath(const QRectF &bounds, double thickness)
{
    const double t = qBound(0.05, thickness, 0.95);
    const double lo = 0.5 - t / 2.0;
    const double hi = 0.5 + t / 2.0;
    const QPainterPath cross =
        polygonPath(Grid{QRectF(0, 0, 1, 1)}, {{lo, 0.0}, {hi, 0.0}, {hi, lo}, {1.0, lo}, {1.0, hi}, {hi, hi},
                                               {hi, 1.0}, {lo, 1.0}, {lo, hi}, {0.0, hi}, {0.0, lo}, {lo, lo}});
    QTransform rotate;
    rotate.translate(0.5, 0.5);
    rotate.rotate(45.0);
    rotate.translate(-0.5, -0.5);
    return fillBounds(rotate.map(cross), bounds);
}

QPainterPath contoursPath(const QVector<ShapeContour> &contours, const QRectF &bounds, Qt::FillRule rule)
{
    const Grid g{bounds};
    const auto map = [&](const QPointF &p) { return g.p(p.x(), p.y()); };
    const auto handle = [&](const QPointF &p, const QPointF &off) {
        return g.p(p.x() + off.x(), p.y() + off.y());
    };

    QPainterPath path;
    path.setFillRule(rule);
    for (const ShapeContour &c : contours) {
        const int n = c.vertices.size();
        if (n < 2)
            continue;
        path.moveTo(map(c.vertices.first().p));
        const int segments = c.closed ? n : n - 1;
        for (int i = 0; i < segments; ++i) {
            const ShapeVertex &a = c.vertices.at(i);
            const ShapeVertex &b = c.vertices.at((i + 1) % n);
            if (!a.out.isNull() || !b.in.isNull())
                path.cubicTo(handle(a.p, a.out), handle(b.p, b.in), map(b.p));
            else if (i + 1 < n)
                path.lineTo(map(b.p));
        }
        if (c.closed)
            path.closeSubpath();
    }
    return path;
}

} // namespace

QVector<ShapeContour> contoursFromPainterPath(const QPainterPath &path, const QRectF &bounds)
{
    const double w = bounds.width() > 0.0 ? bounds.width() : 1.0;
    const double h = bounds.height() > 0.0 ? bounds.height() : 1.0;
    const auto norm = [&](double x, double y) { return QPointF((x - bounds.left()) / w, (y - bounds.top()) / h); };
    const auto offset = [&](const QPainterPath::Element &handle, const QPointF &anchor) {
        return norm(handle.x, handle.y) - anchor;
    };

    QVector<ShapeContour> out;
    ShapeContour current;
    const auto finish = [&]() {
        if (current.vertices.size() >= 2) {
            // QPainterPath has no closed flag; a subpath that ends where it began is the closed one.
            ShapeVertex &first = current.vertices.first();
            ShapeVertex &last = current.vertices.last();
            if (QLineF(first.p, last.p).length() < 1e-9) {
                first.in = last.in;
                current.vertices.removeLast();
                current.closed = true;
            } else {
                current.closed = false;
            }
            for (ShapeVertex &v : current.vertices) {
                const double cross = v.in.x() * v.out.y() - v.in.y() * v.out.x();
                const double dot = v.in.x() * v.out.x() + v.in.y() * v.out.y();
                v.smooth = !v.in.isNull() && !v.out.isNull() && qAbs(cross) < 1e-6 && dot < 0.0;
            }
            if (current.vertices.size() >= 2)
                out.append(current);
        }
        current = {};
    };

    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            finish();
            current.vertices.append({norm(e.x, e.y), {}, {}, false});
            break;
        case QPainterPath::LineToElement:
            current.vertices.append({norm(e.x, e.y), {}, {}, false});
            break;
        case QPainterPath::CurveToElement: {
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            ShapeVertex v{norm(end.x, end.y), {}, {}, false};
            v.in = offset(c2, v.p);
            current.vertices.last().out = offset(e, current.vertices.last().p);
            current.vertices.append(v);
            i += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    finish();
    return out;
}


QPainterPath regularPolygonPath(const QRectF &bounds, int sides, double rotationDeg)
{
    const int n = qMax(3, sides);
    const QPointF center = bounds.center();
    const double rx = bounds.width() / 2.0;
    const double ry = bounds.height() / 2.0;
    const double start = qDegreesToRadians(rotationDeg - 90.0);

    QPainterPath path;
    for (int i = 0; i < n; ++i) {
        const double angle = start + i * 2.0 * M_PI / n;
        const QPointF pt(center.x() + rx * qCos(angle), center.y() + ry * qSin(angle));
        if (i == 0)
            path.moveTo(pt);
        else
            path.lineTo(pt);
    }
    path.closeSubpath();
    return path;
}

QPainterPath maskStarPath(const QRectF &bounds)
{
    return starPath(bounds, 5, 0.4, false);
}

QPainterPath heartPath(const QRectF &bounds)
{
    const Grid g{bounds};
    QPainterPath path;
    path.moveTo(g.p(0.50, 1.00));
    path.cubicTo(g.p(0.42, 0.86), g.p(0.00, 0.58), g.p(0.00, 0.31));
    path.cubicTo(g.p(0.00, 0.11), g.p(0.18, 0.00), g.p(0.32, 0.00));
    path.cubicTo(g.p(0.42, 0.00), g.p(0.50, 0.09), g.p(0.50, 0.18));
    path.cubicTo(g.p(0.50, 0.09), g.p(0.58, 0.00), g.p(0.68, 0.00));
    path.cubicTo(g.p(0.82, 0.00), g.p(1.00, 0.11), g.p(1.00, 0.31));
    path.cubicTo(g.p(1.00, 0.58), g.p(0.58, 0.86), g.p(0.50, 1.00));
    path.closeSubpath();
    return path;
}

QPainterPath shapePath(const ShapeStyle &style, const QRectF &bounds)
{
    if (bounds.width() <= 0.0 || bounds.height() <= 0.0)
        return {};

    const Grid g{bounds};
    // Shared arrow/chevron geometry: head length along x, shaft thickness along y.
    const double head = qBound(0.05, style.headSize, 0.9);
    const double shaft = qBound(0.05, style.thickness, 1.0);
    const double shaftTop = 0.5 - shaft / 2.0;
    const double shaftBottom = 0.5 + shaft / 2.0;

    switch (style.kind) {
    case ShapeKind::Rectangle:
    case ShapeKind::Square: {
        QPainterPath path;
        path.addRect(bounds);
        return path;
    }
    case ShapeKind::RoundedRectangle: {
        const double radius =
            qBound(0.0, style.cornerRadius, qMin(bounds.width(), bounds.height()) / 2.0);
        QPainterPath path;
        path.addRoundedRect(bounds, radius, radius);
        return path;
    }
    case ShapeKind::Ellipse: {
        QPainterPath path;
        path.addEllipse(bounds);
        return path;
    }
    case ShapeKind::Triangle:
        return polygonPath(g, {{0.5, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    case ShapeKind::RightTriangle:
        return polygonPath(g, {{0.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}});
    case ShapeKind::Diamond:
        return polygonPath(g, {{0.5, 0.0}, {1.0, 0.5}, {0.5, 1.0}, {0.0, 0.5}});
    case ShapeKind::Pentagon:
        return regularPolygonPath(bounds, 5, 0.0);
    case ShapeKind::Hexagon:
        return regularPolygonPath(bounds, 6, 0.0);
    case ShapeKind::Octagon:
        return regularPolygonPath(bounds, 8, 22.5);
    case ShapeKind::Parallelogram:
        return polygonPath(g, {{0.22, 0.0}, {1.0, 0.0}, {0.78, 1.0}, {0.0, 1.0}});
    case ShapeKind::Trapezoid:
        return polygonPath(g, {{0.2, 0.0}, {0.8, 0.0}, {1.0, 1.0}, {0.0, 1.0}});

    case ShapeKind::Arrow:
        return polygonPath(g, {{0.0, shaftTop},
                               {1.0 - head, shaftTop},
                               {1.0 - head, 0.0},
                               {1.0, 0.5},
                               {1.0 - head, 1.0},
                               {1.0 - head, shaftBottom},
                               {0.0, shaftBottom}});
    case ShapeKind::DoubleArrow:
        return polygonPath(g, {{0.0, 0.5},
                               {head, 0.0},
                               {head, shaftTop},
                               {1.0 - head, shaftTop},
                               {1.0 - head, 0.0},
                               {1.0, 0.5},
                               {1.0 - head, 1.0},
                               {1.0 - head, shaftBottom},
                               {head, shaftBottom},
                               {head, 1.0}});
    case ShapeKind::BlockArrow:
        return polygonPath(g, {{0.0, 0.0}, {1.0 - head, 0.0}, {1.0, 0.5}, {1.0 - head, 1.0},
                               {0.0, 1.0}});
    case ShapeKind::CurvedArrow:
        return curvedArrowPath(bounds, style);
    case ShapeKind::Chevron:
        return polygonPath(g, {{0.0, 0.0}, {1.0 - head, 0.0}, {1.0, 0.5}, {1.0 - head, 1.0},
                               {0.0, 1.0}, {head, 0.5}});

    case ShapeKind::SpeechBubble:
        return bubblePath(bounds, style, false, false, false);
    case ShapeKind::SpeechBubbleRect:
        return bubblePath(bounds, style, true, false, false);
    case ShapeKind::ThoughtBubble:
        return bubblePath(bounds, style, false, true, false);
    case ShapeKind::Callout:
        return bubblePath(bounds, style, true, false, true);

    case ShapeKind::Star:
        return starPath(bounds, style.points, style.innerRatio, false);
    case ShapeKind::Burst:
        return starPath(bounds, style.points, style.innerRatio, true);
    case ShapeKind::LightningBolt:
        return polygonPath(g, {{0.58, 0.0}, {0.16, 0.56}, {0.44, 0.56}, {0.34, 1.0}, {0.84, 0.40},
                               {0.54, 0.40}, {0.78, 0.0}});
    case ShapeKind::Cloud:
        return cloudPath(bounds);
    case ShapeKind::Heart:
        return heartPath(bounds);
    case ShapeKind::Cross: {
        const double t = qBound(0.05, style.thickness, 0.95);
        const double lo = 0.5 - t / 2.0;
        const double hi = 0.5 + t / 2.0;
        return polygonPath(g, {{lo, 0.0}, {hi, 0.0}, {hi, lo}, {1.0, lo}, {1.0, hi}, {hi, hi},
                               {hi, 1.0}, {lo, 1.0}, {lo, hi}, {0.0, hi}, {0.0, lo}, {lo, lo}});
    }
    case ShapeKind::Path:
        return contoursPath(style.contours, bounds, style.fillRule);
    case ShapeKind::Ring:
        return ringPath(bounds, style.thickness);
    case ShapeKind::Pie:
        return piePath(bounds, style.sweep);
    case ShapeKind::Arc:
        return arcPath(bounds, style.thickness, style.sweep);
    case ShapeKind::Crescent:
        return crescentPath(bounds);
    case ShapeKind::Pill: {
        QPainterPath path;
        const double radius = qMin(bounds.width(), bounds.height()) / 2.0;
        path.addRoundedRect(bounds, radius, radius);
        return path;
    }
    case ShapeKind::Frame:
        return framePath(bounds, style.thickness);
    case ShapeKind::Polygon:
        return regularPolygonPath(bounds, style.points, 0.0);
    case ShapeKind::Gear:
        return gearPath(bounds, style.points, style.innerRatio);
    case ShapeKind::Seal:
        return sealPath(bounds, style.points, style.innerRatio);
    case ShapeKind::Shield:
        return shieldPath(bounds);
    case ShapeKind::Teardrop:
        return teardropPath(bounds);
    case ShapeKind::LocationPin:
        return locationPinPath(bounds);
    case ShapeKind::Tag:
        return tagPath(bounds);
    case ShapeKind::Bookmark:
        return polygonPath(g, {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.5, 0.72}, {0.0, 1.0}});
    case ShapeKind::Checkmark:
        return checkmarkPath(bounds, style.thickness);
    case ShapeKind::XMark:
        return xMarkPath(bounds, style.thickness);
    case ShapeKind::Banner: {
        const double notch = qBound(0.02, style.headSize * 0.3, 0.4);
        return polygonPath(g, {{0.0, 0.0}, {1.0, 0.0}, {1.0 - notch, 0.5}, {1.0, 1.0}, {0.0, 1.0},
                               {notch, 0.5}});
    }
    }

    QPainterPath path;
    path.addRect(bounds);
    return path;
}

QString contoursToSvgPath(const QVector<ShapeContour> &contours, const QSizeF &scale)
{
    const auto num = [](double v) { return QString::number(v, 'g', 7); };
    const auto pt = [&](const QPointF &p) {
        return QStringLiteral("%1 %2").arg(num(p.x() * scale.width()), num(p.y() * scale.height()));
    };

    QString out;
    for (const ShapeContour &c : contours) {
        const int n = c.vertices.size();
        if (n < 2)
            continue;
        out += QStringLiteral("M %1 ").arg(pt(c.vertices.first().p));
        const int segments = c.closed ? n : n - 1;
        for (int i = 0; i < segments; ++i) {
            const ShapeVertex &a = c.vertices.at(i);
            const ShapeVertex &b = c.vertices.at((i + 1) % n);
            if (!a.out.isNull() || !b.in.isNull())
                out += QStringLiteral("C %1 %2 %3 ").arg(pt(a.p + a.out), pt(b.p + b.in), pt(b.p));
            else if (i + 1 < n)
                out += QStringLiteral("L %1 ").arg(pt(b.p));
        }
        if (c.closed)
            out += QStringLiteral("Z ");
    }
    return out.trimmed();
}

QString painterPathToSvg(const QPainterPath &path)
{
    // QString::number keeps the C locale; arg() would emit commas under a European locale and QML
    // would silently render nothing.
    const auto num = [](double v) { return QString::number(v, 'f', 3); };

    QString out;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        switch (e.type) {
        case QPainterPath::MoveToElement:
            out += QStringLiteral("M %1 %2 ").arg(num(e.x), num(e.y));
            break;
        case QPainterPath::LineToElement:
            out += QStringLiteral("L %1 %2 ").arg(num(e.x), num(e.y));
            break;
        case QPainterPath::CurveToElement: {
            // cubicTo() emits the first control point here and the rest as CurveToData.
            const QPainterPath::Element c2 = path.elementAt(i + 1);
            const QPainterPath::Element end = path.elementAt(i + 2);
            out += QStringLiteral("C %1 %2 %3 %4 %5 %6 ")
                       .arg(num(e.x), num(e.y), num(c2.x), num(c2.y), num(end.x), num(end.y));
            i += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
            break;
        }
    }
    return out.trimmed();
}

} // namespace drift
