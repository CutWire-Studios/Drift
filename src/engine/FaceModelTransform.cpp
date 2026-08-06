#include "FaceModelTransform.h"

#include <cmath>

namespace drift {
namespace {

void poseBasis(const FaceAnchors &face, double right[3], double up[3], double fwd[3])
{
    const double x = face.poseQx, y = face.poseQy, z = face.poseQz, w = face.poseQw;
    right[0] = 1 - 2 * (y * y + z * z);
    right[1] = 2 * (x * y + z * w);
    right[2] = 2 * (x * z - y * w);
    up[0] = 2 * (x * y - z * w);
    up[1] = 1 - 2 * (x * x + z * z);
    up[2] = 2 * (y * z + x * w);
    fwd[0] = 2 * (x * z + y * w);
    fwd[1] = 2 * (y * z - x * w);
    fwd[2] = 1 - 2 * (x * x + y * y);
}

// Width-normalized → NDC. y divides by aspect because lengths live in width-normalized space
// while the FBO maps the full frame to [-1,1]. No Y flip: FBO v=0 is the top of the image and
// face uv is top-left origin, so head "up" is already −y in uv and the pose basis encodes that.
//
// Z is negated so +forward (toward the viewer, per Phase-1 pose) maps to *nearer* in GL's
// NDC depth (smaller z wins GL_LESS). Without the negation, glasses land behind the head proxy
// in the depth buffer and glTF's CCW fronts are wound as backs.
QMatrix4x4 wnToNdc(double aspect)
{
    const double a = aspect > 1e-6 ? aspect : 1.0;
    QMatrix4x4 m;
    m.setToIdentity();
    m(0, 0) = 2.0f;
    m(0, 3) = -1.0f;
    m(1, 1) = float(2.0 / a);
    m(1, 3) = -1.0f;
    m(2, 2) = -0.25f;
    m(3, 3) = 1.0f;
    return m;
}

QMatrix4x4 headBasisAt(const FaceAnchors &face, double aspect, double scaleMul = 1.0)
{
    double right[3], up[3], fwd[3];
    if (face.hasPose) {
        poseBasis(face, right, up, fwd);
    } else {
        // Identity in image space: right = +x, up = −y (toward forehead / smaller uv.y),
        // forward = +z (toward camera in the Phase-1 mesh convention).
        right[0] = 1;
        right[1] = 0;
        right[2] = 0;
        up[0] = 0;
        up[1] = -1;
        up[2] = 0;
        fwd[0] = 0;
        fwd[1] = 0;
        fwd[2] = 1;
    }

    const double rx = face.faceRx > 1e-6
                          ? face.faceRx
                          : (face.poseScale > 1e-6 ? face.poseScale * 0.5 : 0.15);
    const double s = 2.0 * rx * scaleMul;

    // pose origin is width-normalized; faceCenter is uv — convert uv.y → wn via *aspect so
    // wnToNdc's /aspect lands the pivot on the face.
    const double cx = face.hasPose ? face.poseOx : face.faceCenter.x();
    const double cy = face.hasPose ? face.poseOy : (face.faceCenter.y() * aspect);
    const double cz = face.hasPose ? face.poseOz : 0.0;

    QMatrix4x4 m;
    m.setToIdentity();
    m(0, 0) = float(right[0] * s);
    m(1, 0) = float(right[1] * s);
    m(2, 0) = float(right[2] * s);
    m(0, 1) = float(up[0] * s);
    m(1, 1) = float(up[1] * s);
    m(2, 1) = float(up[2] * s);
    m(0, 2) = float(fwd[0] * s);
    m(1, 2) = float(fwd[1] * s);
    m(2, 2) = float(fwd[2] * s);
    m(0, 3) = float(cx);
    m(1, 3) = float(cy);
    m(2, 3) = float(cz);
    return m;
}

QMatrix4x4 userTransform(const FaceModelParams &params)
{
    QMatrix4x4 t;
    t.setToIdentity();
    t.translate(float(params.offsetX), float(params.offsetY), float(params.offsetZ));

    QMatrix4x4 r;
    r.setToIdentity();
    r.rotate(float(params.rotZ), 0.f, 0.f, 1.f);
    r.rotate(float(params.rotY), 0.f, 1.f, 0.f);
    r.rotate(float(params.rotX), 1.f, 0.f, 0.f);

    QMatrix4x4 s;
    s.setToIdentity();
    const float sc = float(params.scale);
    s.scale(sc, sc, sc);

    return t * r * s;
}

} // namespace

FaceModelParams faceModelParamsFromMap(const QMap<QString, QVariant> &parameters)
{
    FaceModelParams p;
    p.modelPath = parameters.value(QStringLiteral("model")).toString();
    p.scale = parameters.value(QStringLiteral("scale"), 1.0).toDouble();
    p.offsetX = parameters.value(QStringLiteral("offsetX"), 0.0).toDouble();
    p.offsetY = parameters.value(QStringLiteral("offsetY"), 0.0).toDouble();
    p.offsetZ = parameters.value(QStringLiteral("offsetZ"), 0.0).toDouble();
    p.rotX = parameters.value(QStringLiteral("rotX"), 0.0).toDouble();
    p.rotY = parameters.value(QStringLiteral("rotY"), 0.0).toDouble();
    p.rotZ = parameters.value(QStringLiteral("rotZ"), 0.0).toDouble();

    if (parameters.contains(QStringLiteral("occlusion"))) {
        const QVariant v = parameters.value(QStringLiteral("occlusion"));
        p.occlusion = (v.typeId() == QMetaType::Bool) ? v.toBool() : (v.toDouble() > 0.5);
    }
    p.occlusionDepth = parameters.value(QStringLiteral("occlusionDepth"), 0.0).toDouble();
    p.occlusionSize = parameters.value(QStringLiteral("occlusionSize"), 1.0).toDouble();
    p.occlusionOffset = parameters.value(QStringLiteral("occlusionOffset"), 0.0).toDouble();
    p.lightYaw = parameters.value(QStringLiteral("lightYaw"), 30.0).toDouble();
    p.lightPitch = parameters.value(QStringLiteral("lightPitch"), 20.0).toDouble();
    p.lightIntensity = parameters.value(QStringLiteral("lightIntensity"), 1.0).toDouble();
    p.ambient = parameters.value(QStringLiteral("ambient"), 0.35).toDouble();
    p.faceIndex = int(parameters.value(QStringLiteral("faceIndex"), 0.0).toDouble());
    return p;
}

QMatrix4x4 faceModelMvp(const FaceAnchors &face, const FaceModelParams &params, double aspect)
{
    return wnToNdc(aspect) * headBasisAt(face, aspect) * userTransform(params);
}

QMatrix4x4 faceHeadProxyMvp(const FaceAnchors &face, const FaceModelParams &params, double aspect)
{
    // Unit sphere → ellipsoid whose half-axes match the face oval (rx, ry, rx*kz) in head-width
    // units. headBasisAt scales by 2·faceRx, so a half-axis of 0.5 here is one faceRx in wn.
    //
    // Transform order is T·S (Qt: translate, then scale) so the translation is in head-width
    // units after the sphere is sized — NOT scaled by the half-axes. Push the centre back by
    // exactly the Z half-axis so the front of the ellipsoid sits on the eye plane (z=0); props
    // and markers with z>0 stay visible. occlusionDepth adds extra push (head-widths).
    constexpr double kZ = 1.15;
    const double ryOverRx = (face.faceRx > 1e-6) ? (face.faceRy / face.faceRx) : 1.2;
    const double half = 0.5 * params.occlusionSize;
    const double halfZ = half * kZ;

    QMatrix4x4 squash;
    squash.setToIdentity();
    squash.translate(0.f, float(params.occlusionOffset),
                     float(-(halfZ + params.occlusionDepth)));
    squash.scale(float(half), float(half * ryOverRx), float(halfZ));

    return wnToNdc(aspect) * headBasisAt(face, aspect) * squash;
}

} // namespace drift
