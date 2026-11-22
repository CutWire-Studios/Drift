#include "ui/preview/ToolCursor.h"

#include <QCursor>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QQuickWindow>

#include <cmath>
#include <functional>

namespace {
constexpr int kSize = 32;
constexpr double kCenter = kSize / 2.0;

QPolygonF ngon(const QPointF &c, double r, int n)
{
    QPolygonF poly;
    for (int i = 0; i < n; ++i) {
        const double a = -M_PI / 2 + i * 2 * M_PI / n;
        poly << QPointF(c.x() + std::cos(a) * r, c.y() + std::sin(a) * r);
    }
    return poly;
}

void strokeTwice(QPainter &p, const std::function<void()> &draw)
{
    p.setPen(QPen(QColor(0, 0, 0, 200), 3.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    draw();
    p.setPen(QPen(Qt::white, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    draw();
}

void drawCrosshair(QPainter &p, double gap)
{
    constexpr double arm = 8;
    strokeTwice(p, [&] {
        p.drawLine(QPointF(kCenter - arm, kCenter), QPointF(kCenter - gap, kCenter));
        p.drawLine(QPointF(kCenter + gap, kCenter), QPointF(kCenter + arm, kCenter));
        p.drawLine(QPointF(kCenter, kCenter - arm), QPointF(kCenter, kCenter - gap));
        p.drawLine(QPointF(kCenter, kCenter + gap), QPointF(kCenter, kCenter + arm));
    });
}

void fillTwice(QPainter &p, const QPainterPath &path)
{
    p.setPen(QPen(QColor(0, 0, 0, 200), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::white);
    p.drawPath(path);
}

// "resize:45" and "rotate:30" carry an angle in degrees; anything unparsable reads as 0.
double angleOf(const QString &shape)
{
    const int colon = shape.indexOf(QLatin1Char(':'));
    return colon < 0 ? 0.0 : shape.mid(colon + 1).toDouble();
}

// A double-headed arrow along the angle, 0 = horizontal.
void drawResizeArrow(QPainter &p, double degrees)
{
    constexpr double half = 11.0;
    constexpr double head = 4.5;
    constexpr double wing = 3.5;
    QPainterPath path;
    path.moveTo(-half, 0);
    path.lineTo(-half + head, -wing);
    path.lineTo(-half + head, -1.2);
    path.lineTo(half - head, -1.2);
    path.lineTo(half - head, -wing);
    path.lineTo(half, 0);
    path.lineTo(half - head, wing);
    path.lineTo(half - head, 1.2);
    path.lineTo(-half + head, 1.2);
    path.lineTo(-half + head, wing);
    path.closeSubpath();
    p.save();
    p.translate(kCenter, kCenter);
    p.rotate(degrees);
    fillTwice(p, path);
    p.restore();
}

// A quarter-circle arrow whose middle points away from the object; the angle is where it sits.
void drawRotateArrow(QPainter &p, double degrees)
{
    constexpr double r = 9.0;
    p.save();
    p.translate(kCenter, kCenter);
    p.rotate(degrees);
    p.setBrush(Qt::NoBrush);
    strokeTwice(p, [&] {
        p.drawArc(QRectF(-r, -r, 2 * r, 2 * r), 45 * 16, 90 * 16);
        const QPointF a(r * std::cos(M_PI / 4), -r * std::sin(M_PI / 4));
        const QPointF c(r * std::cos(M_PI / 4), r * std::sin(M_PI / 4));
        p.drawLine(a, a + QPointF(-4, -1));
        p.drawLine(a, a + QPointF(0.5, 4));
        p.drawLine(c, c + QPointF(-4, 1));
        p.drawLine(c, c + QPointF(0.5, -4));
    });
    p.restore();
}

void drawShape(QPainter &p, const QString &shape)
{
    const QPointF b(kCenter + 10, kCenter + 10);
    p.setBrush(Qt::NoBrush);

    if (shape.startsWith(QLatin1String("resize"))) {
        drawResizeArrow(p, angleOf(shape));
        return;
    }
    if (shape.startsWith(QLatin1String("rotate"))) {
        drawRotateArrow(p, angleOf(shape));
        return;
    }
    if (shape == "move") {
        constexpr double r = 11.0;
        constexpr double h = 3.5;
        strokeTwice(p, [&] {
            p.drawLine(QPointF(kCenter - r, kCenter), QPointF(kCenter + r, kCenter));
            p.drawLine(QPointF(kCenter, kCenter - r), QPointF(kCenter, kCenter + r));
            for (int i = 0; i < 4; ++i) {
                const QPointF tip = i == 0   ? QPointF(kCenter + r, kCenter)
                                    : i == 1 ? QPointF(kCenter - r, kCenter)
                                    : i == 2 ? QPointF(kCenter, kCenter - r)
                                             : QPointF(kCenter, kCenter + r);
                const QPointF back = (QPointF(kCenter, kCenter) - tip) / r * h;
                const QPointF side(-back.y(), back.x());
                p.drawLine(tip, tip + back + side);
                p.drawLine(tip, tip + back - side);
            }
        });
        return;
    }
    if (shape == "anchor") {
        drawCrosshair(p, 5);
        strokeTwice(p, [&] { p.drawEllipse(QPointF(kCenter, kCenter), 4.0, 4.0); });
        return;
    }
    if (shape == "penPlus" || shape == "penClose" || shape == "convert") {
        drawCrosshair(p, 2);
        strokeTwice(p, [&] {
            QPolygonF nib;
            nib << QPointF(b.x() - 3.5, b.y() + 4) << QPointF(b.x() - 3.5, b.y() - 1)
                << QPointF(b.x(), b.y() - 4.5) << QPointF(b.x() + 3.5, b.y() - 1)
                << QPointF(b.x() + 3.5, b.y() + 4);
            p.drawPolygon(nib);
            if (shape == "penPlus") {
                p.drawLine(QPointF(b.x() + 6, b.y() + 2), QPointF(b.x() + 10, b.y() + 2));
                p.drawLine(QPointF(b.x() + 8, b.y()), QPointF(b.x() + 8, b.y() + 4));
            } else if (shape == "penClose") {
                p.drawEllipse(QPointF(b.x() + 8, b.y() + 2), 2.0, 2.0);
            } else {
                p.drawLine(QPointF(b.x() + 5.5, b.y() + 4), QPointF(b.x() + 8, b.y()));
                p.drawLine(QPointF(b.x() + 8, b.y()), QPointF(b.x() + 10.5, b.y() + 4));
            }
        });
        return;
    }

    if (shape == "brush") {
        drawCrosshair(p, 2);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 200));
        p.drawEllipse(QPointF(kCenter, kCenter), 2.2, 2.2);
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(kCenter, kCenter), 1.2, 1.2);
        return;
    }
    if (shape == "trackPoint") {
        drawCrosshair(p, 4);
        strokeTwice(p, [&] { p.drawEllipse(QPointF(kCenter, kCenter), 5.0, 5.0); });
        return;
    }

    drawCrosshair(p, 2);
    if (shape == "rect") {
        strokeTwice(p, [&] { p.drawRect(QRectF(b.x() - 3.5, b.y() - 3.5, 7, 7)); });
    } else if (shape == "ellipse") {
        strokeTwice(p, [&] { p.drawEllipse(b, 4.0, 4.0); });
    } else if (shape == "polygon" || shape == "polygonClose") {
        strokeTwice(p, [&] { p.drawPolygon(ngon(b, 4.5, 5)); });
        if (shape == "polygonClose") {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 200));
            p.drawEllipse(b, 2.6, 2.6);
            p.setBrush(Qt::white);
            p.drawEllipse(b, 1.6, 1.6);
        }
    } else if (shape == "aiInclude" || shape == "aiExclude") {
        strokeTwice(p, [&] {
            p.drawLine(QPointF(b.x() - 3.5, b.y()), QPointF(b.x() + 3.5, b.y()));
            if (shape == "aiInclude")
                p.drawLine(QPointF(b.x(), b.y() - 3.5), QPointF(b.x(), b.y() + 3.5));
        });
    } else if (shape == "trackPlanar") {
        QPolygonF quad;
        quad << QPointF(b.x() - 2, b.y() - 3.5) << QPointF(b.x() + 4.5, b.y() - 3.5)
             << QPointF(b.x() + 2, b.y() + 3.5) << QPointF(b.x() - 4.5, b.y() + 3.5);
        strokeTwice(p, [&] { p.drawPolygon(quad); });
    }
}

// Oriented shapes snap to 5 degrees so a continuous rotation cannot grow the cache without bound.
QString canonicalShape(const QString &shape)
{
    const bool resize = shape.startsWith(QLatin1String("resize:"));
    if (!resize && !shape.startsWith(QLatin1String("rotate:")))
        return shape;
    const double period = resize ? 180.0 : 360.0;
    double a = std::fmod(angleOf(shape), period);
    if (a < 0)
        a += period;
    const int step = int(std::lround(a / 5.0)) * 5 % int(period);
    return shape.left(7) + QString::number(step);
}

QPixmap cursorPixmap(const QString &requested, qreal dpr)
{
    static QHash<QString, QPixmap> cache;
    const QString shape = canonicalShape(requested);
    const QString key = shape + QLatin1Char('@') + QString::number(dpr);
    auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QPixmap pm(qRound(kSize * dpr), qRound(kSize * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        drawShape(p, shape);
    }
    cache.insert(key, pm);
    return pm;
}
}

ToolCursor::ToolCursor(QObject *parent) : QObject(parent) {}

ToolCursor::~ToolCursor()
{
    if (m_target && m_applied)
        m_target->unsetCursor();
}

void ToolCursor::setTarget(QQuickItem *target)
{
    if (m_target == target)
        return;
    if (m_target) {
        disconnect(m_target, nullptr, this, nullptr);
        if (m_applied)
            m_target->unsetCursor();
    }
    m_applied = false;
    m_target = target;
    if (m_target)
        connect(m_target, &QQuickItem::windowChanged, this, &ToolCursor::apply);
    emit targetChanged();
    apply();
}

void ToolCursor::setShape(const QString &shape)
{
    if (m_shape == shape)
        return;
    m_shape = shape;
    emit shapeChanged();
    apply();
}

void ToolCursor::apply()
{
    if (!m_target)
        return;
    if (m_shape.isEmpty()) {
        if (m_applied)
            m_target->unsetCursor();
        m_applied = false;
        return;
    }
    if (m_shape == QLatin1String("busy")) {
        m_target->setCursor(QCursor(Qt::BusyCursor));
        m_applied = true;
        return;
    }
    const qreal dpr = m_target->window() ? m_target->window()->devicePixelRatio() : 1.0;
    m_target->setCursor(QCursor(cursorPixmap(m_shape, dpr), int(kCenter), int(kCenter)));
    m_applied = true;
}

#include "moc_ToolCursor.cpp"
