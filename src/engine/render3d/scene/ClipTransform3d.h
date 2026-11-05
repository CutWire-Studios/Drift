#pragma once

#include "core/base/Time.h"

#include <QMatrix4x4>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QVector3D>

namespace drift {

// World space, After Effects style: pixels, origin at the canvas TOP-LEFT, +X right, +Y down and
// +Z AWAY from the viewer — a right-handed system. A rotation about an axis is the right-hand
// rotation in these coordinates (QMatrix4x4::rotate), so +rotationZ is clockwise on screen.
//
// A clip's 3D pose on top of its 2D layout. All lengths are in the same pixels as the layout
// rect and the canvas. The eye sits `perspective` px in front of the canvas centre (z = -perspective),
// so every clip is seen by one virtual camera; a shared scene camera can replace the per-clip
// distance.
//
// transformX/Y stay the layout rect's top-left corner, so the unrotated layer sits where the 2D
// layout puts it at depth `positionZ`. Rotations pivot on the anchor point: a point in the layer's
// own pixels measured from the rect's top-left corner (`anchorZ` out of its plane). Unanchored, the
// pivot is the rect centre and follows width/height changes.
struct ClipPose3d
{
    double rotationX = 0.0; // degrees; + tips the top edge toward the viewer
    double rotationY = 0.0; // degrees; + swings the right edge toward the viewer
    double positionZ = 0.0; // px; + away from the viewer
    double perspective = 2000.0;
    // Each axis of the anchor is either set (px from the rect's left / top edge) or the rect centre.
    bool hasAnchorX = false;
    bool hasAnchorY = false;
    double anchorX = 0.0;
    double anchorY = 0.0;
    double anchorZ = 0.0; // px along +z from the layer's plane

    bool anchored() const { return hasAnchorX || hasAnchorY; }

    bool isActive() const
    {
        return !qFuzzyIsNull(rotationX) || !qFuzzyIsNull(rotationY) || !qFuzzyIsNull(positionZ)
               || anchored();
    }

    // The anchor in the layer's own px, from the rect's top-left corner.
    QVector3D anchorIn(const QRectF &rect) const
    {
        return QVector3D(float(hasAnchorX ? anchorX : rect.width() * 0.5),
                         float(hasAnchorY ? anchorY : rect.height() * 0.5), float(anchorZ));
    }

    // Where the anchor sits in world space: the point rotations turn about.
    QVector3D pivot(const QRectF &rect) const
    {
        return QVector3D(float(rect.x()), float(rect.y()), float(positionZ)) + anchorIn(rect);
    }
};

struct Clip;

// The pose a clip's tracks give at `relativeUs` (clip-local time), lengths scaled by `renderScale`.
// Empty perspective falls back to the default for `projectHeight`.
ClipPose3d clipPoseAt(const Clip &clip, TimeUs relativeUs, double renderScale, double projectHeight);

// The perspective that frames like a 50 mm lens on a 36 mm-wide film back: a vertical field of
// view of 2 * atan(12 / 50) (about 27 degrees) over the project height.
inline double defaultClipPerspective(double projectHeight)
{
    return projectHeight * 0.5 / 0.24;
}

// What a clip's perspective was before the default followed the project height. Stored on clips
// that were saved with the field empty, so they keep their eye distance.
inline constexpr double kDefaultClipPerspective = 2000.0;

// Nearest the eye a point may get, as a fraction of the perspective distance.
inline constexpr double kClipNearFraction = 0.02;

// New world -> the frame the scene camera's own maths and the projection below are written in:
// canvas px about the canvas centre, y down, z toward the viewer (left-handed). Every projection
// is built on this, which is what lets a scene saved before the world flipped render unchanged.
QMatrix4x4 worldToCameraFrame(const QSizeF &canvas);

// The perspective projection for an eye `perspective` px in front of the canvas centre, from the
// camera frame to homogeneous canvas pixels.
QMatrix4x4 eyeProjection(double perspective, const QSizeF &canvas);

// World -> homogeneous canvas pixels through a clip's own eye.
QMatrix4x4 clipEyeView(double perspective, const QSizeF &canvas);

// Maps the unit quad [-1, 1]² onto the canvas in homogeneous canvas pixels (top-left origin):
// divide x, y by w to get the pixel. `rotation` is the existing in-plane spin (clockwise degrees)
// about the anchor. Clip-space z is set so that GL clips anything nearer than a sliver in
// front of the eye, and nothing else. The quad's own z axis points away from the viewer, like
// the world's.
QMatrix4x4 clipQuadToCanvas(const QRectF &rect, double rotation, bool flipH, bool flipV,
                            const ClipPose3d &pose, const QSizeF &canvas);

// The placement half of clipQuadToCanvas on its own: the unit quad [-1, 1]^2 into world space —
// world space (see the top of this file), with no projection and no perspective divide (w stays 1).
// This is the space a shared camera views, so
//
//     cameraViewProjection(camera, canvas) * clipQuadToWorld(...)
//
// is clipQuadToCanvas with one eye for the whole scene instead of the clip's own. `pose.perspective`
// is ignored here on purpose: the eye belongs to the camera, not to the clip. `canvas` is unused.
QMatrix4x4 clipQuadToWorld(const QRectF &rect, double rotation, bool flipH, bool flipV,
                           const ClipPose3d &pose, const QSizeF &canvas);

// The same placement for an item of the rect's size laid out from (0, 0), without the flips and
// with z passed through untouched (a z = 0 point stays at z = 0, and the matrix stays invertible):
// what a QtQuick Matrix4x4 transform needs to overlay the clip.
QMatrix4x4 clipLocalToCanvas(const QRectF &rect, double rotation, const ClipPose3d &pose,
                             const QSizeF &canvas);

// clipLocalToCanvas in world space: an item of the rect's size laid out from (0, 0), placed with no
// projection. What the preview overlay needs before the camera is applied.
QMatrix4x4 clipLocalToWorld(const QRectF &rect, double rotation, const ClipPose3d &pose,
                            const QSizeF &canvas);

// The flat placement of the unit quad: rect, in-plane rotation and flips, z = 0 and w = 1.
QMatrix4x4 flatQuadToCanvas(const QRectF &rect, double rotation, bool flipH, bool flipV);

// The four projected corners (top-left, top-right, bottom-right, bottom-left), in canvas pixels.
// Empty when any corner is at or behind the eye.
QPolygonF projectedClipQuad(const QRectF &rect, double rotation, const ClipPose3d &pose,
                            const QSizeF &canvas);

} // namespace drift
