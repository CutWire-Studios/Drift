#include "engine/EditorView3d.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

constexpr double kMaxPitch = 89.0;

// World is y-down and so left-handed; GL's lookAt and perspective want y-up and right-handed. The
// y flip between them is its own inverse.
QVector3D flipY(const QVector3D &v)
{
    return QVector3D(v.x(), -v.y(), v.z());
}

struct Basis
{
    QVector3D right; // screen right, world space
    QVector3D up;    // screen up, world space
};

Basis basisOf(const EditorView3d &view)
{
    const QVector3D eye = flipY(editorViewEye(view));
    const QVector3D target = flipY(view.target);
    const QVector3D forward = (target - eye).normalized();
    QVector3D right = QVector3D::crossProduct(forward, QVector3D(0.f, 1.f, 0.f));
    if (right.lengthSquared() < 1e-8f)
        right = QVector3D(1.f, 0.f, 0.f);
    right.normalize();
    const QVector3D up = QVector3D::crossProduct(right, forward);
    return {flipY(right), flipY(up)};
}

} // namespace

EditorView3d EditorView3d::overview(const QSizeF &canvas)
{
    EditorView3d view;
    view.yaw = 35.0;
    view.pitch = 20.0;
    view.distance = canvas.height() * 0.5 / std::tan(qDegreesToRadians(view.fovY * 0.5)) * 2.2;
    return view;
}

EditorFrame editorFrame(const QSizeF &project, const QSizeF &viewport)
{
    if (viewport.isEmpty() || project.isEmpty())
        return {project, {}};
    const double aspect = viewport.width() / viewport.height();
    QSizeF size = project;
    if (aspect >= project.width() / project.height())
        size.setWidth(project.height() * aspect);
    else
        size.setHeight(project.width() / aspect);
    return {size, QPointF((size.width() - project.width()) / 2.0, (size.height() - project.height()) / 2.0)};
}

QVector3D editorViewEye(const EditorView3d &view)
{
    const double yaw = qDegreesToRadians(view.yaw);
    const double pitch = qDegreesToRadians(std::clamp(view.pitch, -kMaxPitch, kMaxPitch));
    // + pitch lifts the eye above the scene, and above is world -y.
    const QVector3D offset(float(std::sin(yaw) * std::cos(pitch)), float(-std::sin(pitch)),
                           float(std::cos(yaw) * std::cos(pitch)));
    return view.target + offset * float(view.distance);
}

QMatrix4x4 editorViewProjection(const EditorView3d &view, const QSizeF &canvas,
                                const SceneCamera3d &sceneCamera)
{
    if (view.lookThrough)
        return cameraViewProjection(sceneCamera, canvas);

    const double width = std::max(1.0, canvas.width());
    const double height = std::max(1.0, canvas.height());
    // NDC (y up) -> canvas px (y down, top-left origin), applied to homogeneous coordinates.
    QMatrix4x4 toCanvas;
    toCanvas.setRow(0, QVector4D(float(width * 0.5), 0.f, 0.f, float(width * 0.5)));
    toCanvas.setRow(1, QVector4D(0.f, float(-height * 0.5), 0.f, float(height * 0.5)));
    toCanvas.setRow(2, QVector4D(0.f, 0.f, 1.f, 0.f));
    toCanvas.setRow(3, QVector4D(0.f, 0.f, 0.f, 1.f));

    const double nearPlane = std::max(1.0, view.distance * 0.005);
    const double farPlane = view.distance * 50.0 + 50000.0;
    QMatrix4x4 projection;
    projection.perspective(float(view.fovY), float(width / height), float(nearPlane), float(farPlane));

    QMatrix4x4 lookAt;
    lookAt.lookAt(flipY(editorViewEye(view)), flipY(view.target), QVector3D(0.f, 1.f, 0.f));

    QMatrix4x4 worldToGl;
    worldToGl.scale(1.f, -1.f, 1.f);
    return toCanvas * projection * lookAt * worldToGl;
}

EditorView3d orbitEditorView(const EditorView3d &view, double dxDeg, double dyDeg)
{
    EditorView3d out = view;
    out.yaw = std::remainder(view.yaw - dxDeg, 360.0);
    out.pitch = std::clamp(view.pitch + dyDeg, -kMaxPitch, kMaxPitch);
    out.lookThrough = false;
    return out;
}

EditorView3d panEditorView(const EditorView3d &view, double dxPx, double dyPx, const QSizeF &canvas)
{
    EditorView3d out = view;
    const double perPx = 2.0 * view.distance * std::tan(qDegreesToRadians(view.fovY * 0.5))
                         / std::max(1.0, canvas.height());
    const Basis basis = basisOf(view);
    out.target = view.target - basis.right * float(dxPx * perPx) + basis.up * float(dyPx * perPx);
    out.lookThrough = false;
    return out;
}

EditorView3d dollyEditorView(const EditorView3d &view, double steps)
{
    EditorView3d out = view;
    out.distance = std::clamp(view.distance * std::pow(0.85, steps), 10.0, 1e6);
    out.lookThrough = false;
    return out;
}

EditorView3d frameEditorView(const EditorView3d &view, const QVector3D &centre, double radius)
{
    EditorView3d out = view;
    out.target = centre;
    out.distance = std::max(10.0, radius / std::sin(qDegreesToRadians(view.fovY * 0.5)) * 1.1);
    out.lookThrough = false;
    return out;
}

} // namespace drift
