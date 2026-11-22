#pragma once

#include "ui/color/CurveEdit.h"
#include "ui/manip/CurveGraphModel.h"

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

// The curve editor's plot: grid, every channel's curve and the selected channel's handles, drawn from
// the same evaluator the grade bakes (so a hue curve wraps at red and per-point eases show). The
// interaction is CurveGraphModel's: click adds, drag moves, a drag off the plot deletes on release
// (the plot shows the delete zone first), double-click or Delete removes, arrows nudge 1/255 (Shift
// 10/255), Esc cancels. `curves` maps channel to the stored JSON; edits are written back through
// changed(), one undo step between began() and ended() and cancelled() when Esc reverts them.
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
    Q_PROPERTY(drift::manip::CurveGraphModel *graph READ graph CONSTANT)
    Q_PROPERTY(bool dragging READ dragging NOTIFY selectedChanged)
    Q_PROPERTY(QString hudText READ hudText NOTIFY selectedChanged)
    Q_PROPERTY(QPointF pointer READ pointer NOTIFY selectedChanged)
    Q_PROPERTY(bool deleteZoneVisible READ deleteZoneVisible NOTIFY selectedChanged)
    Q_PROPERTY(bool deleteZoneActive READ deleteZoneActive NOTIFY selectedChanged)
    Q_PROPERTY(QColor deleteColor MEMBER m_deleteColor NOTIFY styleChanged)
    Q_PROPERTY(qreal deleteOpacity MEMBER m_deleteOpacity NOTIFY styleChanged)

public:
    explicit CurvePlot(QQuickItem *parent = nullptr);

    QVariantMap curves() const { return m_curves; }
    void setCurves(const QVariantMap &curves);
    QString channel() const { return m_channel; }
    void setChannel(const QString &channel);
    bool hueMode() const { return m_hue; }
    void setHueMode(bool hue);
    int selected() const;
    QPointF selectedPoint() const;
    int pointCount() const { return m_model.pointCount(); }
    drift::manip::CurveGraphModel *graph() { return &m_model; }
    bool dragging() const { return m_model.dragging(); }
    QString hudText() const { return m_model.hudText(); }
    QPointF pointer() const { return m_model.pointer(); }
    bool deleteZoneVisible() const { return m_model.deleteZoneVisible(); }
    bool deleteZoneActive() const { return m_model.deleteZoneActive(); }

    Q_INVOKABLE void resetChannel();
    // Typed entry from the HUD: the selected point's output, in 0-255 (hue curves: -1..1).
    Q_INVOKABLE void applyTyped(double value);

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
    void cancelled();
    // Hold on a point with a finger, or right-click it: the host opens the point bar.
    void contextRequested(int index, const QPointF &pos);
    // A digit was typed with a point selected: the host opens the HUD's entry with it.
    void entryRequested(const QString &seed);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void keyPressEvent(QKeyEvent *event) override;
    void touchUngrabEvent() override;

    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;

private:
    struct Parsed
    {
        QString json;
        drift::curveedit::Points points;
    };

    QStringList channels() const;
    drift::curveedit::Points pointsOf(const QString &channel) const;
    void loadModel();
    void configureModel();
    void emitEdited();
    bool recentlyAdded(int idx) const;
    static int deviceOf(const QMouseEvent *event);

    QVariantMap m_curves;
    QString m_channel = QStringLiteral("master");
    bool m_hue = false;
    QHash<QString, Parsed> m_parsed;
    drift::manip::CurveGraphModel m_model;
    QElapsedTimer m_clickAddedAt;
    int m_clickAddedIndex = -1;
    QTimer m_holdTimer;
    QPointF m_pressPos;
    QPointF m_lastPos;
    int m_pressDevice = 0;

    QVariantMap m_channelColors;
    QColor m_gridColor;
    qreal m_gridOpacity = 1;
    qreal m_gridLineWidth = 1;
    qreal m_curveLineWidth = 2;
    qreal m_dimLineWidth = 1;
    qreal m_handleRadius = 5;
    QColor m_pointBackground;
    qreal m_knobBorderWidth = 1;
    QColor m_deleteColor;
    qreal m_deleteOpacity = 0.2;
};
