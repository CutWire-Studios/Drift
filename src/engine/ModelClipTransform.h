#pragma once

#include "core/Model3dSource.h"

#include <QMatrix3x3>
#include <QMatrix4x4>
#include <QRectF>
#include <QVector3D>

namespace drift {

// Resolved pose/light knobs for one model clip draw. `centreX/Y` is the model centre as a
// fraction of the canvas (top-left origin), which is how the clip's transformX/Y arrive.
struct ModelClipParams
{
    double scale = 0.5;
    double depth = 0.5;
    double rotX = 0.0;
    double rotY = 0.0;
    double rotZ = 0.0;
    double centreX = 0.5;
    double centreY = 0.5;
    double lightYaw = 30.0;
    double lightPitch = 20.0;
    double lightIntensity = 1.0;
    double ambient = 0.35;
};

ModelClipParams modelClipParamsFromSource(const Model3dSource &source, double centreX,
                                          double centreY);

struct ModelClipCamera
{
    QMatrix4x4 mvp;
    QMatrix4x4 modelView;
    QMatrix3x3 normalMatrix;
};

// Builds the model→clip-space matrix for a free-standing model clip. Takes `aspect`
// (height/width), never a pixel size — the same WYSIWYG invariant as faceModelMvp: preview at
// renderScale 0.5 and export at 1.0 get a bit-identical matrix.
//
// The camera sits on +z looking down −z; `scale` is the fraction of canvas height the model's
// largest rest extent spans at the model-centre plane, and `depth` only changes how much
// perspective foreshortening there is (camera distance and field of view move together so the
// projected size stays put). rotX/rotY/rotZ are intrinsic: about the model's own axes, applied
// X, then Y, then Z, each following the earlier ones. Output is plain GL orientation (+y up);
// drawModelClip flips rows in its resolve pass to match the compositor's v=0-is-top convention.
ModelClipCamera modelClipCamera(const ModelClipParams &params, const QVector3D &aabbMin,
                                const QVector3D &aabbMax, double aspect);

// Projected rest-pose bounds as canvas fractions, top-left origin. Animated parts may leave it.
QRectF modelClipScreenRect(const ModelClipParams &params, const QVector3D &aabbMin,
                           const QVector3D &aabbMax, double aspect);

} // namespace drift
