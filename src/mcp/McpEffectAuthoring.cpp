#include "mcp/McpEffectAuthoring.h"

#include "engine/AddonPackage.h"
#include "engine/AudioEffectCatalog.h"
#include "engine/EffectCatalog.h"
#include "engine/EffectPackageLoader.h"
#include "engine/GlRuntime.h"
#include "engine/TransitionCatalog.h"
#include "engine/TransitionPackageLoader.h"
#include "engine/audio/AudioEffectFactory.h"
#include "mcp/McpJson.h"
#include "models/AddonManager.h"
#include "models/AppController.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include <optional>

namespace drift::mcp::authoring {
namespace {

constexpr qint64 kMaxPackageBytes = 16ll * 1024 * 1024;

struct Kind
{
    QString name;     // "effect" | "transition" | "audio_effect"
    QString subdir;   // also the addon kind
    QString manifest; // file name inside the package folder
};

std::optional<Kind> kindFor(const QString &name)
{
    if (name == QLatin1String("effect"))
        return Kind{name, QStringLiteral("effects"), QStringLiteral("effect.json")};
    if (name == QLatin1String("transition"))
        return Kind{name, QStringLiteral("transitions"), QStringLiteral("transition.json")};
    if (name == QLatin1String("audio_effect"))
        return Kind{name, QStringLiteral("audio-effects"), QStringLiteral("audio-effect.json")};
    return std::nullopt;
}

QJsonObject badKind(const QString &name)
{
    return err("bad_args", QStringLiteral("kind must be effect, transition or audio_effect (got \"%1\")").arg(name));
}

QString userRoot(const Kind &kind)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(kind.subdir);
}

bool isUserDir(const Kind &kind, const QString &packageDir)
{
    return !packageDir.isEmpty()
        && QDir::cleanPath(packageDir).startsWith(QDir::cleanPath(userRoot(kind)) + QLatin1Char('/'));
}

QString packageDirForId(const Kind &kind, const QString &id)
{
    if (kind.name == QLatin1String("effect")) {
        const EffectPresetEntry *def = effectDefForId(id);
        return def ? def->gpu.packageDir : QString();
    }
    if (kind.name == QLatin1String("transition")) {
        const TransitionPresetEntry *def = transitionDefForId(id);
        return def ? def->gpu.packageDir : QString();
    }
    const AudioEffectEntry *def = audioEffectDefForId(id);
    return def ? def->packageDir : QString();
}

QString listOpFor(const Kind &kind)
{
    if (kind.name == QLatin1String("effect"))
        return QStringLiteral("list_effects");
    if (kind.name == QLatin1String("transition"))
        return QStringLiteral("list_transitions");
    return QStringLiteral("list_audio_effects");
}

void reload(AppController *controller, const Kind &kind)
{
    if (controller && controller->addonManager()) {
        controller->addonManager()->reloadForKinds({kind.subdir});
        return;
    }
    if (kind.name == QLatin1String("effect"))
        reloadEffectCatalog();
    else if (kind.name == QLatin1String("transition"))
        reloadTransitionCatalog();
    else
        reloadAudioEffectCatalog();
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

// Writes the manifest and files into a fresh `dir`. Returns an error object, empty on success.
QJsonObject stagePackage(const Kind &kind, const QString &dir, QJsonObject manifest, const QJsonObject &args)
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
QJsonObject validatePackage(const Kind &kind, const QString &dir, QString *shaderCheck)
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
QJsonObject installPackage(AppController *controller, const Kind &kind, const QString &target,
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

    QJsonObject error = stagePackage(kind, staging, manifest, args);
    if (!error.isEmpty())
        return error;
    QString shaderCheck;
    error = validatePackage(kind, staging, &shaderCheck);
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

int usesInProject(AppController *controller, const Kind &kind, const QString &id)
{
    if (!controller)
        return 0;
    int count = 0;
    controller->project()->forEachTrackList([&](const QList<drift::Track> &tracks) {
        for (const drift::Track &track : tracks) {
            if (kind.name == QLatin1String("transition")) {
                for (const drift::Transition &t : track.transitions)
                    count += t.kindId == id ? 1 : 0;
                continue;
            }
            for (const drift::Clip &clip : track.clips) {
                const QList<drift::Effect> &effects =
                    kind.name == QLatin1String("effect") ? clip.effects : clip.audioEffects;
                for (const drift::Effect &effect : effects)
                    count += effect.catalogId == id ? 1 : 0;
            }
        }
    });
    return count;
}

bool sandboxed()
{
#if defined(Q_OS_ANDROID)
    return false;
#else
    return qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists(QStringLiteral("/.flatpak-info"))
        || qEnvironmentVariableIsSet("SNAP");
#endif
}

const char *const kGuideCommon = R"(# Authoring Drift effects

create_effect writes a package into My Effects; the returned id goes straight into add_effect,
add_transition or add_audio_effect. Ids are always "user.<slug>". Fork a bundled one with
get_effect_source({kind, id}) — every list_effects / list_transitions / list_audio_effects id works.
Iterate with update_effect({kind, id, manifest, files}); export_effect writes a shareable .driftfx.

## Parameters (all kinds)

parameters[] entries become inspector controls. Common fields: identifier (also the GLSL uniform
name), displayName, type, defaultValue, minValue, maxValue, group (folds into a named section),
groupCollapsed.

| type | GUI control | GLSL uniform | notes |
|---|---|---|---|
| float (default) | slider | float | min/max/default; optional step snaps; keyframable |
| int | slider snapping to whole numbers | float (rounded) | keyframable |
| bool | switch | float 0/1 | keyframable (rounds at 0.5) |
| color | swatch + picker | vec3 | defaultValue "#rrggbb"; "alpha": true → vec4 + opacity slider, default "#rrggbbaa"; optional "swatches": ["#rrggbb"…]; "enables": "<bool id>" flips that switch on when a colour is picked; keyframable |
| vec2 / point | 2D pad | vec2 | defaultValue [x, y]; minValue/maxValue [lo, lo]/[hi, hi] (one shared range); keyframable per axis |
| enum / choice | dropdown | float (option index) | options: ["A", "B", …], at least two; defaultValue is the index |

fixedParams: {"name": value} binds hidden uniforms (numbers, "#rrggbb" colours, enum option strings).
Audio effects use the same parameter grammar, but only float/int/bool/enum make sense there.
)";

const char *const kGuideGpu = R"(
## GPU pipeline (effects and transitions)

"pipeline": {
  "intermediateBuffers": [{"id": "blurH", "scale": 0.5}],      // optional; scale vs canvas
  "textures": [{"id": "grain", "file": "grain.png"}],           // optional static images (binary_files)
  "passes": [{
    "passIndex": 0,
    "fragmentShader": "main.frag",                               // a key in `files`
    "inputs": [{"type": "source_texture"}],                      // or {"type":"buffer","id":…} / {"type":"texture","id":…}
    "output": {"type": "canvas"}                                 // or {"type":"buffer","id":…}
  }]
}
The last pass writes "canvas". Inputs bind in order as u_currentTexture (unit 0), u_texture1, u_texture2…
Static textures wrap (GL_REPEAT), no mipmaps.

GLSL: write `#version 330 core` (translated to GLSL ES on mobile), `in vec2 v_texCoord;`
(v_texCoord.y == 0 is the top), `out vec4 fragColor;`. Declare each parameter as a uniform named by
its identifier. Engine uniforms: u_resolution (vec2, pixels), u_time (float seconds),
u_frameIndex (int), u_timeUs (float). Do not declare parameters with reserved names: u_currentTexture,
u_textureN, u_resolution, u_time, u_timeUs, u_frameIndex, u_progress, u_fromTexture, u_toTexture,
u_depth*, u_hasDepth, u_templateBounds, u_meshAspect, u_face*.
Shaders are test-compiled on create/update; compile errors come back as shader_compile_failed with
the driver log per pass. A shader that fails at render time shows the frame unchanged.
)";

const char *const kGuideEffect = R"(
## Video effect — effect.json (kind: "effect")

{"displayName": "Teal Tint", "category": "color", "order": 900, "backend": "gpu",
 "parameters": [{"identifier": "amount", "displayName": "Amount", "type": "float",
                 "minValue": 0, "maxValue": 1, "defaultValue": 0.5},
                {"identifier": "tint", "displayName": "Tint", "type": "color", "defaultValue": "#00b3a4"}],
 "pipeline": {"intermediateBuffers": [], "passes": [{"passIndex": 0, "fragmentShader": "main.frag",
   "inputs": [{"type": "source_texture"}], "output": {"type": "canvas"}}]}}

files: {"main.frag": "#version 330 core\nin vec2 v_texCoord; out vec4 fragColor;\nuniform sampler2D u_currentTexture; uniform float amount; uniform vec3 tint;\nvoid main() { vec4 c = texture(u_currentTexture, v_texCoord); fragColor = vec4(mix(c.rgb, c.rgb * tint * 2.0, amount), c.a); }"}

A thumbnail.png in binary_files becomes the browser card image.
category is a free slug (color, blur, distort, stylize, dreamy, …); backend must be "gpu".
Keep alpha: write the source alpha back unless the effect means to change coverage.
"requires": "face" / "depth" exist for face- and depth-aware effects; read a bundled one
(e.g. depth.fog) with get_effect_source before using them.
)";

const char *const kGuideTransition = R"(
## Transition — transition.json (kind: "transition")

Same parameters and pipeline grammar. source_texture takes "index": 0 (outgoing) or 1 (incoming),
and every pass may also declare u_fromTexture, u_toTexture and u_progress (0..1 across the window).
Both sides are full-canvas straight-alpha layers, transparent outside the clip — composite with a
proper "over", don't assume opaque. Optional "audioCurve": "crossfade" (default) | "dip" | "hold".

Determinism rule: output must be a pure function of (from, to, u_progress, parameters). Never use
u_time or u_frameIndex; derive noise from v_texCoord and u_progress (quantise progress to hold a
glitch). Intermediate buffers do not persist between frames.

{"displayName": "Soft Wipe", "category": "basic", "order": 900,
 "parameters": [{"identifier": "softness", "displayName": "Softness", "type": "float",
                 "minValue": 0.0, "maxValue": 0.5, "defaultValue": 0.1}],
 "pipeline": {"intermediateBuffers": [], "passes": [{"passIndex": 0, "fragmentShader": "main.frag",
   "inputs": [{"type": "source_texture", "index": 0}, {"type": "source_texture", "index": 1}],
   "output": {"type": "canvas"}}]}}

files: {"main.frag": "#version 330 core\nin vec2 v_texCoord; out vec4 fragColor;\nuniform sampler2D u_fromTexture; uniform sampler2D u_toTexture; uniform float u_progress; uniform float softness;\nvoid main() { float e = u_progress * (1.0 + 2.0 * softness) - softness; float m = smoothstep(e - softness, e + softness, v_texCoord.x); fragColor = mix(texture(u_toTexture, v_texCoord), texture(u_fromTexture, v_texCoord), m); }"}
)";

const char *const kGuideAudio = R"(
## Audio effect — audio-effect.json (kind: "audio_effect")

Audio effects carry no code: one of Drift's built-in DSP processors, with its parameters exposed
under your names, ranges and defaults. Send the manifest only (no files).

{"displayName": "Long Hall Echo", "category": "space", "order": 900, "backend": "juce",
 "processor": "echo", "prerollMs": 2000, "parameters": [ …same identifiers the processor reads… ]}

A parameter's identifier must be one the processor reads — copy them from a bundled effect that
uses the same processor (see `processors` below, then get_effect_source) and change displayName,
ranges and defaults. prerollMs is how much earlier audio the processor needs to be correct from an
arbitrary start (0 for stateless processors, the tail length for echoes and reverbs).
)";

QJsonArray processorTable()
{
    QJsonArray rows;
    for (const QString &processor : drift::audiofx::processorIds()) {
        QJsonArray examples;
        QJsonArray params;
        for (const AudioEffectEntry &entry : audioEffectCatalog()) {
            if (entry.processorId != processor)
                continue;
            examples.append(entry.id);
            if (params.isEmpty()) {
                for (const drift::EffectParamSpec &spec : entry.parameters)
                    params.append(spec.key);
            }
        }
        rows.append(QJsonObject{{QStringLiteral("processor"), processor},
                                {QStringLiteral("examples"), examples},
                                {QStringLiteral("params"), params}});
    }
    return rows;
}

} // namespace

QJsonObject guide(const QString &kind)
{
    if (!kind.isEmpty() && !kindFor(kind))
        return badKind(kind);
    QString text = QString::fromUtf8(kGuideCommon);
    if (kind.isEmpty() || kind != QLatin1String("audio_effect"))
        text += QString::fromUtf8(kGuideGpu);
    if (kind.isEmpty() || kind == QLatin1String("effect"))
        text += QString::fromUtf8(kGuideEffect);
    if (kind.isEmpty() || kind == QLatin1String("transition"))
        text += QString::fromUtf8(kGuideTransition);
    QJsonObject reply{{QStringLiteral("guide"), text}};
    if (kind.isEmpty() || kind == QLatin1String("audio_effect")) {
        reply.insert(QStringLiteral("guide"), text + QString::fromUtf8(kGuideAudio));
        reply.insert(QStringLiteral("processors"), processorTable());
    }
    return ok(reply);
}

QJsonObject create(AppController *controller, const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = kindFor(kindName);
    if (!kind)
        return badKind(kindName);

    static const QRegularExpression kSlug(QStringLiteral("^[a-z0-9_]{1,48}$"));
    QString slug = args.value(QStringLiteral("slug")).toString().trimmed().toLower();
    if (slug.startsWith(QLatin1String("user.")))
        slug = slug.mid(5);
    if (!kSlug.match(slug).hasMatch())
        return err("bad_args", QStringLiteral("slug must be 1–48 of a-z, 0-9, _ (got \"%1\")").arg(slug));

    const QString id = QStringLiteral("user.") + slug;
    const QString target = QDir(userRoot(*kind)).filePath(slug);
    if (QFileInfo::exists(target) || !packageDirForId(*kind, id).isEmpty())
        return err("exists", QStringLiteral("%1 already exists — use update_effect, or pick another slug").arg(id));
    return installPackage(controller, *kind, target, id, args);
}

QJsonObject update(AppController *controller, const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = kindFor(kindName);
    if (!kind)
        return badKind(kindName);
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString dir = packageDirForId(*kind, id);
    if (dir.isEmpty())
        return err("not_found", QStringLiteral("No %1 with id %2 — call %3").arg(kind->name, id, listOpFor(*kind)));
    if (!isUserDir(*kind, dir))
        return err("not_user_effect", QStringLiteral("%1 ships with Drift or an add-on; fork it with create_effect instead").arg(id));
    return installPackage(controller, *kind, QDir::cleanPath(dir), id, args);
}

QJsonObject source(const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = kindFor(kindName);
    if (!kind)
        return badKind(kindName);
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString dir = packageDirForId(*kind, id);
    if (dir.isEmpty())
        return err("not_found", QStringLiteral("No %1 package with id %2 — call %3").arg(kind->name, id, listOpFor(*kind)));

    QFile manifestFile(QDir(dir).filePath(kind->manifest));
    if (!manifestFile.open(QIODevice::ReadOnly))
        return err("io_error", QStringLiteral("cannot read %1").arg(manifestFile.fileName()));
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();

    QJsonObject files;
    QJsonArray binaries;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = QDir(dir).relativeFilePath(path);
        if (rel == kind->manifest)
            continue;
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QLatin1String("frag") || suffix == QLatin1String("glsl")) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly))
                files.insert(rel, QString::fromUtf8(f.readAll()));
        } else {
            binaries.append(rel);
        }
    }
    return ok({{QStringLiteral("id"), id},
               {QStringLiteral("kind"), kind->name},
               {QStringLiteral("user"), isUserDir(*kind, dir)},
               {QStringLiteral("manifest"), manifest},
               {QStringLiteral("files"), files},
               {QStringLiteral("binaryFiles"), binaries}});
}

QJsonObject remove(AppController *controller, const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = kindFor(kindName);
    if (!kind)
        return badKind(kindName);
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString dir = packageDirForId(*kind, id);
    if (dir.isEmpty())
        return err("not_found", QStringLiteral("No %1 with id %2 — call %3").arg(kind->name, id, listOpFor(*kind)));
    if (!isUserDir(*kind, dir))
        return err("not_user_effect", QStringLiteral("%1 ships with Drift or an add-on and cannot be deleted").arg(id));

    const int uses = usesInProject(controller, *kind, id);
    if (uses > 0 && !args.value(QStringLiteral("force")).toBool()) {
        QJsonObject e = err("in_use", QStringLiteral("%1 is used %2 time(s) in this project; those uses render "
                                                     "as passthrough once it is gone. Pass force:true to delete anyway")
                                          .arg(id).arg(uses));
        e.insert(QStringLiteral("uses"), uses);
        return e;
    }
    if (!QDir(dir).removeRecursively())
        return err("io_error", QStringLiteral("cannot remove %1").arg(dir));
    reload(controller, *kind);
    return ok({{QStringLiteral("id"), id}, {QStringLiteral("deleted"), true}, {QStringLiteral("uses"), uses}});
}

QJsonObject exportPackage(const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = kindFor(kindName);
    if (!kind)
        return badKind(kindName);
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString dir = packageDirForId(*kind, id);
    if (dir.isEmpty())
        return err("not_found", QStringLiteral("No %1 with id %2 — call %3").arg(kind->name, id, listOpFor(*kind)));
    if (!isUserDir(*kind, dir))
        return err("not_user_effect", QStringLiteral("Only My Effects packages can be exported; fork %1 with create_effect first").arg(id));

    QFile manifestFile(QDir(dir).filePath(kind->manifest));
    manifestFile.open(QIODevice::ReadOnly);
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
        if (drift::addon::writeUserPackage(dir, kind->subdir, meta, requested, &error))
            return ok({{QStringLiteral("path"), QDir::cleanPath(requested)}, {QStringLiteral("fellBack"), false}});
        reason = error;
    }

    QString error;
    if (!drift::addon::writeUserPackage(dir, kind->subdir, meta, fallback, &error))
        return err("io_error", error);
    return ok({{QStringLiteral("path"), fallback},
               {QStringLiteral("fellBack"), !requested.isEmpty()},
               {QStringLiteral("reason"), reason}});
}

} // namespace drift::mcp::authoring
