#include "engine/SceneCamera3d.h"

#include <QVector4D>

#include <algorithm>

namespace drift {

QMatrix4x4 cameraViewProjection(const SceneCamera3d &camera, const QSizeF &canvas)
{
    // Identical to the projection half of ClipTransform3d's placeCentre, down to the clamp and the
    // row layout: the eye `d` px in front of the canvas centre, with clip-space z set so GL clips
    // only what is nearer than a sliver in front of the eye. Keeping it line-for-line the same is
    // what makes a default camera a no-op rather than an almost-no-op.
    const double d = std::max(1.0, camera.perspective);
    QMatrix4x4 projection;
    projection.translate(float(canvas.width() * 0.5), float(canvas.height() * 0.5));
    QMatrix4x4 persp;
    persp.setRow(2, QVector4D(0.f, 0.f, float(-1.0 / d), 1.f - 2.f * float(kClipNearFraction)));
    persp.setRow(3, QVector4D(0.f, 0.f, float(-1.0 / d), 1.f));
    projection *= persp;

    // The inverse of C = rotX · rotY · rotZ · translate(P), which is translate(-P) · rotZ⁻¹ ·
    // rotY⁻¹ · rotX⁻¹. Built directly rather than through QMatrix4x4::inverted() so an unrotated,
    // untranslated camera leaves the projection untouched instead of running it through a general
    // 4x4 inversion.
    projection.translate(float(-camera.positionX), float(-camera.positionY),
                         float(-camera.positionZ));
    projection.rotate(float(-camera.rotationZ), 0.f, 0.f, 1.f);
    projection.rotate(float(-camera.rotationY), 0.f, 1.f, 0.f);
    projection.rotate(float(-camera.rotationX), 1.f, 0.f, 0.f);
    return projection;
}

QMatrix4x4 worldParentFromAffine(const QTransform &parent, const QSizeF &canvas)
{
    // Canvas coordinates are top-left origin and world coordinates are centre origin, so the
    // parent is conjugated by that shift: world -> canvas, the parent, then canvas -> world.
    const float cx = float(canvas.width() * 0.5);
    const float cy = float(canvas.height() * 0.5);
    // Column-vector 4x4 from the row-vector QTransform, the same extraction parentedQuadToCanvas
    // uses. z is passed through, and w stays 1 because an affine parent has no projective row.
    const QMatrix4x4 lift(float(parent.m11()), float(parent.m21()), 0.f, float(parent.m31()),
                          float(parent.m12()), float(parent.m22()), 0.f, float(parent.m32()),
                          0.f, 0.f, 1.f, 0.f,
                          0.f, 0.f, 0.f, 1.f);
    QMatrix4x4 m;
    m.translate(-cx, -cy);
    m *= lift;
    m.translate(cx, cy);
    return m;
}

QMatrix4x4 cameraCanvasPlaneToCanvas(const SceneCamera3d &camera, const QSizeF &canvas)
{
    // (X, Y, *, W) standing for the canvas pixel (X/W, Y/W) becomes the world point
    // (X - cx*W, Y - cy*W, 0, W). Dropping the incoming z is deliberate: it carried the clip's own
    // near-plane encoding, and the camera's projection supplies that afresh.
    const float cx = float(canvas.width() * 0.5);
    const float cy = float(canvas.height() * 0.5);
    const QMatrix4x4 flatten(1.f, 0.f, 0.f, -cx,
                             0.f, 1.f, 0.f, -cy,
                             0.f, 0.f, 0.f, 0.f,
                             0.f, 0.f, 0.f, 1.f);
    return cameraViewProjection(camera, canvas) * flatten;
}

} // namespace drift
