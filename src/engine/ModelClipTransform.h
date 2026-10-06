#pragma once

#include "core/Model3dSource.h"
#include "engine/ClipTransform3d.h"

#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QRectF>
#include <QSizeF>
#include <QVector3D>

namespace drift {

// Resolved light knobs for one model clip draw.
struct ModelClipParams
{
    double lightYaw = 30.0;
    double lightPitch = 20.0;
    double lightIntensity = 1.0;
    double ambient = 0.35;
};

ModelClipParams modelClipParamsFromSource(const Model3dSource &source);

// Model space (the file's own units, glTF axes: +y up, +z toward the viewer) to world space
// (canvas px about the canvas centre, y down, +z toward the viewer), the space every clip's quad
// lives in. The model's rest bounding box is fitted to the clip: its front face is the layout
// `rect` at the rect's centre and `pose.positionZ`, its depth scales with the geometric mean of
// the two face scales, and the clip's rotations turn it about its centre exactly as they turn a
// flat clip (X, then Y, then the in-plane spin). `pose.perspective` plays no part: the eye
// belongs to whoever views the world.
QMatrix4x4 modelClipWorld(const QRectF &rect, double rotation, const ClipPose3d &pose,
                          const QVector3D &aabbMin, const QVector3D &aabbMax, const QSizeF &canvas);

// The light frame of a model draw: world space with y flipped up, the frame screenLightDir's
// yaw/pitch are measured in. Normals go model -> world -> here, so the light stays put in the
// world whichever way the model or the viewer turns.
QMatrix3x3 modelClipNormalMatrix(const QMatrix4x4 &world);

// The size a model's bounding box front face gets when the largest rest extent spans `span` px.
QSizeF modelClipFaceSize(const QVector3D &aabbMin, const QVector3D &aabbMax, double span);

} // namespace drift
