#pragma once

#include "engine/FaceLandmarker.h"

#include <QMap>
#include <QMatrix4x4>
#include <QString>
#include <QVariant>

namespace drift {

// User-facing knobs for a model3d face-prop effect. Resolved from the effect's parameter map
// before the draw; kept as a plain struct so FaceModelTransform and the renderer share one shape.
struct FaceModelParams
{
    QString modelPath;
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
    double offsetZ = 0.0;
    double rotX = 0.0; // degrees
    double rotY = 0.0;
    double rotZ = 0.0;
    bool occlusion = true;
    double occlusionDepth = 0.0; // extra push back of the head proxy, in head-widths (0 = front at eye plane)
    double occlusionSize = 1.0;   // multiplier on the ellipsoid radii
    double occlusionOffset = 0.0; // extra Y offset in head-widths
    double lightYaw = 30.0;       // degrees, screen-space
    double lightPitch = 20.0;
    double lightIntensity = 1.0;
    double ambient = 0.35;
    int faceIndex = 0;
};

FaceModelParams faceModelParamsFromMap(const QMap<QString, QVariant> &parameters);

// Builds the model→NDC matrix. Takes `aspect` (height/width), never a pixel size — that is the
// WYSIWYG invariant: preview at renderScale 0.5 and export at 1.0 must produce a bit-identical
// matrix for the same face pose and user params.
//
// Composition (column vectors, applied right-to-left):
//   NDC · HeadBasis · UserTransform · (already-normalised model verts)
//
// Head "up" maps to decreasing NDC y: the FBO's v=0 row is the top of the image and face uv is
// top-left origin, so the pose basis already encodes up as −y in uv. Do not Y-flip here.
// Depth: z_ndc = −z_wn/4 so +forward (toward the viewer) is nearer under GL_LESS.
QMatrix4x4 faceModelMvp(const FaceAnchors &face, const FaceModelParams &params, double aspect);

// Head-proxy transform only (no user scale/rot/offset). Used to place the occlusion ellipsoid.
QMatrix4x4 faceHeadProxyMvp(const FaceAnchors &face, const FaceModelParams &params, double aspect);

} // namespace drift
