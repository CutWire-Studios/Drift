#pragma once

#include "core/geom/BezierPath.h"
#include "ui/manip/ManipModifiers.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>
#include <QVector>

namespace drift::manip {

// The editing state of Bezier outlines: contours normalized to a frame, a selection, and every
// gesture that reshapes them. Hit-testing and drags work in viewport px through `view`, the map from
// the normalized frame to the viewport (a homography, so a clip tilted in 3D works too), which keeps
// radii and tolerances in screen px whatever the zoom. Shape Path clips and Freeform masks share it;
// the caller owns reading the outline from, and writing it back to, the project.
class PathEditCore
{
public:
    enum class HitKind { None, Vertex, Handle, Segment, Inside };
    struct Hit
    {
        HitKind kind = HitKind::None;
        int contour = -1;
        int vertex = -1;
        geom::TangentHandle handle = geom::TangentHandle::Out;
        int segment = -1;
        double t = 0.0;
        QPointF point; // viewport px
    };
    struct Tolerances
    {
        double vertex = 8.0;
        double handle = 8.0;
        double segment = 6.0;
    };
    struct VRef
    {
        int c = 0;
        int v = 0;
        bool operator==(const VRef &o) const = default;
        bool operator<(const VRef &o) const { return c != o.c ? c < o.c : v < o.v; }
    };
    struct HandleView
    {
        int c = 0;
        int v = 0;
        geom::TangentHandle which = geom::TangentHandle::Out;
        QPointF tip;    // viewport px
        QPointF anchor; // viewport px
    };
    enum class SelectMode { Replace, Add, Toggle };
    enum class Gesture { None, Move, Handle, Bend, Body, PenDrag };
    enum class PenResult { Ignored, Added, Closed, Continued };

    void setContours(const QVector<ShapeContour> &contours);
    const QVector<ShapeContour> &contours() const { return m_contours; }
    // Drops the selection and any pen path in progress along with the outline.
    void clear();

    // Masks are one closed contour that never drops below three vertices.
    void setClosedOnly(bool on) { m_closedOnly = on; }
    bool closedOnly() const { return m_closedOnly; }

    // While a gesture is open the view it began with stays in force (the outline is held in that
    // frame's units); the newest one is applied when it ends.
    void setView(const QTransform &normalizedToViewport);
    QTransform view() const { return m_view; }
    // The frame's size in its own px, for HUD values and nudge steps.
    void setFrameSize(const QSizeF &px) { m_frame = px; }
    QSizeF frameSize() const { return m_frame; }
    QPointF toView(const QPointF &n) const { return m_view.map(n); }
    QPointF toNorm(const QPointF &vp) const { return m_inverse.map(vp); }
    bool axisAligned() const { return m_axisAligned; }

    // ---- selection ----
    const QList<VRef> &selection() const { return m_selected; }
    bool isSelected(int c, int v) const;
    void select(int c, int v, SelectMode mode);
    void selectAll();
    void clearSelection();
    void beginMarquee();
    // Vertices whose viewport position is inside `rect`, combined with what beginMarquee kept.
    void selectInRect(const QRectF &rect, SelectMode mode);
    // -2 mixed, -1 nothing selected, else the TangentMode shared by the selection.
    int selectedMode() const;

    // ---- hit testing (viewport px) ----
    Hit hitTest(const QPointF &vp, const Tolerances &tol) const;
    QVector<HandleView> visibleHandles() const;
    QPointF vertexView(int c, int v) const;

    // ---- one-shot edits; structural ones are recorded in ops() ----
    int insertAt(int c, int segment, double t);
    bool deleteSelected(bool heal);
    bool setSelectedMode(TangentMode mode);
    bool toggleSmooth(int c, int v);
    bool toggleClosed(int c);
    // Moves the selection (everything when nothing is selected) by a step in frame px.
    bool nudge(double dxPx, double dyPx);

    const QVector<geom::PathOp> &ops() const { return m_ops; }
    QVector<geom::PathOp> takeOps();

    // ---- gestures ----
    void beginMove(int c, int v, const QPointF &pressVp);
    void beginHandle(int c, int v, geom::TangentHandle which, const QPointF &pressVp, const ManipModifiers &mods);
    void beginBend(int c, int segment, double t, const QPointF &pressVp);
    void beginBody(const QPointF &pressVp);
    void beginPenDrag(int c, int v, const QPointF &pressVp);
    // Applies the pointer at `vp` to the outline as it was at the press. True when it changed.
    bool drag(const QPointF &vp, const ManipModifiers &mods);
    void endGesture();
    // Restores the outline the gesture began from.
    void cancelGesture();
    Gesture gesture() const { return m_gesture; }
    bool bodyMoved() const { return m_bodyMoved; }
    // Normalized 0, 0.5, 1 line the last drag snapped to, else -1.
    double snapGuideX() const { return m_guideX; }
    double snapGuideY() const { return m_guideY; }
    QString hudText() const { return m_hud; }

    // ---- pen ----
    // Index of the open contour being drawn, or -1.
    int penContour() const { return m_penContour; }
    bool penNearFirst(const QPointF &vp, double tolPx) const;
    bool isOpenEnd(int c, int v) const;
    // Click: close on the first vertex, else append a corner vertex (constrained to 45 degrees from
    // the last one with Shift). Starts a new open contour when none is active.
    PenResult penPress(const QPointF &vp, const ManipModifiers &mods, double closeTolPx);
    // Makes the open contour whose end vertex v is picked the active one, reversing it when v is
    // its first vertex.
    bool penContinue(int c, int v);
    void penEnd();
    // Backspace: drops the active contour's last vertex.
    bool penUndo();
    int vertexCount() const;

    // ---- drawing ----
    QString outlineSvg() const;
    QString handleSvg() const;
    // The segment a click would add, from the active contour's last vertex to `vp`.
    QString rubberSvg(const QPointF &vp, const ManipModifiers &mods) const;

private:
    void applyView(const QTransform &normalizedToViewport);
    QPointF constrainedFromLast(const QPointF &vp, const ManipModifiers &mods) const;
    void applySnap(QPointF &deltaNorm, const QVector<double> &xs, const QVector<double> &ys, const ManipModifiers &mods,
                   bool lockedX, bool lockedY);
    void setSelectionSorted(QList<VRef> list);
    void shiftSelection(const QPointF &deltaNorm);
    double pxPerNormX() const;
    double pxPerNormY() const;

    QVector<ShapeContour> m_contours;
    QList<VRef> m_selected;
    QList<VRef> m_marqueeBase;
    QVector<geom::PathOp> m_ops;
    QTransform m_view;
    QTransform m_inverse;
    QTransform m_pendingView;
    bool m_hasPendingView = false;
    QSizeF m_frame{1, 1};
    bool m_axisAligned = true;
    bool m_closedOnly = false;

    Gesture m_gesture = Gesture::None;
    QVector<ShapeContour> m_start;
    QPointF m_pressVp;
    QPointF m_grab;
    int m_gc = -1;
    int m_gv = -1;
    geom::TangentHandle m_gh = geom::TangentHandle::Out;
    int m_gseg = -1;
    double m_gt = 0.5;
    bool m_gbreak = false;
    bool m_bodyMoved = false;
    bool m_moved = false;
    double m_guideX = -1.0;
    double m_guideY = -1.0;
    QString m_hud;

    int m_penContour = -1;
};

} // namespace drift::manip
