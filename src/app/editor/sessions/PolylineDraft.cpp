#include "app/editor/sessions/PolylineDraft.h"

#include <QLineF>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kCloseTolerancePx = 8.0;
constexpr double kDuplicatePx = 3.0;
}

PolylineDraft::PolylineDraft(QObject *parent) : QObject(parent) {}

void PolylineDraft::setMinPoints(int n)
{
    if (m_minPoints == n)
        return;
    m_minPoints = n;
    emit minPointsChanged();
    emit hoverChanged();
}

bool PolylineDraft::hoverNearFirst() const
{
    return nearFirst(m_hover, kCloseTolerancePx);
}

QPointF PolylineDraft::constrained(const QPointF &p, bool constrain45) const
{
    if (!constrain45 || m_points.isEmpty())
        return p;
    const QPointF anchor = m_points.last();
    const double step = M_PI / 4;
    const double angle = std::round(std::atan2(p.y() - anchor.y(), p.x() - anchor.x()) / step) * step;
    const double len = QLineF(p, anchor).length();
    return {anchor.x() + std::cos(angle) * len, anchor.y() + std::sin(angle) * len};
}

bool PolylineDraft::nearFirst(const QPointF &p, double tolPx) const
{
    return m_points.size() >= m_minPoints && !m_points.isEmpty() && QLineF(p, m_points.first()).length() <= tolPx;
}

bool PolylineDraft::add(const QPointF &p, bool constrain45)
{
    const QPointF q = constrained(p, constrain45);
    if (!m_points.isEmpty() && QLineF(q, m_points.last()).length() < kDuplicatePx)
        return false;
    m_points.append(q);
    emit pointsChanged();
    emit hoverChanged();
    return true;
}

void PolylineDraft::moveHover(const QPointF &p, bool constrain45)
{
    const QPointF q = constrained(p, constrain45);
    if (q == m_hover)
        return;
    m_hover = q;
    emit hoverChanged();
}

void PolylineDraft::undo()
{
    if (m_points.isEmpty())
        return;
    m_points.removeLast();
    emit pointsChanged();
    emit hoverChanged();
}

void PolylineDraft::clear()
{
    if (m_points.isEmpty())
        return;
    m_points.clear();
    emit pointsChanged();
    emit hoverChanged();
}

QVariantList PolylineDraft::normalized(double frameW, double frameH, bool clamp) const
{
    const double w = std::max(1.0, frameW);
    const double h = std::max(1.0, frameH);
    QVariantList out;
    out.reserve(m_points.size());
    for (const QPointF &p : m_points) {
        double x = p.x() / w;
        double y = p.y() / h;
        if (clamp) {
            x = std::clamp(x, 0.0, 1.0);
            y = std::clamp(y, 0.0, 1.0);
        }
        out.append(QVariantMap{{"x", x}, {"y", y}});
    }
    return out;
}
