#include "app/mcp/tools/McpProceduralAuthoring.h"

#include "app/editor/AppController.h"
#include "app/mcp/protocol/McpJson.h"
#include "app/procedural/ProceduralAuthoringService.h"
#include "core/edit/TimelineOps.h"
#include "core/project/Project.h"
#include "engine/packages/procedurals/ProceduralCatalog.h"
#include "engine/render3d/scene/SceneCamera3d.h"

#include <QFile>

namespace drift::mcp::procedural {
namespace {

QMap<QString, QString> filesArg(const QJsonObject &args, QJsonObject *error)
{
    QMap<QString, QString> files;
    const QJsonObject object = args.value(QStringLiteral("files")).toObject();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!it.value().isString()) {
            *error = err("bad_args", QStringLiteral("files.%1 must be the source text").arg(it.key()));
            return {};
        }
        files.insert(it.key(), it.value().toString());
    }
    return files;
}

QString paramType(const EffectParamSpec &spec)
{
    if (spec.isBoolean())
        return QStringLiteral("bool");
    if (spec.isColor())
        return QStringLiteral("color");
    if (spec.isVec2())
        return QStringLiteral("vec2");
    if (spec.isRegion())
        return QStringLiteral("region");
    if (spec.isEnum())
        return QStringLiteral("enum");
    if (spec.isInt())
        return QStringLiteral("int");
    return QStringLiteral("float");
}

QSize canvasOf(AppController *controller, const QJsonObject &args)
{
    const int w = args.value(QStringLiteral("width")).toInt(controller ? controller->project()->width() : 1920);
    const int h = args.value(QStringLiteral("height")).toInt(controller ? controller->project()->height() : 1080);
    return QSize(qBound(16, w, 8192), qBound(16, h, 8192));
}

} // namespace

QJsonObject guide()
{
    // The guide is docs/procedural.md itself, compiled in, so the tool and the docs cannot drift.
    QFile file(QStringLiteral(":/docs/procedural.md"));
    if (!file.open(QIODevice::ReadOnly))
        return err("io_error", QStringLiteral("the embedded procedural guide is missing"));
    return ok({{QStringLiteral("guide"), QString::fromUtf8(file.readAll())}});
}

QJsonObject list()
{
    QJsonArray rows;
    for (const ProceduralPackage &pkg : proceduralCatalog()) {
        QJsonArray params;
        for (const EffectParamSpec &spec : pkg.parameters) {
            params.append(QJsonObject{{QStringLiteral("key"), spec.key},
                                      {QStringLiteral("label"), spec.label},
                                      {QStringLiteral("type"), paramType(spec)},
                                      {QStringLiteral("min"), spec.min},
                                      {QStringLiteral("max"), spec.max},
                                      {QStringLiteral("default"), QJsonValue::fromVariant(spec.defaultVariant())}});
        }
        QJsonObject row{{QStringLiteral("id"), pkg.id},
                        {QStringLiteral("name"), pkg.displayName},
                        {QStringLiteral("description"), pkg.description},
                        {QStringLiteral("category"), pkg.category},
                        {QStringLiteral("shaders"), QJsonArray::fromStringList(pkg.shaders.keys())},
                        {QStringLiteral("params"), params}};
        if (ProceduralAuthoringService::isUserDir(pkg.packageDir))
            row.insert(QStringLiteral("user"), true);
        rows.append(row);
    }
    return ok({{QStringLiteral("procedurals"), rows}});
}

QJsonObject create(const QJsonObject &args)
{
    if (!args.value(QStringLiteral("manifest")).isObject())
        return err("bad_args", QStringLiteral("manifest (object) required"));
    QJsonObject error;
    const QMap<QString, QString> files = filesArg(args, &error);
    if (!error.isEmpty())
        return error;
    return ProceduralAuthoringService::create(args.value(QStringLiteral("slug")).toString(),
                                              args.value(QStringLiteral("manifest")).toObject(), files,
                                              canvasOf(nullptr, args))
        .toJson();
}

QJsonObject update(const QJsonObject &args)
{
    QJsonObject error;
    const QMap<QString, QString> files = filesArg(args, &error);
    if (!error.isEmpty())
        return error;
    std::optional<QJsonObject> manifest;
    if (args.contains(QStringLiteral("manifest"))) {
        if (!args.value(QStringLiteral("manifest")).isObject())
            return err("bad_args", QStringLiteral("manifest must be an object"));
        manifest = args.value(QStringLiteral("manifest")).toObject();
    }
    QStringList remove;
    for (const QJsonValue &v : args.value(QStringLiteral("remove_files")).toArray())
        remove.append(v.toString());
    return ProceduralAuthoringService::update(args.value(QStringLiteral("id")).toString().trimmed(), manifest, files,
                                              remove, canvasOf(nullptr, args))
        .toJson();
}

QJsonObject source(AppController *controller, const QJsonObject &args)
{
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const ProceduralAuthoringService::Source src = ProceduralAuthoringService::getSource(id);
    if (!src.ok)
        return err(src.code.toUtf8().constData(), src.message);
    QJsonObject files;
    for (auto it = src.files.cbegin(); it != src.files.cend(); ++it)
        files.insert(it.key(), it.value());
    QJsonObject reply{{QStringLiteral("id"), id},
                      {QStringLiteral("user"), src.user},
                      {QStringLiteral("manifest"), src.manifest},
                      {QStringLiteral("files"), files}};
    if (src.user && controller)
        reply.insert(QStringLiteral("uses"), ProceduralAuthoringService::usesInProject(*controller->project(), id));
    return ok(reply);
}

QJsonObject remove(AppController *controller, const QJsonObject &args)
{
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const int uses = controller ? ProceduralAuthoringService::usesInProject(*controller->project(), id) : 0;
    if (uses > 0 && !args.value(QStringLiteral("force")).toBool()) {
        QJsonObject e = err("in_use", QStringLiteral("%1 is used %2 time(s) in this project; those clips keep their "
                                                     "data but render nothing once it is gone. Pass force:true to "
                                                     "delete anyway").arg(id).arg(uses));
        e.insert(QStringLiteral("uses"), uses);
        return e;
    }
    QJsonObject out = ProceduralAuthoringService::remove(id).toJson();
    if (out.value(QStringLiteral("ok")).toBool()) {
        out.insert(QStringLiteral("deleted"), true);
        out.insert(QStringLiteral("uses"), uses);
    }
    return out;
}

QJsonObject add(AppController *controller, const QJsonObject &args)
{
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty())
        return err("bad_args", QStringLiteral("id required - call list_procedurals"));
    if (!hasProcedural(id))
        return err("not_found", QStringLiteral("No procedural with id %1 - call list_procedurals").arg(id));
    const double at = args.contains(QStringLiteral("at")) ? args.value(QStringLiteral("at")).toDouble()
                                                          : controller->playheadSeconds();
    QVariantMap opts;
    for (const char *key : {"duration", "seed", "name", "params"}) {
        if (args.contains(QLatin1String(key)))
            opts.insert(QString::fromUtf8(key), args.value(QLatin1String(key)).toVariant());
    }
    const QVariantMap reply = controller->addProceduralClip(
        id, args.contains(QStringLiteral("track")) ? args.value(QStringLiteral("track")).toInt(-1) : -1, at, opts);
    if (!reply.value(QStringLiteral("ok")).toBool())
        return err("bad_args", reply.value(QStringLiteral("error")).toString());
    QJsonObject out = QJsonObject::fromVariantMap(reply);
    out.insert(QStringLiteral("n"), 1);
    return out;
}

QJsonObject evaluate(AppController *controller, const QJsonObject &args, const drift::Clip *clip)
{
    ProceduralAuthoringService::EvaluateRequest request;
    const drift::Project *project = controller ? controller->project() : nullptr;
    if (project)
        request.canvas = QSize(project->width(), project->height());
    if (args.contains(QStringLiteral("width")) || args.contains(QStringLiteral("height")))
        request.canvas = canvasOf(controller, args);
    request.frameRate = project && project->fps() > 0 ? int(std::lround(project->fps())) : 30;
    request.seed = args.value(QStringLiteral("seed")).toInt();
    const double t = args.value(QStringLiteral("t")).toDouble(clip ? usToSeconds(clip->timelineStart) : 0.0);
    request.timeSeconds = t;

    if (clip) {
        if (clip->type != ClipType::Procedural)
            return err("type_mismatch", QStringLiteral("clip is not a procedural clip"));
        const TimeUs timelineUs = secondsToUs(t);
        const TimeUs clipTimeUs = qMax<TimeUs>(0, timelineUs - clip->timelineStart);
        request.packageId = clip->procedural.packageId;
        request.parameters = clip->procedural.resolvedAt(clipTimeUs).parameters;
        request.seed = clip->procedural.seed;
        request.timeSeconds = usToSeconds(clip->timelineToSourceUs(timelineUs));
        const QSizeF size(request.canvas.width(), request.canvas.height());
        request.camera = defaultSceneCamera(size);
        if (const Clip *cameraClip = cameraClipAt(project->tracks(), timelineUs))
            request.camera = sceneCameraFromClip(*cameraClip, timelineUs, 1.0, size);
    } else {
        request.packageId = args.value(QStringLiteral("id")).toString().trimmed();
        if (request.packageId.isEmpty())
            return err("bad_args", QStringLiteral("id (package) or clip required"));
    }

    const QJsonObject overrides = args.value(QStringLiteral("params")).toObject();
    const ProceduralPackage pkg = proceduralForId(request.packageId);
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
        bool known = false;
        for (const EffectParamSpec &spec : pkg.parameters)
            known = known || spec.key == it.key() || it.key().startsWith(spec.key + QLatin1Char('.'));
        if (pkg.valid && !known)
            return err("bad_args", QStringLiteral("%1 has no param \"%2\" - see list_procedurals").arg(request.packageId, it.key()));
        request.parameters.insert(it.key(), it.value().toVariant());
    }

    const ProceduralAuthoringService::EvaluateResult result = ProceduralAuthoringService::evaluate(request);
    return result.toJson();
}

} // namespace drift::mcp::procedural
