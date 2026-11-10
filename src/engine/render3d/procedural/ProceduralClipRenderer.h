#pragma once

#include "core/project/sources/ProceduralSource.h"
#include "engine/procedural/ProceduralEvaluator.h"
#include "engine/procedural/ProceduralSceneCache.h"
#include "engine/render3d/procedural/ProceduralRequest.h"
#include "engine/render3d/scene/SceneCamera3d.h"

#include <QRectF>
#include <QSizeF>
#include <QString>

#include <memory>

// Resolves a Procedural clip for one instant into a value the GL thread can draw: its package's
// parameters, the Lua scene evaluated through the process-wide ProceduralSceneCache, and the
// clip's placement. Built on the compositor worker; no GL here.

namespace drift::procedural3d {

struct BuildInput
{
    QString clipId;
    ProceduralSource source; // keyframes already resolved for this instant
    // The clip's source time (Clip::timelineToSourceUs): trimming the head offsets the script's clock.
    TimeUs sourceTimeUs = 0;
    int frameRate = 30;
    QSizeF projectSize;
    double renderScale = 1.0;
    // The scene camera in project px (renderScale 1), or the default flat camera.
    SceneCamera3d camera;
    // The clip's layout rect and rotation in project px, and its pose in project px and in render px.
    QRectF projectRect;
    double rotation = 0.0;
    ClipPose3d projectPose;
    ClipPose3d renderPose;
    // Export evaluates with the longer wall-clock limit.
    bool exporting = false;
};

// What the Lua `camera` global reports for a scene camera, in project px.
ProceduralCameraInput cameraInputFor(const SceneCamera3d &camera, const QSizeF &projectSize);

// The evaluation request for `pkg` at one instant: manifest defaults filled in under `parameters`
// and the camera reported as the Lua `camera` global. `resolved` receives the normalised param
// values. Shared by the compositor and the authoring service.
ProceduralEvalRequest makeEvalRequest(const ProceduralPackage &pkg, const QMap<QString, QVariant> &parameters,
                                      TimeUs sourceTimeUs, int frameRate, const QSizeF &projectSize,
                                      const SceneCamera3d &camera, int seed, bool exporting,
                                      QMap<QString, QVariant> *resolved = nullptr);

// Null when nothing should be drawn: unknown or invalid package, a script error or budget overflow,
// or an empty scene. The reason is recorded in ProceduralDiagnostics under the clip id.
std::shared_ptr<const ProceduralRequest> makeProceduralRequest(const BuildInput &input);

} // namespace drift::procedural3d
