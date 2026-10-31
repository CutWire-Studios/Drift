#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QQuickPaintedItem>

#include <algorithm>

// A colour wheel's disc, crosshair, vector and puck, and its drag: relative to where the offset was,
// Shift for fine control, clamped to the unit circle, arrows nudge, double-click and Delete ask for
// a reset. The owner writes the offset back through offsetMoved(); began() and ended() bracket a
// gesture (one undo step). Colours come in as properties; nothing here reads the theme.
class ColorWheelPad : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(QPointF offset READ offset WRITE setOffset NOTIFY offsetChanged)
    Q_PROPERTY(bool pressed READ pressed NOTIFY pressedChanged)
    Q_PROPERTY(QColor neutralColor MEMBER m_neutral NOTIFY discStyleChanged)
    Q_PROPERTY(qreal rimStrength MEMBER m_rimStrength NOTIFY discStyleChanged)
    Q_PROPERTY(QColor ringColor MEMBER m_ring NOTIFY styleChanged)
    Q_PROPERTY(QColor ringActiveColor MEMBER m_ringActive NOTIFY styleChanged)
    Q_PROPERTY(QColor focusRingColor MEMBER m_focusRing NOTIFY styleChanged)
    Q_PROPERTY(qreal ringWidth MEMBER m_ringWidth NOTIFY styleChanged)
    Q_PROPERTY(QColor crosshairColor MEMBER m_crosshair NOTIFY styleChanged)
    Q_PROPERTY(qreal crosshairOpacity MEMBER m_crosshairOpacity NOTIFY styleChanged)
    Q_PROPERTY(qreal crosshairWidth MEMBER m_crosshairWidth NOTIFY styleChanged)
    Q_PROPERTY(QColor puckColor MEMBER m_puck NOTIFY styleChanged)
    Q_PROPERTY(QColor puckBorderColor MEMBER m_puckBorder NOTIFY styleChanged)
    Q_PROPERTY(qreal puckBorderWidth MEMBER m_puckBorderWidth NOTIFY styleChanged)
    Q_PROPERTY(qreal puckSize MEMBER m_puckSize NOTIFY styleChanged)
    Q_PROPERTY(QColor vectorColor MEMBER m_vector NOTIFY styleChanged)
    Q_PROPERTY(qreal vectorOpacity MEMBER m_vectorOpacity NOTIFY styleChanged)

public:
    explicit ColorWheelPad(QQuickItem *parent = nullptr);

    QPointF offset() const { return m_offset; }
    void setOffset(const QPointF &offset);
    bool pressed() const { return m_pressed; }

    void paint(QPainter *painter) override;
    bool contains(const QPointF &point) const override;

signals:
    void offsetChanged();
    void pressedChanged();
    void discStyleChanged();
    void styleChanged();
    void began();
    void offsetMoved(qreal x, qreal y);
    void ended();
    void resetRequested();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void touchUngrabEvent() override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    double radius() const { return std::min(width(), height()) / 2.0; }
    void setPressed(bool pressed);
    void finishDrag();
    const QImage &disc(double dpr);

    QPointF m_offset;
    QPointF m_live;
    bool m_pressed = false;
    bool m_begun = false;
    QPointF m_last;

    QImage m_disc;
    int m_discPx = 0;
    QColor m_discNeutral;
    qreal m_discRim = -1;

    QColor m_neutral;
    qreal m_rimStrength = 0.6;
    QColor m_ring;
    QColor m_ringActive;
    QColor m_focusRing;
    qreal m_ringWidth = 1;
    QColor m_crosshair;
    qreal m_crosshairOpacity = 0.2;
    qreal m_crosshairWidth = 1;
    QColor m_puck;
    QColor m_puckBorder;
    qreal m_puckBorderWidth = 2;
    qreal m_puckSize = 12;
    QColor m_vector;
    qreal m_vectorOpacity = 0.5;
};
