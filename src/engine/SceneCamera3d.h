#pragma once

#include "engine/ClipTransform3d.h"

#include <QMatrix4x4>
#include <QSizeF>

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

// World (canvas px about the canvas centre) → homogeneous canvas pixels, top-left origin: divide
// x, y by w to get the pixel. This is the projection half of clipQuadToCanvas lifted out so one
// eye can serve every clip, with the camera's inverse transform in front of it.
//
// For a camera that is `isIdentity()` and carries the clip's own `perspective`, this is exactly
// the projection clipQuadToCanvas applies, so camera · clipQuadToWorld agrees with it.
QMatrix4x4 cameraViewProjection(const SceneCamera3d &camera, const QSizeF &canvas);

} // namespace drift
