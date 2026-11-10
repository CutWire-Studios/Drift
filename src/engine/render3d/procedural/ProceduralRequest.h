#pragma once

#include "engine/packages/procedurals/ProceduralPackageLoader.h"
#include "engine/procedural/ProceduralScene.h"
#include "engine/render3d/scene/ClipTransform3d.h"

#include <QMap>
#include <QMatrix4x4>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>
#include <string>

namespace drift {

// Everything the GL thread needs to draw one procedural clip, without touching the project, the
// catalog or Lua. Built on the compositor worker by makeProceduralRequest; immutable after that.
struct ProceduralRequest
{
    // The evaluated scene. Always ok() with at least one mesh: failures never reach the GL thread.
    std::shared_ptr<const ProceduralScene> scene;
    QString packageId;
    std::string sourceHash; // keys the compiled programs and their compile logs
    QMap<QString, ProceduralShaderPair> shaders; // by the name scene:add refers to

    // The manifest params at this instant, normalised as an effect's are (colours as "#rrggbb[aa]",
    // vec2 as "<key>.x"/"<key>.y", ...), for setPackageUniforms. regionParams lists the keys that
    // bind as a vec4.
    QMap<QString, QVariant> uniforms;
    QStringList regionParams;

    // The clip's placement of the scene in project world px (its position, rotation and tilt), for
    // u_model. Identity for a clip at its default full-canvas layout, and ignored when the script
    // set its own camera.
    QMatrix4x4 model;
    // The clip's own eye in render px, which frames the scene when no scene camera is active.
    ClipPose3d pose3d;

    // Some shader reads u_audioLevel/u_audioBass/u_audioBeat, so the compositor analyses the timeline mix.
    bool usesGlobalAudio = false;

    double renderScale = 1.0;
    QSize projectSize;           // the canvas the script was evaluated for
    TimeUs sourceTimeUs = 0;     // u_time and the script's `time`
    int frameRate = 30;

    // The script chose its own camera: the clip is a flat full-canvas layer drawn through it.
    bool ownCamera() const { return scene && scene->hasCamera; }
};

} // namespace drift
