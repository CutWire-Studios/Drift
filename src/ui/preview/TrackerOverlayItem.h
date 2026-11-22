#pragma once

#include "app/jobs/vision/TrackingController.h"

#include <QColor>
#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QSet>
#include <QVariantMap>

// Draws a tracker over the clip frame it fills: each point's pattern and search boxes and crosshair,
// ghost markers off the tracked span, the onion-skin path of dots over +-N frames (user keys in their
// own colour), and a planar tracker's outline. The grips themselves are QML HandleKnobs placed from
// pointInfo / cornerAt. It reads the frame straight from TrackingController and owns the drag:
// beginDrag(kind, ...) / dragTo / endDrag move a marker, a corner or a box's size, with the dragged
// values held here so the frame can keep refreshing underneath; cancelDrag drops it unwritten. The
// clip is addressed by id and resolved on every call.
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
    Q_PROPERTY(QColor patternColor MEMBER m_pattern NOTIFY colorsChanged)
    Q_PROPERTY(QColor trailColor MEMBER m_trail NOTIFY colorsChanged)
    Q_PROPERTY(QColor goodColor MEMBER m_good NOTIFY colorsChanged)
    Q_PROPERTY(QColor warnColor MEMBER m_warn NOTIFY colorsChanged)
    Q_PROPERTY(QColor badColor MEMBER m_bad NOTIFY colorsChanged)
    Q_PROPERTY(QColor userKeyColor MEMBER m_userKey NOTIFY colorsChanged)
    Q_PROPERTY(QColor regionColor MEMBER m_region NOTIFY colorsChanged)
    Q_PROPERTY(QColor searchColor MEMBER m_search NOTIFY colorsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY layoutChanged)
    Q_PROPERTY(QString dragKind READ dragKind NOTIFY layoutChanged)
    Q_PROPERTY(int dragIndex READ dragIndex NOTIFY layoutChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
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
    int revision() const { return m_revision; }
    QString dragKind() const;
    int dragIndex() const { return m_dragIndex; }
    int selectionCount() const { return m_selected.size(); }
    int pointCount() const { return m_frame.points.size(); }
    int cornerCount() const { return m_frame.region.size(); }

    // Reads the tracker's frame at `seconds` again. Does nothing while a drag runs.
    Q_INVOKABLE void refresh();
    // {kind: "point"|"", index} for a position in item pixels; a point's hit area is at least
    // minSide square, which the caller sets from the press device.
    Q_INVOKABLE QVariantMap hitTest(double x, double y, double minSide = 24) const;
    // {x, y, pw, ph, sw, sh (item px; the dragged values while one runs), nx, ny, boxW (normalized),
    // confidence, tint: "good"|"warn"|"bad", has, ghost, userKey, shown, selected}. Empty when there is no such point.
    Q_INVOKABLE QVariantMap pointInfo(int index) const;
    Q_INVOKABLE QPointF cornerAt(int index) const;
    // Starts a drag of kind "point" (the selection moves together), "corner", "pattern" or "search"
    // (the box resizes about its centre). `extend` toggles the point in the selection instead of
    // replacing it. False when nothing can be dragged.
    Q_INVOKABLE bool beginDrag(const QString &kind, int index, double x, double y, bool extend = false);
    Q_INVOKABLE void dragTo(double x, double y, bool constrain = false);
    // The kind when the drag moved something, else "". Marker moves are preview edits the caller
    // commits; corner and size changes are written here, each as its own undo step.
    Q_INVOKABLE QString endDrag();
    // Drops a drag without writing; the caller reverts the project.
    Q_INVOKABLE void cancelDrag();
    // Moves the selected points (all of them when none is selected) by a pixel delta, as preview edits.
    Q_INVOKABLE void nudge(double dx, double dy);
    Q_INVOKABLE void selectInRect(double x0, double y0, double x1, double y1, bool extend);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(int index) const;

signals:
    void trackingChanged();
    void clipIdChanged();
    void trackerIdChanged();
    void secondsChanged();
    void minConfidenceChanged();
    void showTrailsChanged();
    void lineWidthChanged();
    void layoutChanged();
    void selectionChanged();
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
    QColor m_pattern, m_trail, m_good, m_warn, m_bad, m_userKey, m_region, m_search;
    TrackingController::FrameView m_frame;

    QSizeF boxPx(const TrackingController::FramePoint &p, bool search) const;
    void bump();
    QVector<int> targets() const;

    enum class Drag { None, Point, Corner, Pattern, Search };
    Drag m_drag = Drag::None;
    int m_dragIndex = -1;
    QPointF m_dragPress;
    QHash<int, QPointF> m_dragStart;
    QHash<int, QPointF> m_dragNorms;
    double m_dragWidth = 0;
    bool m_dragMoved = false;
    QVector<QPointF> m_dragCorners;
    QSet<int> m_selected;
    int m_revision = 0;
};
