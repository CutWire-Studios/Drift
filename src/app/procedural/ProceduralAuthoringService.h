#pragma once

#include "engine/render3d/scene/SceneCamera3d.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

namespace drift {

class Project;

// Writing, validating and trying out procedural packages under <AppData>/procedurals/<slug>/. Shared
// by the MCP tools and the Procedural workspace. Stateless; every call is synchronous.
//
// Threading: call from the GUI thread. Writes rescan the process-wide catalog, validation runs the
// Lua on the calling thread (up to the preview deadline, 250 ms) and blocks on the GL thread through
// GlRuntime::exec to compile the shaders, so never call it from the GL thread or from a compositor
// worker. Without OpenGL the shaders are not compiled and shaderCheck is "skipped".
class ProceduralAuthoringService
{
public:
    struct ShaderReport
    {
        QString name;
        bool compiled = false; // false: compile failed, or not attempted (see Result::shaderCheck)
        QString log;           // driver output; empty on a clean build
    };

    struct Result
    {
        bool ok = false;
        // Empty on success. Else one of: bad_args, exists, not_found, not_user_procedural, io_error,
        // bad_manifest, lua_error, shader_compile_failed.
        QString code;
        QString message;
        QString id;         // user.<slug>
        QString packageDir; // where it was (or would have been) installed
        QStringList errors; // one entry per problem: Lua errors, then shader logs prefixed with the shader name
        QStringList warnings;
        QStringList luaLog; // print() output of the validation run
        QList<ShaderReport> shaders;
        QString shaderCheck; // "ok", "skipped" (no OpenGL) or "failed"
        QJsonObject stats;   // meshCount, vertexCount, instanceCount, triangleCount, luaBytesPeak, elapsedMs
        QString warning;     // set when another package with the same id wins the catalog lookup

        QJsonObject toJson() const;
    };

    struct Source
    {
        bool ok = false;
        QString code;
        QString message;
        QString id;
        bool user = false; // lives in the user-writable folder, so update/delete accept it
        QJsonObject manifest;
        QMap<QString, QString> files; // main.lua and every shader, by file name
    };

    struct EvaluateRequest
    {
        QString packageId;
        QMap<QString, QVariant> parameters; // overrides; the manifest defaults fill the rest
        double timeSeconds = 0.0;           // the script's `time`
        int frameRate = 30;
        QSize canvas = QSize(1920, 1080);
        std::optional<SceneCamera3d> camera; // the `camera` global; the default flat camera when unset
        int seed = 0;
    };

    struct EvaluateResult
    {
        bool ok = false;
        QString code;
        QString message;
        QString packageId;
        QString cameraMode; // "world" (draws through the shared scene camera) or "own" (script called scene:camera)
        bool cameraRead = false;
        QStringList errors;
        QStringList luaLog;
        QJsonObject stats;
        QJsonObject bounds; // {min:[x,y,z], max:[x,y,z]}; empty when the scene has no geometry
        QJsonArray meshes;  // [{shader, vertices, instances, uniforms:[names]}]
        QJsonArray lights;
        QJsonObject camera; // the script's own camera, own mode only

        QJsonObject toJson() const;
    };

    static QString userRoot();
    // Lower-cases, strips a leading "user." and enforces 1-48 of a-z 0-9 _; empty on a bad slug.
    static QString normalizeSlug(const QString &slug);
    static QString packageDirForId(const QString &id);
    static bool isUserDir(const QString &packageDir);

    // files maps a bare file name to its text: "main.lua", "<name>.vert", "<name>.frag". The id is
    // forced to user.<slug>. Nothing is installed unless the package validates.
    static Result create(const QString &slug, const QJsonObject &manifest, const QMap<QString, QString> &files,
                         QSize canvas = QSize(1920, 1080));

    // Partial updates: a null manifest keeps the current one, `files` overwrite or add files, and
    // `removeFiles` drops others. The id and folder stay, so clips using it pick the change up.
    static Result update(const QString &id, const std::optional<QJsonObject> &manifest,
                         const QMap<QString, QString> &files, const QStringList &removeFiles = {},
                         QSize canvas = QSize(1920, 1080));

    // The same staging and validation as create/update, but nothing is installed. For an editor
    // that wants live feedback; `id` may be empty for a package that has no slug yet.
    static Result check(const QString &id, const std::optional<QJsonObject> &manifest,
                        const QMap<QString, QString> &files, QSize canvas = QSize(1920, 1080));

    static Source getSource(const QString &id);

    // Removes a user package and rescans. Clips using it keep their data and render nothing.
    static Result remove(const QString &id);

    // How many procedural clips in the project use the package.
    static int usesInProject(const Project &project, const QString &id);

    static EvaluateResult evaluate(const EvaluateRequest &request);
};

} // namespace drift
