#include "ModelClipTransform.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

struct CameraSetup
{
    QMatrix4x4 model;
    QMatrix4x4 view;
    QMatrix4x4 projection;
    QMatrix4x4 ndcOffset;
    double refScale = 1.0;
    double radius = 0.5;
    bool ortho = false;
};

CameraSetup setupCamera(const ModelClipParams &params, const QVector3D &aabbMin,
                        const QVector3D &aabbMax, double aspect)
{
    CameraSetup c;
    const QVector3D extent = aabbMax - aabbMin;
    const double maxExtent =
        std::max({double(extent.x()), double(extent.y()), double(extent.z()), 1e-6});
    c.refScale = 1.0 / maxExtent;
    // Bounding-sphere radius after refScale: half the box diagonal, at most sqrt(3)/2.
    c.radius = std::max(1e-3, 0.5 * double(extent.length()) * c.refScale);

    // Model: recentre (the loader already centres, but a rig's rest AABB may not be), spin,
    // then normalise the largest axis to one unit.
    //
    // Rotations are intrinsic — about the model's own axes, X then Y then Z, each later one
    // following the earlier: rot = Rx · Ry · Rz, so Y spins about the model's up axis as tilted
    // by X, and Z rolls about the model's forward axis after both. That is what lets a keyframed
    // rotY spin a tilted globe about its own axis instead of wobbling it about the world's.
    const QVector3D centre = (aabbMin + aabbMax) * 0.5f;
    QMatrix4x4 rot;
    rot.rotate(float(params.rotX), 1.f, 0.f, 0.f);
    rot.rotate(float(params.rotY), 0.f, 1.f, 0.f);
    rot.rotate(float(params.rotZ), 0.f, 0.f, 1.f);
    QMatrix4x4 recentre;
    recentre.translate(-centre);
    QMatrix4x4 norm;
    norm.scale(float(c.refScale));
    c.model = rot * norm * recentre;

    const double scale = std::max(0.01, params.scale);
    double t = std::clamp(params.depth, 0.0, 1.0); // vertical half-tangent; 1 = 90° FOV
    const double r = c.radius;

    if (t < 1e-3) {
        c.ortho = true;
        c.view.setToIdentity();
        c.projection.setToIdentity();
        c.projection(0, 0) = float(2.0 * scale * aspect);
        c.projection(1, 1) = float(2.0 * scale);
        c.projection(2, 2) = float(-1.0 / (8.0 * r));
    } else {
        // Size compensation: a unit length at the centre plane must span 2*scale NDC.
        double d = 1.0 / (2.0 * scale * t);
        if (d < 1.5 * r) {
            d = 1.5 * r;
            t = 1.0 / (2.0 * scale * d);
        }
        c.view.setToIdentity();
        c.view.translate(0.f, 0.f, float(-d));
        const double n = std::max(d - 4.0 * r, 0.05 * d);
        const double f = d + 4.0 * r;
        QMatrix4x4 p;
        p.fill(0.f);
        p(0, 0) = float(aspect / t);
        p(1, 1) = float(1.0 / t);
        p(2, 2) = float(-(f + n) / (f - n));
        p(2, 3) = float(-2.0 * f * n / (f - n));
        p(3, 2) = -1.f;
        c.projection = p;
    }

    // Clip-space translate: adds ox*w so it survives the divide and moves the whole projected
    // image like a sticker rather than moving the object past a fixed camera.
    c.ndcOffset.setToIdentity();
    c.ndcOffset(0, 3) = float(2.0 * params.centreX - 1.0);
    c.ndcOffset(1, 3) = float(1.0 - 2.0 * params.centreY);
    return c;
}

} // namespace

ModelClipParams modelClipParamsFromSource(const Model3dSource &source, double centreX,
                                          double centreY)
{
    ModelClipParams p;
    p.scale = source.scale;
    p.depth = source.depth;
    p.rotX = source.rotX;
    p.rotY = source.rotY;
    p.rotZ = source.rotZ;
    p.centreX = centreX;
    p.centreY = centreY;
    p.lightYaw = source.lightYaw;
    p.lightPitch = source.lightPitch;
    p.lightIntensity = source.lightIntensity;
    p.ambient = source.ambient;
    return p;
}

ModelClipCamera modelClipCamera(const ModelClipParams &params, const QVector3D &aabbMin,
                                const QVector3D &aabbMax, double aspect)
{
    const CameraSetup c = setupCamera(params, aabbMin, aabbMax, aspect);
    ModelClipCamera out;
    out.modelView = c.view * c.model;
    out.mvp = c.ndcOffset * c.projection * out.modelView;
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            out.normalMatrix(r, col) = out.modelView(r, col);
    return out;
}

QRectF modelClipScreenRect(const ModelClipParams &params, const QVector3D &aabbMin,
                           const QVector3D &aabbMax, double aspect)
{
    const ModelClipCamera cam = modelClipCamera(params, aabbMin, aabbMax, aspect);
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (int i = 0; i < 8; ++i) {
        const QVector4D corner((i & 1) ? aabbMax.x() : aabbMin.x(),
                               (i & 2) ? aabbMax.y() : aabbMin.y(),
                               (i & 4) ? aabbMax.z() : aabbMin.z(), 1.f);
        const QVector4D clip = cam.mvp * corner;
        const double w = std::max(1e-4, double(clip.w()));
        const double xFrac = (double(clip.x()) / w + 1.0) * 0.5;
        const double yFrac = (1.0 - double(clip.y()) / w) * 0.5;
        minX = std::min(minX, xFrac);
        maxX = std::max(maxX, xFrac);
        minY = std::min(minY, yFrac);
        maxY = std::max(maxY, yFrac);
    }
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

} // namespace drift
