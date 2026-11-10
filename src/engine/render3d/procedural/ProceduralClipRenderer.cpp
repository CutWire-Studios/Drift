#include "engine/render3d/procedural/ProceduralClipRenderer.h"

#include "core/effects/Effect.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/procedurals/ProceduralCatalog.h"
#include "engine/procedural/ProceduralCamera.h"
#include "engine/procedural/ProceduralDiagnostics.h"
#include "engine/procedural/ProceduralSceneCache.h"

#include <QColor>

#include <algorithm>
#include <cmath>

namespace drift::procedural3d {

namespace {

std::vector<double> colorComponents(const QString &hex, bool alpha)
{
    const QColor c = hex.size() == 9 ? QColor(hex.left(7)) : QColor(hex);
    std::vector<double> v{c.redF(), c.greenF(), c.blueF()};
    if (alpha)
        v.push_back(hex.size() == 9 ? double(hex.right(2).toInt(nullptr, 16)) / 255.0 : 1.0);
    return v;
}

// The script's `params`: one entry per manifest param, typed by what the param is.
ProceduralParams scriptParams(const ProceduralPackage &pkg, const QMap<QString, QVariant> &values)
{
    ProceduralParams out;
    for (const EffectParamSpec &spec : pkg.parameters) {
        const QString &key = spec.key;
        const std::string name = key.toStdString();
        const auto number = [&values](const QString &k) { return values.value(k).toDouble(); };
        if (spec.isBoolean()) {
            out[name] = ProceduralParam::fromBool(values.value(key).toBool());
        } else if (spec.isColor()) {
            out[name] = ProceduralParam::fromVector(colorComponents(values.value(key).toString(), spec.alpha));
        } else if (spec.isVec2()) {
            out[name] = ProceduralParam::fromVector({number(key + QLatin1String(".x")), number(key + QLatin1String(".y"))});
        } else if (spec.isRegion()) {
            out[name] = ProceduralParam::fromVector({number(key + QLatin1String(".x")), number(key + QLatin1String(".y")),
                                                     number(key + QLatin1String(".w")), number(key + QLatin1String(".h"))});
        } else if (values.value(key).typeId() == QMetaType::QString) {
            out[name] = ProceduralParam::fromString(values.value(key).toString().toStdString());
        } else {
            out[name] = ProceduralParam::fromNumber(number(key));
        }
    }
    return out;
}

void report(const QString &clipId, const QString &packageId, std::shared_ptr<const ProceduralScene> scene,
            const std::string &error)
{
    ProceduralClipReport r;
    r.packageId = packageId.toStdString();
    r.scene = std::move(scene);
    r.error = error;
    ProceduralDiagnostics::shared().setClip(clipId.toStdString(), std::move(r));
}

} // namespace

ProceduralCameraInput cameraInputFor(const SceneCamera3d &camera, const QSizeF &projectSize)
{
    ProceduralCameraInput in;
    const QMatrix4x4 rotation = sceneCameraRotation(camera);
    const auto axis = [&rotation](float x, float y, float z) {
        const QVector3D v = rotation.mapVector(QVector3D(x, y, z));
        return std::array<float, 3>{v.x(), v.y(), v.z()};
    };
    const QVector3D eye = sceneCameraEye(camera);
    in.eye = {eye.x(), eye.y(), eye.z()};
    in.right = axis(1.f, 0.f, 0.f);
    in.up = axis(0.f, -1.f, 0.f); // the camera's +Y points down the image; scripts want world "up"
    in.forward = axis(0.f, 0.f, 1.f);
    in.fov = float(fovFromZoom(camera.zoom, projectSize.height()) * M_PI / 180.0);
    in.nearPlane = float(std::max(1.0, camera.zoom * kClipNearFraction));
    in.farPlane = 100000.f;
    const std::array<float, 3> target{in.eye[0] + in.forward[0], in.eye[1] + in.forward[1], in.eye[2] + in.forward[2]};
    in.view = proceduralcam::lookAt(in.eye, target, in.up);
    in.proj = proceduralcam::perspective(in.fov, float(projectSize.width() / std::max(1.0, projectSize.height())),
                                         in.nearPlane, in.farPlane);
    return in;
}

ProceduralEvalRequest makeEvalRequest(const ProceduralPackage &pkg, const QMap<QString, QVariant> &parameters,
                                      TimeUs sourceTimeUs, int frameRate, const QSizeF &projectSize,
                                      const SceneCamera3d &camera, int seed, bool exporting,
                                      QMap<QString, QVariant> *resolved)
{
    // The manifest's params follow the effect schema, so the effect resolver does the defaults and
    // the colour / bool / int clean-up for us.
    EffectPresetEntry def;
    def.meta.parameters = pkg.parameters;
    Effect holder;
    holder.parameters = parameters;
    const QMap<QString, QVariant> values = resolvedEffectParameters(holder, def);
    if (resolved)
        *resolved = values;

    ProceduralEvalRequest eval;
    eval.source = pkg.luaSource.toStdString();
    eval.sourceHash = pkg.sourceHash.toStdString();
    eval.params = scriptParams(pkg, values);
    eval.time = double(sourceTimeUs) / 1'000'000.0;
    eval.frame = int(std::llround(eval.time * std::max(1, frameRate)));
    eval.seed = seed;
    eval.canvas = {int(std::lround(projectSize.width())), int(std::lround(projectSize.height()))};
    eval.camera = cameraInputFor(camera, projectSize);
    eval.budgets = ProceduralBudgets::forPlatform(exporting);
    if (pkg.maxVertices > 0)
        eval.budgets.maxVertices = std::min(eval.budgets.maxVertices, pkg.maxVertices);
    if (pkg.maxInstances > 0)
        eval.budgets.maxInstances = std::min(eval.budgets.maxInstances, pkg.maxInstances);
    return eval;
}

std::shared_ptr<const ProceduralRequest> makeProceduralRequest(const BuildInput &input)
{
    const ProceduralPackage pkg = proceduralForId(input.source.packageId);
    if (!pkg.valid) {
        const QString why = pkg.id.isEmpty() ? QStringLiteral("procedural package '%1' is not installed")
                                                   .arg(input.source.packageId)
                                             : QStringLiteral("procedural package '%1' is invalid: %2")
                                                   .arg(input.source.packageId, pkg.errorMessage);
        report(input.clipId, input.source.packageId, nullptr, why.toStdString());
        return nullptr;
    }

    QMap<QString, QVariant> values;
    const ProceduralEvalRequest eval = makeEvalRequest(pkg, input.source.parameters, input.sourceTimeUs,
                                                       input.frameRate, input.projectSize, input.camera,
                                                       input.source.seed, input.exporting, &values);

    const std::shared_ptr<const ProceduralScene> scene = ProceduralSceneCache::shared().evaluate(eval);
    report(input.clipId, input.source.packageId, scene, {});
    if (!scene->ok() || scene->meshes.empty())
        return nullptr;

    auto request = std::make_shared<ProceduralRequest>();
    request->scene = scene;
    request->packageId = pkg.id;
    request->sourceHash = pkg.sourceHash.toStdString();
    request->shaders = pkg.shaders;
    for (const ProceduralShaderPair &pair : pkg.shaders) {
        for (const QString *source : {&pair.vert, &pair.frag}) {
            if (source->contains(QLatin1String("u_audioLevel")) || source->contains(QLatin1String("u_audioBass"))
                || source->contains(QLatin1String("u_audioBeat"))) {
                request->usesGlobalAudio = true;
            }
        }
    }
    request->uniforms = values;
    for (const EffectParamSpec &spec : pkg.parameters) {
        if (spec.isRegion())
            request->regionParams.append(spec.key);
    }
    request->model = clipLocalToWorld(input.projectRect, input.rotation, input.projectPose, input.projectSize);
    request->pose3d = input.renderPose;
    request->renderScale = input.renderScale;
    request->projectSize = QSize(eval.canvas.width, eval.canvas.height);
    request->sourceTimeUs = input.sourceTimeUs;
    request->frameRate = input.frameRate;
    return request;
}

} // namespace drift::procedural3d
