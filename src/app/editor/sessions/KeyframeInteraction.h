#pragma once

#include "app/editor/sessions/KeyframeCurves.h"
#include "app/editor/sessions/KeyframeRows.h"
#include "app/editor/sessions/KeyframeSelection.h"
#include "ui/manip/DragSession.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class KeyframeCurvesModel;
class KeyframeRowsModel;
class KeyframeTimeView;

// The one pointer and keyboard state machine behind every keyframe surface: the Keyframes panel's
// lanes (time only, one lane per row) and curves, and the clip properties graph. A surface forwards
// raw events in its own pixels, with the event's modifiers and device, and draws what this reports.
//
// Mouse and touch share one contract. Hit reach is per press (14 px mouse, 24 px touch); Shift
// constrains (axis lock on a key drag, a flat handle), Alt breaks a tangent, Ctrl/Cmd suspends
// snapping. A drag, a handle or segment edit, a stretch and a nudge burst are each one undo step
// through the DragSession, and Esc reverts them whole. A finger pans until a long press picks a key,
// a handle or the marquee up; a tap on a selected key asks for the context menu.
//
// The geometry comes from a Scene the host fills, and project edits go through Ops, so the machine
// itself needs no project and is tested on plain data.
class KeyframeInteraction : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString mode MEMBER m_modeName NOTIFY setupChanged)
    Q_PROPERTY(KeyframeSelection *selection READ selection WRITE setSelection NOTIFY setupChanged)
    Q_PROPERTY(int track MEMBER m_track NOTIFY setupChanged)
    Q_PROPERTY(int clip MEMBER m_clip NOTIFY setupChanged)
    Q_PROPERTY(double rowHeight MEMBER m_rowHeight NOTIFY setupChanged)
    Q_PROPERTY(double contentY MEMBER m_contentY NOTIFY setupChanged)
    Q_PROPERTY(double viewWidth MEMBER m_viewWidth NOTIFY setupChanged)
    Q_PROPERTY(double viewHeight MEMBER m_viewHeight NOTIFY setupChanged)
    Q_PROPERTY(double playhead MEMBER m_playhead NOTIFY setupChanged)
    Q_PROPERTY(double fps MEMBER m_fps NOTIFY setupChanged)
    // Off, a curve cannot be bent or its handles grabbed (the properties graph while it is only an overview).
    Q_PROPERTY(bool curveEditing MEMBER m_curveEditing NOTIFY setupChanged)
    // "playhead" moves the playhead to a key that is double-clicked; "remove" deletes it.
    Q_PROPERTY(QString doubleClickKey MEMBER m_doubleClickKey NOTIFY setupChanged)
    // The clip properties graph also snaps to what the timeline snaps to.
    Q_PROPERTY(bool timelineSnap MEMBER m_timelineSnap NOTIFY setupChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool dragging READ dragging NOTIFY busyChanged)
    Q_PROPERTY(bool marqueeActive READ marqueeActive NOTIFY changed)
    Q_PROPERTY(QRectF marquee READ marquee NOTIFY changed)
    Q_PROPERTY(QString readout READ readout NOTIFY changed)
    Q_PROPERTY(QPointF pointer READ pointer NOTIFY changed)
    Q_PROPERTY(QString hoverId READ hoverId NOTIFY changed)
    Q_PROPERTY(int hoverRow READ hoverRow NOTIFY changed)
    Q_PROPERTY(double hoverSeconds READ hoverSeconds NOTIFY changed)
    Q_PROPERTY(QString hoverTip READ hoverTip NOTIFY changed)
    Q_PROPERTY(int cursorShape READ cursorShape NOTIFY changed)
    Q_PROPERTY(bool touch READ touch NOTIFY changed)
    // Which value a typed number sets while a key is dragged: "time" (seconds) or "value".
    Q_PROPERTY(QString entryTarget READ entryTarget WRITE setEntryTarget NOTIFY entryTargetChanged)
    Q_PROPERTY(QString entryUnit READ entryUnit NOTIFY entryTargetChanged)

public:
    static constexpr double kMouseReachPx = 14.0;
    static constexpr double kTouchReachPx = 24.0;
    static constexpr double kMovePx = 3.0;
    static constexpr double kEdgeZonePx = 24.0;

    struct Scene
    {
        QList<KeyframeRows::Row> rows;
        QList<KeyframeCurves::Series> series;
        KeyframeRows::Axis axis;
        double clipStart = 0;
        double clipDuration = 1;
        double graphMargin = 0;
        double graphHeight = 1;
        int focusedIndex = -1;
        bool showHandles = false;
    };

    struct Ops
    {
        std::function<void(Scene &)> scene;
        std::function<QVariantList(const QVariantList &refs, double dtSeconds)> moveKeys;
        std::function<QVariantList(const QVariantList &refs, double pivot, double factor)> stretchKeys;
        std::function<void(const QString &prop, double seconds, double inDx, double inDy, double outDx,
                           double outDy, bool corner)>
            setTangents;
        std::function<void(const QVariantList &refs)> removeKeys;
        std::function<void(const QStringList &props, double seconds)> addKeys;
        // `seconds` snapped to frames, the playhead and the other keys; `noSnap` suspends the latter two.
        std::function<double(double seconds, const QVariantList &exclude, bool noSnap)> snapTime;
        std::function<void()> clearCollisions;
        // Holds every curve's axis where it is for the length of a drag.
        std::function<void(bool lock)> lockRanges;
    };

    explicit KeyframeInteraction(QObject *parent = nullptr);

    KeyframeSelection *selection() const { return m_selection; }
    void setSelection(KeyframeSelection *selection);
    void setOps(Ops ops) { m_ops = std::move(ops); }
    void setDragSession(drift::manip::DragSession *session);
    void setCurves(bool on) { m_modeName = on ? QStringLiteral("curves") : QStringLiteral("lanes"); }

    bool busy() const;
    bool dragging() const { return m_keyDrag || m_shaping || m_stretching; }
    bool marqueeActive() const { return m_marqueeActive; }
    QRectF marquee() const { return m_marqueeActive ? m_marquee : QRectF(); }
    QString readout() const { return m_readout; }
    QPointF pointer() const { return m_last; }
    QString hoverId() const { return m_hoverId; }
    int hoverRow() const { return m_hoverRow; }
    double hoverSeconds() const { return m_hoverSeconds; }
    QString hoverTip() const { return m_hoverTip; }
    int cursorShape() const;
    bool touch() const { return m_touch; }
    QString entryTarget() const { return m_entryTarget; }
    void setEntryTarget(const QString &target);
    QString entryUnit() const;

    // button: Qt::MouseButton; modifiers: Qt::KeyboardModifiers; device: DragSession.Device.
    // Returns false when the press should not be tracked.
    Q_INVOKABLE bool press(double x, double y, int button, int modifiers, int device);
    Q_INVOKABLE void move(double x, double y, int modifiers);
    Q_INVOKABLE void release(double x, double y, int button, int modifiers);
    // A finger held still: picks the key, handle, segment or marquee under it up. False when the
    // press was not a finger's.
    Q_INVOKABLE bool longPress();
    // The press was taken away (a pinch started, say): nothing is committed.
    Q_INVOKABLE void cancelPress();
    Q_INVOKABLE void hover(double x, double y, int device);
    Q_INVOKABLE void clearHover();
    Q_INVOKABLE void doubleClick(double x, double y, int device);
    // Called on a timer while a drag or a marquee is held near an edge of the view.
    Q_INVOKABLE void autoScroll();

    Q_INVOKABLE void beginStretch(bool fromRight, int modifiers, int device);
    Q_INVOKABLE void updateStretch(double x, int modifiers);
    Q_INVOKABLE void endStretch();

    // Arrow keys: `dirX` is a step in frames (+ later), `dirY` a step of 1 px of the curve's axis (+ up,
    // curves only); Shift makes either step ten. A burst within the nudge window is one undo step.
    Q_INVOKABLE bool nudge(int dirX, int dirY, bool shift);
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE QVariantList allRefs() const;
    // Esc: reverts a gesture in flight, else clears the selection. False when there was nothing to do.
    Q_INVOKABLE bool escape();
    Q_INVOKABLE void cancel();
    // A number typed into the readout while a key is dragged; it sets the entry target and finishes the drag.
    Q_INVOKABLE void applyTyped(double value);
    Q_INVOKABLE bool entryAvailable() const { return m_keyDrag; }

    // Where the surface's host wires models and the project in; defined in KeyframeInteractionBinding.cpp.
    Q_INVOKABLE void attach(QObject *rowsModel, QObject *curvesModel, QObject *timeView, QObject *controller);

signals:
    void setupChanged();
    void changed();
    void busyChanged();
    void entryTargetChanged();
    // Content offsets: the view should show `dx`/`dy` px further on (autoscroll).
    void scrollRequested(double dx, double dy);
    // A finger dragged the view by this many px; the content follows it.
    void panRequested(double dx, double dy);
    // The surface should focus this curve: `track` is the key's own track, `rowProp` its row.
    void focusRequested(const QString &track, const QString &rowProp);
    void contextRequested(double x, double y, const QVariantMap &hit);
    void playheadRequested(double seconds);
    void keysAdded(const QStringList &props);
    // Where a drag was cancelled through the session (Esc, or anything else that cancels it).
    void cancelled();

private:
    enum class Mode { Idle, Pan, Touch, Key, Handle, Segment, Marquee };

    struct Hit
    {
        QString kind; // "dot", "handle", "segment" or ""
        QVariantMap map;
        QVariantList refs;
        double seconds = 0;
        double value = 0;
        int row = -1;
        int entryIndex = -1;
        QString track;
        QString rowProp;
        // handle / segment
        bool outgoing = false;
        KeyframeCurves::Point point;
        KeyframeCurves::Point other;
        double dx = 0;
        double handleValue = 0;
        double spanIn = 0;
        double spanOut = 0;
        double t = 0;
    };

    struct PropRange
    {
        double min = 0;
        double max = 1;
        bool fixed = false;
    };

    bool curves() const { return m_modeName == QLatin1String("curves"); }
    double reach() const { return m_touch ? kTouchReachPx : kMouseReachPx; }
    void refresh();
    Hit dotAt(double x, double y, double reach) const;
    Hit handleAt(double x, double y, double reach) const;
    Hit segmentAt(double x, double y, double reach) const;
    Hit grabAt(double x, double y) const;
    bool isFocusedSeries(int index) const;
    double screenY(double value, const KeyframeCurves::Series &series) const;
    double xFor(double seconds) const { return m_scene.axis.xFor(seconds); }
    double valuePerPx(const KeyframeCurves::Series &series) const;
    QString tipFor(const Hit &hit) const;
    QString formatTimecode(double seconds) const;
    QVariantMap hitMap(const Hit &hit) const;

    void beginKeyDrag();
    void updateKeyDrag(double x, double y, int modifiers);
    void finishKeyDrag();
    void beginShape();
    void updateShape(double x, double y, int modifiers);
    void finishShape();
    void beginMarquee(int modifiers);
    void updateMarquee(double x, double y);
    void pressKey(int modifiers);
    void applyKeyDrag();
    void captureValues();
    void openContext(double x, double y, int modifiers);
    // `refs` with the value change each gets: `upPx` px of its own axis upward, or `absolute` for all.
    QVariantList valueRefs(const QVariantList &refs, double upPx, bool overridden, double absolute) const;
    double snapDelta(double dxPx, bool noSnap, bool zeroWhenStill) const;
    void setReadout(const QString &text);
    void onSessionCancelled();
    void onSessionCommitted();
    void setBusyDirty();
    void notifyChanged();
    static bool sameRefs(const QVariantList &a, const QVariantList &b);

    QString m_modeName = QStringLiteral("lanes");
    QPointer<KeyframeSelection> m_selection;
    QPointer<drift::manip::DragSession> m_session;
    Ops m_ops;
    Scene m_scene;
    int m_track = -1;
    int m_clip = -1;
    double m_rowHeight = 32;
    double m_contentY = 0;
    double m_viewWidth = 0;
    double m_viewHeight = 0;
    double m_playhead = 0;
    double m_fps = 30;
    bool m_curveEditing = true;
    bool m_timelineSnap = false;
    QString m_doubleClickKey = QStringLiteral("playhead");

    Mode m_mode = Mode::Idle;
    bool m_pressed = false;
    bool m_touch = false;
    int m_deviceInt = 0;
    bool m_armed = false;
    bool m_collapse = false;
    bool m_moved = false;
    QPointF m_press;
    QPointF m_last;
    QPointF m_panLast;
    int m_lastMods = 0;
    double m_pressContentX = 0;
    double m_pressContentY = 0;
    Hit m_pressHit;

    bool m_keyDrag = false;
    bool m_shaping = false;
    bool m_stretching = false;
    bool m_nudging = false;
    QVariantList m_dragRefs;
    QVariantList m_dragResult;
    QHash<QString, double> m_baseValue;
    QHash<QString, PropRange> m_ranges;
    double m_origin = 0;
    double m_dragTime = 0;
    double m_dragValue = 0;
    double m_pressValue = 0;
    double m_dt = 0;
    double m_dyPx = 0;
    double m_overrideValue = 0;
    bool m_useOverride = false;
    bool m_busyLast = false;

    bool m_marqueeActive = false;
    QRectF m_marquee;
    QVariantList m_marqueeBase;

    QVariantList m_stretchRefs;
    double m_pivot = 0;
    double m_edgeSeconds = 0;

    QVariantList m_nudgeBase;
    QVariantList m_nudgeResult;
    double m_nudgeDt = 0;
    double m_nudgePx = 0;
    QHash<QString, PropRange> m_nudgeRanges;

    QString m_readout;
    QString m_hoverId;
    int m_hoverRow = -1;
    double m_hoverSeconds = 0;
    QString m_hoverTip;
    bool m_hoverGrab = false;
    QString m_entryTarget = QStringLiteral("time");
};
