#pragma once

#include <QMatrix4x4>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>

namespace drift {

// A clip's 3D pose on top of its 2D layout. All lengths are in the same pixels as the layout
// rect and the canvas. The eye sits `perspective` px in front of the canvas centre, so every clip
// is seen by one virtual camera; a shared scene camera can later replace the per-clip distance.
struct ClipPose3d
{
    double rotationX = 0.0; // degrees; + tips the top edge away from the viewer
    double rotationY = 0.0; // degrees; + swings the right edge away from the viewer
    double positionZ = 0.0; // px; + toward the viewer
    double perspective = 2000.0;

    bool isActive() const
    {
        return !qFuzzyIsNull(rotationX) || !qFuzzyIsNull(rotationY) || !qFuzzyIsNull(positionZ);
    }
};

inline constexpr double kDefaultClipPerspective = 2000.0;

// Maps the unit quad [-1, 1]² onto the canvas in homogeneous canvas pixels (top-left origin):
// divide x, y by w to get the pixel. `rotation` is the existing in-plane spin (clockwise degrees)
// about the rect centre. Clip-space z is set so that GL clips anything nearer than a sliver in
// front of the eye, and nothing else.
QMatrix4x4 clipQuadToCanvas(const QRectF &rect, double rotation, bool flipH, bool flipV,
                            const ClipPose3d &pose, const QSizeF &canvas);

// The same placement for an item of the rect's size laid out from (0, 0), without the flips and
// with z passed through untouched (a z = 0 point stays at z = 0, and the matrix stays invertible):
// what a QtQuick Matrix4x4 transform needs to overlay the clip.
QMatrix4x4 clipLocalToCanvas(const QRectF &rect, double rotation, const ClipPose3d &pose,
                             const QSizeF &canvas);

// The four projected corners (top-left, top-right, bottom-right, bottom-left), in canvas pixels.
// Empty when any corner is at or behind the eye.
QPolygonF projectedClipQuad(const QRectF &rect, double rotation, const ClipPose3d &pose,
                            const QSizeF &canvas);

} // namespace drift
