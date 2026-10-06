#include "ModelClipTransform.h"

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
    p.lightYaw = source.lightYaw;
    p.lightPitch = source.lightPitch;
    p.lightIntensity = source.lightIntensity;
    p.ambient = source.ambient;
    return p;
}

QMatrix4x4 modelClipWorld(const QRectF &rect, double rotation, const ClipPose3d &pose,
                          const QVector3D &aabbMin, const QVector3D &aabbMax, const QSizeF &canvas)
{
    const QVector3D extent = flooredExtent(aabbMin, aabbMax);
    const double sx = rect.width() / double(extent.x());
    const double sy = rect.height() / double(extent.y());
    const QPointF centre = rect.center();
    QMatrix4x4 m;
    // The same placement clipQuadToWorld gives a flat clip, so the overlay's box, the gizmo and the
    // model all turn about one point.
    m.translate(float(centre.x() - canvas.width() * 0.5), float(centre.y() - canvas.height() * 0.5),
                float(pose.positionZ));
    m.rotate(float(pose.rotationX), 1.f, 0.f, 0.f);
    m.rotate(float(pose.rotationY), 0.f, 1.f, 0.f);
    m.rotate(float(rotation), 0.f, 0.f, 1.f);
    m.scale(float(sx), float(sy), float(std::sqrt(sx * sy)));
    // glTF is y-up; world is y-down. Flipping y keeps the model the right way up on screen.
    m.scale(1.f, -1.f, 1.f);
    m.translate(-(aabbMin + aabbMax) * 0.5f);
    return m;
}

QMatrix3x3 modelClipNormalMatrix(const QMatrix4x4 &world)
{
    QMatrix3x3 n = world.normalMatrix();
    for (int c = 0; c < 3; ++c)
        n(1, c) = -n(1, c);
    return n;
}

QSizeF modelClipFaceSize(const QVector3D &aabbMin, const QVector3D &aabbMax, double span)
{
    const QVector3D extent = flooredExtent(aabbMin, aabbMax);
    const double largest = std::max({double(extent.x()), double(extent.y()), double(extent.z())});
    return QSizeF(span * extent.x() / largest, span * extent.y() / largest);
}

} // namespace drift
