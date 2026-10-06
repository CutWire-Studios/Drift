#include "engine/EditorOverlay.h"

#include <QVector4D>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

const QColor kGrid(255, 255, 255, 18);
const QColor kAxisX(235, 80, 80, 140);
const QColor kAxisZ(80, 130, 235, 140);
const QColor kStage(255, 255, 255, 115);
const QColor kCamera(225, 225, 225, 220);
const QColor kCameraRays(225, 225, 225, 70);
const QColor kSelected(255, 160, 50, 235);
const QColor kProjection(255, 160, 50, 170);
const QColor kModelBox(80, 160, 255, 200);

void addLine(QList<EditorLine> &out, const QVector3D &a, const QVector3D &b, const QColor &color,
             float width, bool under = false)
{
    out.append(EditorLine{a, b, color, width, under});
}

void addLoop(QList<EditorLine> &out, const QList<QVector3D> &points, const QColor &color, float width)
{
    for (int i = 0; i < points.size(); ++i)
        addLine(out, points.at(i), points.at((i + 1) % points.size()), color, width);
}

// A grid step near a tenth of the canvas height, rounded to 1, 2 or 5 times a power of ten.
double gridStep(double height)
{
    const double raw = std::max(1.0, height / 10.0);
    const double base = std::pow(10.0, std::floor(std::log10(raw)));
    for (const double m : {1.0, 2.0, 5.0, 10.0}) {
        if (base * m >= raw)
            return base * m;
    }
    return base * 10.0;
}

} // namespace

QList<EditorLine> buildEditorOverlay(const EditorOverlayInput &input)
{
    QList<EditorLine> out;
    const double w = input.canvas.width();
    const double h = input.canvas.height();
    if (w <= 0.0 || h <= 0.0)
        return out;

    // The floor: the plane level with the canvas's bottom edge, reaching well past it either way.
    const float floorY = float(h * 0.5);
    const double step = gridStep(h);
    const int count = int(std::ceil(std::max(w, h) * 2.0 / step));
    const float reach = float(count * step);
    for (int i = -count; i <= count; ++i) {
        const float at = float(i * step);
        addLine(out, {at, floorY, -reach}, {at, floorY, reach}, i == 0 ? kAxisZ : kGrid,
                i == 0 ? 1.5f : 1.f, true);
        addLine(out, {-reach, floorY, at}, {reach, floorY, at}, i == 0 ? kAxisX : kGrid,
                i == 0 ? 1.5f : 1.f, true);
    }

    // The stage: what an unmoved camera frames, at z = 0.
    const float hw = float(w * 0.5);
    const float hh = float(h * 0.5);
    addLoop(out, {{-hw, -hh, 0.f}, {hw, -hh, 0.f}, {hw, hh, 0.f}, {-hw, hh, 0.f}}, kStage, 1.5f);

    if (input.cameraActive) {
        const QMatrix4x4 c = sceneCameraWorld(input.camera);
        const float d = float(std::max(1.0, input.camera.perspective));
        const QVector3D eye = c.map(QVector3D(0.f, 0.f, d));
        // The frame it sees: its focal plane, which an unmoved camera lays exactly on the stage.
        const QList<QVector3D> focal{c.map({-hw, -hh, 0.f}), c.map({hw, -hh, 0.f}),
                                     c.map({hw, hh, 0.f}), c.map({-hw, hh, 0.f})};
        const QColor body = input.cameraSelected ? kSelected : kCamera;
        for (const QVector3D &corner : focal)
            addLine(out, eye, corner, kCameraRays, 1.f);
        addLoop(out, focal, body, input.cameraSelected ? 2.f : 1.5f);

        // A Blender-style body: a short pyramid from the eye, with a triangle marking its top.
        const float t = 0.15f;
        const float bz = d * (1.f - t);
        const QList<QVector3D> near{c.map({-hw * t, -hh * t, bz}), c.map({hw * t, -hh * t, bz}),
                                    c.map({hw * t, hh * t, bz}), c.map({-hw * t, hh * t, bz})};
        for (const QVector3D &corner : near)
            addLine(out, eye, corner, body, 2.f);
        addLoop(out, near, body, 2.f);
        addLoop(out,
                {c.map({-hw * t * 0.5f, -hh * t * 1.1f, bz}), c.map({hw * t * 0.5f, -hh * t * 1.1f, bz}),
                 c.map({0.f, -hh * t * 1.6f, bz})},
                body, 2.f);

        // Where the camera's frame lands on the selected clip's plane.
        if (input.hasSelectedQuad) {
            QList<QVector3D> hits;
            for (const QVector3D &corner : focal) {
                QVector3D hit;
                if (!rayHitsQuadPlane(eye, corner, input.selectedQuad, &hit))
                    break;
                hits.append(hit);
            }
            if (hits.size() == 4)
                addLoop(out, hits, kProjection, 1.5f);
        }
    }

    if (input.hasSelectedModel) {
        const QVector3D lo = input.selectedModelMin;
        const QVector3D hi = input.selectedModelMax;
        const auto corner = [&](int i) {
            return input.selectedModel.map(QVector3D((i & 1) ? hi.x() : lo.x(), (i & 2) ? hi.y() : lo.y(),
                                                     (i & 4) ? hi.z() : lo.z()));
        };
        for (int i = 0; i < 8; ++i) {
            for (const int bit : {1, 2, 4}) {
                if (!(i & bit))
                    addLine(out, corner(i), corner(i | bit), kModelBox, 1.5f);
            }
        }
    }
    return out;
}

bool clipEditorSegment(const QMatrix4x4 &viewProj, const QVector3D &a, const QVector3D &b,
                       QPointF *pa, QPointF *pb)
{
    QVector4D ha = viewProj.map(QVector4D(a, 1.f));
    QVector4D hb = viewProj.map(QVector4D(b, 1.f));
    // GL's near plane, z >= -w: whatever the renderer would clip, the guides clip too.
    const float da = ha.z() + ha.w();
    const float db = hb.z() + hb.w();
    if (da < 0.f && db < 0.f)
        return false;
    if (da < 0.f)
        ha = ha + (hb - ha) * (da / (da - db));
    else if (db < 0.f)
        hb = hb + (ha - hb) * (db / (db - da));
    if (ha.w() <= 1e-6f || hb.w() <= 1e-6f)
        return false;
    *pa = QPointF(ha.x() / ha.w(), ha.y() / ha.w());
    *pb = QPointF(hb.x() / hb.w(), hb.y() / hb.w());
    return true;
}

bool rayHitsQuadPlane(const QVector3D &eye, const QVector3D &through, const QMatrix4x4 &quad,
                      QVector3D *hit)
{
    const QVector3D origin = quad.map(QVector3D());
    const QVector3D normal = QVector3D::crossProduct(quad.mapVector(QVector3D(1.f, 0.f, 0.f)),
                                                     quad.mapVector(QVector3D(0.f, 1.f, 0.f)));
    const QVector3D dir = through - eye;
    const float denom = QVector3D::dotProduct(normal, dir);
    if (std::abs(denom) < 1e-9f * normal.length() * dir.length())
        return false;
    const float t = QVector3D::dotProduct(normal, origin - eye) / denom;
    if (t <= 0.f)
        return false;
    *hit = eye + dir * t;
    return true;
}

} // namespace drift
