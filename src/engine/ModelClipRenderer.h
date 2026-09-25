#pragma once

#include "core/Model3dSource.h"
#include "engine/ModelClipTransform.h"

#include <QPointF>

#include <memory>

// Resolves a Model3d clip for one instant into a value the GL thread can draw without touching
// the project: the parsed asset (so pose and vertex buffers come from one parse), the sampled
// animation pose, and the camera/light knobs. Built on the compositor worker; no GL here.

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
    QPointF centre; // model centre as a canvas fraction, top-left origin
};

struct ModelDrawRequest
{
    QString path;
    std::shared_ptr<const ModelAsset> asset;
    std::shared_ptr<const ModelPose> pose; // null → static (baked) draw
    ModelClipParams params;
};

// Null when nothing should be drawn: an unloadable file, or Hide outside the animation.
std::shared_ptr<const ModelDrawRequest> makeDrawRequest(const RenderRequest &request);

} // namespace drift::model3d
