#include "engine/ClipTransform3d.h"

#include <QVector4D>

#include <algorithm>

namespace drift {

namespace {

// Nearest the eye a point may get, as a fraction of the perspective distance.
constexpr float kNearFraction = 0.02f;

// Rect centre → canvas, in pixel space relative to the canvas centre, then the perspective divide
// about that centre. Rotations are intrinsic, X then Y then the in-plane spin (the same order as
// model clips): the spin turns the clip within its tilted plane.
//
// Pixel space is y-down with z toward the viewer, so QMatrix4x4's right-handed rotations read as
// the ClipPose3d conventions: +X tips the top back, +Y swings the right edge back, +Z (the existing
// rotation) is clockwise on screen.
QMatrix4x4 placeCentre(const QRectF &rect, double rotation, const ClipPose3d &pose,
                       const QSizeF &canvas)
{
    const double d = std::max(1.0, pose.perspective);
    QMatrix4x4 projection;
    projection.translate(float(canvas.width() * 0.5), float(canvas.height() * 0.5));
    QMatrix4x4 persp;
    persp.setRow(2, QVector4D(0.f, 0.f, float(-1.0 / d), 1.f - 2.f * kNearFraction));
    persp.setRow(3, QVector4D(0.f, 0.f, float(-1.0 / d), 1.f));
    projection *= persp;

    const QPointF centre = rect.center();
    QMatrix4x4 m = projection;
    m.translate(float(centre.x() - canvas.width() * 0.5), float(centre.y() - canvas.height() * 0.5),
                float(pose.positionZ));
    m.rotate(float(pose.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(pose.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(rotation), 0.f, 0.f, 1.f);
    return m;
}

} // namespace

QMatrix4x4 clipQuadToCanvas(const QRectF &rect, double rotation, bool flipH, bool flipV,
                            const ClipPose3d &pose, const QSizeF &canvas)
{
    QMatrix4x4 m = placeCentre(rect, rotation, pose, canvas);
    m.scale(float(rect.width() * 0.5), float(rect.height() * 0.5));
    m.scale(flipH ? -1.f : 1.f, flipV ? -1.f : 1.f);
    return m;
}

QMatrix4x4 clipLocalToCanvas(const QRectF &rect, double rotation, const ClipPose3d &pose,
                             const QSizeF &canvas)
{
    QMatrix4x4 m = placeCentre(rect, rotation, pose, canvas);
    m.translate(float(-rect.width() * 0.5), float(-rect.height() * 0.5));
    m.setRow(2, QVector4D(0.f, 0.f, 1.f, 0.f));
    return m;
}

QPolygonF projectedClipQuad(const QRectF &rect, double rotation, const ClipPose3d &pose,
                            const QSizeF &canvas)
{
    const QMatrix4x4 m = clipQuadToCanvas(rect, rotation, false, false, pose, canvas);
    QPolygonF quad;
    for (const QPointF corner : {QPointF(-1, -1), QPointF(1, -1), QPointF(1, 1), QPointF(-1, 1)}) {
        const QVector4D p = m.map(QVector4D(float(corner.x()), float(corner.y()), 0.f, 1.f));
        if (p.w() <= kNearFraction)
            return {};
        quad << QPointF(p.x() / p.w(), p.y() / p.w());
    }
    return quad;
}

} // namespace drift
