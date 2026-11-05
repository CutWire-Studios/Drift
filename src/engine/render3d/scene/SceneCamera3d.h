#pragma once

#include "engine/render3d/scene/ClipTransform3d.h"

#include "core/base/Time.h"

#include <QMatrix4x4>
#include <QSizeF>
#include <QTransform>
#include <QVector3D>

namespace drift {

// The sequence's shared viewpoint: one eye every clip is seen through, in place of the per-clip
// eye that ClipPose3d::perspective describes.
//
// The camera lives in the same world as the clips (ClipTransform3d.h: canvas top-left origin, px,
// +X right, +Y down, +Z away from the viewer), After Effects style. A camera at rest stands at
// (W/2, H/2, -zoom) looking along +Z, which frames the canvas exactly as the per-clip eye does.
//
// The camera's own transform is
//
//     C = translate(eye) · Ry(rotationY) · Rx(rotationX) · Rz(rotationZ)
//
// Yaw comes first and turns about the eye, so for any pitch and yaw with zero roll the camera's
// right vector has no Y component: the horizon stays level. Gimbal lock sits at pitch +-90
// degrees. Turning never moves the eye, so two keys that differ only in rotation keep the eye
// still between them. Local axes: +x right, +y down, +z ahead (right-handed, like the world).
//
// With `aimAtTarget` the camera first faces (target - eye) with world up = -Y (zero roll), and the
// rotation then acts as an offset in the camera's own frame: R = Look(eye -> target) · Ry · Rx · Rz,
// the way After Effects combines a two-node camera's Orientation and Rotation.
//
// `zoom` is the distance in px from the eye at which the canvas fills the frame (After Effects'
// Zoom). It is a real zoom: changing it never moves the eye. Focal length and field of view are
// the same number in other units (see below).
struct SceneCamera3d
{
    double positionX = 0.0; // the eye, world px
    double positionY = 0.0;
    double positionZ = 0.0;
    double rotationX = 0.0; // pitch, degrees; + tips the view up (toward -Y)
    double rotationY = 0.0; // yaw, degrees; + turns the view toward +X
    double rotationZ = 0.0; // roll, degrees, clockwise on screen
    bool aimAtTarget = false;
    double targetX = 0.0; // point of interest, world px; used only with aimAtTarget
    double targetY = 0.0;
    double targetZ = 0.0;
    // A camera is one eye for the whole scene, so when one is active this value replaces every
    // clip's own perspective.
    double zoom = kDefaultClipPerspective;
};

// The camera standing at rest for a project of this size: the default eye and zoom, aimed along +Z.
SceneCamera3d defaultSceneCamera(const QSizeF &project);

// Zoom <-> focal length on a 36 x 24 mm film back, and <-> vertical field of view, for a frame
// `height` px tall. The default zoom (defaultClipPerspective) is a 50 mm lens.
double zoomFromFocalLength(double millimetres, double height);
double focalLengthFromZoom(double zoom, double height);
double zoomFromFov(double degrees, double height);
double fovFromZoom(double zoom, double height);

// The camera's rotation: Ry · Rx · Rz, preceded by the look-at rotation when aimAtTarget is on.
QMatrix4x4 sceneCameraRotation(const SceneCamera3d &camera);
// The camera's own transform, C = translate(eye) · rotation: local points to world.
QMatrix4x4 sceneCameraWorld(const SceneCamera3d &camera);
QVector3D sceneCameraEye(const SceneCamera3d &camera);

// The camera turned to rotationX/rotationY with its eye held where it was. Roll, zoom and the
// target are kept.
SceneCamera3d rotatedKeepingEye(const SceneCamera3d &camera, double rotationX, double rotationY);

// The camera standing at `eye` and facing `target` with a level horizon (roll 0). Without
// aimAtTarget the pitch and yaw are written to the rotation; with it the target becomes the point
// of interest and the rotation offset is cleared. Eye and target coinciding leaves the camera
// unchanged. Zoom is kept.
SceneCamera3d aimAt(const SceneCamera3d &camera, const QVector3D &eye, const QVector3D &target);
// The same with the eye where the camera already has it: it turns to face `target` in place.
SceneCamera3d aimAt(const SceneCamera3d &camera, const QVector3D &target);

struct Clip;

// The viewpoint a Camera clip describes at `timelineUs`, reading the clip's ordinary tracks with
// camera meanings:
//
//   transformX/Y, positionZ       the eye (default: the canvas centre, one default zoom back)
//   rotationX, rotationY, rotation  pitch, yaw, roll
//   perspective                   zoom (default: defaultClipPerspective of the project height)
//   anchorX/Y/Z, Clip::aimAtTarget  the point of interest (default: the canvas centre) and whether
//                                 the camera faces it
//
// `projectSize` is the project's size in px; lengths are multiplied by `renderScale` so they track
// the canvas the way every other layout value does, while the angles are not, which keeps a preview
// at renderScale 0.5 framed exactly like the export at 1.0. The compositor and the preview overlay
// both resolve the camera through this, so the grips cannot be computed from different numbers
// than the pixels.
SceneCamera3d sceneCameraFromClip(const Clip &clip, TimeUs timelineUs, double renderScale,
                                  const QSizeF &projectSize);

// World → homogeneous canvas pixels, top-left origin: divide
// x, y by w to get the pixel. This is the projection half of clipQuadToCanvas lifted out so one
// eye can serve every clip, with the camera's inverse transform in front of it.
//
// For defaultSceneCamera with the clip's own `perspective` as its zoom, this is exactly the
// projection clipQuadToCanvas applies, so camera · clipQuadToWorld agrees with it.
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
