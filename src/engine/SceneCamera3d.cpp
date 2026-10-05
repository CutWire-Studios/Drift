#include "engine/SceneCamera3d.h"

#include "core/Clip.h"
#include "engine/TransformLayer.h"

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

namespace {

double trackValue(const KeyframeTrack<double> &track, TimeUs relative, double fallback)
{
    return track.isEmpty() ? fallback : track.evaluateAt(relative);
}

} // namespace

SceneCamera3d sceneCameraFromClip(const Clip &clip, TimeUs timelineUs, double renderScale)
{
    const TimeUs relative = timelineUs - clip.timelineStart;
    SceneCamera3d camera;
    camera.positionX = trackValue(clip.transformX, relative, 0.0) * renderScale;
    camera.positionY = trackValue(clip.transformY, relative, 0.0) * renderScale;
    camera.positionZ = trackValue(clip.positionZ, relative, 0.0) * renderScale;
    camera.rotationX = trackValue(clip.rotationX, relative, 0.0);
    camera.rotationY = trackValue(clip.rotationY, relative, 0.0);
    camera.rotationZ = trackValue(clip.rotation, relative, 0.0);
    camera.perspective =
        trackValue(clip.perspective, relative, kDefaultClipPerspective) * renderScale;
    return camera;
}

namespace {

// A canvas->canvas parent homography as a 4x4 acting on x, y and w with z passed through. The same
// lift the preview overlay has always used for a flat parent chain; only the local (invertible)
// path needs it, because the quad path hands its near-plane work to parentedQuadToCanvas.
QMatrix4x4 liftParent(const QTransform &t)
{
    return QMatrix4x4(float(t.m11()), float(t.m21()), 0.f, float(t.m31()),
                      float(t.m12()), float(t.m22()), 0.f, float(t.m32()),
                      0.f, 0.f, 1.f, 0.f,
                      float(t.m13()), float(t.m23()), 0.f, float(t.m33()));
}

} // namespace

QMatrix4x4 cameraQuadToCanvas(const SceneCamera3d &camera, const QRectF &rect, double rotation,
                              bool flipH, bool flipV, const ClipPose3d &pose,
                              const QTransform &parent, bool hasParent, const QSizeF &canvas)
{
    const QMatrix4x4 view = cameraViewProjection(camera, canvas);
    if (!hasParent) {
        // The ordinary case, and the whole point of the feature: the clip sits in world space and
        // one eye looks at it, so clips at different depths move by different amounts.
        return view * clipQuadToWorld(rect, rotation, flipH, flipV, pose, canvas);
    }
    if (parent.isAffine()) {
        return view * worldParentFromAffine(parent, canvas)
               * clipQuadToWorld(rect, rotation, flipH, flipV, pose, canvas);
    }
    // Tilted transform layer: see cameraCanvasPlaneToCanvas for why this goes through the card's
    // own eye first and is then viewed as a flat picture.
    const QMatrix4x4 quad = pose.isActive()
                                ? clipQuadToCanvas(rect, rotation, flipH, flipV, pose, canvas)
                                : flatQuadToCanvas(rect, rotation, flipH, flipV);
    return cameraCanvasPlaneToCanvas(camera, canvas) * parentedQuadToCanvas(parent, quad);
}

QMatrix4x4 cameraClipLocalToCanvas(const SceneCamera3d &camera, const QRectF &rect, double rotation,
                                   const ClipPose3d &pose, const QTransform &parent, bool hasParent,
                                   const QSizeF &canvas)
{
    QMatrix4x4 m;
    if (!hasParent) {
        m = cameraViewProjection(camera, canvas) * clipLocalToWorld(rect, rotation, pose, canvas);
    } else if (parent.isAffine()) {
        m = cameraViewProjection(camera, canvas) * worldParentFromAffine(parent, canvas)
            * clipLocalToWorld(rect, rotation, pose, canvas);
    } else {
        // The projective-parent fallback, matching cameraQuadToCanvas: the card is placed through
        // its own eye and then viewed flat.
        m = cameraCanvasPlaneToCanvas(camera, canvas) * liftParent(parent)
            * clipLocalToCanvas(rect, rotation, pose, canvas);
    }
    // A z = 0 point stays at z = 0 and the matrix stays invertible, exactly as clipLocalToCanvas
    // arranges — the overlay maps the pointer back through the inverse of this.
    m.setRow(2, QVector4D(0.f, 0.f, 1.f, 0.f));
    return m;
}

} // namespace drift
