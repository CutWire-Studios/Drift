#include "app/mcp/tools/McpEffectAuthoring.h"

#include "app/forge/PackageInstaller.h"
#include "engine/packages/audio/AudioEffectCatalog.h"
#include "engine/audio/dsp/AudioEffectFactory.h"
#include "engine/audio/dsp/graph/PedalCatalog.h"
#include "app/mcp/protocol/McpJson.h"
#include "app/online/addons/AddonManager.h"
#include "app/editor/AppController.h"

#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <optional>

namespace drift::mcp::authoring {
namespace {

using forge::PackageKind;
using forge::packageKind;

QJsonObject badKind(const QString &name)
{
    return err("bad_args", QStringLiteral("kind must be effect, transition, animation or audio_effect (got \"%1\")").arg(name));
}


int usesInProject(AppController *controller, const PackageKind &kind, const QString &id)
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
            if (kind.name == QLatin1String("animation")) {
                for (const drift::Clip &clip : track.clips)
                    count += (clip.animIn.kindId == id ? 1 : 0) + (clip.animOut.kindId == id ? 1 : 0);
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

const char *const kGuideCommon = R"(# Authoring Drift effects

create_effect writes a package into My Effects; the returned id goes straight into add_effect,
add_transition, set_animation or add_audio_effect. Ids are always "user.<slug>". Fork a bundled one with
get_effect_source({kind, id}) — every list_effects / list_transitions / list_animations / list_audio_effects id works.
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
## GPU pipeline (effects, transitions and animations)

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
u_frameIndex (int, at the project frame rate), u_frameRate (float), u_timeUs (float). Do not declare parameters with reserved names: u_currentTexture,
u_textureN, u_resolution, u_time, u_timeUs, u_frameIndex, u_frameRate, u_progress, u_direction, u_fromTexture, u_toTexture,
u_depth*, u_hasDepth, u_clipMask, u_hasClipMask, u_history*, u_prevOutput, u_templateBounds, u_meshAspect, u_face*.
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
"requires": "mask" reads the clip's masks instead of cutting the clip out with them: every pass gets
float driftMask(vec2 uv) (1 inside the mask stack, 0 outside) and u_hasClipMask (0: the clip has no
mask, driftMask is 0 everywhere). Pair it with Cut out subject for a person matte.
)";

const char *const kGuideTime = R"(
## Time effects (kind: "effect")

Two optional top-level manifest fields let an effect look back in time; a package may set both.
List their nextFeatures names as well, so an older Drift refuses the package ("needs a newer
Drift") instead of failing to compile it:
  "history": N (whole number 1-32)  ->  "nextFeatures": [..., "history:source"]
  "previousOutput": true            ->  "nextFeatures": [..., "history:previous-output"]

history keeps the clip's last N source frames (the picture before the clip's effect chain, one per
project frame). Every pass then has, declared for you:
  vec4 historyFrame(float age, vec2 uv)  the source frame `age` project frames ago (1 = the previous
                                         one) at uv, same uv convention as the source. Ages round
                                         and clamp to what is recorded; with nothing recorded it
                                         reads the effect's own input.
  float historyCount()                   frames recorded so far: 0 on the first frame and after a
                                         seek, up to N.
previousOutput gives sampler2D u_prevOutput, also declared for you: this effect's own final output
for the previous project frame, same size and uv. It is transparent on the first frame and after a
seek. Feed it back (scale or rotate it a little, fade it by a decay) for trails, tunnels and echoes.

previousOutput must be a JSON boolean (true); any other value counts as false.

Those names are reserved: never declare historyFrame, historyCount, u_history* or u_prevOutput
yourself (the engine already declares them, and redeclaring may fail to compile), and don't use them
without the matching field.
A seek, scrub, loop or trim clears the history, so an effect must look right while historyCount()
is 0: pass the picture through. Export composes frame after frame and is deterministic; effect
thumbnails and the CPU compositor always see an empty history. Long rings on large canvases are
stored downscaled, so historyFrame can be softer than the live frame.

{"displayName": "Echo Trail", "category": "stylize", "order": 900, "backend": "gpu",
 "history": 8, "previousOutput": true, "nextFeatures": ["history:source", "history:previous-output"],
 "parameters": [{"identifier": "echoes", "displayName": "Echoes", "type": "int",
                 "minValue": 1, "maxValue": 8, "defaultValue": 4},
                {"identifier": "decay", "displayName": "Decay", "type": "float",
                 "minValue": 0, "maxValue": 0.95, "defaultValue": 0.6}],
 "pipeline": {"intermediateBuffers": [], "passes": [{"passIndex": 0, "fragmentShader": "main.frag",
   "inputs": [{"type": "source_texture"}], "output": {"type": "canvas"}}]}}

files: {"main.frag": "#version 330 core\nin vec2 v_texCoord; out vec4 fragColor;\nuniform sampler2D u_currentTexture; uniform float echoes; uniform float decay;\nvoid main() { vec4 now = texture(u_currentTexture, v_texCoord); vec3 sum = now.rgb; float w = 1.0; float n = min(echoes, historyCount()); for (int i = 1; i <= 8; ++i) { if (float(i) > n) break; float k = pow(decay, float(i)); sum += historyFrame(float(i), v_texCoord).rgb * k; w += k; } vec4 prev = texture(u_prevOutput, v_texCoord); fragColor = vec4(max(sum / w, prev.rgb * decay), now.a); }"}
)";

const char *const kGuideTransition = R"(
## Transition — transition.json (kind: "transition")

Same parameters and pipeline grammar. source_texture takes "index": 0 (outgoing) or 1 (incoming),
and every pass may also declare u_fromTexture, u_toTexture and u_progress (0..1 across the window).
Both sides are full-canvas straight-alpha layers, transparent outside the clip — composite with a
proper "over", don't assume opaque. Optional "audioCurve": "crossfade" (default) | "dip" | "hold".

Determinism rule: output must be a pure function of (from, to, u_progress, parameters). Never use
u_time or u_frameIndex; derive noise from v_texCoord and u_progress (quantise progress to hold a
glitch). Intermediate buffers do not persist between frames. Transitions have no frame history:
history and previousOutput are for effects only.

{"displayName": "Soft Wipe", "category": "basic", "order": 900,
 "parameters": [{"identifier": "softness", "displayName": "Softness", "type": "float",
                 "minValue": 0.0, "maxValue": 0.5, "defaultValue": 0.1}],
 "pipeline": {"intermediateBuffers": [], "passes": [{"passIndex": 0, "fragmentShader": "main.frag",
   "inputs": [{"type": "source_texture", "index": 0}, {"type": "source_texture", "index": 1}],
   "output": {"type": "canvas"}}]}}

files: {"main.frag": "#version 330 core\nin vec2 v_texCoord; out vec4 fragColor;\nuniform sampler2D u_fromTexture; uniform sampler2D u_toTexture; uniform float u_progress; uniform float softness;\nvoid main() { float e = u_progress * (1.0 + 2.0 * softness) - softness; float m = smoothstep(e - softness, e + softness, v_texCoord.x); fragColor = mix(texture(u_toTexture, v_texCoord), texture(u_fromTexture, v_texCoord), m); }"}
)";

const char *const kGuideAnimation = R"(
## Animation — animation.json (kind: "animation")

A clip's in/out animation, set with set_animation({which: "animIn" | "animOut", kind: "<id>"}) and
listed by list_animations. Same parameters and pipeline grammar; the single input is the clip's
isolated layer: u_currentTexture, canvas-sized, straight alpha, transparent outside the clip.
source_texture takes only "index": 0.

Contract: u_progress runs 0..1 in time order across the animation window (already eased) and
u_direction is 0 for an in animation, 1 for an out animation. Write the shader once for both ends:
  float settled = mix(u_progress, 1.0 - u_progress, u_direction);
settled is 0 when the clip is hidden or displaced and 1 when it rests, for in and out alike.

Determinism rule: output must be a pure function of (u_currentTexture, u_progress, u_direction,
parameters). Never use u_time or u_frameIndex; derive noise from v_texCoord and u_progress.

Optional manifest fields: "directions": ["in", "out"] (default both; list one to restrict) and
"defaultDurationMs" (default 500).

{"displayName": "Slide Pop", "category": "basic", "order": 900, "directions": ["in", "out"],
 "defaultDurationMs": 400,
 "parameters": [{"identifier": "distance", "displayName": "Distance", "type": "float",
                 "minValue": 0.0, "maxValue": 1.0, "defaultValue": 0.3}],
 "pipeline": {"intermediateBuffers": [], "passes": [{"passIndex": 0, "fragmentShader": "main.frag",
   "inputs": [{"type": "source_texture", "index": 0}], "output": {"type": "canvas"}}]}}

files: {"main.frag": "#version 330 core\nin vec2 v_texCoord; out vec4 fragColor;\nuniform sampler2D u_currentTexture; uniform float u_progress; uniform float u_direction; uniform float distance;\nvoid main() { float settled = mix(u_progress, 1.0 - u_progress, u_direction); vec2 uv = v_texCoord + vec2(0.0, (1.0 - settled) * distance); fragColor = texture(u_currentTexture, uv) * settled; }"}
)";

const char *const kGuideAudio = R"(
## Audio effect — audio-effect.json (kind: "audio_effect")

Audio effects carry no code. Send the manifest only (no files). Two shapes:

1. One built-in processor, its parameters exposed under your names, ranges and defaults:

{"displayName": "Long Hall Echo", "category": "space", "order": 900, "backend": "juce",
 "processor": "echo", "prerollMs": 2000, "parameters": [ …same identifiers the processor reads… ]}

A parameter's identifier must be one the processor reads — copy them from a bundled effect that
uses the same processor (see `processors` below, then get_effect_source) and change displayName,
ranges and defaults. prerollMs is how much earlier audio the processor needs to be correct from an
arbitrary start (0 for stateless processors, the tail length for echoes and reverbs).

2. A graph of pedals ("processor": "graph"), wired from the `pedals` catalog below:

{"displayName": "Swirl", "category": "space", "backend": "juce", "processor": "graph", "prerollMs": 0,
 "parameters": [{"identifier": "space", "displayName": "Space", "type": "float",
                 "minValue": 0, "maxValue": 1, "defaultValue": 0.4}],
 "graph": {"version": 1,
   "modulators": [{"id": "m1", "type": "lfo", "knobs": {"rate": 0.5}}],
   "chain": [
     {"id": "f", "type": "filter", "knobs": {"mode": 2, "cutoff": 900},
      "mod": {"cutoff": [{"from": "m1", "depth": 0.35}]}},
     {"id": "s", "type": "split", "mode": "parallel", "crossfade": true, "blend": {"param": "space"},
      "lanes": [{"chain": []}, {"chain": [{"id": "r", "type": "reverb", "knobs": {"mix": 1}}]}]}]}}

- chain runs in order. Pedal types and their knobs (ids, ranges, choices as indices) are in
  `pedals.pedals`; classic.<processor> pedals are the processors of shape 1.
- A knob is a number or {"param": "<identifier>"}, which makes it a slider (and keyframable).
- split: "parallel" (same sound in every lane, summed; crossfade + blend for two lanes) or "bands"
  (with "crossovers": [Hz, …], one per lane boundary). 2–4 lanes, nested at most two deep.
- modulators (`pedals.modulators`): lfo, envelope ("source": "input" or a pedal id), steps ("steps":
  [0..1, …]). A pedal's "mod" maps a knob to routes; depth is a fraction of the knob's range, -1..1.
- "bypass": true or {"param": …} switches a pedal off. prerollMs may be 0: Drift measures tails itself.
- The convolution pedal needs an impulse-response file in the package; build those in Drift Forge.
)";

QJsonArray processorTable()
{
    QJsonArray rows;
    for (const std::string &id : drift::audiofx::processorIds()) {
        const QString processor = QString::fromStdString(id);
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
    if (!kind.isEmpty() && !packageKind(kind))
        return badKind(kind);
    QString text = QString::fromUtf8(kGuideCommon);
    if (kind.isEmpty() || kind != QLatin1String("audio_effect"))
        text += QString::fromUtf8(kGuideGpu);
    if (kind.isEmpty() || kind == QLatin1String("effect")) {
        text += QString::fromUtf8(kGuideEffect);
        text += QString::fromUtf8(kGuideTime);
    }
    if (kind.isEmpty() || kind == QLatin1String("transition"))
        text += QString::fromUtf8(kGuideTransition);
    if (kind.isEmpty() || kind == QLatin1String("animation"))
        text += QString::fromUtf8(kGuideAnimation);
    QJsonObject reply{{QStringLiteral("guide"), text}};
    if (kind.isEmpty() || kind == QLatin1String("audio_effect")) {
        reply.insert(QStringLiteral("guide"), text + QString::fromUtf8(kGuideAudio));
        reply.insert(QStringLiteral("processors"), processorTable());
        reply.insert(QStringLiteral("pedals"),
                     QJsonDocument::fromJson(QByteArray::fromStdString(drift::audiofx::pedalCatalogJson())).object());
    }
    return ok(reply);
}

QJsonObject create(AppController *controller, const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = packageKind(kindName);
    if (!kind)
        return badKind(kindName);

    static const QRegularExpression kSlug(QStringLiteral("^[a-z0-9_]{1,48}$"));
    QString slug = args.value(QStringLiteral("slug")).toString().trimmed().toLower();
    if (slug.startsWith(QLatin1String("user.")))
        slug = slug.mid(5);
    if (!kSlug.match(slug).hasMatch())
        return err("bad_args", QStringLiteral("slug must be 1–48 of a-z, 0-9, _ (got \"%1\")").arg(slug));

    const QString id = QStringLiteral("user.") + slug;
    const QString target = QDir(forge::userRoot(*kind)).filePath(slug);
    if (QFileInfo::exists(target) || !forge::packageDirForId(*kind, id).isEmpty())
        return err("exists", QStringLiteral("%1 already exists — use update_effect, or pick another slug").arg(id));
    return forge::install(controller, *kind, target, id, args);
}

QJsonObject update(AppController *controller, const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = packageKind(kindName);
    if (!kind)
        return badKind(kindName);
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString dir = forge::packageDirForId(*kind, id);
    if (dir.isEmpty())
        return err("not_found", QStringLiteral("No %1 with id %2 — call %3").arg(kind->name, id, forge::listOp(*kind)));
    if (!forge::isUserDir(*kind, dir))
        return err("not_user_effect", QStringLiteral("%1 ships with Drift or an add-on; fork it with create_effect instead").arg(id));
    return forge::install(controller, *kind, QDir::cleanPath(dir), id, args);
}

QJsonObject source(AppController *controller, const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = packageKind(kindName);
    if (!kind)
        return badKind(kindName);
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString dir = forge::packageDirForId(*kind, id);
    if (dir.isEmpty())
        return err("not_found", QStringLiteral("No %1 package with id %2 — call %3").arg(kind->name, id, forge::listOp(*kind)));

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
    QJsonObject reply{{QStringLiteral("id"), id},
                      {QStringLiteral("kind"), kind->name},
                      {QStringLiteral("user"), forge::isUserDir(*kind, dir)},
                      {QStringLiteral("manifest"), manifest},
                      {QStringLiteral("files"), files},
                      {QStringLiteral("binaryFiles"), binaries}};
    if (forge::isUserDir(*kind, dir))
        reply.insert(QStringLiteral("uses"), usesInProject(controller, *kind, id));
    return ok(reply);
}

QJsonObject remove(AppController *controller, const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = packageKind(kindName);
    if (!kind)
        return badKind(kindName);
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString dir = forge::packageDirForId(*kind, id);
    if (dir.isEmpty())
        return err("not_found", QStringLiteral("No %1 with id %2 — call %3").arg(kind->name, id, forge::listOp(*kind)));
    if (!forge::isUserDir(*kind, dir))
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
    forge::reload(controller, *kind);
    return ok({{QStringLiteral("id"), id}, {QStringLiteral("deleted"), true}, {QStringLiteral("uses"), uses}});
}

QJsonObject exportPackage(const QJsonObject &args)
{
    const QString kindName = args.value(QStringLiteral("kind")).toString();
    const auto kind = packageKind(kindName);
    if (!kind)
        return badKind(kindName);
    return forge::exportDriftfx(*kind, args);
}

QJsonObject importPackage(AppController *controller, const QJsonObject &args)
{
    QString path = args.value(QStringLiteral("path")).toString().trimmed();
    if (path.startsWith(QLatin1String("file:")))
        path = QUrl(path).toLocalFile();
    if (path.isEmpty() || !QDir::isAbsolutePath(path))
        return err("bad_args", QStringLiteral("path must be absolute"));
    if (!QFileInfo::exists(path))
        return err("not_found", QStringLiteral("%1 does not exist").arg(path));
    AddonManager *manager = controller ? controller->addonManager() : nullptr;
    if (!manager)
        return err("bad_args", QStringLiteral("Addon manager is only available in the running editor"));

    const QVariantMap info = manager->inspectUserPackage(QUrl::fromLocalFile(path));
    if (info.contains(QStringLiteral("error")))
        return err("bad_package", info.value(QStringLiteral("error")).toString());

    const QString addonKind = info.value(QStringLiteral("kind")).toString();
    const auto kind = forge::packageKindForAddon(addonKind);
    if (!kind)
        return err("bad_package", QStringLiteral("unsupported package kind %1").arg(addonKind));

    const QString folder = info.value(QStringLiteral("folder")).toString();
    const QString destination = QDir(forge::userRoot(*kind)).filePath(folder);
    if (QFileInfo::exists(destination) && !args.value(QStringLiteral("overwrite")).toBool())
        return err("exists", QStringLiteral("%1 is already installed in My Effects; pass overwrite:true to replace it")
                                 .arg(info.value(QStringLiteral("name")).toString()));

    QString installError;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    installError = QStringLiteral("timed out");
    QObject::connect(manager, &AddonManager::userPackageInstalled, &loop, [&](const QString &, const QString &error) {
        installError = error;
        loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    timeout.start(30000);
    manager->installUserPackage();
    loop.exec();
    if (!installError.isEmpty())
        return err("io_error", installError);

    QFile manifestFile(QDir(destination).filePath(kind->manifest));
    QString id;
    if (manifestFile.open(QIODevice::ReadOnly))
        id = QJsonDocument::fromJson(manifestFile.readAll()).object().value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        id = QStringLiteral("user.") + folder;
    return ok({{QStringLiteral("ids"), QJsonArray{id}},
               {QStringLiteral("kind"), kind->name},
               {QStringLiteral("name"), info.value(QStringLiteral("name")).toString()}});
}

} // namespace drift::mcp::authoring
