#pragma once

#include "engine/ClipTransform3d.h"

#include "core/Time.h"

#include <QMatrix4x4>
#include <QSizeF>
#include <QTransform>
#include <QVector3D>

namespace drift {

// The sequence's shared viewpoint: one eye every clip is seen through, in place of the per-clip
// eye that ClipPose3d::perspective describes. World space is the space the clip poses already
// live in — canvas pixels, origin at the canvas centre, y down, +z toward the viewer — so a
// default camera reproduces the per-clip projection exactly and nothing moves.
//
// The camera's own transform in world space is
//
//     C = rotX · rotY · rotZ · translate(positionX, positionY, positionZ)
//
// and the eye sits at C · (0, 0, perspective). Two consequences fall out of that order, and both
// are what the UI wants:
//
//  - With no translation, rotating the camera *orbits* it about the world origin (the canvas
//    plane) at radius `perspective`, the way a turntable orbit behaves. It does not spin in place.
//  - The translation is applied in the camera's own frame, so positionX/Y pan across the frame
//    whatever way the camera is facing, and positionZ dollies along the direction it looks —
//    which also changes the orbit radius.
//
// Rotations are intrinsic and applied X, then Y, then Z, the same order and the same sign
// conventions as ClipPose3d and model clips, so drift::gizmo::eulerFromMatrix can solve for them.
struct SceneCamera3d
{
    double positionX = 0.0; // px across the frame; + moves the camera right, so the image goes left
    double positionY = 0.0; // px down the frame; + moves the camera down, so the image goes up
    double positionZ = 0.0; // px along the view direction; + pulls the camera back from the scene
    double rotationX = 0.0; // pitch, degrees; + tips the camera's top away from the scene
    double rotationY = 0.0; // yaw, degrees
    double rotationZ = 0.0; // roll, degrees, clockwise on screen
    // Eye distance, and with it the strength of the perspective — the same quantity, and the same
    // default, as ClipPose3d::perspective. A camera is one eye for the whole scene, so when one is
    // active this value replaces every clip's own.
    double perspective = kDefaultClipPerspective;

    // True while the camera changes nothing, which is what lets callers keep the untouched
    // per-clip path. `perspective` is deliberately not part of this: a scene whose only camera
    // edit is the lens still has to go through the shared eye, or clips with differing
    // per-clip perspective values would disagree about where the viewer is.
    bool isIdentity() const
    {
        return qFuzzyIsNull(positionX) && qFuzzyIsNull(positionY) && qFuzzyIsNull(positionZ)
               && qFuzzyIsNull(rotationX) && qFuzzyIsNull(rotationY) && qFuzzyIsNull(rotationZ);
    }
};

// The camera's own transform in world space, C = rotX · rotY · rotZ · translate(P), and the eye
// it puts at C · (0, 0, perspective). What the 3D view draws the camera's body and frustum from.
QMatrix4x4 sceneCameraWorld(const SceneCamera3d &camera);
QVector3D sceneCameraEye(const SceneCamera3d &camera);

struct Clip;

// The viewpoint a Camera clip describes at `timelineUs`, reading the clip's ordinary transform
// tracks with camera meanings: pan from transformX/Y, dolly from positionZ, pitch/yaw from
// rotationX/Y, roll from rotation and the lens from perspective.
//
// Lengths are multiplied by `renderScale` so they track the canvas the way every other layout
// value does; the angles are not, which is what keeps a preview at renderScale 0.5 framed exactly
// like the export at 1.0. The compositor and the preview overlay both resolve the camera through
// this, so the grips cannot be computed from different numbers than the pixels.
SceneCamera3d sceneCameraFromClip(const Clip &clip, TimeUs timelineUs, double renderScale);

// World (canvas px about the canvas centre) → homogeneous canvas pixels, top-left origin: divide
// x, y by w to get the pixel. This is the projection half of clipQuadToCanvas lifted out so one
// eye can serve every clip, with the camera's inverse transform in front of it.
//
// For a camera that is `isIdentity()` and carries the clip's own `perspective`, this is exactly
// the projection clipQuadToCanvas applies, so camera · clipQuadToWorld agrees with it.
QMatrix4x4 cameraViewProjection(const SceneCamera3d &camera, const QSizeF &canvas);

// An *affine* transform-layer parent (canvas px -> canvas px) as the same map in world space, with
// z untouched: the layer slides, scales and spins its children within the canvas plane, and the
// camera then views that plane in depth. Exact for an affine parent, which is every transform layer
// that is not itself tilted.
QMatrix4x4 worldParentFromAffine(const QTransform &parent, const QSizeF &canvas);

// Homogeneous canvas pixels — what clipQuadToCanvas and parentedQuadToCanvas produce — read back as
// points on the world's z = 0 plane and then viewed through the camera.
//
// This is the fallback for a clip under a *projective* (tilted) transform layer, where the parent is
// a plane-to-plane homography that only means anything through the card's own eye, so it cannot be
// lifted into world space without widening transformLayerMatrix to a full 4x4. The camera still
// moves and turns the card correctly as a flat picture; what the clip does not get is parallax from
// the card's own tilt. An identity camera leaves the pixels exactly where they were.
QMatrix4x4 cameraCanvasPlaneToCanvas(const SceneCamera3d &camera, const QSizeF &canvas);

// Where a clip's unit quad lands, in homogeneous canvas pixels, once the camera is looking at it —
// through its transform-layer parent, if it has one.
//
// This is the one placement rule for a camera-lit scene, and it has two callers on purpose: the
// compositor's model matrix and the preview overlay's box and handles. Splitting them was what let
// the grips drift away from the picture, so they share this instead. Only for an active camera;
// both callers keep their own untouched path for a scene without one.
QMatrix4x4 cameraQuadToCanvas(const SceneCamera3d &camera, const QRectF &rect, double rotation,
                              bool flipH, bool flipV, const ClipPose3d &pose,
                              const QTransform &parent, bool hasParent, const QSizeF &canvas);

// The same placement for an item of the rect's size laid out from (0, 0), with z passed through so
// the matrix inverts: what a QtQuick Matrix4x4 needs to lay the overlay over a clip, and what the
// pointer is mapped back through. The camera counterpart of clipLocalToCanvas.
QMatrix4x4 cameraClipLocalToCanvas(const SceneCamera3d &camera, const QRectF &rect, double rotation,
                                   const ClipPose3d &pose, const QTransform &parent, bool hasParent,
                                   const QSizeF &canvas);

// The three placements above for any viewpoint, given as its world -> homogeneous canvas px
// matrix (cameraViewProjection, or the preview's free 3D view). The camera* forms are these with
// cameraViewProjection.
QMatrix4x4 viewCanvasPlaneToCanvas(const QMatrix4x4 &view, const QSizeF &canvas);
QMatrix4x4 viewQuadToCanvas(const QMatrix4x4 &view, const QRectF &rect, double rotation, bool flipH,
                            bool flipV, const ClipPose3d &pose, const QTransform &parent,
                            bool hasParent, const QSizeF &canvas);
QMatrix4x4 viewClipLocalToCanvas(const QMatrix4x4 &view, const QRectF &rect, double rotation,
                                 const ClipPose3d &pose, const QTransform &parent, bool hasParent,
                                 const QSizeF &canvas);

} // namespace drift
