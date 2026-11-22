#include "ui/manip/StrokeSmoother.h"

#include <QLineF>

#include <algorithm>

namespace drift::manip {

StrokeSmoother::StrokeSmoother(QObject *parent) : QObject(parent) {}

void StrokeSmoother::begin(double x, double y)
{
    m_head = QPointF(x, y);
    emit headChanged();
}

QPointF StrokeSmoother::follow(double x, double y, double smoothing)
{
    const QPointF cursor(x, y);
    const double length = std::clamp(smoothing, 0.0, 1.0) * kMaxStringPx;
    QPointF next = m_head;
    if (length <= 0.0) {
        next = cursor;
    } else if (QLineF(cursor, m_head).length() > length) {
        QLineF slack(cursor, m_head);
        slack.setLength(length);
        next = slack.p2();
    }
    if (next != m_head) {
        m_head = next;
        emit headChanged();
    }
    return m_head;
}

} // namespace drift::manip
