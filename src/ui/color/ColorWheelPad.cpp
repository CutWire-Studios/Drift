#include "ui/color/ColorWheelPad.h"

#include "ui/color/ColorWheelMath.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>

#include <cmath>

using namespace drift::colorwheel;

ColorWheelPad::ColorWheelPad(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setActiveFocusOnTab(true);
    setAntialiasing(true);
    setCursor(QCursor(Qt::CrossCursor));
    connect(this, &ColorWheelPad::styleChanged, this, [this] { update(); });
    connect(this, &ColorWheelPad::discStyleChanged, this, [this] { update(); });
    connect(this, &ColorWheelPad::offsetChanged, this, [this] { update(); });
    connect(this, &ColorWheelPad::pressedChanged, this, [this] { update(); });
}

void ColorWheelPad::setOffset(const QPointF &offset)
{
    if (offset == m_offset)
        return;
    m_offset = offset;
    emit offsetChanged();
}

void ColorWheelPad::setPressed(bool pressed)
{
    if (pressed == m_pressed)
        return;
    m_pressed = pressed;
    emit pressedChanged();
}

bool ColorWheelPad::contains(const QPointF &point) const
{
    return std::hypot(point.x() - width() / 2, point.y() - height() / 2) <= radius() + 6;
}

void ColorWheelPad::mousePressEvent(QMouseEvent *event)
{
    forceActiveFocus(Qt::MouseFocusReason);
    setKeepMouseGrab(true);
    setKeepTouchGrab(true);
    m_last = event->position();
    m_live = m_offset;
    setPressed(true);
    event->accept();
}

void ColorWheelPad::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressed)
        return;
    if (!m_begun) {
        m_begun = true;
        emit began();
    }
    const QPointF pos = event->position();
    m_live = dragged(m_live, pos - m_last, radius(), event->modifiers().testFlag(Qt::ShiftModifier));
    m_last = pos;
    emit offsetMoved(m_live.x(), m_live.y());
    update();
    event->accept();
}

void ColorWheelPad::finishDrag()
{
    setKeepMouseGrab(false);
    setKeepTouchGrab(false);
    setPressed(false);
    if (m_begun) {
        m_begun = false;
        emit ended();
    }
}

void ColorWheelPad::mouseReleaseEvent(QMouseEvent *event)
{
    finishDrag();
    event->accept();
}

void ColorWheelPad::mouseUngrabEvent()
{
    finishDrag();
}

void ColorWheelPad::touchUngrabEvent()
{
    finishDrag();
}

void ColorWheelPad::mouseDoubleClickEvent(QMouseEvent *event)
{
    emit resetRequested();
    event->accept();
}

void ColorWheelPad::keyPressEvent(QKeyEvent *event)
{
    const double step = event->modifiers().testFlag(Qt::ShiftModifier) ? 0.001 : 0.01;
    double dx = 0;
    double dy = 0;
    switch (event->key()) {
    case Qt::Key_Left:
        dx = -step;
        break;
    case Qt::Key_Right:
        dx = step;
        break;
    case Qt::Key_Up:
        dy = step;
        break;
    case Qt::Key_Down:
        dy = -step;
        break;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        emit resetRequested();
        event->accept();
        return;
    default:
        QQuickPaintedItem::keyPressEvent(event);
        return;
    }
    const QPointF next = nudged(m_offset, dx, dy);
    emit began();
    emit offsetMoved(next.x(), next.y());
    emit ended();
    event->accept();
}

void ColorWheelPad::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    update();
}

void ColorWheelPad::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
    update();
}

const QImage &ColorWheelPad::disc(double dpr)
{
    const int px = std::max(1, int(std::lround(2 * radius() * dpr)));
    if (m_disc.isNull() || px != m_discPx || m_neutral != m_discNeutral || m_rimStrength != m_discRim) {
        m_disc = buildDisc(px, m_neutral, m_rimStrength);
        m_disc.setDevicePixelRatio(dpr);
        m_discPx = px;
        m_discNeutral = m_neutral;
        m_discRim = m_rimStrength;
    }
    return m_disc;
}

void ColorWheelPad::paint(QPainter *painter)
{
    const double rad = radius();
    if (rad < 1)
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    const QPointF center(width() / 2, height() / 2);
    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;

    painter->drawImage(QRectF(center.x() - rad, center.y() - rad, 2 * rad, 2 * rad), disc(dpr));

    const bool focused = hasActiveFocus();
    const double ringWidth = focused ? m_puckBorderWidth : m_ringWidth;
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(focused ? m_focusRing : m_pressed ? m_ringActive : m_ring, ringWidth));
    painter->drawEllipse(center, rad - ringWidth / 2, rad - ringWidth / 2);

    painter->setOpacity(m_crosshairOpacity);
    painter->fillRect(QRectF(center.x() - m_crosshairWidth / 2, 0, m_crosshairWidth, height()), m_crosshair);
    painter->fillRect(QRectF(0, center.y() - m_crosshairWidth / 2, width(), m_crosshairWidth), m_crosshair);

    const QPointF shown = m_pressed ? m_live : m_offset;
    const QPointF puck(center.x() + shown.x() * rad, center.y() - shown.y() * rad);
    if (std::hypot(shown.x(), shown.y()) * rad > 0.5) {
        painter->setOpacity(m_vectorOpacity);
        painter->setPen(QPen(m_vector, m_crosshairWidth));
        painter->drawLine(center, puck);
    }

    painter->setOpacity(1.0);
    painter->setBrush(m_puck);
    painter->setPen(QPen(m_puckBorder, m_puckBorderWidth));
    const double pr = (m_puckSize - m_puckBorderWidth) / 2;
    painter->drawEllipse(puck, pr, pr);
}
