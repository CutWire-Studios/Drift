#pragma once

#include "ui/color/CurveEdit.h"

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QStringList>
#include <QVariantMap>

// The curve editor's plot: grid, every channel's curve and the selected channel's handles, drawn from
// the same evaluator the grade bakes (so a hue curve wraps at red and per-point eases show). It owns
// point editing: click adds, drag moves, double-click or dragging off removes, arrows nudge. `curves`
// maps channel to the stored JSON; edits are written back through changed(), one undo step between
// began() and ended().
class CurvePlot : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap curves READ curves WRITE setCurves NOTIFY curvesChanged)
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)
    Q_PROPERTY(bool hueMode READ hueMode WRITE setHueMode NOTIFY hueModeChanged)
    Q_PROPERTY(QVariantMap channelColors MEMBER m_channelColors NOTIFY styleChanged)
    Q_PROPERTY(QColor gridColor MEMBER m_gridColor NOTIFY styleChanged)
    Q_PROPERTY(qreal gridOpacity MEMBER m_gridOpacity NOTIFY styleChanged)
    Q_PROPERTY(qreal gridLineWidth MEMBER m_gridLineWidth NOTIFY styleChanged)
    Q_PROPERTY(qreal curveLineWidth MEMBER m_curveLineWidth NOTIFY styleChanged)
    Q_PROPERTY(qreal dimLineWidth MEMBER m_dimLineWidth NOTIFY styleChanged)
    Q_PROPERTY(qreal handleRadius MEMBER m_handleRadius NOTIFY styleChanged)
    Q_PROPERTY(QColor pointBackground MEMBER m_pointBackground NOTIFY styleChanged)
    Q_PROPERTY(qreal knobBorderWidth MEMBER m_knobBorderWidth NOTIFY styleChanged)
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    Q_PROPERTY(QPointF selectedPoint READ selectedPoint NOTIFY selectedChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY selectedChanged)

public:
    explicit CurvePlot(QQuickItem *parent = nullptr);

    QVariantMap curves() const { return m_curves; }
    void setCurves(const QVariantMap &curves);
    QString channel() const { return m_channel; }
    void setChannel(const QString &channel);
    bool hueMode() const { return m_hue; }
    void setHueMode(bool hue);
    int selected() const { return m_selected; }
    QPointF selectedPoint() const;
    int pointCount() const { return int(pointsOf(m_channel).size()); }

    Q_INVOKABLE void resetChannel();

    void paint(QPainter *painter) override;

signals:
    void curvesChanged();
    void channelChanged();
    void hueModeChanged();
    void styleChanged();
    void selectedChanged();
    void began();
    void changed(const QString &channel, const QString &json);
    void ended();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void keyPressEvent(QKeyEvent *event) override;
    void touchUngrabEvent() override;

private:
    struct Parsed
    {
        QString json;
        drift::curveedit::Points points;
    };

    QStringList channels() const;
    const drift::curveedit::Points &pointsOf(const QString &channel) const;
    double reach() const;
    bool recentlyAdded(int idx) const;
    void openGesture();
    void closeGesture();
    void emitPoints(const drift::curveedit::Points &pts);
    void commitPoints(const drift::curveedit::Points &pts);
    void setSelected(int idx);
    void beginPress(const QPointF &pos);
    void endDrag();

    QVariantMap m_curves;
    QString m_channel = QStringLiteral("master");
    bool m_hue = false;
    QHash<QString, Parsed> m_parsed;
    drift::curveedit::Points m_live;
    bool m_hasLive = false;
    int m_selected = -1;
    bool m_dragging = false;
    int m_dragIndex = -1;
    bool m_gestureBegun = false;
    QElapsedTimer m_clickAddedAt;
    int m_clickAddedIndex = -1;

    QVariantMap m_channelColors;
    QColor m_gridColor;
    qreal m_gridOpacity = 1;
    qreal m_gridLineWidth = 1;
    qreal m_curveLineWidth = 2;
    qreal m_dimLineWidth = 1;
    qreal m_handleRadius = 5;
    QColor m_pointBackground;
    qreal m_knobBorderWidth = 1;
};
