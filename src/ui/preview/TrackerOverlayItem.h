#pragma once

#include "app/jobs/vision/TrackingController.h"

#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QVariantMap>

// Draws a tracker over the clip frame it fills: each point's pattern box and crosshair, ghost markers
// off the tracked span, the trails, and a planar tracker's outline and corner handles. It reads the
// frame straight from TrackingController and owns the drag: hitTest picks a marker, beginDrag /
// dragTo / endDrag move it, with the dragged marker's position held here so the frame can keep
// refreshing underneath. The clip is addressed by id and resolved on every call.
class TrackerOverlayItem : public QQuickItem
{
    Q_OBJECT

    Q_PROPERTY(QObject *tracking READ tracking WRITE setTracking NOTIFY trackingChanged)
    Q_PROPERTY(QString clipId READ clipId WRITE setClipId NOTIFY clipIdChanged)
    Q_PROPERTY(QString trackerId READ trackerId WRITE setTrackerId NOTIFY trackerIdChanged)
    Q_PROPERTY(double seconds READ seconds WRITE setSeconds NOTIFY secondsChanged)
    Q_PROPERTY(double minConfidence READ minConfidence WRITE setMinConfidence NOTIFY minConfidenceChanged)
    Q_PROPERTY(bool showTrails READ showTrails WRITE setShowTrails NOTIFY showTrailsChanged)
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY lineWidthChanged)
    Q_PROPERTY(double handleSize READ handleSize WRITE setHandleSize NOTIFY handleSizeChanged)
    Q_PROPERTY(double handleMargin READ handleMargin WRITE setHandleMargin NOTIFY handleMarginChanged)
    Q_PROPERTY(QColor patternColor MEMBER m_pattern NOTIFY colorsChanged)
    Q_PROPERTY(QColor trailColor MEMBER m_trail NOTIFY colorsChanged)
    Q_PROPERTY(QColor goodColor MEMBER m_good NOTIFY colorsChanged)
    Q_PROPERTY(QColor warnColor MEMBER m_warn NOTIFY colorsChanged)
    Q_PROPERTY(QColor badColor MEMBER m_bad NOTIFY colorsChanged)
    Q_PROPERTY(QColor userKeyColor MEMBER m_userKey NOTIFY colorsChanged)
    Q_PROPERTY(QColor regionColor MEMBER m_region NOTIFY colorsChanged)
    Q_PROPERTY(QColor handleColor MEMBER m_handle NOTIFY colorsChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY frameChanged)
    Q_PROPERTY(int cornerCount READ cornerCount NOTIFY frameChanged)

public:
    explicit TrackerOverlayItem(QQuickItem *parent = nullptr);

    QObject *tracking() const;
    void setTracking(QObject *tracking);
    QString clipId() const { return m_clipId; }
    void setClipId(const QString &id);
    QString trackerId() const { return m_trackerId; }
    void setTrackerId(const QString &id);
    double seconds() const { return m_seconds; }
    void setSeconds(double seconds);
    double minConfidence() const { return m_minConfidence; }
    void setMinConfidence(double value);
    bool showTrails() const { return m_showTrails; }
    void setShowTrails(bool show);
    double lineWidth() const { return m_lineWidth; }
    void setLineWidth(double width);
    double handleSize() const { return m_handleSize; }
    void setHandleSize(double size);
    double handleMargin() const { return m_handleMargin; }
    void setHandleMargin(double margin);
    int pointCount() const { return m_frame.points.size(); }
    int cornerCount() const { return m_frame.region.size(); }

    // Reads the tracker's frame at `seconds` again. Does nothing while a drag runs.
    Q_INVOKABLE void refresh();
    // {kind: "point"|"corner"|"", index} for a position in item pixels; corners sit above points.
    Q_INVOKABLE QVariantMap hitTest(double x, double y) const;
    // Picks the marker under (x, y) and starts dragging it; false when there is none.
    Q_INVOKABLE bool beginDrag(double x, double y);
    Q_INVOKABLE void dragTo(double x, double y);
    // "point" or "corner" when the drag moved something (a corner drag is written here; a point drag
    // is a preview edit the caller commits), else "".
    Q_INVOKABLE QString endDrag();

signals:
    void trackingChanged();
    void clipIdChanged();
    void trackerIdChanged();
    void secondsChanged();
    void minConfidenceChanged();
    void showTrailsChanged();
    void lineWidthChanged();
    void handleSizeChanged();
    void handleMarginChanged();
    void colorsChanged();
    void frameChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QPointF pointPos(int i) const;
    QPointF cornerPos(int i) const;

    QPointer<TrackingController> m_tracking;
    QString m_clipId;
    QString m_trackerId;
    double m_seconds = 0;
    double m_minConfidence = 0.5;
    bool m_showTrails = true;
    double m_lineWidth = 1;
    double m_handleSize = 16;
    double m_handleMargin = 8;
    QColor m_pattern, m_trail, m_good, m_warn, m_bad, m_userKey, m_region, m_handle;
    TrackingController::FrameView m_frame;

    enum class Drag { None, Point, Corner };
    Drag m_drag = Drag::None;
    int m_dragIndex = -1;
    QPointF m_dragGrab;
    QPointF m_dragNorm;
    bool m_dragMoved = false;
    QVector<QPointF> m_dragCorners;
};
