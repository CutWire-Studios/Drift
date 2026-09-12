#include "ShapePath.h"

#include <QPointF>
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

QPainterPath cloudPath(const QRectF &bounds)
{
    const Grid g{bounds};
    QPainterPath path;
    const QRectF base(g.x(0.02), g.y(0.48), bounds.width() * 0.96, bounds.height() * 0.50);
    path.addRoundedRect(base, base.height() / 2.0, base.height() / 2.0);

    QPainterPath puffs;
    puffs.addEllipse(g.p(0.30, 0.46), bounds.width() * 0.20, bounds.height() * 0.28);
    puffs.addEllipse(g.p(0.54, 0.34), bounds.width() * 0.24, bounds.height() * 0.33);
    puffs.addEllipse(g.p(0.76, 0.48), bounds.width() * 0.18, bounds.height() * 0.25);
    return path.united(puffs);
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

} // namespace

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
