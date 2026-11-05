#include "engine/render3d/scene/ClipTransform3d.h"

#include "core/project/clip/Clip.h"

#include <QVector4D>

#include <algorithm>

namespace drift {

namespace {

constexpr float kNearFraction = float(kClipNearFraction);

// Rotations are intrinsic, X then Y then the in-plane spin (the same order as model clips): the
// spin turns the clip within its tilted plane. World space is right-handed, so QMatrix4x4's
// rotations read as the ClipPose3d conventions: +X tips the top toward the viewer, +Y swings the
// right edge toward the viewer, +Z (the existing rotation) is clockwise on screen.
//
// Rect-local px (from the rect's top-left, z = 0 the layer's plane) into the world: the rotations
// turn about the anchor, which stays at its world position.
QMatrix4x4 placeLocal(const QRectF &rect, double rotation, const ClipPose3d &pose)
{
    const QVector3D anchor = pose.anchorIn(rect);
    QMatrix4x4 m;
    m.translate(pose.pivot(rect));
    m.rotate(float(pose.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(pose.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(rotation), 0.f, 0.f, 1.f);
    m.translate(-anchor);
    return m;
}

// The unit quad's own origin is the rect centre; placeLocal's is the rect corner.
QMatrix4x4 placeQuad(const QRectF &rect, double rotation, const ClipPose3d &pose)
{
    const QVector3D anchor = pose.anchorIn(rect);
    const QVector3D centre(float(rect.width() * 0.5), float(rect.height() * 0.5), 0.f);
    QMatrix4x4 m;
    m.translate(pose.pivot(rect));
    m.rotate(float(pose.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(pose.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(rotation), 0.f, 0.f, 1.f);
    if (anchor != centre)
        m.translate(centre - anchor);
    return m;
}

} // namespace

ClipPose3d clipPoseAt(const Clip &clip, TimeUs relativeUs, double renderScale, double projectHeight)
{
    const auto value = [relativeUs](const KeyframeTrack<double> &track, double fallback) {
        return track.isEmpty() ? fallback : track.evaluateAt(relativeUs);
    };
    ClipPose3d pose;
    pose.rotationX = value(clip.rotationX, 0.0);
    pose.rotationY = value(clip.rotationY, 0.0);
    pose.positionZ = value(clip.positionZ, 0.0) * renderScale;
    pose.perspective = value(clip.perspective, defaultClipPerspective(projectHeight)) * renderScale;
    pose.hasAnchorX = !clip.anchorX.isEmpty();
    pose.hasAnchorY = !clip.anchorY.isEmpty();
    pose.anchorX = value(clip.anchorX, 0.0) * renderScale;
    pose.anchorY = value(clip.anchorY, 0.0) * renderScale;
    pose.anchorZ = value(clip.anchorZ, 0.0) * renderScale;
    return pose;
}

QMatrix4x4 worldToCameraFrame(const QSizeF &canvas)
{
    QMatrix4x4 m;
    m.translate(float(-canvas.width() * 0.5), float(-canvas.height() * 0.5));
    m.scale(1.f, 1.f, -1.f);
    return m;
}

QMatrix4x4 eyeProjection(double perspective, const QSizeF &canvas)
{
    const double d = std::max(1.0, perspective);
    QMatrix4x4 projection;
    projection.translate(float(canvas.width() * 0.5), float(canvas.height() * 0.5));
    QMatrix4x4 persp;
    persp.setRow(2, QVector4D(0.f, 0.f, float(-1.0 / d), 1.f - 2.f * kNearFraction));
    persp.setRow(3, QVector4D(0.f, 0.f, float(-1.0 / d), 1.f));
    projection *= persp;
    return projection;
}

QMatrix4x4 clipEyeView(double perspective, const QSizeF &canvas)
{
    return eyeProjection(perspective, canvas) * worldToCameraFrame(canvas);
}

QMatrix4x4 clipQuadToCanvas(const QRectF &rect, double rotation, bool flipH, bool flipV,
                            const ClipPose3d &pose, const QSizeF &canvas)
{
    return clipEyeView(pose.perspective, canvas) * clipQuadToWorld(rect, rotation, flipH, flipV, pose, canvas);
}

QMatrix4x4 clipQuadToWorld(const QRectF &rect, double rotation, bool flipH, bool flipV,
                           const ClipPose3d &pose, const QSizeF &)
{
    QMatrix4x4 m = placeQuad(rect, rotation, pose);
    m.scale(float(rect.width() * 0.5), float(rect.height() * 0.5));
    m.scale(flipH ? -1.f : 1.f, flipV ? -1.f : 1.f);
    return m;
}

QMatrix4x4 clipLocalToCanvas(const QRectF &rect, double rotation, const ClipPose3d &pose,
                             const QSizeF &canvas)
{
    QMatrix4x4 m = clipEyeView(pose.perspective, canvas) * placeLocal(rect, rotation, pose);
    m.setRow(2, QVector4D(0.f, 0.f, 1.f, 0.f));
    return m;
}

QMatrix4x4 clipLocalToWorld(const QRectF &rect, double rotation, const ClipPose3d &pose,
                            const QSizeF &)
{
    // z is left alone here — the caller flattens it once the camera has been applied, which is what
    // keeps the whole chain invertible.
    return placeLocal(rect, rotation, pose);
}

QMatrix4x4 flatQuadToCanvas(const QRectF &rect, double rotation, bool flipH, bool flipV)
{
    QMatrix4x4 m;
    m.translate(float(rect.center().x()), float(rect.center().y()));
    m.rotate(float(rotation), 0.f, 0.f, 1.f);
    m.scale(float(rect.width() * 0.5), float(rect.height() * 0.5));
    m.scale(flipH ? -1.f : 1.f, flipV ? -1.f : 1.f);
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
