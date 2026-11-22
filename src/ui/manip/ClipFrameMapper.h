#pragma once

#include <QMatrix4x4>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace drift::manip {

// Moves a point between the spaces a preview tool works in:
//   viewport  the overlay item's px, with the canvas fitted into `viewportRect`
//   canvas    project px
//   layout    the clip's own px (its rect laid out from 0, 0), before pose and rotation
//   clip      layout divided by the clip's size, 0..1; masks and crops are stored in this
// A clip's pose is its rotation about the rect's pivot, or a full matrix when the clip is tilted
// in 3D. A transform-layer parent adds one more matrix, in viewport px.
class ClipFrameMapper
{
    Q_GADGET

public:
    // Where the canvas sits in the overlay item.
    void setViewport(const QSizeF &canvas, const QRectF &viewportRect);
    // The clip's layout rect in canvas px, rotation in degrees about `pivot` (layout px; the
    // centre when unset).
    void setClip(const QRectF &layoutRect, double rotationDegrees);
    void setPivot(const QPointF &layoutPivot);
    // layout px -> homogeneous canvas px, overriding the rect and rotation.
    void setPose(const QMatrix4x4 &layoutToCanvas);
    // Viewport px in the parented frame -> the clip's own viewport frame is inverse(parent).
    void setParent(const QMatrix4x4 &parentViewportMatrix);

    double viewScale() const;
    QSizeF clipSize() const { return m_clip.size(); }

    Q_INVOKABLE QPointF canvasToViewport(const QPointF &p) const;
    Q_INVOKABLE QPointF viewportToCanvas(const QPointF &p) const;
    Q_INVOKABLE QPointF layoutToCanvas(const QPointF &p) const;
    Q_INVOKABLE QPointF canvasToLayout(const QPointF &p) const;
    Q_INVOKABLE QPointF clipToCanvas(const QPointF &n) const;
    Q_INVOKABLE QPointF canvasToClip(const QPointF &p) const;
    Q_INVOKABLE QPointF clipToViewport(const QPointF &n) const;
    Q_INVOKABLE QPointF viewportToClip(const QPointF &p) const;
    Q_INVOKABLE QPointF toParentLocal(const QPointF &viewportPoint) const;

private:
    QMatrix4x4 pose() const;

    QSizeF m_canvas{1, 1};
    QRectF m_viewport{0, 0, 1, 1};
    QRectF m_clip{0, 0, 1, 1};
    double m_rotation = 0.0;
    QPointF m_pivot;
    bool m_hasPivot = false;
    QMatrix4x4 m_pose;
    bool m_hasPose = false;
    QMatrix4x4 m_parentInverse;
    bool m_hasParent = false;
};

} // namespace drift::manip
