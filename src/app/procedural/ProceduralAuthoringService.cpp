#include "app/procedural/ProceduralAuthoringService.h"

#include "core/project/Project.h"
#include "engine/gpu/GlRuntime.h"
#include "engine/packages/procedurals/ProceduralCatalog.h"
#include "engine/packages/procedurals/ProceduralPackageLoader.h"
#include "engine/procedural/ProceduralEvaluator.h"
#include "engine/procedural/ProceduralSceneCache.h"
#include "engine/render3d/procedural/ProceduralClipRenderer.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>

namespace drift {
namespace {

QString failure(ProceduralAuthoringService::Result *r, const char *code, const QString &message)
{
    r->ok = false;
    r->code = QString::fromLatin1(code);
    r->message = message;
    return message;
}

bool validFileName(const QString &name)
{
    static const QRegularExpression kName(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$"));
    if (!kName.match(name).hasMatch())
        return false;
    const QString suffix = QFileInfo(name).suffix().toLower();
    return name == QLatin1String("main.lua") || suffix == QLatin1String("vert") || suffix == QLatin1String("frag");
}

bool writeFile(const QString &path, const QByteArray &bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) {
        *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

QJsonArray vec3(const std::array<float, 3> &v)
{
    return {double(v[0]), double(v[1]), double(v[2])};
}

QJsonObject statsJson(const ProceduralScene &scene)
{
    return {{QStringLiteral("meshCount"), double(scene.stats.meshCount)},
            {QStringLiteral("vertexCount"), double(scene.stats.vertexCount)},
            {QStringLiteral("instanceCount"), double(scene.stats.instanceCount)},
            {QStringLiteral("triangleCount"), double(scene.stats.triangleCount)},
            {QStringLiteral("luaBytesPeak"), double(scene.stats.luaBytesPeak)},
            {QStringLiteral("elapsedMs"), scene.stats.elapsedMs}};
}

QStringList toList(const std::vector<std::string> &lines)
{
    QStringList out;
    for (const std::string &line : lines)
        out.append(QString::fromStdString(line));
    return out;
}

// Stages the package into `staging` (existing files first when `base` is a package folder, then the
// requested changes) and writes the manifest with `id` forced.
bool stage(const QString &staging, const QString &base, const QString &id, const std::optional<QJsonObject> &manifest,
           const QMap<QString, QString> &files, const QStringList &removeFiles,
           ProceduralAuthoringService::Result *r)
{
    QDir(staging).removeRecursively();
    if (!QDir().mkpath(staging)) {
        failure(r, "io_error", QStringLiteral("cannot create %1").arg(staging));
        return false;
    }
    QString error;
    QJsonObject body;
    if (!base.isEmpty()) {
        const QFileInfoList existing = QDir(base).entryInfoList(QDir::Files);
        for (const QFileInfo &info : existing) {
            if (info.fileName() == QLatin1String("procedural.json")) {
                QFile f(info.absoluteFilePath());
                if (f.open(QIODevice::ReadOnly))
                    body = QJsonDocument::fromJson(f.readAll()).object();
                continue;
            }
            if (!QFile::copy(info.absoluteFilePath(), QDir(staging).filePath(info.fileName()))) {
                failure(r, "io_error", QStringLiteral("cannot copy %1").arg(info.absoluteFilePath()));
                return false;
            }
        }
    }
    if (manifest)
        body = *manifest;
    if (body.isEmpty()) {
        failure(r, "bad_args", QStringLiteral("manifest required"));
        return false;
    }
    body.insert(QStringLiteral("id"), id);

    for (const QString &name : removeFiles) {
        if (!validFileName(name) || name == QLatin1String("main.lua")) {
            failure(r, "bad_args", QStringLiteral("remove_files: \"%1\" must be a shader file name").arg(name));
            return false;
        }
        QFile::remove(QDir(staging).filePath(name));
    }
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!validFileName(it.key())) {
            failure(r, "bad_args",
                    QStringLiteral("files: \"%1\" must be main.lua or a bare .vert / .frag file name").arg(it.key()));
            return false;
        }
        if (!writeFile(QDir(staging).filePath(it.key()), it.value().toUtf8(), &error)) {
            failure(r, "io_error", error);
            return false;
        }
    }
    if (!writeFile(QDir(staging).filePath(QStringLiteral("procedural.json")),
                   QJsonDocument(body).toJson(QJsonDocument::Indented), &error)) {
        failure(r, "io_error", error);
        return false;
    }
    return true;
}

// Loader, then the Lua at t = 0 with default params and camera, then every shader pair. Each stage
// reports into `r` even when an earlier one failed, so one round trip shows all the problems.
void validate(const QString &dir, QSize canvas, ProceduralAuthoringService::Result *r)
{
    QString error;
    const ProceduralPackage pkg = ProceduralPackageLoader::loadPackage(dir, &error);
    if (!pkg.valid) {
        failure(r, "bad_manifest", error.isEmpty() ? pkg.errorMessage : error);
        r->errors << r->message;
        return;
    }

    const QSizeF project(canvas.width(), canvas.height());
    const ProceduralEvalRequest eval = procedural3d::makeEvalRequest(pkg, {}, 0, 30, project, defaultSceneCamera(project), 0, false);
    const ProceduralScene scene = ProceduralEvaluator::evaluate(eval.source, eval.params, eval.time, eval.camera,
                                                                eval.canvas, eval.budgets, eval.frame, eval.seed);
    r->luaLog = toList(scene.log);
    r->stats = statsJson(scene);
    if (!scene.ok()) {
        r->errors << toList(scene.errors);
        failure(r, "lua_error", r->errors.join(QLatin1Char('\n')));
    } else {
        if (scene.meshes.empty())
            r->warnings << QStringLiteral("main.lua added no meshes at t = 0 with default params");
        for (const ProceduralMeshEntry &entry : scene.meshes) {
            const QString shader = QString::fromStdString(entry.shader);
            if (!pkg.shaders.contains(shader)) {
                const QString text = QStringLiteral("scene:add refers to shader \"%1\" but the package has: %2")
                                         .arg(shader, QStringList(pkg.shaders.keys()).join(QStringLiteral(", ")));
                r->errors << text;
                if (r->code.isEmpty())
                    failure(r, "lua_error", text);
            }
        }
    }

    bool compileFailed = false;
    const bool glAvailable = drift::gl::runtime().available();
    for (auto it = pkg.shaders.cbegin(); it != pkg.shaders.cend(); ++it) {
        ProceduralAuthoringService::ShaderReport report;
        report.name = it.key();
        if (glAvailable) {
            QString log;
            bool built = false;
            drift::gl::runtime().exec([&] {
                built = drift::gl::runtime().compileUserProgram(pkg.sourceHash, it.key(), it.value().vert,
                                                                it.value().frag, &log) != nullptr;
            });
            report.compiled = built;
            report.log = log;
            if (!built) {
                compileFailed = true;
                r->errors << QStringLiteral("%1: %2").arg(it.key(), log.trimmed());
            }
        }
        r->shaders.append(report);
    }
    r->shaderCheck = !glAvailable ? QStringLiteral("skipped") : compileFailed ? QStringLiteral("failed") : QStringLiteral("ok");
    if (compileFailed && r->code.isEmpty())
        failure(r, "shader_compile_failed", r->errors.join(QLatin1Char('\n')));
    if (r->code.isEmpty())
        r->ok = true;
}

ProceduralAuthoringService::Result install(const QString &id, const QString &folder, const QString &base,
                                           const std::optional<QJsonObject> &manifest,
                                           const QMap<QString, QString> &files, const QStringList &removeFiles,
                                           QSize canvas, bool dryRun)
{
    ProceduralAuthoringService::Result r;
    r.id = id;
    const QString root = ProceduralAuthoringService::userRoot();
    const QString target = folder.isEmpty() ? QString() : QDir(root).filePath(folder);
    r.packageDir = target;

    const QString staging = QDir(root).filePath(QStringLiteral(".mcp-") + (folder.isEmpty() ? QStringLiteral("check") : folder));
    struct Cleanup
    {
        QString path;
        ~Cleanup() { QDir(path).removeRecursively(); }
    } cleanup{staging};

    if (!stage(staging, base, id.isEmpty() ? QStringLiteral("user.check") : id, manifest, files, removeFiles, &r))
        return r;
    validate(staging, canvas, &r);
    if (!r.ok || dryRun)
        return r;

    const QString previous = QDir(root).filePath(QStringLiteral(".mcp-old-") + folder);
    QDir(previous).removeRecursively();
    const bool replacing = QFileInfo::exists(target);
    if (replacing && !QDir().rename(target, previous)) {
        r.ok = false;
        failure(&r, "io_error", QStringLiteral("cannot move the old %1 aside").arg(target));
        return r;
    }
    if (!QDir().rename(staging, target)) {
        if (replacing)
            QDir().rename(previous, target);
        r.ok = false;
        failure(&r, "io_error", QStringLiteral("cannot move the package into %1").arg(target));
        return r;
    }
    QDir(previous).removeRecursively();

    reloadProceduralCatalog();
    const ProceduralPackage installed = proceduralForId(id);
    if (QDir::cleanPath(installed.packageDir) != QDir::cleanPath(target)) {
        r.warning = QStringLiteral("Installed, but %1 resolves to another package with the same id, which wins "
                                   "the catalog lookup").arg(id);
    }
    return r;
}

} // namespace

QJsonObject ProceduralAuthoringService::Result::toJson() const
{
    QJsonObject o{{QStringLiteral("ok"), ok}};
    if (!id.isEmpty())
        o.insert(QStringLiteral("id"), id);
    if (!ok) {
        o.insert(QStringLiteral("error"), code);
        o.insert(QStringLiteral("detail"), message);
    }
    if (!errors.isEmpty())
        o.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errors));
    if (!warnings.isEmpty())
        o.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(warnings));
    if (!warning.isEmpty())
        o.insert(QStringLiteral("warning"), warning);
    if (!luaLog.isEmpty())
        o.insert(QStringLiteral("log"), QJsonArray::fromStringList(luaLog));
    if (!stats.isEmpty())
        o.insert(QStringLiteral("stats"), stats);
    if (!shaderCheck.isEmpty()) {
        o.insert(QStringLiteral("shaderCheck"), shaderCheck);
        QJsonObject compile;
        for (const ShaderReport &s : shaders)
            compile.insert(s.name, QJsonObject{{QStringLiteral("ok"), s.compiled}, {QStringLiteral("log"), s.log}});
        o.insert(QStringLiteral("shaders"), compile);
        if (shaderCheck == QLatin1String("skipped"))
            o.insert(QStringLiteral("shaderCheckReason"),
                     QStringLiteral("OpenGL is unavailable, so the shaders were not compiled"));
    }
    return o;
}

QJsonObject ProceduralAuthoringService::EvaluateResult::toJson() const
{
    QJsonObject o{{QStringLiteral("ok"), ok}};
    if (!ok && !code.isEmpty()) {
        o.insert(QStringLiteral("error"), code);
        o.insert(QStringLiteral("detail"), message);
    }
    o.insert(QStringLiteral("id"), packageId);
    if (!cameraMode.isEmpty())
        o.insert(QStringLiteral("cameraMode"), cameraMode);
    o.insert(QStringLiteral("cameraRead"), cameraRead);
    if (!stats.isEmpty())
        o.insert(QStringLiteral("stats"), stats);
    if (!bounds.isEmpty())
        o.insert(QStringLiteral("bounds"), bounds);
    o.insert(QStringLiteral("meshes"), meshes);
    o.insert(QStringLiteral("lights"), lights);
    if (!camera.isEmpty())
        o.insert(QStringLiteral("camera"), camera);
    o.insert(QStringLiteral("log"), QJsonArray::fromStringList(luaLog));
    o.insert(QStringLiteral("errors"), QJsonArray::fromStringList(errors));
    return o;
}

QString ProceduralAuthoringService::userRoot()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("procedurals"));
}

QString ProceduralAuthoringService::normalizeSlug(const QString &slug)
{
    static const QRegularExpression kSlug(QStringLiteral("^[a-z0-9_]{1,48}$"));
    QString s = slug.trimmed().toLower();
    if (s.startsWith(QLatin1String("user.")))
        s = s.mid(5);
    return kSlug.match(s).hasMatch() ? s : QString();
}

QString ProceduralAuthoringService::packageDirForId(const QString &id)
{
    return proceduralForId(id).packageDir;
}

bool ProceduralAuthoringService::isUserDir(const QString &packageDir)
{
    return !packageDir.isEmpty()
        && QDir::cleanPath(packageDir).startsWith(QDir::cleanPath(userRoot()) + QLatin1Char('/'));
}

ProceduralAuthoringService::Result ProceduralAuthoringService::create(const QString &slug, const QJsonObject &manifest,
                                                                      const QMap<QString, QString> &files, QSize canvas)
{
    const QString clean = normalizeSlug(slug);
    if (clean.isEmpty()) {
        Result r;
        failure(&r, "bad_args", QStringLiteral("slug must be 1-48 of a-z, 0-9, _ (got \"%1\")").arg(slug));
        return r;
    }
    const QString id = QStringLiteral("user.") + clean;
    if (QFileInfo::exists(QDir(userRoot()).filePath(clean)) || hasProcedural(id)) {
        Result r;
        r.id = id;
        failure(&r, "exists", QStringLiteral("%1 already exists - use update_procedural, or pick another slug").arg(id));
        return r;
    }
    return install(id, clean, {}, manifest, files, {}, canvas, false);
}

ProceduralAuthoringService::Result ProceduralAuthoringService::update(const QString &id,
                                                                      const std::optional<QJsonObject> &manifest,
                                                                      const QMap<QString, QString> &files,
                                                                      const QStringList &removeFiles, QSize canvas)
{
    Result r;
    r.id = id;
    const QString dir = packageDirForId(id);
    if (dir.isEmpty()) {
        failure(&r, "not_found", QStringLiteral("No procedural with id %1 - call list_procedurals").arg(id));
        return r;
    }
    if (!isUserDir(dir)) {
        failure(&r, "not_user_procedural",
                QStringLiteral("%1 ships with Drift or an add-on; fork it with create_procedural instead").arg(id));
        return r;
    }
    return install(id, QFileInfo(QDir::cleanPath(dir)).fileName(), QDir::cleanPath(dir), manifest, files, removeFiles,
                   canvas, false);
}

ProceduralAuthoringService::Result ProceduralAuthoringService::check(const QString &id,
                                                                     const std::optional<QJsonObject> &manifest,
                                                                     const QMap<QString, QString> &files, QSize canvas)
{
    const QString dir = id.isEmpty() ? QString() : packageDirForId(id);
    return install(id, dir.isEmpty() ? QString() : QFileInfo(QDir::cleanPath(dir)).fileName(), dir, manifest, files,
                   {}, canvas, true);
}

ProceduralAuthoringService::Source ProceduralAuthoringService::getSource(const QString &id)
{
    Source s;
    s.id = id;
    const QString dir = packageDirForId(id);
    if (dir.isEmpty()) {
        s.code = QStringLiteral("not_found");
        s.message = QStringLiteral("No procedural with id %1 - call list_procedurals").arg(id);
        return s;
    }
    QFile manifestFile(QDir(dir).filePath(QStringLiteral("procedural.json")));
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        s.code = QStringLiteral("io_error");
        s.message = QStringLiteral("cannot read %1").arg(manifestFile.fileName());
        return s;
    }
    s.manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &info : entries) {
        if (!validFileName(info.fileName()))
            continue;
        QFile f(info.absoluteFilePath());
        if (f.open(QIODevice::ReadOnly))
            s.files.insert(info.fileName(), QString::fromUtf8(f.readAll()));
    }
    s.user = isUserDir(dir);
    s.ok = true;
    return s;
}

ProceduralAuthoringService::Result ProceduralAuthoringService::remove(const QString &id)
{
    Result r;
    r.id = id;
    const QString dir = packageDirForId(id);
    if (dir.isEmpty()) {
        failure(&r, "not_found", QStringLiteral("No procedural with id %1 - call list_procedurals").arg(id));
        return r;
    }
    if (!isUserDir(dir)) {
        failure(&r, "not_user_procedural", QStringLiteral("%1 ships with Drift or an add-on and cannot be deleted").arg(id));
        return r;
    }
    if (!QDir(dir).removeRecursively()) {
        failure(&r, "io_error", QStringLiteral("cannot remove %1").arg(dir));
        return r;
    }
    reloadProceduralCatalog();
    r.ok = true;
    return r;
}

int ProceduralAuthoringService::usesInProject(const Project &project, const QString &id)
{
    int count = 0;
    project.forEachTrackList([&](const QList<Track> &tracks) {
        for (const Track &track : tracks) {
            for (const Clip &clip : track.clips)
                count += clip.type == ClipType::Procedural && clip.procedural.packageId == id ? 1 : 0;
        }
    });
    return count;
}

ProceduralAuthoringService::EvaluateResult ProceduralAuthoringService::evaluate(const EvaluateRequest &request)
{
    EvaluateResult out;
    out.packageId = request.packageId;
    const ProceduralPackage pkg = proceduralForId(request.packageId);
    if (!pkg.valid) {
        out.code = QStringLiteral("not_found");
        out.message = pkg.id.isEmpty() ? QStringLiteral("No procedural with id %1 - call list_procedurals").arg(request.packageId)
                                       : QStringLiteral("%1 is invalid: %2").arg(request.packageId, pkg.errorMessage);
        out.errors << out.message;
        return out;
    }
    const QSizeF project(request.canvas.width(), request.canvas.height());
    const ProceduralEvalRequest eval = procedural3d::makeEvalRequest(
        pkg, request.parameters, secondsToUs(request.timeSeconds), request.frameRate, project,
        request.camera.value_or(defaultSceneCamera(project)), request.seed, false);
    const ProceduralScene scene = ProceduralEvaluator::evaluate(eval.source, eval.params, eval.time, eval.camera,
                                                                eval.canvas, eval.budgets, eval.frame, eval.seed);
    out.luaLog = toList(scene.log);
    out.errors = toList(scene.errors);
    out.stats = statsJson(scene);
    out.cameraRead = scene.cameraRead;
    out.cameraMode = scene.hasCamera ? QStringLiteral("own") : QStringLiteral("world");
    if (!scene.ok()) {
        out.code = QStringLiteral("lua_error");
        out.message = out.errors.join(QLatin1Char('\n'));
        return out;
    }
    if (scene.aabbValid)
        out.bounds = {{QStringLiteral("min"), vec3(scene.aabbMin)}, {QStringLiteral("max"), vec3(scene.aabbMax)}};
    for (const ProceduralMeshEntry &entry : scene.meshes) {
        QJsonArray uniforms;
        for (const auto &[name, values] : entry.uniforms)
            uniforms.append(QString::fromStdString(name));
        out.meshes.append(QJsonObject{{QStringLiteral("shader"), QString::fromStdString(entry.shader)},
                                      {QStringLiteral("vertices"), double(entry.mesh ? entry.mesh->vertexCount() : 0)},
                                      {QStringLiteral("instances"), double(entry.instanceCount())},
                                      {QStringLiteral("uniforms"), uniforms}});
    }
    static const char *const kTypes[] = {"point", "directional", "spot"};
    for (const ProceduralLight &light : scene.lights) {
        out.lights.append(QJsonObject{{QStringLiteral("type"), QString::fromLatin1(kTypes[int(light.type)])},
                                      {QStringLiteral("pos"), vec3(light.pos)},
                                      {QStringLiteral("dir"), vec3(light.dir)},
                                      {QStringLiteral("color"), vec3(light.color)},
                                      {QStringLiteral("intensity"), double(light.intensity)},
                                      {QStringLiteral("range"), double(light.range)},
                                      {QStringLiteral("cone"), double(light.cone)}});
    }
    if (scene.hasCamera) {
        const ProceduralSceneCamera &c = scene.camera;
        out.camera = {{QStringLiteral("eye"), vec3(c.eye)},         {QStringLiteral("target"), vec3(c.target)},
                      {QStringLiteral("up"), vec3(c.up)},           {QStringLiteral("fov"), double(c.fov)},
                      {QStringLiteral("near"), double(c.nearPlane)}, {QStringLiteral("far"), double(c.farPlane)}};
    }
    out.ok = true;
    return out;
}

} // namespace drift
