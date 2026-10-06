#pragma once

#include "core/Model3dSource.h"
#include "engine/ModelClipTransform.h"

#include <QRectF>

#include <memory>

// Resolves a Model3d clip for one instant into a value the GL thread can draw without touching
// the project: the parsed asset (so pose and vertex buffers come from one parse), the sampled
// animation pose, where the clip places the model and the light knobs. Built on the compositor
// worker; no GL here.

namespace drift {
struct ModelAsset;
struct ModelPose;
}

namespace drift::model3d {

struct RenderRequest
{
    QString path;
    Model3dSource source; // keyframes already resolved for this instant
    // The clip's source time. startOffsetUs is added here, then the result is folded by the
    // source's loop mode against the chosen animation's duration.
    TimeUs animUs = 0;
    // The clip's placement at this instant, in render-scale canvas px: see modelClipWorld.
    QRectF rect;
    double rotation = 0.0;
    ClipPose3d pose;
    QSizeF canvas; // the canvas `rect` is laid out on
};

struct ModelDrawRequest
{
    QString path;
    std::shared_ptr<const ModelAsset> asset;
    std::shared_ptr<const ModelPose> pose; // null → static (baked) draw
    ModelClipParams params;
    QRectF rect;
    double rotation = 0.0;
    ClipPose3d pose3d;
    QSizeF canvas;
};

// Null when nothing should be drawn: an unloadable file, or Hide outside the animation.
std::shared_ptr<const ModelDrawRequest> makeDrawRequest(const RenderRequest &request);

} // namespace drift::model3d
