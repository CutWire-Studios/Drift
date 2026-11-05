#include "engine/render3d/scene/SceneCamera3d.h"

#include "core/project/clip/Clip.h"
#include "engine/compositor/layers/TransformLayer.h"

#include <QVector4D>

#include <algorithm>
#include <cmath>

namespace drift {

SceneCamera3d defaultSceneCamera(const QSizeF &project)
{
    SceneCamera3d camera;
    camera.zoom = defaultClipPerspective(project.height());
    camera.positionX = project.width() * 0.5;
    camera.positionY = project.height() * 0.5;
    camera.positionZ = -camera.zoom;
    camera.targetX = camera.positionX;
    camera.targetY = camera.positionY;
    return camera;
}

double zoomFromFocalLength(double millimetres, double height)
{
    return height * 0.5 * millimetres / 12.0;
}

double focalLengthFromZoom(double zoom, double height)
{
    return 24.0 * zoom / std::max(1.0, height);
}

double zoomFromFov(double degrees, double height)
{
    return height * 0.5 / std::tan(std::clamp(degrees, 0.01, 179.9) * M_PI / 360.0);
}

double fovFromZoom(double zoom, double height)
{
    return 2.0 * std::atan(height * 0.5 / std::max(1.0, zoom)) * 180.0 / M_PI;
}

QMatrix4x4 sceneCameraRotation(const SceneCamera3d &camera)
{
    QMatrix4x4 m;
    if (camera.aimAtTarget) {
        const QVector3D ahead =
            QVector3D(float(camera.targetX - camera.positionX), float(camera.targetY - camera.positionY),
                      float(camera.targetZ - camera.positionZ));
        if (ahead.lengthSquared() > 1e-6f) {
            const QVector3D f = ahead.normalized();
            m.rotate(float(std::atan2(double(f.x()), double(f.z())) * 180.0 / M_PI), 0.f, 1.f, 0.f);
            m.rotate(float(std::asin(std::clamp(double(-f.y()), -1.0, 1.0)) * 180.0 / M_PI), 1.f, 0.f, 0.f);
        }
    }
    m.rotate(float(camera.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(camera.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(camera.rotationZ), 0.f, 0.f, 1.f);
    return m;
}

QMatrix4x4 sceneCameraWorld(const SceneCamera3d &camera)
{
    QMatrix4x4 m;
    m.translate(float(camera.positionX), float(camera.positionY), float(camera.positionZ));
    return m * sceneCameraRotation(camera);
}

QVector3D sceneCameraEye(const SceneCamera3d &camera)
{
    return QVector3D(float(camera.positionX), float(camera.positionY), float(camera.positionZ));
}

QMatrix4x4 cameraViewProjection(const SceneCamera3d &camera, const QSizeF &canvas)
{
    const double d = std::max(1.0, camera.zoom);
    // The same eyeProjection a clip's own eye uses, so a default camera is a no-op rather than an
    // almost-no-op. It is written for a frame with the eye d px in front of the canvas plane and z
    // toward the viewer; a point at camera depth q has frame depth d - q.
    QMatrix4x4 view = eyeProjection(d, canvas);
    view.translate(0.f, 0.f, float(d));
    view.scale(1.f, 1.f, -1.f);
    view *= sceneCameraRotation(camera).transposed();
    view.translate(float(-camera.positionX), float(-camera.positionY), float(-camera.positionZ));
    return view;
}

SceneCamera3d rotatedKeepingEye(const SceneCamera3d &camera, double rotationX, double rotationY)
{
    SceneCamera3d out = camera;
    out.rotationX = rotationX;
    out.rotationY = rotationY;
    return out;
}

SceneCamera3d aimAt(const SceneCamera3d &camera, const QVector3D &eye, const QVector3D &target)
{
    const QVector3D ahead = target - eye;
    if (ahead.lengthSquared() < 1e-6f)
        return camera;
    SceneCamera3d out = camera;
    out.positionX = eye.x();
    out.positionY = eye.y();
    out.positionZ = eye.z();
    out.rotationZ = 0.0;
    if (camera.aimAtTarget) {
        out.targetX = target.x();
        out.targetY = target.y();
        out.targetZ = target.z();
        out.rotationX = 0.0;
        out.rotationY = 0.0;
        return out;
    }
    // Ry(yaw) · Rx(pitch) takes +z to (sinY cosX, -sinX, cosY cosX).
    const QVector3D f = ahead.normalized();
    out.rotationX = std::asin(std::clamp(double(-f.y()), -1.0, 1.0)) * 180.0 / M_PI;
    out.rotationY = std::atan2(double(f.x()), double(f.z())) * 180.0 / M_PI;
    return out;
}

SceneCamera3d aimAt(const SceneCamera3d &camera, const QVector3D &target)
{
    return aimAt(camera, sceneCameraEye(camera), target);
}

QMatrix4x4 worldParentFromAffine(const QTransform &parent, const QSizeF &)
{
    // World x, y are canvas px, so the parent acts on them as it is. Column-vector 4x4 from the row-vector QTransform, the same extraction parentedQuadToCanvas
    // uses. z is passed through, and w stays 1 because an affine parent has no projective row.
    return QMatrix4x4(float(parent.m11()), float(parent.m21()), 0.f, float(parent.m31()),
                      float(parent.m12()), float(parent.m22()), 0.f, float(parent.m32()),
                      0.f, 0.f, 1.f, 0.f,
                      0.f, 0.f, 0.f, 1.f);
}

QMatrix4x4 cameraCanvasPlaneToCanvas(const SceneCamera3d &camera, const QSizeF &canvas)
{
    return viewCanvasPlaneToCanvas(cameraViewProjection(camera, canvas), canvas);
}

QMatrix4x4 viewCanvasPlaneToCanvas(const QMatrix4x4 &view, const QSizeF &)
{
    // (X, Y, *, W) standing for the canvas pixel (X/W, Y/W) becomes the world point (X, Y, 0, W).
    // Dropping the incoming z is deliberate: it carried the clip's own near-plane encoding, and
    // the camera's projection supplies that afresh.
    const QMatrix4x4 flatten(1.f, 0.f, 0.f, 0.f,
                             0.f, 1.f, 0.f, 0.f,
                             0.f, 0.f, 0.f, 0.f,
                             0.f, 0.f, 0.f, 1.f);
    return view * flatten;
}

namespace {

double trackValue(const KeyframeTrack<double> &track, TimeUs relative, double fallback)
{
    return track.isEmpty() ? fallback : track.evaluateAt(relative);
}

} // namespace

SceneCamera3d sceneCameraFromClip(const Clip &clip, TimeUs timelineUs, double renderScale,
                                  const QSizeF &projectSize)
{
    const TimeUs relative = timelineUs - clip.timelineStart;
    const double defaultZoom = defaultClipPerspective(projectSize.height());
    SceneCamera3d camera;
    camera.positionX = trackValue(clip.transformX, relative, projectSize.width() * 0.5) * renderScale;
    camera.positionY = trackValue(clip.transformY, relative, projectSize.height() * 0.5) * renderScale;
    camera.positionZ = trackValue(clip.positionZ, relative, -defaultZoom) * renderScale;
    camera.rotationX = trackValue(clip.rotationX, relative, 0.0);
    camera.rotationY = trackValue(clip.rotationY, relative, 0.0);
    camera.rotationZ = trackValue(clip.rotation, relative, 0.0);
    camera.zoom = trackValue(clip.perspective, relative, defaultZoom) * renderScale;
    camera.aimAtTarget = clip.aimAtTarget;
    camera.targetX = trackValue(clip.anchorX, relative, projectSize.width() * 0.5) * renderScale;
    camera.targetY = trackValue(clip.anchorY, relative, projectSize.height() * 0.5) * renderScale;
    camera.targetZ = trackValue(clip.anchorZ, relative, 0.0) * renderScale;
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
    return viewQuadToCanvas(cameraViewProjection(camera, canvas), rect, rotation, flipH, flipV, pose,
                            parent, hasParent, canvas);
}

QMatrix4x4 viewQuadToCanvas(const QMatrix4x4 &view, const QRectF &rect, double rotation, bool flipH,
                            bool flipV, const ClipPose3d &pose, const QTransform &parent,
                            bool hasParent, const QSizeF &canvas)
{
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
    return viewCanvasPlaneToCanvas(view, canvas) * parentedQuadToCanvas(parent, quad);
}

QMatrix4x4 cameraClipLocalToCanvas(const SceneCamera3d &camera, const QRectF &rect, double rotation,
                                   const ClipPose3d &pose, const QTransform &parent, bool hasParent,
                                   const QSizeF &canvas)
{
    return viewClipLocalToCanvas(cameraViewProjection(camera, canvas), rect, rotation, pose, parent,
                                 hasParent, canvas);
}

QMatrix4x4 viewClipLocalToCanvas(const QMatrix4x4 &view, const QRectF &rect, double rotation,
                                 const ClipPose3d &pose, const QTransform &parent, bool hasParent,
                                 const QSizeF &canvas)
{
    QMatrix4x4 m;
    if (!hasParent) {
        m = view * clipLocalToWorld(rect, rotation, pose, canvas);
    } else if (parent.isAffine()) {
        m = view * worldParentFromAffine(parent, canvas) * clipLocalToWorld(rect, rotation, pose, canvas);
    } else {
        // The projective-parent fallback, matching viewQuadToCanvas: the card is placed through
        // its own eye and then viewed flat.
        m = viewCanvasPlaneToCanvas(view, canvas) * liftParent(parent)
            * clipLocalToCanvas(rect, rotation, pose, canvas);
    }
    // A z = 0 point stays at z = 0 and the matrix stays invertible, exactly as clipLocalToCanvas
    // arranges — the overlay maps the pointer back through the inverse of this.
    m.setRow(2, QVector4D(0.f, 0.f, 1.f, 0.f));
    return m;
}

} // namespace drift
