#include "app/forge/PackageInstaller.h"

#include "engine/addons/AddonPackage.h"
#include "engine/packages/animations/AnimationCatalog.h"
#include "engine/packages/animations/AnimationPackageLoader.h"
#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/packages/effects/EffectCatalog.h"
#include "engine/packages/effects/EffectPackageLoader.h"
#include "engine/gpu/GlRuntime.h"
#include "engine/packages/transitions/TransitionCatalog.h"
#include "engine/packages/transitions/TransitionPackageLoader.h"
#include "app/mcp/protocol/McpJson.h"
#include "app/online/addons/AddonManager.h"
#include "app/editor/AppController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace drift::forge {

using drift::mcp::err;
using drift::mcp::ok;

namespace {

constexpr qint64 kMaxPackageBytes = 16ll * 1024 * 1024;

bool sandboxed()
{
#if defined(Q_OS_ANDROID)
    return false;
#else
    return qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists(QStringLiteral("/.flatpak-info"))
        || qEnvironmentVariableIsSet("SNAP");
#endif
}

} // namespace

std::optional<PackageKind> packageKind(const QString &name)
{
    if (name == QLatin1String("effect"))
        return PackageKind{name, QStringLiteral("effects"), QStringLiteral("effect.json")};
    if (name == QLatin1String("transition"))
        return PackageKind{name, QStringLiteral("transitions"), QStringLiteral("transition.json")};
    if (name == QLatin1String("animation"))
        return PackageKind{name, QStringLiteral("animations"), QStringLiteral("animation.json")};
    if (name == QLatin1String("audio_effect"))
        return PackageKind{name, QStringLiteral("audio-effects"), QStringLiteral("audio-effect.json")};
    return std::nullopt;
}

std::optional<PackageKind> packageKindForAddon(const QString &subdir)
{
    for (const char *name : {"effect", "transition", "animation", "audio_effect"}) {
        const auto kind = packageKind(QLatin1String(name));
        if (kind->subdir == subdir)
            return kind;
    }
    return std::nullopt;
}

QString userRoot(const PackageKind &kind)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(kind.subdir);
}

bool isUserDir(const PackageKind &kind, const QString &packageDir)
{
    return !packageDir.isEmpty()
        && QDir::cleanPath(packageDir).startsWith(QDir::cleanPath(userRoot(kind)) + QLatin1Char('/'));
}

QString packageDirForId(const PackageKind &kind, const QString &id)
{
    if (kind.name == QLatin1String("effect")) {
        const EffectPresetEntry *def = effectDefForId(id);
        return def ? def->gpu.packageDir : QString();
    }
    if (kind.name == QLatin1String("transition")) {
        const TransitionPresetEntry *def = transitionDefForId(id);
        return def ? def->gpu.packageDir : QString();
    }
    if (kind.name == QLatin1String("animation")) {
        const AnimationPresetEntry *def = animationDefForId(id);
        return def ? def->gpu.packageDir : QString();
    }
    const AudioEffectEntry *def = audioEffectDefForId(id);
    return def ? def->packageDir : QString();
}

QString listOp(const PackageKind &kind)
{
    if (kind.name == QLatin1String("effect"))
        return QStringLiteral("list_effects");
    if (kind.name == QLatin1String("transition"))
        return QStringLiteral("list_transitions");
    if (kind.name == QLatin1String("animation"))
        return QStringLiteral("list_animations");
    return QStringLiteral("list_audio_effects");
}

void reload(AppController *controller, const PackageKind &kind)
{
    if (controller && controller->addonManager()) {
        controller->addonManager()->reloadForKinds({kind.subdir});
        return;
    }
    if (kind.name == QLatin1String("effect"))
        reloadEffectCatalog();
    else if (kind.name == QLatin1String("transition"))
        reloadTransitionCatalog();
    else if (kind.name == QLatin1String("animation"))
        reloadAnimationCatalog();
    else
        reloadAudioEffectCatalog();
}

static bool writeFile(const QString &path, const QByteArray &bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) {
        *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

// Writes the manifest and files into a fresh `dir`. Returns an error object, empty on success.
QJsonObject stage(const PackageKind &kind, const QString &dir, QJsonObject manifest, const QJsonObject &args)
{
    static const QRegularExpression kFileName(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.-]{0,63}$"));
    const QJsonObject files = args.value(QStringLiteral("files")).toObject();
    const QJsonObject binaries = args.value(QStringLiteral("binary_files")).toObject();

    QMap<QString, QByteArray> contents;
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString suffix = QFileInfo(it.key()).suffix().toLower();
        if (!kFileName.match(it.key()).hasMatch()
            || (suffix != QLatin1String("frag") && suffix != QLatin1String("glsl"))) {
            return err("bad_args", QStringLiteral("files: \"%1\" must be a bare .frag or .glsl file name").arg(it.key()));
        }
        if (!it.value().isString())
            return err("bad_args", QStringLiteral("files.%1 must be the source text").arg(it.key()));
        contents.insert(it.key(), it.value().toString().toUtf8());
    }
    for (auto it = binaries.begin(); it != binaries.end(); ++it) {
        if (!kFileName.match(it.key()).hasMatch() || QFileInfo(it.key()).suffix().toLower() != QLatin1String("png"))
            return err("bad_args", QStringLiteral("binary_files: \"%1\" must be a bare .png file name").arg(it.key()));
        if (contents.contains(it.key()))
            return err("bad_args", QStringLiteral("%1 appears in both files and binary_files").arg(it.key()));
        const auto decoded = QByteArray::fromBase64Encoding(it.value().toString().toLatin1(),
                                                            QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded)
            return err("bad_args", QStringLiteral("binary_files.%1 is not valid base64").arg(it.key()));
        contents.insert(it.key(), *decoded);
    }

    // Forge's own export: any bare file name (textures, forge.json, thumbnails), base64 encoded.
    const QJsonObject trusted = args.value(QStringLiteral("trusted_files")).toObject();
    for (auto it = trusted.begin(); it != trusted.end(); ++it) {
        if (!kFileName.match(it.key()).hasMatch() || it.key() == kind.manifest)
            return err("bad_args", QStringLiteral("trusted_files: bad file name \"%1\"").arg(it.key()));
        contents.insert(it.key(), QByteArray::fromBase64(it.value().toString().toLatin1()));
    }

    if (kind.name == QLatin1String("audio_effect")) {
        if (!files.isEmpty())
            return err("bad_args", QStringLiteral("audio effects carry no shaders; send the manifest only"));
        if (!manifest.contains(QStringLiteral("backend")))
            manifest.insert(QStringLiteral("backend"), QStringLiteral("juce"));
    } else if (kind.name == QLatin1String("effect")) {
        const QString backend = manifest.value(QStringLiteral("backend")).toString(QStringLiteral("gpu"));
        if (backend != QLatin1String("gpu"))
            return err("bad_args", QStringLiteral("manifest.backend must be \"gpu\" for an agent-made effect"));
        manifest.insert(QStringLiteral("backend"), backend);
    }

    qint64 total = 0;
    for (const QByteArray &bytes : std::as_const(contents))
        total += bytes.size();
    if (total > kMaxPackageBytes)
        return err("bad_args", QStringLiteral("package files exceed %1 MiB").arg(kMaxPackageBytes >> 20));

    QDir(dir).removeRecursively();
    if (!QDir().mkpath(dir))
        return err("io_error", QStringLiteral("cannot create %1").arg(dir));
    QString error;
    if (!writeFile(QDir(dir).filePath(kind.manifest), QJsonDocument(manifest).toJson(QJsonDocument::Indented), &error))
        return err("io_error", error);
    for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
        if (!writeFile(QDir(dir).filePath(it.key()), it.value(), &error))
            return err("io_error", error);
    }
    return {};
}

// Parses the staged package and, for GPU kinds, compiles its shaders. Fills shaderCheck with
// "ok", "skipped" or "n/a". Returns an error object, empty on success.
QJsonObject validate(const PackageKind &kind, const QString &dir, QString *shaderCheck)
{
    QString error;
    drift::GpuEffectDefinition gpu;
    if (kind.name == QLatin1String("effect")) {
        const EffectPresetEntry entry = EffectPackageLoader::loadPackage(dir, &error);
        if (error.isEmpty() && !entry.gpu.valid)
            error = entry.gpu.errorMessage.isEmpty() ? QStringLiteral("invalid pipeline") : entry.gpu.errorMessage;
        gpu = entry.gpu;
    } else if (kind.name == QLatin1String("transition")) {
        const TransitionPresetEntry entry = TransitionPackageLoader::loadPackage(dir, &error);
        if (error.isEmpty() && !entry.gpu.valid)
            error = entry.gpu.errorMessage.isEmpty() ? QStringLiteral("invalid pipeline") : entry.gpu.errorMessage;
        gpu = entry.gpu;
    } else if (kind.name == QLatin1String("animation")) {
        const AnimationPresetEntry entry = AnimationPackageLoader::loadPackage(dir, &error);
        if (error.isEmpty() && !entry.gpu.valid)
            error = entry.gpu.errorMessage.isEmpty() ? QStringLiteral("invalid pipeline") : entry.gpu.errorMessage;
        gpu = entry.gpu;
    } else {
        loadAudioEffectPackage(dir, &error);
        *shaderCheck = QStringLiteral("n/a");
        return error.isEmpty() ? QJsonObject{} : err("bad_manifest", error);
    }
    if (!error.isEmpty())
        return err("bad_manifest", error);

    QStringList compileErrors;
    if (!drift::gl::runtime().validateProgram(gpu, &compileErrors)) {
        *shaderCheck = QStringLiteral("skipped");
        return {};
    }
    if (!compileErrors.isEmpty()) {
        QJsonObject e = err("shader_compile_failed", compileErrors.join(QLatin1Char('\n')));
        e.insert(QStringLiteral("errors"), QJsonArray::fromStringList(compileErrors));
        return e;
    }
    *shaderCheck = QStringLiteral("ok");
    return {};
}

// Stage → validate → swap into `target`. `id` is written into the manifest.
QJsonObject install(AppController *controller, const PackageKind &kind, const QString &target,
                           const QString &id, const QJsonObject &args)
{
    QJsonObject manifest = args.value(QStringLiteral("manifest")).toObject();
    manifest.insert(QStringLiteral("id"), id);

    const QString folder = QFileInfo(target).fileName();
    const QString staging = QDir(userRoot(kind)).filePath(QStringLiteral(".mcp-") + folder);
    struct Cleanup
    {
        QString path;
        ~Cleanup() { QDir(path).removeRecursively(); }
    } cleanup{staging};

    QJsonObject error = stage(kind, staging, manifest, args);
    if (!error.isEmpty())
        return error;
    QString shaderCheck;
    error = validate(kind, staging, &shaderCheck);
    if (!error.isEmpty())
        return error;

    const QString previous = QDir(userRoot(kind)).filePath(QStringLiteral(".mcp-old-") + folder);
    QDir(previous).removeRecursively();
    const bool replacing = QFileInfo::exists(target);
    if (replacing && !QDir().rename(target, previous))
        return err("io_error", QStringLiteral("cannot move the old %1 aside").arg(target));
    if (!QDir().rename(staging, target)) {
        if (replacing)
            QDir().rename(previous, target);
        return err("io_error", QStringLiteral("cannot move the package into %1").arg(target));
    }
    QDir(previous).removeRecursively();

    reload(controller, kind);
    QJsonObject reply{{QStringLiteral("id"), id},
                      {QStringLiteral("kind"), kind.name},
                      {QStringLiteral("shaderCheck"), shaderCheck}};
    if (packageDirForId(kind, id) != target) {
        reply.insert(QStringLiteral("warning"),
                     QStringLiteral("Installed, but %1 resolves to another package with the same id, "
                                    "which wins the catalog lookup").arg(id));
    }
    if (shaderCheck == QLatin1String("skipped"))
        reply.insert(QStringLiteral("shaderCheckReason"), QStringLiteral("OpenGL is unavailable, so the shaders were not compiled"));
    return ok(reply);
}

QJsonObject exportDriftfx(const PackageKind &kind, const QJsonObject &args)
{
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString dir = packageDirForId(kind, id);
    if (dir.isEmpty())
        return err("not_found", QStringLiteral("No %1 with id %2 — call %3").arg(kind.name, id, listOp(kind)));
    if (!isUserDir(kind, dir))
        return err("not_user_effect", QStringLiteral("Only My Effects packages can be exported; fork %1 with create_effect first").arg(id));

    QFile manifestFile(QDir(dir).filePath(kind.manifest));
    if (!manifestFile.open(QIODevice::ReadOnly))
        return err("io_error", manifestFile.errorString());
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    const QString folder = QFileInfo(QDir::cleanPath(dir)).fileName();
    const QString displayName = manifest.value(QStringLiteral("displayName")).toString(folder);
    const QJsonObject meta{
        {QStringLiteral("id"), id.startsWith(QLatin1String("user.")) ? id : QStringLiteral("user.") + id},
        {QStringLiteral("name"), displayName},
        {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("author"), args.value(QStringLiteral("author")).toString()},
        {QStringLiteral("description"), args.value(QStringLiteral("description")).toString()},
        {QStringLiteral("details"), QString()},
        {QStringLiteral("license"), QString()},
        {QStringLiteral("generator"), QStringLiteral("drift-mcp")},
    };

    const QString fallbackDir =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("exports"));
    const QString fallback = QDir(fallbackDir).filePath(folder + QStringLiteral(".driftfx"));
    QString requested = args.value(QStringLiteral("path")).toString().trimmed();
    if (requested.startsWith(QLatin1String("file:")))
        requested = QUrl(requested).toLocalFile();

    QString reason;
    if (requested.isEmpty()) {
        reason = QStringLiteral("no path given");
    } else if (!QDir::isAbsolutePath(requested) || !requested.endsWith(QLatin1String(".driftfx"))) {
        return err("bad_args", QStringLiteral("path must be an absolute .driftfx path"));
    } else if (sandboxed() && !QDir::cleanPath(requested).startsWith(QDir::cleanPath(fallbackDir) + QLatin1Char('/'))) {
        // Inside flatpak or snap a write can "succeed" into a private /tmp or an unmapped home
        // directory the user never sees, so only the app's own data dir is trusted.
        reason = QStringLiteral("Drift is sandboxed and can only write inside its own data folder");
    } else {
        QString error;
        if (drift::addon::writeUserPackage(dir, kind.subdir, meta, requested, &error))
            return ok({{QStringLiteral("path"), QDir::cleanPath(requested)}, {QStringLiteral("fellBack"), false}});
        reason = error;
    }

    QString error;
    if (!drift::addon::writeUserPackage(dir, kind.subdir, meta, fallback, &error))
        return err("io_error", error);
    return ok({{QStringLiteral("path"), fallback},
               {QStringLiteral("fellBack"), !requested.isEmpty()},
               {QStringLiteral("reason"), reason}});
}

} // namespace drift::forge
