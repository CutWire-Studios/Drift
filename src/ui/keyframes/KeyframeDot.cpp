#include "ui/keyframes/KeyframeDot.h"

#include <QEasingCurve>
#include <QPainter>

#include <algorithm>

namespace KeyframeDot {

namespace {

QColor colorOf(const QVariantMap &map, const char *key)
{
    return map.value(QLatin1String(key)).value<QColor>();
}

qreal numberOf(const QVariantMap &map, const char *key, qreal fallback)
{
    const QVariant v = map.value(QLatin1String(key));
    return v.isValid() ? v.toReal() : fallback;
}

} // namespace

Style Style::fromMap(const QVariantMap &map)
{
    Style s;
    s.size = numberOf(map, "size", s.size);
    s.radius = numberOf(map, "radius", s.radius);
    s.borderWidth = numberOf(map, "borderWidth", s.borderWidth);
    s.borderWidthSelected = numberOf(map, "borderWidthSelected", s.borderWidthSelected);
    s.scaleHover = numberOf(map, "scaleHover", s.scaleHover);
    s.scaleSelected = numberOf(map, "scaleSelected", s.scaleSelected);
    s.opacityDisabled = numberOf(map, "opacityDisabled", s.opacityDisabled);
    s.border = colorOf(map, "border");
    s.borderSelected = colorOf(map, "borderSelected");
    s.borderCollision = colorOf(map, "borderCollision");
    s.summary = colorOf(map, "summary");
    return s;
}

qreal targetScale(const Style &style, const State &state)
{
    return state.selected ? style.scaleSelected : (state.hovered ? style.scaleHover : 1.0);
}

void paint(QPainter *painter, const QPointF &center, const Style &style, const QColor &fill, const State &state,
           qreal scale)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setOpacity(painter->opacity() * state.opacity * (state.disabled ? style.opacityDisabled : 1.0));
    painter->translate(center);
    if (!state.hold)
        painter->rotate(45);
    painter->scale(scale, scale);

    const qreal half = style.size / 2;
    const QRectF box(-half, -half, style.size, style.size);
    const qreal radius = std::min(style.radius, half);
    painter->setPen(Qt::NoPen);
    painter->setBrush(fill);
    painter->drawRoundedRect(box, radius, radius);

    const qreal width = state.selected || state.collides ? style.borderWidthSelected : style.borderWidth;
    const QColor border = state.collides ? style.borderCollision
                                         : (state.selected ? style.borderSelected : style.border);
    if (width > 0) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(border, width));
        const qreal inset = width / 2;
        painter->drawRoundedRect(box.adjusted(inset, inset, -inset, -inset), std::max(0.0, radius - inset),
                                 std::max(0.0, radius - inset));
    }
    painter->restore();
}

qreal Tween::scaleFor(const QString &key, qreal target)
{
    if (!m_clock.isValid())
        m_clock.start();
    const qint64 now = m_clock.elapsed();
    const auto it = m_entries.find(key);
    if (it == m_entries.end()) {
        Entry entry;
        entry.from = target;
        entry.target = target;
        entry.startedAt = now - m_duration;
        entry.seen = true;
        m_entries.insert(key, entry);
        return target;
    }
    Entry &entry = it.value();
    const auto current = [&entry, now, this]() {
        if (m_duration <= 0)
            return entry.target;
        const qreal t = std::clamp(qreal(now - entry.startedAt) / m_duration, 0.0, 1.0);
        return entry.from + (entry.target - entry.from) * QEasingCurve(QEasingCurve::OutCubic).valueForProgress(t);
    };
    if (entry.target != target) {
        entry.from = current();
        entry.target = target;
        entry.startedAt = now;
    }
    entry.seen = true;
    return current();
}

bool Tween::finishPass()
{
    const qint64 now = m_clock.isValid() ? m_clock.elapsed() : 0;
    bool moving = false;
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        Entry &entry = it.value();
        const bool done = now - entry.startedAt >= m_duration;
        if (!entry.seen) {
            it = m_entries.erase(it);
            continue;
        }
        entry.seen = false;
        moving = moving || !done;
        ++it;
    }
    return moving;
}

} // namespace KeyframeDot
