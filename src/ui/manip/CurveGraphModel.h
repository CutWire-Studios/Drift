#pragma once

#include "core/geom/BezierPath.h"
#include "ui/manip/DragSession.h"
#include "ui/manip/ManipModifiers.h"

#include <QList>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace drift::manip {

// One point of a graph. `t` holds the handle offsets (dx, dy in value units) for the kinds that have
// tangents; `smooth` is a Mirrored point (Alt breaks it for one drag), otherwise Free / Corner.
struct GraphPoint
{
    double x = 0.0;
    double y = 0.0;
    geom::GraphTangents t;
    bool smooth = false;
    bool lockX = false;
    bool lockY = false;
    bool removable = true;
    QString tag;

    bool operator==(const GraphPoint &o) const
    {
        return x == o.x && y == o.y && t.inDx == o.t.inDx && t.inDy == o.t.inDy && t.outDx == o.t.outDx
               && t.outDy == o.t.outDy && smooth == o.smooth && lockX == o.lockX && lockY == o.lockY
               && removable == o.removable && tag == o.tag;
    }
};

// The interaction core of every 2D curve editor: the plot rect, the value <-> pixel mapping with an
// optional zoom/pan, hit-testing of points and handles at a per-press screen reach, the clamping
// rules of each backend, marquee and nudge, snapping, and the drag session around one gesture.
// QML (CurveGraphView, CurvePlot) forwards pointer events in plot px and draws what the model lists.
//
// The backends are set up with setupCubic / setupPolyline / setupSpeed / setupColor / setupSamples,
// which fix the ranges and rules; everything else is shared. The model owns no project state:
// edits are reported through edited(), and a backend that edits the project hands it a
// DragSession so one gesture is one undo step and Esc reverts it whole.
class CurveGraphModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString backend READ backend NOTIFY setupChanged)
    Q_PROPERTY(QRectF plot READ plot WRITE setPlot NOTIFY layoutChanged)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QObject *session READ sessionObject WRITE setSessionObject NOTIFY sessionChanged)
    Q_PROPERTY(QString undoText MEMBER m_undoText)
    Q_PROPERTY(bool readOnly READ readOnly WRITE setReadOnly NOTIFY setupChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY changed)
    Q_PROPERTY(QList<QPointF> curvePath READ curvePath NOTIFY changed)
    Q_PROPERTY(QList<QPointF> fillPath READ fillPath NOTIFY changed)
    Q_PROPERTY(QVariantList knobs READ knobs NOTIFY changed)
    Q_PROPERTY(QVariantList arms READ arms NOTIFY changed)
    Q_PROPERTY(QVariantList gridLines READ gridLines NOTIFY changed)
    Q_PROPERTY(QRectF marquee READ marquee NOTIFY changed)
    Q_PROPERTY(bool marqueeActive READ marqueeActive NOTIFY changed)
    Q_PROPERTY(QString hudText READ hudText NOTIFY changed)
    Q_PROPERTY(QString entryUnit READ entryUnit NOTIFY setupChanged)
    Q_PROPERTY(QPointF pointer READ pointer NOTIFY changed)
    Q_PROPERTY(double snapGuideX READ snapGuideX NOTIFY changed)
    Q_PROPERTY(double snapGuideY READ snapGuideY NOTIFY changed)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapChanged)
    Q_PROPERTY(int hot READ hot NOTIFY changed)
    Q_PROPERTY(bool chipConstrain MEMBER m_chipConstrain)
    Q_PROPERTY(bool chipBreak MEMBER m_chipBreak)
    Q_PROPERTY(bool deleteZoneVisible READ deleteZoneVisible NOTIFY changed)
    Q_PROPERTY(bool deleteZoneActive READ deleteZoneActive NOTIFY changed)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY changed)
    Q_PROPERTY(int primary READ primary NOTIFY changed)
    Q_PROPERTY(bool selectedSmooth READ selectedSmooth NOTIFY changed)
    Q_PROPERTY(bool canDelete READ canDelete NOTIFY changed)
    Q_PROPERTY(bool hasTangents READ hasTangents NOTIFY setupChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    Q_PROPERTY(bool zoomX MEMBER m_zoomX NOTIFY setupChanged)
    Q_PROPERTY(bool zoomY MEMBER m_zoomY NOTIFY setupChanged)
    Q_PROPERTY(bool zoomed READ zoomed NOTIFY layoutChanged)
    Q_PROPERTY(QVariantList gridX MEMBER m_gridXList NOTIFY setupChanged)
    Q_PROPERTY(QVariantList gridY MEMBER m_gridYList NOTIFY setupChanged)
    Q_PROPERTY(QVariantList emphasisY MEMBER m_emphasisYList NOTIFY setupChanged)
    Q_PROPERTY(QString gridLabelSuffix MEMBER m_gridLabelSuffix NOTIFY setupChanged)
    Q_PROPERTY(QVariantList snapTargetsX MEMBER m_snapX NOTIFY setupChanged)
    Q_PROPERTY(QVariantList snapTargetsY MEMBER m_snapY NOTIFY setupChanged)
    Q_PROPERTY(double mouseReachPx MEMBER m_mouseReach)
    Q_PROPERTY(double touchReachPx MEMBER m_touchReach)

public:
    explicit CurveGraphModel(QObject *parent = nullptr);

    static constexpr double kMouseReachPx = 14.0;
    static constexpr double kTouchReachPx = 24.0;

    enum class Target { None, Point, HandleIn, HandleOut, Curve };
    struct Hit
    {
        Target target = Target::None;
        int index = -1;
        bool valid() const { return target != Target::None; }
    };

    QString backend() const;
    QRectF plot() const { return m_plot; }
    void setPlot(const QRectF &rect);
    int revision() const { return m_revision; }
    QObject *sessionObject() const { return m_session; }
    void setSessionObject(QObject *session);
    void setSession(DragSession *session);
    bool readOnly() const { return m_readOnly; }
    void setReadOnly(bool on);
    bool dragging() const { return m_open || m_gesture != Gesture::None; }
    QList<QPointF> curvePath() const;
    // The curve closed down to the plot's bottom edge, for a fill under it.
    QList<QPointF> fillPath() const;
    QVariantList knobs() const;
    QVariantList arms() const;
    QVariantList gridLines() const;
    QRectF marquee() const { return m_marqueeActive ? m_marquee : QRectF(); }
    bool marqueeActive() const { return m_marqueeActive; }
    QString hudText() const;
    QString entryUnit() const;
    QPointF pointer() const { return m_pointer; }
    double snapGuideX() const { return m_guideX; }
    double snapGuideY() const { return m_guideY; }
    bool snapEnabled() const { return m_snap; }
    void setSnapEnabled(bool on);
    bool deleteZoneVisible() const { return m_gesture == Gesture::Point && m_deleteOff && m_dragRemovable; }
    bool deleteZoneActive() const { return m_deleting; }
    int selectionCount() const { return int(m_selection.size()); }
    int primary() const { return m_primary; }
    int hot() const { return m_hover; }
    bool moved() const { return m_moved; }
    Q_INVOKABLE bool isSelected(int index) const { return m_selection.contains(index); }
    bool selectedSmooth() const;
    bool canDelete() const;
    bool hasTangents() const { return m_tangents; }
    int pointCount() const { return int(m_points.size()); }
    bool zoomed() const;

    // --- Backends ---------------------------------------------------------------------------
    // Cubic bezier with pinned ends (0,0) and (1,1) and two handles: easing and the fade bezier.
    // The y range is [-0.5, 1.5] unless `unitRange`.
    Q_INVOKABLE void setupCubic(bool unitRange);
    // Polyline through any number of knots, y in [0,1], ends pinned: the fade points.
    Q_INVOKABLE void setupPolyline();
    // Speed ramp: x = position 0..1, y = speed on a log axis, cubic segments with tangents.
    Q_INVOKABLE void setupSpeed(double minSpeed, double maxSpeed);
    // Colour curve: unit square, points only, dragging an inner point off the plot deletes it.
    // `hue` ties the end points' y. `eval` gives the drawn curve for adding a point on it.
    void setupColor(bool hue, std::function<double(double)> eval);
    // A read-only curve drawn from samples (function easings, hold).
    Q_INVOKABLE void setupSamples();

    Q_INVOKABLE void setCubic(const QVariantList &handles);
    Q_INVOKABLE QVariantList cubic() const;
    // Generic maps {x, y, inDx, inDy, outDx, outDy, smooth, tag}; the ends' locks are set by the backend.
    Q_INVOKABLE void setPoints(const QVariantList &points);
    Q_INVOKABLE QVariantList points() const;
    Q_INVOKABLE void setSamples(const QVariantList &samples);

    const QList<GraphPoint> &graphPoints() const { return m_points; }
    void setGraphPoints(const QList<GraphPoint> &points);

    // --- Mapping ------------------------------------------------------------------------------
    Q_INVOKABLE QPointF toPx(double x, double y) const;
    Q_INVOKABLE QPointF fromPx(const QPointF &px) const;
    // Zoom about a plot px by `factor` (>1 zooms in) and pan by a px delta; both are ignored for
    // an axis the backend does not zoom. resetView() shows the whole range again.
    Q_INVOKABLE void zoomAt(double px, double py, double factor);
    Q_INVOKABLE void panBy(double dxPx, double dyPx);
    Q_INVOKABLE void resetView();

    // --- Pointer ------------------------------------------------------------------------------
    // `modifiers` is the event's Qt::KeyboardModifiers and `device` a DragSession.Device. press
    // returns what it landed on: "point", "handle", "curve" (a point will be added), "marquee" or "none".
    Q_INVOKABLE QString press(const QPointF &px, int modifiers, int device);
    Q_INVOKABLE void move(const QPointF &px, int modifiers);
    // "drag", "add", "select", "delete", "click" or "" for nothing.
    Q_INVOKABLE QString release(const QPointF &px, int modifiers);
    // Drops a press that changed nothing, as when a long press opens the point bar.
    Q_INVOKABLE void cancelPress();
    // Hover: the index under the pointer for the highlight, -1 for none.
    Q_INVOKABLE int hover(const QPointF &px, int device);
    Q_INVOKABLE void clearHover();
    // Double-click removes the point under the pointer.
    Q_INVOKABLE bool doubleClick(const QPointF &px, int device);
    // The point index at a press, for a long-press menu; -1 when none.
    Q_INVOKABLE int pointAt(const QPointF &px, int device) const;
    Hit hitTest(const QPointF &px, double reach) const;

    // --- Edits (each one undo step) -----------------------------------------------------------
    Q_INVOKABLE void selectOnly(int index);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE bool setSelectedSmooth(bool smooth);
    Q_INVOKABLE void resetSelected();
    Q_INVOKABLE void resetCurve();
    Q_INVOKABLE int addPointAtX(double x);
    // Arrow keys: a step in the direction (-1, 0, 1 each; y up is positive). Colour curves move by
    // 1/255 (Shift 10/255), everything else by 1 px (Shift 10).
    Q_INVOKABLE void nudge(int dirX, int dirY, bool shift);
    // Typed entry from the HUD: the selected point's (or handle's) y in the backend's entry units.
    Q_INVOKABLE void applyTyped(double value);
    // Esc: reverts the gesture in flight through the session and the local copy.
    Q_INVOKABLE void cancel();

signals:
    void setupChanged();
    void layoutChanged();
    void sessionChanged();
    void snapChanged();
    void changed();
    // The points changed, live during a drag and for each discrete edit.
    void edited();
    void gestureBegan();
    void gestureEnded();
    void gestureCancelled();

private:
    enum class Kind { Samples, Cubic, Polyline, Speed, Color };
    enum class Gesture { None, Point, Handle, Marquee };

    double yToU(double y) const;
    double uToY(double u) const;
    double xSpan() const { return m_vx1 - m_vx0; }
    double uSpan() const { return m_vu1 - m_vu0; }
    QPointF handlePos(int index, bool out) const;
    bool handleActive(int index, bool out) const;
    bool handlesShown(int index) const;
    double spanIn(int index) const;
    double spanOut(int index) const;
    double gap() const { return m_gap; }
    double evalY(double x) const;
    void sortKeep();
    void touch();
    void bump();
    void ensureOpen(const QString &undoText = {});
    void closeGesture(bool commit);
    void restoreBefore();
    void discrete(const QString &undoText, const std::function<void()> &fn);
    void applyLocks();
    void onSessionCancelled();
    int insertPoint(double x);
    void applyPointDrag(const QPointF &px, const ManipModifiers &mods);
    void applyHandleDrag(const QPointF &px, const ManipModifiers &mods);
    void setHandleAbs(int index, bool out, double x, double y, bool breakTangent, bool horizontalOnly);
    void snapValue(double &x, double &y, bool lockX, bool lockY, bool noSnap);
    bool handleHittable(int index, bool out) const;
    void moveSelection(double dx, double du);
    void tieEnds(int index);
    void removeIndices(QList<int> indices);
    double reachFor(int device) const;
    void rebuildCurve() const;
    bool outsideZone(const QPointF &px) const;
    void markEdited();
    QVariantMap knobAt(const QString &kind, int index, bool out, const QPointF &px, bool selected, bool smooth,
                       bool fixed) const;

    ManipModifiers modsFrom(int qtFlags) const;

    Kind m_kind = Kind::Samples;
    QList<GraphPoint> m_points;
    QList<QPointF> m_samples;
    std::function<double(double)> m_eval;

    double m_xMin = 0.0;
    double m_xMax = 1.0;
    double m_yMin = 0.0;
    double m_yMax = 1.0;
    bool m_yLog = false;
    double m_gap = 0.002;
    int m_maxPoints = 64;
    bool m_tangents = false;
    bool m_canAdd = false;
    bool m_addAnywhere = false;
    bool m_marqueeOk = false;
    bool m_deleteOff = false;
    bool m_hue = false;
    bool m_readOnly = false;
    bool m_zoomX = false;
    bool m_zoomY = false;
    bool m_snap = true;
    bool m_chipConstrain = false;
    bool m_chipBreak = false;
    double m_mouseReach = kMouseReachPx;
    double m_touchReach = kTouchReachPx;
    QString m_entryUnit;
    QString m_gridLabelSuffix;
    QVariantList m_gridXList;
    QVariantList m_gridYList;
    QVariantList m_emphasisYList;
    QVariantList m_snapX;
    QVariantList m_snapY;

    QRectF m_plot{0, 0, 100, 100};
    double m_vx0 = 0.0;
    double m_vx1 = 1.0;
    double m_vu0 = 0.0;
    double m_vu1 = 1.0;

    DragSession *m_session = nullptr;
    QString m_undoText;
    int m_revision = 0;
    mutable QList<QPointF> m_path;
    mutable bool m_pathDirty = true;

    QList<int> m_selection;
    int m_primary = -1;
    bool m_primaryOut = false;

    Gesture m_gesture = Gesture::None;
    bool m_open = false;
    bool m_moved = false;
    bool m_pendingAdd = false;
    double m_pendingAddX = 0.0;
    int m_device = 0;
    QPointF m_pressPx;
    QPointF m_grabOffset;
    QPointF m_pointer;
    QList<GraphPoint> m_before;
    QList<GraphPoint> m_orig;
    Hit m_hit;
    bool m_shiftOnPress = false;
    bool m_wasSelected = false;
    bool m_dragRemovable = false;
    bool m_deleting = false;
    bool m_added = false;
    QRectF m_marquee;
    bool m_marqueeActive = false;
    bool m_marqueeAdd = false;
    QList<int> m_marqueeBase;
    double m_guideX = -1.0;
    double m_guideY = -1.0;
    int m_hover = -1;
};

} // namespace drift::manip
