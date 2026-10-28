#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QVariantMap>

class QPainter;

// How a key diamond (or a hold square) is drawn, read from the keyframesKeyDot theme tokens that
// QML hands the Keyframes items. The animation duration rides along so selection and hover scale
// tween the way the QML key dots did.
namespace KeyframeDot {

struct Style
{
    qreal size = 10;
    qreal radius = 1;
    qreal borderWidth = 1;
    qreal borderWidthSelected = 2;
    qreal scaleHover = 1.15;
    qreal scaleSelected = 1.3;
    qreal opacityDisabled = 0.35;
    QColor border;
    QColor borderSelected;
    QColor borderCollision;
    QColor summary;

    static Style fromMap(const QVariantMap &map);
};

struct State
{
    bool hold = false;
    bool selected = false;
    bool collides = false;
    bool hovered = false;
    bool disabled = false;
    qreal opacity = 1;
};

void paint(QPainter *painter, const QPointF &center, const Style &style, const QColor &fill, const State &state,
           qreal scale);

qreal targetScale(const Style &style, const State &state);

// Tweens a dot's scale towards its target. A dot seen for the first time starts at its target, so
// only a change of state animates.
class Tween
{
public:
    void setDuration(int ms) { m_duration = ms; }
    qreal scaleFor(const QString &key, qreal target);
    // Drops the dots that were not asked for since the last call; true while any is still moving.
    bool finishPass();

private:
    struct Entry
    {
        qreal from = 1;
        qreal target = 1;
        qint64 startedAt = 0;
        bool seen = false;
    };

    QHash<QString, Entry> m_entries;
    QElapsedTimer m_clock;
    int m_duration = 150;
};

} // namespace KeyframeDot
