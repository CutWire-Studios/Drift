#include "engine/render3d/scene/EditorView3d.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

// Straight down is allowed: the screen's right comes from the yaw alone, so the view stays stable
// at the poles (Top and Bottom) and the turntable stops there rather than flipping over.
constexpr double kMaxPitch = 90.0;

// Exact for multiples of a quarter turn, so the preset views look along an axis to the last bit.
void sinCos(double degrees, double *s, double *c)
{
    const double quarters = degrees / 90.0;
    if (std::abs(quarters - std::round(quarters)) < 1e-9) {
        static const double sines[4] = {0.0, 1.0, 0.0, -1.0};
        const int q = ((int(std::lround(quarters)) % 4) + 4) % 4;
        *s = sines[q];
        *c = sines[(q + 1) % 4];
        return;
    }
    const double r = qDegreesToRadians(degrees);
    *s = std::sin(r);
    *c = std::cos(r);
}

// Unit vector from the target toward the eye, in world space.
QVector3D eyeOffset(const EditorView3d &view)
{
    double sy, cy, sp, cp;
    sinCos(view.yaw, &sy, &cy);
    sinCos(std::clamp(view.pitch, -kMaxPitch, kMaxPitch), &sp, &cp);
    // + pitch lifts the eye above the scene, and above is world -y.
    return QVector3D(float(sy * cp), float(-sp), float(-cy * cp));
}

// World is y-down, z-away; GL's lookAt and perspective want y-up, z-toward-the-viewer. Turning half
// a turn about x gets between them and is its own inverse.
QVector3D flipY(const QVector3D &v)
{
    return QVector3D(v.x(), -v.y(), -v.z());
}

struct Basis
{
    QVector3D right; // screen right, world space
    QVector3D up;    // screen up, world space
};

// In GL space (y up, z toward the viewer).
struct GlBasis
{
    QVector3D right;
    QVector3D up;
    QVector3D forward;
};

GlBasis glBasisOf(const EditorView3d &view)
{
    double sy, cy;
    sinCos(view.yaw, &sy, &cy);
    GlBasis b;
    b.forward = flipY(-eyeOffset(view));
    b.right = QVector3D(float(cy), 0.f, float(-sy));
    b.up = QVector3D::crossProduct(b.right, b.forward);
    return b;
}

Basis basisOf(const EditorView3d &view)
{
    const GlBasis b = glBasisOf(view);
    return {flipY(b.right), flipY(b.up)};
}

} // namespace

EditorView3d EditorView3d::overview(const QSizeF &canvas)
{
    EditorView3d view;
    view.target = QVector3D(float(canvas.width() * 0.5), float(canvas.height() * 0.5), 0.f);
    view.yaw = 35.0;
    view.pitch = 20.0;
    view.distance = canvas.height() * 0.5 / std::tan(qDegreesToRadians(view.fovY * 0.5)) * 2.2;
    return view;
}

EditorViewAxes editorViewAxes(const EditorView3d &view)
{
    const GlBasis b = glBasisOf(view);
    return {flipY(b.right), flipY(-b.up), flipY(b.forward)};
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
    return view.target + eyeOffset(view) * float(view.distance);
}

QMatrix4x4 editorViewProjection(const EditorView3d &view, const EditorFrame &frame, const QSizeF &project,
                                const SceneCamera3d &sceneCamera)
{
    if (view.lookThrough) {
        QMatrix4x4 place;
        place.translate(float(frame.offset.x()), float(frame.offset.y()));
        return place * cameraViewProjection(sceneCamera, project);
    }

    const QSizeF &canvas = frame.size;
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
    if (view.ortho) {
        // The same height is in view at the target as in perspective, and the eye is no limit:
        // things behind it still draw.
        const double halfH = view.distance * std::tan(qDegreesToRadians(view.fovY * 0.5));
        const double halfW = halfH * width / height;
        projection.ortho(float(-halfW), float(halfW), float(-halfH), float(halfH), float(-farPlane * 0.5),
                         float(farPlane));
    } else {
        projection.perspective(float(view.fovY), float(width / height), float(nearPlane), float(farPlane));
    }

    const GlBasis b = glBasisOf(view);
    const QVector3D eye = flipY(editorViewEye(view));
    QMatrix4x4 lookAt;
    lookAt.setRow(0, QVector4D(b.right, -QVector3D::dotProduct(b.right, eye)));
    lookAt.setRow(1, QVector4D(b.up, -QVector3D::dotProduct(b.up, eye)));
    lookAt.setRow(2, QVector4D(-b.forward, QVector3D::dotProduct(b.forward, eye)));
    lookAt.setRow(3, QVector4D(0.f, 0.f, 0.f, 1.f));

    QMatrix4x4 worldToGl;
    worldToGl.scale(1.f, -1.f, -1.f);
    return toCanvas * projection * lookAt * worldToGl;
}

QMatrix4x4 editorViewProjection(const EditorView3d &view, const QSizeF &canvas, const SceneCamera3d &sceneCamera)
{
    return editorViewProjection(view, EditorFrame{canvas, {}}, canvas, sceneCamera);
}

EditorView3d orbitEditorView(const EditorView3d &view, double dxDeg, double dyDeg)
{
    EditorView3d out = view;
    out.yaw = std::remainder(view.yaw - dxDeg, 360.0);
    out.pitch = std::clamp(view.pitch + dyDeg, -kMaxPitch, kMaxPitch);
    out.lookThrough = false;
    return out;
}

EditorView3d orbitEditorViewAround(const EditorView3d &view, double dxDeg, double dyDeg, const QVector3D &pivot)
{
    EditorView3d out = orbitEditorView(view, dxDeg, dyDeg);
    // The whole rig turns rigidly about the pivot, so the pivot keeps its place on screen.
    const GlBasis before = glBasisOf(view);
    const GlBasis after = glBasisOf(out);
    const QVector3D offset = flipY(view.target - pivot);
    const QVector3D turned = QVector3D::dotProduct(offset, before.right) * after.right
                             + QVector3D::dotProduct(offset, before.up) * after.up
                             + QVector3D::dotProduct(offset, before.forward) * after.forward;
    out.target = pivot + flipY(turned);
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

EditorView3d dollyEditorViewToward(const EditorView3d &view, double steps, double ndcX, double ndcY, double aspect)
{
    EditorView3d out = dollyEditorView(view, steps);
    const double k = out.distance / view.distance;
    // The point of the focal plane under the pointer stays under it: the target slides toward it.
    const double halfH = view.distance * std::tan(qDegreesToRadians(view.fovY * 0.5));
    const Basis basis = basisOf(view);
    const QVector3D under = basis.right * float(ndcX * halfH * aspect) + basis.up * float(ndcY * halfH);
    out.target = view.target + under * float(1.0 - k);
    return out;
}

EditorView3d presetEditorView(const EditorView3d &view, const QString &axis)
{
    EditorView3d out = view;
    out.lookThrough = false;
    out.yaw = 0.0;
    out.pitch = 0.0;
    if (axis == QLatin1String("back"))
        out.yaw = 180.0;
    else if (axis == QLatin1String("right"))
        out.yaw = 90.0;
    else if (axis == QLatin1String("left"))
        out.yaw = -90.0;
    else if (axis == QLatin1String("top"))
        out.pitch = 90.0;
    else if (axis == QLatin1String("bottom"))
        out.pitch = -90.0;
    return out;
}

EditorView3d editorViewOfCamera(const SceneCamera3d &camera)
{
    const QVector3D eye = sceneCameraEye(camera);
    const QVector3D ahead = sceneCameraRotation(camera).mapVector(QVector3D(0.f, 0.f, 1.f)).normalized();
    EditorView3d view;
    view.distance = std::max(1.0, camera.zoom);
    if (camera.aimAtTarget) {
        const QVector3D toTarget = QVector3D(float(camera.targetX), float(camera.targetY), float(camera.targetZ)) - eye;
        if (toTarget.length() > 1.0f)
            view.distance = toTarget.length();
    }
    view.pitch = qRadiansToDegrees(std::asin(std::clamp(double(ahead.y()), -1.0, 1.0)));
    view.yaw = qRadiansToDegrees(std::atan2(-double(ahead.x()), double(ahead.z())));
    view.target = eye + ahead * float(view.distance);
    return view;
}

EditorView3d flyEditorView(const EditorView3d &view, double turnRightDeg, double turnUpDeg,
                           double forward, double right, double up)
{
    const QVector3D eye = editorViewEye(view);
    EditorView3d out = view;
    out.yaw = std::remainder(view.yaw - turnRightDeg, 360.0);
    // Looking up lowers the eye relative to the target, and + pitch raises it.
    out.pitch = std::clamp(std::clamp(view.pitch, -kMaxPitch, kMaxPitch) - turnUpDeg, -kMaxPitch, kMaxPitch);
    out.lookThrough = false;
    out.target = QVector3D();
    const QVector3D offset = editorViewEye(out);
    out.target = eye - offset;
    const QVector3D ahead = -offset.normalized();
    // World up is -y.
    out.target += ahead * float(forward) + basisOf(out).right * float(right) + QVector3D(0.f, -float(up), 0.f);
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
