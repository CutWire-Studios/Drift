#pragma once

#include "ui/manip/DragSession.h"
#include "ui/manip/PathEditCore.h"

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class AppController;
class MaskingController;

// The pen and path editor's session: PathEditCore (the geometry, in viewport px) behind one of three
// backends. "shape" edits a Path clip's contours, "mask" a Freeform mask adjustment's vertices (through
// MaskEditSession, so path keys and tracker correction behave as they always did), and "draft" is an
// outline being drawn that is not in the project yet. Every change to the project is one undo step on
// EditorState.drag; Esc on a drag restores the project and this session reloads from it.
class PathEditSession : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(bool attached READ attached NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(bool penMode READ penMode WRITE setPenMode NOTIFY changed)
    Q_PROPERTY(bool canPen READ canPen NOTIFY changed)
    Q_PROPERTY(bool penActive READ penActive NOTIFY changed)
    Q_PROPERTY(bool healOnDelete READ healOnDelete WRITE setHealOnDelete NOTIFY changed)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY changed)
    Q_PROPERTY(int selectedMode READ selectedMode NOTIFY changed)
    Q_PROPERTY(bool canExtend READ canExtend NOTIFY changed)
    Q_PROPERTY(bool canToggleClosed READ canToggleClosed NOTIFY changed)
    Q_PROPERTY(bool selectedClosed READ selectedClosed NOTIFY changed)
    Q_PROPERTY(int vertexCount READ vertexCount NOTIFY changed)
    Q_PROPERTY(bool gesturing READ gesturing NOTIFY changed)
    Q_PROPERTY(double snapGuideX READ snapGuideX NOTIFY changed)
    Q_PROPERTY(double snapGuideY READ snapGuideY NOTIFY changed)
    Q_PROPERTY(QRectF marquee READ marquee NOTIFY changed)
    Q_PROPERTY(bool marqueeActive READ marqueeActive NOTIFY changed)

public:
    explicit PathEditSession(AppController *app, MaskingController *owner);

    QString kind() const { return m_kind; }
    bool attached() const { return !m_kind.isEmpty(); }
    int revision() const { return m_revision; }
    bool penMode() const { return m_penMode || m_kind == QLatin1String("draft"); }
    void setPenMode(bool on);
    // A mask is always a closed polygon, so only a Shape Path or a draft can be pen-drawn.
    bool canPen() const { return m_kind == QLatin1String("shape") || m_kind == QLatin1String("draft"); }
    bool penActive() const { return m_core.penContour() >= 0; }
    bool healOnDelete() const { return m_heal; }
    void setHealOnDelete(bool on);
    int selectionCount() const { return int(m_core.selection().size()); }
    int selectedMode() const { return m_core.selectedMode(); }
    bool canExtend() const;
    bool canToggleClosed() const;
    bool selectedClosed() const;
    int vertexCount() const { return m_core.vertexCount(); }
    bool gesturing() const { return m_core.gesture() != drift::manip::PathEditCore::Gesture::None; }
    double snapGuideX() const { return m_core.snapGuideX(); }
    double snapGuideY() const { return m_core.snapGuideY(); }
    QRectF marquee() const { return m_marquee; }
    bool marqueeActive() const { return m_marqueeActive; }

    // The frame's corners in viewport px: where normalized (0,0), (1,0), (1,1) and (0,1) land, and
    // its size in its own px. Call again whenever the frame or the view moves.
    Q_INVOKABLE void setView(const QPointF &p00, const QPointF &p10, const QPointF &p11, const QPointF &p01,
                             double frameW, double frameH);

    Q_INVOKABLE bool attachShape(int track, int clip);
    // `clipId` is the mask adjustment's id.
    Q_INVOKABLE bool attachMask(const QString &clipId);
    // An outline that is not in the project yet. `closedOnly` is a mask polygon.
    Q_INVOKABLE void attachDraft(bool closedOnly);
    Q_INVOKABLE void detach();
    // Reads the outline again; does nothing while a gesture is open.
    Q_INVOKABLE bool reload();

    // Pointer, in viewport px. `modifiers` are the event's Qt::KeyboardModifiers and `device` a
    // DragSession.Device; each press picks its own hit size and behaviour from the device.
    // Returns {cursor, kind, x, y, c, v}: the ToolCursor shape for here, and what is under it.
    Q_INVOKABLE QVariantMap hover(const QPointF &vp, int modifiers, int device);
    // What the press landed on: "vertex", "handle", "segment", "body", "pen", "right" or "none". "none"
    // in the edit tool leaves the press to the caller (a marquee).
    Q_INVOKABLE QString press(const QPointF &vp, int modifiers, int device, int button);
    Q_INVOKABLE void move(const QPointF &vp, int modifiers);
    // "drag", "insert", "convert", "select", "pen" or "" for a click that changed nothing.
    Q_INVOKABLE QString release(const QPointF &vp, int modifiers);
    // Drops a press that has not changed anything, as when a long press opens the point bar.
    Q_INVOKABLE void cancelPress();

    Q_INVOKABLE void marqueeBegin(const QPointF &vp, int modifiers);
    Q_INVOKABLE void marqueeTo(const QPointF &vp);
    Q_INVOKABLE void marqueeEnd();

    // One undo step each.
    Q_INVOKABLE bool deleteSelected(bool heal);
    Q_INVOKABLE bool setSelectedMode(int mode);
    Q_INVOKABLE bool toggleSmoothSelected();
    Q_INVOKABLE bool toggleClosedSelected();
    // Arrow keys: a step in frame px, inside the nudge burst ManipOverlay opened on EditorState.drag.
    Q_INVOKABLE void nudge(double dxPx, double dyPx);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectOnly(int contour, int vertex);
    Q_INVOKABLE void clearSelection();

    // Pen path in progress: Enter / Esc end it, Backspace drops its last point.
    Q_INVOKABLE void penEnd();
    Q_INVOKABLE bool penUndo();
    // The drawn outline, for the caller to turn into a clip or mask. Positions are scaled by the
    // frame size for `contoursScaled`, normalized in `vertices`.
    Q_INVOKABLE QVariantList draftContoursScaled(double w, double h) const;
    Q_INVOKABLE QVariantList draftVertices() const;
    Q_INVOKABLE int draftContourCount() const;
    Q_INVOKABLE void clearDraft();

    // Drawing, all in viewport px.
    Q_INVOKABLE QVariantList vertexList() const;
    Q_INVOKABLE QVariantList handleList() const;
    Q_INVOKABLE QString outlineSvg() const { return m_core.outlineSvg(); }
    Q_INVOKABLE QString handleSvg() const { return m_core.handleSvg(); }
    Q_INVOKABLE QString rubberSvg() const;
    // Normalized (0..1) to viewport px, for the snap guides.
    Q_INVOKABLE QPointF toViewport(const QPointF &n) const { return m_core.toView(n); }

    const drift::manip::PathEditCore &core() const { return m_core; }
    drift::manip::PathEditCore &core() { return m_core; }

signals:
    void changed();
    // The shape's box was refitted by a write, so its frame in the viewport moved.
    void frameInvalidated();
    // A pen click closed the draft on its first point.
    void draftClosed();

private:
    using Core = drift::manip::PathEditCore;
    Core::Tolerances tolerances(int device) const;
    double closeTolerance(int device) const;
    drift::manip::ManipModifiers modifiersOf(int qtFlags) const;
    void bump();
    void ensureOpen(const QString &kind, int device, const QString &undoText);
    // Writes the core's outline to the project inside the open drag.
    void writeLive();
    void finishDrag();
    void onCommitted();
    void onCancelled();
    bool oneShot(const QString &undoText, const std::function<bool()> &edit);
    void hoverReset();

    AppController *m_app = nullptr;
    MaskingController *m_owner = nullptr;
    Core m_core;
    QString m_kind;
    int m_track = -1;
    int m_clip = -1;
    QString m_clipId;
    QTransform m_liveView;
    QTransform m_liveInverse;
    double m_frameW = 1;
    double m_frameH = 1;
    bool m_penMode = false;
    bool m_heal = true;
    bool m_open = false;
    int m_revision = 0;

    struct Press
    {
        bool active = false;
        Core::Hit hit;
        QPointF vp;
        int device = 0;
        bool moved = false;
        bool alt = false;
        bool additive = false;
        bool deferReplace = false;
        bool pen = false;
        bool penAdded = false;
    } m_press;

    QPointF m_hover;
    drift::manip::ManipModifiers m_hoverMods;
    QPointF m_marqueeOrigin;
    QRectF m_marquee;
    bool m_marqueeActive = false;
    int m_marqueeMode = 0;
};
