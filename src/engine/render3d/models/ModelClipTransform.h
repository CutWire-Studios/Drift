#pragma once

#include "core/project/sources/Model3dSource.h"
#include "engine/render3d/scene/ClipTransform3d.h"

#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QRectF>
#include <QSizeF>
#include <QVector3D>

#include <array>

namespace drift {

// Resolved light knobs for one model clip draw.
struct ModelClipParams
{
    std::array<ModelLight, kModelLightCount> lights = defaultModelLights();
    double ambient = 0.35;
    // Uniform size multiplier about the anchor, and an extra multiplier on the depth alone.
    double scale = 1.0;
    double scaleZ = 1.0;
};

// The frame a model clip's lights are placed in (see ModelLight), in world space: the clip's rect
// laid flat at the clip's depth (positionZ), unturned by the clip's rotations and unaffected by
// the anchor and the model scale. A light's z runs in clip heights with + away from the viewer,
// the same way as the world's z.
struct ModelLightFrame
{
    QVector3D origin; // light (0, 0, 0): the rect's top-left corner at positionZ
    double width = 1.0;
    double height = 1.0;

    QVector3D toWorld(const QVector3D &light) const;
    QVector3D fromWorld(const QVector3D &world) const;
    // Where a spot aimed at (aimX, aimY) points.
    QVector3D aimWorld(double aimX, double aimY) const { return toWorld(QVector3D(float(aimX), float(aimY), 0.f)); }
};
ModelLightFrame modelLightFrame(const QRectF &rect, const ClipPose3d &pose, const QSizeF &canvas);

ModelClipParams modelClipParamsFromSource(const Model3dSource &source);

// Model space (the file's own units, glTF axes: +y up, +z toward the viewer) to world space (see
// ClipTransform3d.h: canvas top-left origin, y down, +z away from the viewer), the space every
// clip's quad lives in. The model's rest bounding box is fitted to the clip: it is centred on the
// layer's own centre, its front face is the layout `rect` (at depth `pose.positionZ`), and its
// depth is scaled by sqrt(sx * sy), the geometric mean of the two face scales. `scale` then
// resizes everything about the anchor, and `scaleZ` multiplies the depth alone, so the depth is
// sqrt(sx * sy) * scale * scaleZ. The clip's rotations turn the whole thing about the anchor
// (rect centre unless the pose is anchored) exactly as they turn a flat clip (X, then Y, then the
// in-plane spin). The anchor is a point of the rect's layout, so it stays put when the animated
// bounds change. `pose.perspective` plays no part: the eye belongs to whoever views the world.
QMatrix4x4 modelClipWorld(const QRectF &rect, double rotation, const ClipPose3d &pose,
                          const QVector3D &aabbMin, const QVector3D &aabbMax, const QSizeF &canvas,
                          double scale = 1.0, double scaleZ = 1.0);

// The size a model's bounding box front face gets when the largest rest extent spans `span` px.
QSizeF modelClipFaceSize(const QVector3D &aabbMin, const QVector3D &aabbMax, double span);

} // namespace drift
