#include "engine/render3d/models/ModelClipTransform.h"

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

// Rest extents with a floor, so a model that is flat along an axis still scales sanely along it.
QVector3D flooredExtent(const QVector3D &aabbMin, const QVector3D &aabbMax)
{
    const QVector3D extent = aabbMax - aabbMin;
    const float largest = std::max({extent.x(), extent.y(), extent.z()});
    // Nothing probed (or a single point): a unit cube, so there is still a box to place.
    if (largest <= 1e-6f)
        return QVector3D(1.f, 1.f, 1.f);
    const float floor = largest * 1e-3f;
    return QVector3D(std::max(extent.x(), floor), std::max(extent.y(), floor),
                     std::max(extent.z(), floor));
}

} // namespace

ModelClipParams modelClipParamsFromSource(const Model3dSource &source)
{
    ModelClipParams p;
    p.lights = source.lights;
    p.ambient = source.ambient;
    p.scale = source.modelScale;
    p.scaleZ = source.modelScaleZ;
    return p;
}

QVector3D ModelLightFrame::toWorld(const QVector3D &light) const
{
    return origin + QVector3D(float(light.x() * width), float(light.y() * height), float(light.z() * height));
}

QVector3D ModelLightFrame::fromWorld(const QVector3D &world) const
{
    const QVector3D d = world - origin;
    return QVector3D(float(d.x() / std::max(1e-6, width)), float(d.y() / std::max(1e-6, height)),
                     float(d.z() / std::max(1e-6, height)));
}

ModelLightFrame modelLightFrame(const QRectF &rect, const ClipPose3d &pose, const QSizeF &)
{
    ModelLightFrame frame;
    frame.origin = QVector3D(float(rect.x()), float(rect.y()), float(pose.positionZ));
    frame.width = rect.width();
    frame.height = rect.height();
    return frame;
}

QMatrix4x4 modelClipWorld(const QRectF &rect, double rotation, const ClipPose3d &pose,
                          const QVector3D &aabbMin, const QVector3D &aabbMax, const QSizeF &,
                          double scale, double scaleZ)
{
    const QVector3D extent = flooredExtent(aabbMin, aabbMax);
    const double sx = rect.width() / double(extent.x());
    const double sy = rect.height() / double(extent.y());
    const QVector3D anchor = pose.anchorIn(rect);
    const QVector3D centre(float(rect.width() * 0.5), float(rect.height() * 0.5), 0.f);
    QMatrix4x4 m;
    // The same placement clipQuadToWorld gives a flat clip, so the overlay's box, the gizmo and the
    // model all turn about one point.
    m.translate(pose.pivot(rect));
    m.rotate(float(pose.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(pose.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(rotation), 0.f, 0.f, 1.f);
    if (scale != 1.0 || scaleZ != 1.0)
        m.scale(float(scale), float(scale), float(scale * scaleZ));
    if (anchor != centre)
        m.translate(centre - anchor);
    // glTF is y-up and z toward the viewer; world is y-down and z away. Flipping both keeps the
    // model the right way up on screen, and is a half turn, so the winding is kept.
    m.scale(float(sx), float(-sy), float(-std::sqrt(sx * sy)));
    m.translate(-(aabbMin + aabbMax) * 0.5f);
    return m;
}

QSizeF modelClipFaceSize(const QVector3D &aabbMin, const QVector3D &aabbMax, double span)
{
    const QVector3D extent = flooredExtent(aabbMin, aabbMax);
    const double largest = std::max({double(extent.x()), double(extent.y()), double(extent.z())});
    return QSizeF(span * extent.x() / largest, span * extent.y() / largest);
}

} // namespace drift
