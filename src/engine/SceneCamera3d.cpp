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

} // namespace drift
