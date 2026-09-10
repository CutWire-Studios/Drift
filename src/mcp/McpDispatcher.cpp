#include "mcp/McpDispatcher.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpJson.h"
#include "mcp/McpValidate.h"

#include "core/Time.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

namespace drift::mcp {
namespace {

QSet<QString> clipIdSet(AppController *c)
{
    QSet<QString> ids;
    const auto tracks = c->tracks();
    for (int t = 0; t < tracks.size(); ++t) {
        const auto clips = tracks.at(t).toMap().value(QStringLiteral("clips")).toList();
        for (const QVariant &clip : clips)
            ids.insert(clip.toMap().value(QStringLiteral("id")).toString());
    }
    return ids;
}

int jsonInt(const QJsonValue &v, int fallback = -1)
{
    if (v.isDouble())
        return v.toInt(fallback);
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().toInt(&ok);
        return ok ? n : fallback;
    }
    return fallback;
}

double jsonNumber(const QJsonValue &v, double fallback)
{
    if (v.isDouble())
        return v.toDouble(fallback);
    if (v.isString()) {
        bool ok = false;
        const double n = v.toString().toDouble(&ok);
        return ok ? n : fallback;
    }
    return fallback;
}

bool jsonBool(const QJsonValue &v, bool fallback = false)
{
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toInt() != 0;
    if (v.isString()) {
        const QString s = v.toString().toLower();
        if (s == QLatin1String("true") || s == QLatin1String("1"))
            return true;
        if (s == QLatin1String("false") || s == QLatin1String("0"))
            return false;
    }
    return fallback;
}

QStringList jsonStringList(const QJsonValue &v)
{
    QStringList out;
    if (v.isString()) {
        out.append(v.toString());
        return out;
    }
    if (v.isArray()) {
        for (const QJsonValue &item : v.toArray()) {
            if (item.isString())
                out.append(item.toString());
        }
    }
    return out;
}

QJsonObject compactCatalogItem(const QVariantMap &item)
{
    QJsonObject o;
    const QString id = item.value(QStringLiteral("id")).toString();
    if (!id.isEmpty())
        o.insert(QStringLiteral("id"), id);
    const QString label = item.value(QStringLiteral("label")).toString();
    if (!label.isEmpty())
        o.insert(QStringLiteral("label"), label);
    else if (item.contains(QStringLiteral("displayName")))
        o.insert(QStringLiteral("label"), item.value(QStringLiteral("displayName")).toString());
    const QString category = item.value(QStringLiteral("category")).toString();
    if (!category.isEmpty())
        o.insert(QStringLiteral("cat"), category);
    const QVariantList params = item.value(QStringLiteral("params")).toList();
    if (!params.isEmpty()) {
        QJsonArray compactParams;
        for (const QVariant &param : params) {
            const QVariantMap p = param.toMap();
            QJsonObject row;
            const QString key = p.value(QStringLiteral("key")).toString();
            if (!key.isEmpty())
                row.insert(QStringLiteral("key"), key);
            const QString type = p.value(QStringLiteral("type")).toString();
            if (!type.isEmpty())
                row.insert(QStringLiteral("type"), type);
            if (p.contains(QStringLiteral("default")))
                row.insert(QStringLiteral("default"), QJsonValue::fromVariant(p.value(QStringLiteral("default"))));
            if (p.contains(QStringLiteral("min")))
                row.insert(QStringLiteral("min"), p.value(QStringLiteral("min")).toDouble());
            if (p.contains(QStringLiteral("max")))
                row.insert(QStringLiteral("max"), p.value(QStringLiteral("max")).toDouble());
            if (p.value(QStringLiteral("isBoolean")).toBool())
                row.insert(QStringLiteral("bool"), true);
            if (!row.isEmpty())
                compactParams.append(row);
        }
        if (!compactParams.isEmpty())
            o.insert(QStringLiteral("params"), compactParams);
    }
    return o;
}

QString findNewClipId(const QSet<QString> &before, const QSet<QString> &after)
{
    for (const QString &id : after) {
        if (!before.contains(id))
            return id;
    }
    return {};
}

QString catalogKey(const QString &id)
{
    QString k = id.trimmed().toLower();
    if (k.startsWith(QLatin1String("builtin.effects.")))
        k.remove(0, 16);
    k.replace(QLatin1Char('.'), QLatin1Char('_'));
    return k;
}

QStringList catalogIds(const QVariantList &catalog)
{
    QStringList ids;
    for (const QVariant &v : catalog)
        ids.append(v.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

// Exact, then case-insensitive, then with '.'≡'_' and the builtin.effects. prefix dropped. On a
// miss, *nearest gets the closest ids for the error message.
QString resolveCatalogId(const QString &raw, const QStringList &ids, QStringList *nearest)
{
    if (ids.contains(raw))
        return raw;
    for (const QString &id : ids) {
        if (id.compare(raw, Qt::CaseInsensitive) == 0)
            return id;
    }
    const QString key = catalogKey(raw);
    if (!key.isEmpty()) {
        for (const QString &id : ids) {
            if (catalogKey(id) == key)
                return id;
        }
    }
    if (nearest)
        *nearest = nearestStrings(raw, ids, 3);
    return {};
}

QJsonObject unknownCatalogId(const char *what, const QString &raw, const QStringList &nearest, const char *listOp)
{
    QString detail = QStringLiteral("Unknown %1 id \"%2\"").arg(QString::fromUtf8(what), raw);
    if (!nearest.isEmpty())
        detail += QStringLiteral("; did you mean %1?").arg(nearest.join(QStringLiteral(", ")));
    detail += QStringLiteral(". Call %1({q:\"%2\"})").arg(QString::fromUtf8(listOp), raw);
    return err("not_found", detail);
}

bool catalogItemMatches(const QVariantMap &item, const QString &q)
{
    for (auto it = item.begin(); it != item.end(); ++it) {
        if (it.value().typeId() == QMetaType::QString && it.value().toString().contains(q, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// Default: {cats:{cat:[{id,label}]}, n}. id → that item with params; cat → the category with
// params; q → substring hits, with params when there are five or fewer.
QJsonObject catalogListing(const QVariantList &catalog, const QJsonObject &args, const char *key, const char *what,
                           const char *listOp)
{
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    const QString cat = args.value(QStringLiteral("cat")).toString().trimmed();
    const QString q = args.value(QStringLiteral("q")).toString().trimmed();
    const int limit = qBound(1, jsonInt(args.value(QStringLiteral("limit")), 50), 500);

    if (!id.isEmpty()) {
        QStringList nearest;
        const QString canonical = resolveCatalogId(id, catalogIds(catalog), &nearest);
        if (canonical.isEmpty())
            return unknownCatalogId(what, id, nearest, listOp);
        for (const QVariant &v : catalog) {
            const QVariantMap item = v.toMap();
            if (item.value(QStringLiteral("id")).toString() == canonical)
                return ok({{QString::fromUtf8(key), QJsonArray{compactCatalogItem(item)}}, {QStringLiteral("n"), 1}});
        }
    }

    if (!cat.isEmpty() || !q.isEmpty()) {
        QList<QVariantMap> hits;
        for (const QVariant &v : catalog) {
            const QVariantMap item = v.toMap();
            if (!cat.isEmpty() && item.value(QStringLiteral("category")).toString().compare(cat, Qt::CaseInsensitive) != 0)
                continue;
            if (!q.isEmpty() && !catalogItemMatches(item, q))
                continue;
            hits.append(item);
        }
        const bool withParams = !cat.isEmpty() || hits.size() <= 5;
        QJsonArray items;
        for (const QVariantMap &item : hits) {
            if (items.size() >= limit)
                break;
            QJsonObject row = compactCatalogItem(item);
            if (!withParams)
                row.remove(QStringLiteral("params"));
            items.append(row);
        }
        return ok({{QString::fromUtf8(key), items}, {QStringLiteral("n"), hits.size()}});
    }

    QJsonObject cats;
    int n = 0;
    for (const QVariant &v : catalog) {
        const QVariantMap item = v.toMap();
        QString category = item.value(QStringLiteral("category")).toString();
        if (category.isEmpty())
            category = QStringLiteral("other");
        QJsonArray rows = cats.value(category).toArray();
        rows.append(QJsonObject{{QStringLiteral("id"), item.value(QStringLiteral("id")).toString()},
                                {QStringLiteral("label"), item.value(QStringLiteral("label")).toString()}});
        cats.insert(category, rows);
        ++n;
    }
    return ok({{QStringLiteral("cats"), cats}, {QStringLiteral("n"), n}});
}

QJsonArray compactAvailableCodecs(const QVariantList &list)
{
    QJsonArray out;
    for (const QVariant &v : list) {
        const QVariantMap m = v.toMap();
        if (!m.value(QStringLiteral("available")).toBool())
            continue;
        out.append(QJsonObject{{QStringLiteral("id"), m.value(QStringLiteral("id")).toString()},
                               {QStringLiteral("label"), m.value(QStringLiteral("label")).toString()}});
    }
    return out;
}

QVariantMap mergedExportSettings(AppController *c)
{
    QVariantMap merged = c->exportDefaultSettings();
    const QVariantMap last = c->lastExportSettings();
    for (auto it = last.begin(); it != last.end(); ++it)
        merged.insert(it.key(), it.value());
    return merged;
}

void applyScaleId(AppController *c, QVariantMap &map, const QString &scaleId)
{
    if (scaleId.isEmpty())
        return;
    map.insert(QStringLiteral("scaleId"), scaleId);
    for (const QVariant &v : c->exportScaleOptions()) {
        const QVariantMap opt = v.toMap();
        if (opt.value(QStringLiteral("id")).toString() == scaleId) {
            map.insert(QStringLiteral("targetHeight"), opt.value(QStringLiteral("targetHeight")).toInt());
            if (!map.contains(QStringLiteral("videoBitrateKbps")))
                map.insert(QStringLiteral("videoBitrateKbps"),
                           opt.value(QStringLiteral("videoBitrateKbps")).toInt());
            return;
        }
    }
}

bool applyFps(AppController *c, QVariantMap &map, const QJsonValue &value)
{
    if (value.isString()) {
        const QString id = value.toString().trimmed().toLower();
        if (id == QLatin1String("project") || id == QLatin1String("0")) {
            map.insert(QStringLiteral("fpsNum"), 0);
            map.insert(QStringLiteral("fpsDen"), 1);
            return true;
        }
        for (const QVariant &v : c->exportFrameRateOptions()) {
            const QVariantMap opt = v.toMap();
            if (opt.value(QStringLiteral("id")).toString().compare(id, Qt::CaseInsensitive) == 0) {
                map.insert(QStringLiteral("fpsNum"), opt.value(QStringLiteral("fpsNum")).toInt());
                map.insert(QStringLiteral("fpsDen"), opt.value(QStringLiteral("fpsDen")).toInt());
                return true;
            }
        }
        bool ok = false;
        const double n = id.toDouble(&ok);
        if (!ok)
            return false;
        return applyFps(c, map, QJsonValue(n));
    }
    if (!value.isDouble())
        return false;
    const double fps = value.toDouble();
    if (fps <= 0) {
        map.insert(QStringLiteral("fpsNum"), 0);
        map.insert(QStringLiteral("fpsDen"), 1);
        return true;
    }
    for (const QVariant &v : c->exportFrameRateOptions()) {
        const QVariantMap opt = v.toMap();
        const int num = opt.value(QStringLiteral("fpsNum")).toInt();
        const int den = qMax(1, opt.value(QStringLiteral("fpsDen")).toInt());
        if (num <= 0)
            continue;
        if (qAbs((double(num) / double(den)) - fps) < 0.01) {
            map.insert(QStringLiteral("fpsNum"), num);
            map.insert(QStringLiteral("fpsDen"), den);
            return true;
        }
    }
    map.insert(QStringLiteral("fpsNum"), qMax(1, qRound(fps)));
    map.insert(QStringLiteral("fpsDen"), 1);
    return true;
}

QString argString(const QJsonObject &args, const QString &a, const QString &b = {})
{
    if (args.contains(a) && args.value(a).isString())
        return args.value(a).toString().trimmed();
    if (!b.isEmpty() && args.contains(b) && args.value(b).isString())
        return args.value(b).toString().trimmed();
    return {};
}

bool applyExportPatch(AppController *c, QVariantMap &map, const QJsonObject &args, QString *error)
{
    const QString scale = argString(args, QStringLiteral("scale"), QStringLiteral("scaleId"));
    if (!scale.isEmpty())
        applyScaleId(c, map, scale);

    if (args.contains(QStringLiteral("height")) || args.contains(QStringLiteral("targetHeight"))) {
        const int height = jsonInt(args.contains(QStringLiteral("height")) ? args.value(QStringLiteral("height"))
                                                                          : args.value(QStringLiteral("targetHeight")),
                                   0);
        map.insert(QStringLiteral("targetHeight"), qMax(0, height));
    }

    if (args.contains(QStringLiteral("fps"))) {
        if (!applyFps(c, map, args.value(QStringLiteral("fps")))) {
            if (error)
                *error = QStringLiteral("Unknown fps");
            return false;
        }
    }
    if (args.contains(QStringLiteral("fpsNum")))
        map.insert(QStringLiteral("fpsNum"), jsonInt(args.value(QStringLiteral("fpsNum")), 0));
    if (args.contains(QStringLiteral("fpsDen")))
        map.insert(QStringLiteral("fpsDen"), qMax(1, jsonInt(args.value(QStringLiteral("fpsDen")), 1)));

    const QString video = argString(args, QStringLiteral("video"), QStringLiteral("videoCodecId"));
    if (!video.isEmpty())
        map.insert(QStringLiteral("videoCodecId"), video);
    const QString audio = argString(args, QStringLiteral("audio"), QStringLiteral("audioCodecId"));
    if (!audio.isEmpty())
        map.insert(QStringLiteral("audioCodecId"), audio);
    const QString rate = argString(args, QStringLiteral("rate"), QStringLiteral("rateControl"));
    if (!rate.isEmpty())
        map.insert(QStringLiteral("rateControl"), rate);
    if (args.contains(QStringLiteral("crf")))
        map.insert(QStringLiteral("crf"), jsonInt(args.value(QStringLiteral("crf")), 18));
    if (args.contains(QStringLiteral("bitrate")) || args.contains(QStringLiteral("videoBitrateKbps"))) {
        map.insert(QStringLiteral("videoBitrateKbps"),
                   jsonInt(args.contains(QStringLiteral("bitrate")) ? args.value(QStringLiteral("bitrate"))
                                                                   : args.value(QStringLiteral("videoBitrateKbps")),
                           12000));
    }
    const QString preset = argString(args, QStringLiteral("preset"), QStringLiteral("videoPreset"));
    if (!preset.isEmpty())
        map.insert(QStringLiteral("videoPreset"), preset);
    if (args.contains(QStringLiteral("audio_bitrate")) || args.contains(QStringLiteral("audioBitrateKbps"))) {
        map.insert(QStringLiteral("audioBitrateKbps"),
                   jsonInt(args.contains(QStringLiteral("audio_bitrate"))
                               ? args.value(QStringLiteral("audio_bitrate"))
                               : args.value(QStringLiteral("audioBitrateKbps")),
                           192));
    }

    if (args.contains(QStringLiteral("audio_only")) || args.contains(QStringLiteral("audioOnly"))) {
        map.insert(QStringLiteral("audioOnly"),
                   jsonBool(args.contains(QStringLiteral("audio_only")) ? args.value(QStringLiteral("audio_only"))
                                                                       : args.value(QStringLiteral("audioOnly"))));
    }
    if (args.contains(QStringLiteral("gif")) || args.contains(QStringLiteral("gifExport"))) {
        map.insert(QStringLiteral("gifExport"),
                   jsonBool(args.contains(QStringLiteral("gif")) ? args.value(QStringLiteral("gif"))
                                                                : args.value(QStringLiteral("gifExport"))));
    }

    if (map.value(QStringLiteral("gifExport")).toBool()) {
        map.insert(QStringLiteral("audioOnly"), false);
        if (map.value(QStringLiteral("fpsNum")).toInt() <= 0) {
            map.insert(QStringLiteral("fpsNum"), 15);
            map.insert(QStringLiteral("fpsDen"), 1);
        }
    }

    const bool workAreaRequested =
        args.contains(QStringLiteral("work_area")) || args.contains(QStringLiteral("exportWorkAreaOnly"));
    if (workAreaRequested) {
        map.insert(QStringLiteral("exportWorkAreaOnly"),
                   jsonBool(args.contains(QStringLiteral("work_area"))
                                ? args.value(QStringLiteral("work_area"))
                                : args.value(QStringLiteral("exportWorkAreaOnly"))));
    }

    const bool hasIn = args.contains(QStringLiteral("in"));
    const bool hasOut = args.contains(QStringLiteral("out"));
    if (hasIn || hasOut) {
        const double inSec = hasIn ? jsonNumber(args.value(QStringLiteral("in")), 0) : 0;
        const double outSec = hasOut ? jsonNumber(args.value(QStringLiteral("out")), -1) : -1;
        if (hasOut && outSec <= inSec) {
            if (error)
                *error = QStringLiteral("out must be greater than in");
            return false;
        }
        map.insert(QStringLiteral("startUs"), static_cast<double>(drift::secondsToUs(inSec)));
        if (hasOut)
            map.insert(QStringLiteral("endUs"), static_cast<double>(drift::secondsToUs(outSec)));
        map.insert(QStringLiteral("exportWorkAreaOnly"), false);
    } else if (map.value(QStringLiteral("exportWorkAreaOnly")).toBool()) {
        if (!c->workAreaActive()) {
            if (workAreaRequested) {
                if (error)
                    *error = QStringLiteral("No In/Out work area is set");
                return false;
            }
            map.insert(QStringLiteral("exportWorkAreaOnly"), false);
            map.remove(QStringLiteral("startUs"));
            map.remove(QStringLiteral("endUs"));
        } else {
            map.insert(QStringLiteral("startUs"),
                       static_cast<double>(drift::secondsToUs(c->workAreaInSeconds())));
            map.insert(QStringLiteral("endUs"),
                       static_cast<double>(drift::secondsToUs(c->workAreaOutSeconds())));
        }
    } else {
        map.remove(QStringLiteral("startUs"));
        map.remove(QStringLiteral("endUs"));
    }
    return true;
}

QJsonObject compactExportSettings(AppController *c, const QVariantMap &map)
{
    const int num = map.value(QStringLiteral("fpsNum")).toInt();
    const int den = qMax(1, map.value(QStringLiteral("fpsDen")).toInt());
    const bool gif = map.value(QStringLiteral("gifExport")).toBool();
    const bool audioOnly = map.value(QStringLiteral("audioOnly")).toBool();
    const QString video = map.value(QStringLiteral("videoCodecId")).toString();
    const QString audio = map.value(QStringLiteral("audioCodecId")).toString();
    const QString container = gif ? QStringLiteral("gif")
                                  : audioOnly ? c->exportPreferredAudioOnlyContainer(audio)
                                              : c->exportPreferredContainer(video, audio);
    QJsonObject o{
        {QStringLiteral("scale"), map.value(QStringLiteral("scaleId")).toString()},
        {QStringLiteral("height"), map.value(QStringLiteral("targetHeight")).toInt()},
        {QStringLiteral("fps"), num <= 0 ? 0.0 : (double(num) / double(den))},
        {QStringLiteral("video"), video},
        {QStringLiteral("audio"), audio},
        {QStringLiteral("rate"), map.value(QStringLiteral("rateControl")).toString()},
        {QStringLiteral("crf"), map.value(QStringLiteral("crf")).toInt()},
        {QStringLiteral("bitrate"), map.value(QStringLiteral("videoBitrateKbps")).toInt()},
        {QStringLiteral("preset"), map.value(QStringLiteral("videoPreset")).toString()},
        {QStringLiteral("audio_bitrate"), map.value(QStringLiteral("audioBitrateKbps")).toInt()},
        {QStringLiteral("audio_only"), audioOnly},
        {QStringLiteral("gif"), gif},
        {QStringLiteral("work_area"), map.value(QStringLiteral("exportWorkAreaOnly")).toBool()},
        {QStringLiteral("container"), container},
        {QStringLiteral("suffix"), c->exportDefaultSuffix(container, audioOnly && !gif)},
    };
    if (map.contains(QStringLiteral("startUs")) && map.contains(QStringLiteral("endUs"))) {
        o.insert(QStringLiteral("in"), drift::usToSeconds(
                                           static_cast<drift::TimeUs>(map.value(QStringLiteral("startUs")).toDouble())));
        o.insert(QStringLiteral("out"), drift::usToSeconds(
                                            static_cast<drift::TimeUs>(map.value(QStringLiteral("endUs")).toDouble())));
    }
    return o;
}

QString localExportPath(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.startsWith(QLatin1String("file:")))
        return QUrl(trimmed).toLocalFile();
    return trimmed;
}

} // namespace

McpDispatcher::McpDispatcher(AppController *controller)
    : m_controller(controller)
{
}

McpDispatcher::ClipRef McpDispatcher::resolveClip(const QJsonObject &args) const
{
    ClipRef ref;
    const QString id = args.value(QStringLiteral("clip")).toString().trimmed();
    if (!id.isEmpty()) {
        const QPair<int, int> loc = m_controller->mcpLocateClip(id);
        ref.track = loc.first;
        ref.clip = loc.second;
        ref.id = id;
        return ref;
    }
    ref.track = jsonInt(args.value(QStringLiteral("track")));
    ref.clip = jsonInt(args.value(QStringLiteral("index")));
    if (ref.valid()) {
        ref.id = m_controller->mcpClipId(ref.track, ref.clip);
        if (ref.id.isEmpty())
            return {};
    }
    return ref;
}

int McpDispatcher::resolveAsset(const QJsonValue &value) const
{
    AssetLibrary *lib = m_controller->assetLibrary();
    if (!lib)
        return -1;
    if (value.isDouble()) {
        const int index = value.toInt(-1);
        return lib->assetAt(index).isEmpty() ? -1 : index;
    }
    const QString key = value.toString().trimmed();
    if (key.isEmpty())
        return -1;
    bool asInt = false;
    const int numeric = key.toInt(&asInt);
    if (asInt && !key.contains(QLatin1Char('-'))) {
        if (!lib->assetAt(numeric).isEmpty())
            return numeric;
    }
    const int byId = lib->indexOfId(key);
    if (byId >= 0)
        return byId;
    for (int i = 0; i < lib->count(); ++i) {
        if (lib->assetAt(i).value(QStringLiteral("name")).toString().compare(key, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

QJsonObject McpDispatcher::clipFeedback(const ClipRef &ref, const QJsonObject &extra) const
{
    if (!ref.valid())
        return extra;
    const QVariantMap clip = m_controller->mcpCompactClip(ref.track, ref.clip);
    QJsonObject out = extra;
    out.insert(QStringLiteral("id"), clip.value(QStringLiteral("id")).toString());
    out.insert(QStringLiteral("track"), ref.track);
    out.insert(QStringLiteral("index"), ref.clip);
    auto copyNum = [&](const char *src, const char *dst) {
        if (clip.contains(QLatin1String(src)))
            out.insert(QString::fromUtf8(dst), clip.value(QLatin1String(src)).toDouble());
    };
    copyNum("start", "start");
    copyNum("duration", "dur");
    copyNum("inPoint", "in");
    copyNum("outPoint", "out");
    if (clip.contains(QStringLiteral("x")))
        out.insert(QStringLiteral("x"), clip.value(QStringLiteral("x")).toDouble());
    if (clip.contains(QStringLiteral("y")))
        out.insert(QStringLiteral("y"), clip.value(QStringLiteral("y")).toDouble());
    if (clip.contains(QStringLiteral("w")))
        out.insert(QStringLiteral("w"), clip.value(QStringLiteral("w")).toDouble());
    if (clip.contains(QStringLiteral("h")))
        out.insert(QStringLiteral("h"), clip.value(QStringLiteral("h")).toDouble());
    return out;
}

void McpDispatcher::moveClipToRequested(const ClipRef &ref, double at)
{
    if (!ref.valid() || !m_controller->allowClipOverlap())
        return;
    const QVariantMap clip = m_controller->mcpCompactClip(ref.track, ref.clip);
    if (qAbs(clip.value(QStringLiteral("start")).toDouble() - at) <= 0.001)
        return;
    m_controller->selectClip(ref.track, ref.clip);
    m_controller->moveClip(ref.track, ref.clip, at);
}

QJsonObject McpDispatcher::inspect(const QJsonObject &args) const
{
    const int since = args.contains(QStringLiteral("since"))
                          ? jsonInt(args.value(QStringLiteral("since")), -1)
                          : -1;
    AppController::McpInspectOptions options;
    options.clips = jsonBool(args.value(QStringLiteral("clips")));
    options.detail = jsonBool(args.value(QStringLiteral("detail")));
    options.cues = jsonBool(args.value(QStringLiteral("cues")));
    options.verbose = jsonBool(args.value(QStringLiteral("verbose")));
    options.since = since;
    options.track = args.contains(QStringLiteral("track"))
                        ? jsonInt(args.value(QStringLiteral("track")), -1)
                        : -1;
    options.clip = args.value(QStringLiteral("clip")).toString();
    return m_controller->mcpInspect(options);
}

bool McpDispatcher::isUndoable(const QString &tool) const
{
    if (isReadOnlyOp(tool))
        return false;
    return !undoExemptOps().contains(tool);
}

QJsonObject McpDispatcher::apply(const QJsonObject &args)
{
    const QJsonArray ops = args.value(QStringLiteral("ops")).toArray();
    if (ops.isEmpty())
        return err("bad_args", QStringLiteral("ops must be a non-empty array"));

    auto batchLabel = [&](int count) {
        QStringList names;
        for (int i = 0; i < ops.size() && names.size() < 3; ++i)
            names.append(ops.at(i).toObject().value(QStringLiteral("tool")).toString());
        QString label = QStringLiteral("MCP: ") + names.join(QStringLiteral(", "));
        if (count > 3)
            label += QStringLiteral(" + %1 more").arg(count - 3);
        return label;
    };

    m_controller->mcpBeginBatch();
    bool hadUndoable = false;
    QJsonArray results;
    int i = 0;
    for (; i < ops.size(); ++i) {
        const QJsonObject op = ops.at(i).toObject();
        const QString tool = op.value(QStringLiteral("tool")).toString();
        const QJsonObject opArgs = op.value(QStringLiteral("args")).toObject();
        const QJsonObject one =
            tool == QLatin1String("get_waveform") && jsonBool(opArgs.value(QStringLiteral("image")))
                ? err("bad_args", QStringLiteral("image:true cannot be batched — call get_waveform directly"))
                : applyOne(tool, opArgs);
        if (!one.value(QStringLiteral("ok")).toBool()) {
            m_controller->mcpEndBatch(batchLabel(i),
                                      hadUndoable && i > 0);
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), QStringLiteral("apply_failed")},
                    {QStringLiteral("stopped"), i},
                    {QStringLiteral("tool"), tool},
                    {QStringLiteral("failed"), one},
                    {QStringLiteral("done"), results}};
        }
        results.append(QJsonObject{{QStringLiteral("tool"), tool}, {QStringLiteral("result"), one}});
        if (isUndoable(tool))
            hadUndoable = true;
    }
    m_controller->mcpEndBatch(batchLabel(ops.size()), hadUndoable);
    return ok({{QStringLiteral("n"), ops.size()}, {QStringLiteral("done"), results}});
}

QJsonObject McpDispatcher::applyOne(const QString &tool, const QJsonObject &args)
{
    if (!isKnownOp(tool))
        return unknownOpError(tool);
    QJsonObject checked = args;
    QStringList ignored;
    const QJsonObject invalid = validateArgs(tool, opInputSchema(tool), checked, &ignored);
    if (!invalid.isEmpty())
        return invalid;
    QJsonObject result = applyOneUnchecked(tool, checked);
    if (!ignored.isEmpty() && result.value(QStringLiteral("ok")).toBool())
        result.insert(QStringLiteral("ignored"), QJsonArray::fromStringList(ignored));
    return result;
}

QJsonObject McpDispatcher::applyOneUnchecked(const QString &tool, const QJsonObject &args)
{
    if (tool == QLatin1String("import_media"))
        return opImportMedia(args);
    if (tool == QLatin1String("list_assets"))
        return opListAssets();
    if (tool == QLatin1String("rename_asset"))
        return opRenameAsset(args);
    if (tool == QLatin1String("add_track"))
        return opAddTrack(args);
    if (tool == QLatin1String("remove_track"))
        return opRemoveTrack(args);
    if (tool == QLatin1String("set_track"))
        return opSetTrack(args);
    if (tool == QLatin1String("place_clip"))
        return opPlaceClip(args);
    if (tool == QLatin1String("move_clip"))
        return opMoveClip(args);
    if (tool == QLatin1String("set_duration"))
        return opSetDuration(args);
    if (tool == QLatin1String("set_trim"))
        return opSetTrim(args);
    if (tool == QLatin1String("move_to_track"))
        return opMoveToTrack(args);
    if (tool == QLatin1String("split_clip"))
        return opSplitClip(args);
    if (tool == QLatin1String("delete_clip"))
        return opDeleteClip(args);
    if (tool == QLatin1String("duplicate_clip"))
        return opDuplicateClip(args);
    if (tool == QLatin1String("undo"))
        return opUndo();
    if (tool == QLatin1String("redo"))
        return opRedo();
    if (tool == QLatin1String("set_overlap"))
        return opSetOverlap(args);
    if (tool == QLatin1String("set_transform"))
        return opSetTransform(args);
    if (tool == QLatin1String("reset_transform"))
        return opResetTransform(args);
    if (tool == QLatin1String("seek"))
        return opSeek(args);
    if (tool == QLatin1String("play"))
        return opPlay();
    if (tool == QLatin1String("pause"))
        return opPause();
    if (tool == QLatin1String("set_work_area"))
        return opSetWorkArea(args);
    if (tool == QLatin1String("clear_work_area"))
        return opClearWorkArea();
    if (tool == QLatin1String("add_text"))
        return opAddText(args);
    if (tool == QLatin1String("set_text"))
        return opSetText(args);
    if (tool == QLatin1String("list_effects"))
        return opListEffects(args);
    if (tool == QLatin1String("list_audio_effects"))
        return opListAudioEffects(args);
    if (tool == QLatin1String("list_transitions"))
        return opListTransitions(args);
    if (tool == QLatin1String("add_effect"))
        return opAddEffect(args);
    if (tool == QLatin1String("remove_effect"))
        return opRemoveEffect(args);
    if (tool == QLatin1String("set_effect_param"))
        return opSetEffectParam(args);
    if (tool == QLatin1String("add_audio_effect"))
        return opAddAudioEffect(args);
    if (tool == QLatin1String("remove_audio_effect"))
        return opRemoveAudioEffect(args);
    if (tool == QLatin1String("set_audio_effect_param"))
        return opSetAudioEffectParam(args);
    if (tool == QLatin1String("add_transition"))
        return opAddTransition(args);
    if (tool == QLatin1String("remove_transition"))
        return opRemoveTransition(args);
    if (tool == QLatin1String("set_project_setup"))
        return opSetProjectSetup(args);
    if (tool == QLatin1String("set_background"))
        return opSetBackground(args);
    if (tool == QLatin1String("set_metadata"))
        return opSetMetadata(args);
    if (tool == QLatin1String("save_project"))
        return opSaveProject(args);
    if (tool == QLatin1String("list_export_options"))
        return opListExportOptions();
    if (tool == QLatin1String("export_video"))
        return opExportVideo(args);
    if (tool == QLatin1String("export_status"))
        return opExportStatus();
    if (tool == QLatin1String("list_animated_properties"))
        return opListAnimatedProperties(args);
    if (tool == QLatin1String("list_keyframes"))
        return opListKeyframes(args);
    if (tool == QLatin1String("set_keyframe"))
        return opSetKeyframe(args);
    if (tool == QLatin1String("remove_keyframe"))
        return opRemoveKeyframe(args);
    if (tool == QLatin1String("set_keyframe_interpolation"))
        return opSetKeyframeInterpolation(args);
    if (tool == QLatin1String("set_keyframe_tangents"))
        return opSetKeyframeTangents(args);
    if (tool == QLatin1String("set_keyframe_hold"))
        return opSetKeyframeHold(args);
    if (tool == QLatin1String("set_property_keyframes_enabled"))
        return opSetPropertyKeyframesEnabled(args);
    if (tool == QLatin1String("list_speed_curve"))
        return opListSpeedCurve(args);
    if (tool == QLatin1String("set_speed_curve"))
        return opSetSpeedCurve(args);
    if (tool == QLatin1String("clear_speed_curve"))
        return opClearSpeedCurve(args);
    if (tool == QLatin1String("get_ui_preferences"))
        return opGetUiPreferences();
    if (tool == QLatin1String("set_theme"))
        return opSetTheme(args);
    if (tool == QLatin1String("list_shortcuts"))
        return opListShortcuts(args);
    if (tool == QLatin1String("set_shortcut"))
        return opSetShortcut(args);
    if (tool == QLatin1String("reset_shortcuts"))
        return opResetShortcuts();
    const QJsonObject extended = applyOneExtended(tool, args);
    if (!extended.value(QStringLiteral("error")).toString().startsWith(QLatin1String("unknown_op")))
        return extended;
    return unknownOpError(tool);
}

QJsonObject McpDispatcher::capture(const QJsonObject &args)
{
    const double at = args.contains(QStringLiteral("at"))
                          ? jsonNumber(args.value(QStringLiteral("at")), -1.0)
                          : -1.0;
    const bool full = jsonBool(args.value(QStringLiteral("full")));
    return m_controller->mcpCaptureFrame(at, full);
}

QJsonObject McpDispatcher::frames(const QJsonObject &args)
{
    AppController::McpFrameSheetRequest req;
    req.start = jsonNumber(args.value(QStringLiteral("start")), -1.0);
    req.end = jsonNumber(args.value(QStringLiteral("end")), -1.0);
    for (const QJsonValue &v : args.value(QStringLiteral("at")).toArray())
        req.at.append(jsonNumber(v, 0.0));
    if (args.contains(QStringLiteral("sample")))
        req.sample = args.value(QStringLiteral("sample")).toString();
    req.n = jsonInt(args.value(QStringLiteral("n")), 12);
    req.cols = jsonInt(args.value(QStringLiteral("cols")), 0);
    req.tileWidth = jsonInt(args.value(QStringLiteral("tile")), 0);
    req.minChange = jsonInt(args.value(QStringLiteral("min_change")), 12);
    req.label = jsonBool(args.value(QStringLiteral("label")), true);
    req.toPath = args.value(QStringLiteral("return")).toString() == QLatin1String("path");
    if (args.contains(QStringLiteral("clip")) || args.contains(QStringLiteral("track"))
        || args.contains(QStringLiteral("index"))) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return textResult(clipRefError(args), true);
        req.track = ref.track;
        req.clip = ref.clip;
    }
    const QJsonObject result = m_controller->mcpFrameSheet(req);
    return isRawResult(result) ? result : textResult(result);
}

QJsonObject McpDispatcher::activity(const QJsonObject &args)
{
    AppController::McpActivityRequest req;
    req.start = jsonNumber(args.value(QStringLiteral("start")), -1.0);
    req.end = jsonNumber(args.value(QStringLiteral("end")), -1.0);
    req.samples = jsonInt(args.value(QStringLiteral("samples")), 200);
    req.peaks = jsonInt(args.value(QStringLiteral("peaks")), 8);
    req.audio = jsonBool(args.value(QStringLiteral("audio")), true);
    if (args.contains(QStringLiteral("clip")) || args.contains(QStringLiteral("track"))
        || args.contains(QStringLiteral("index"))) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        req.track = ref.track;
        req.clip = ref.clip;
    }
    return m_controller->mcpActivity(req);
}

QJsonObject McpDispatcher::waitImport(const QStringList &ids)
{
    AssetLibrary *lib = m_controller->assetLibrary();
    if (!lib)
        return err("not_found", QStringLiteral("No media bin"));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(15000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(lib, &AssetLibrary::assetMetadataChanged, &loop, &QEventLoop::quit);

    while (timeout.isActive()) {
        bool pending = false;
        for (const QString &id : ids) {
            if (lib->isImportPending(id)) {
                pending = true;
                break;
            }
        }
        if (!pending)
            break;
        loop.exec();
    }

    QJsonArray assets;
    bool anyTimeout = false;
    for (const QString &id : ids) {
        if (lib->isImportPending(id))
            anyTimeout = true;
        const int index = lib->indexOfId(id);
        const QVariantMap a = lib->assetAt(index);
        assets.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("index"), index},
            {QStringLiteral("name"), a.value(QStringLiteral("name")).toString()},
            {QStringLiteral("kind"), a.value(QStringLiteral("kind")).toString()},
            {QStringLiteral("dur"), a.value(QStringLiteral("durationSeconds")).toDouble()},
            {QStringLiteral("pending"), lib->isImportPending(id)},
        });
    }
    if (anyTimeout) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("import_timeout")},
                {QStringLiteral("detail"), QStringLiteral("Probe still running")},
                {QStringLiteral("assets"), assets}};
    }
    return ok({{QStringLiteral("assets"), assets}});
}

QJsonObject McpDispatcher::opImportMedia(const QJsonObject &args)
{
    AssetLibrary *lib = m_controller->assetLibrary();
    if (!lib)
        return err("not_found", QStringLiteral("No media bin"));
    const QStringList paths = jsonStringList(args.value(QStringLiteral("paths")));
    if (paths.isEmpty())
        return err("bad_args", QStringLiteral("paths required"));

    QStringList missing;
    QStringList existing;
    for (const QString &path : paths) {
        if (!QFileInfo::exists(path) || !QFileInfo(path).isFile())
            missing.append(path);
        else if (lib->indexOfPath(path) >= 0)
            existing.append(path);
    }
    const QStringList ids = lib->importLocalPaths(paths);
    if (ids.isEmpty() && !missing.isEmpty())
        return err("import_failed", QStringLiteral("No readable files"));
    QJsonObject result = waitImport(ids);
    if (!missing.isEmpty())
        result.insert(QStringLiteral("missing"), QJsonArray::fromStringList(missing));
    if (!existing.isEmpty())
        result.insert(QStringLiteral("refreshed"), QJsonArray::fromStringList(existing));
    return result;
}

QJsonObject McpDispatcher::opListAssets() const
{
    AssetLibrary *lib = m_controller->assetLibrary();
    QJsonArray assets;
    if (lib) {
        for (int i = 0; i < lib->count(); ++i) {
            const QVariantMap a = lib->assetAt(i);
            assets.append(QJsonObject{
                {QStringLiteral("index"), i},
                {QStringLiteral("id"), a.value(QStringLiteral("id")).toString()},
                {QStringLiteral("name"), a.value(QStringLiteral("name")).toString()},
                {QStringLiteral("kind"), a.value(QStringLiteral("kind")).toString()},
                {QStringLiteral("dur"), a.value(QStringLiteral("durationSeconds")).toDouble()},
                {QStringLiteral("w"), a.value(QStringLiteral("width")).toInt()},
                {QStringLiteral("h"), a.value(QStringLiteral("height")).toInt()},
            });
        }
    }
    return ok({{QStringLiteral("assets"), assets}});
}

QJsonObject McpDispatcher::opRenameAsset(const QJsonObject &args)
{
    const int index = resolveAsset(args.value(QStringLiteral("asset")));
    const QString name = args.value(QStringLiteral("name")).toString().trimmed();
    if (index < 0)
        return err("not_found", QStringLiteral("Unknown asset"));
    if (name.isEmpty())
        return err("bad_args", QStringLiteral("name required"));
    if (!m_controller->renameAsset(index, name))
        return err("bad_args", QStringLiteral("Rename refused"));
    return ok({{QStringLiteral("index"), index}, {QStringLiteral("name"), name}});
}

QJsonObject McpDispatcher::opAddTrack(const QJsonObject &args)
{
    const QString type = args.value(QStringLiteral("type")).toString().trimmed().toLower();
    const int before = m_controller->tracks().size();
    m_controller->addTrack(type);
    if (m_controller->tracks().size() == before)
        return err("bad_args", QStringLiteral("type must be video|audio|text|subtitle|shape"));
    return ok({{QStringLiteral("track"), 0}, {QStringLiteral("type"), type}});
}

QJsonObject McpDispatcher::opRemoveTrack(const QJsonObject &args)
{
    const int track = jsonInt(args.value(QStringLiteral("track")));
    if (track < 0 || track >= m_controller->tracks().size())
        return err("not_found", QStringLiteral("No such track"));
    m_controller->removeTrack(track);
    return ok({{QStringLiteral("removed"), track}});
}

QJsonObject McpDispatcher::opSetTrack(const QJsonObject &args)
{
    const int track = jsonInt(args.value(QStringLiteral("track")));
    if (track < 0 || track >= m_controller->tracks().size())
        return err("not_found", QStringLiteral("No such track"));
    if (args.contains(QStringLiteral("muted")))
        m_controller->setTrackMuted(track, jsonBool(args.value(QStringLiteral("muted"))));
    if (args.contains(QStringLiteral("hidden")))
        m_controller->setTrackHidden(track, jsonBool(args.value(QStringLiteral("hidden"))));
    return ok({{QStringLiteral("track"), track},
               {QStringLiteral("muted"), m_controller->trackMuted(track)},
               {QStringLiteral("hidden"), m_controller->trackHidden(track)}});
}

QJsonObject McpDispatcher::opPlaceClip(const QJsonObject &args)
{
    const int asset = resolveAsset(args.value(QStringLiteral("asset")));
    if (asset < 0)
        return err("not_found", QStringLiteral("Unknown asset"));
    const double at = args.contains(QStringLiteral("at"))
                          ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                          : m_controller->playheadSeconds();
    const QSet<QString> before = clipIdSet(m_controller);
    if (jsonBool(args.value(QStringLiteral("new_track")))) {
        const int insert = jsonInt(args.value(QStringLiteral("track")), 0);
        m_controller->addClipFromAssetOnNewTrackAt(asset, qMax(0, insert), at);
    } else if (args.contains(QStringLiteral("track"))) {
        const int track = jsonInt(args.value(QStringLiteral("track")));
        if (!m_controller->trackAcceptsAsset(track, asset))
            return err("type_mismatch", QStringLiteral("Track does not accept this asset"));
        m_controller->addClipFromAssetAt(asset, track, at);
    } else {
        m_controller->addClipFromAssetAt(
            asset,
            [&] {
                for (int t = 0; t < m_controller->tracks().size(); ++t) {
                    if (m_controller->trackAcceptsAsset(t, asset))
                        return t;
                }
                return 0;
            }(),
            at);
        if (findNewClipId(before, clipIdSet(m_controller)).isEmpty())
            m_controller->addClipFromAssetOnNewTrackAt(asset, 0, at);
    }

    const QSet<QString> after = clipIdSet(m_controller);
    const QString newId = findNewClipId(before, after);
    if (newId.isEmpty())
        return err("type_mismatch", QStringLiteral("Clip was not placed"));
    const QPair<int, int> loc = m_controller->mcpLocateClip(newId);
    ClipRef ref{loc.first, loc.second, newId};
    moveClipToRequested(ref, at);
    const ClipRef placedRef = resolveClip(QJsonObject{{QStringLiteral("clip"), newId}});
    const QVariantMap clip = m_controller->mcpCompactClip(placedRef.track, placedRef.clip);
    const double placed = clip.value(QStringLiteral("start")).toDouble();
    QJsonObject extra{{QStringLiteral("requested"), at}};
    if (qAbs(placed - at) > 0.001)
        extra.insert(QStringLiteral("reason"), QStringLiteral("gap"));
    extra.insert(QStringLiteral("placed"), placed);
    extra.insert(QStringLiteral("asset"), asset);
    return ok(clipFeedback(placedRef, extra));
}

QJsonObject McpDispatcher::opMoveClip(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    if (!args.contains(QStringLiteral("at")))
        return err("bad_args", QStringLiteral("at required"));
    const double requested = jsonNumber(args.value(QStringLiteral("at")), 0);
    if (m_controller->allowClipOverlap()) {
        m_controller->selectClip(ref.track, ref.clip);
        m_controller->moveClip(ref.track, ref.clip, requested);
    } else {
        m_controller->setClipStart(ref.track, ref.clip, requested);
    }
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    const QVariantMap clip = m_controller->mcpCompactClip(after.track, after.clip);
    const double placed = clip.value(QStringLiteral("start")).toDouble();
    QJsonObject extra{{QStringLiteral("requested"), requested}, {QStringLiteral("placed"), placed}};
    if (qAbs(placed - requested) > 0.001)
        extra.insert(QStringLiteral("reason"), QStringLiteral("gap"));
    return ok(clipFeedback(after, extra));
}

QJsonObject McpDispatcher::opSetDuration(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const double requested = jsonNumber(args.value(QStringLiteral("duration")), -1);
    if (requested <= 0)
        return err("bad_args", QStringLiteral("duration must be > 0"));
    m_controller->setClipDuration(ref.track, ref.clip, requested);
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    const QVariantMap clip = m_controller->mcpCompactClip(after.track, after.clip);
    QJsonObject extra{{QStringLiteral("requested"), requested},
                      {QStringLiteral("dur"), clip.value(QStringLiteral("duration")).toDouble()}};
    return ok(clipFeedback(after, extra));
}

QJsonObject McpDispatcher::opSetTrim(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    m_controller->setClipTrim(ref.track, ref.clip, jsonNumber(args.value(QStringLiteral("in")), 0),
                              jsonNumber(args.value(QStringLiteral("out")), 0));
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    return ok(clipFeedback(after));
}

QJsonObject McpDispatcher::opMoveToTrack(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const int toTrack = jsonInt(args.value(QStringLiteral("to_track")));
    if (toTrack < 0 || toTrack >= m_controller->tracks().size())
        return err("not_found", QStringLiteral("No such track"));
    const QVariantMap before = m_controller->mcpCompactClip(ref.track, ref.clip);
    const double at = args.contains(QStringLiteral("at"))
                          ? jsonNumber(args.value(QStringLiteral("at")), before.value(QStringLiteral("start")).toDouble())
                          : before.value(QStringLiteral("start")).toDouble();
    m_controller->moveClipToTrack(ref.track, ref.clip, toTrack, at);
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    if (!after.valid())
        return err("type_mismatch", QStringLiteral("Move refused (type or locked)"));
    moveClipToRequested(after, at);
    const ClipRef placed = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    return ok(clipFeedback(placed));
}

QJsonObject McpDispatcher::opSplitClip(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const double at = args.contains(QStringLiteral("at"))
                          ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                          : m_controller->playheadSeconds();
    const QVariantMap clip = m_controller->mcpCompactClip(ref.track, ref.clip);
    const double clipStart = clip.value(QStringLiteral("start")).toDouble();
    const double clipEnd = clipStart + clip.value(QStringLiteral("duration")).toDouble();
    const QSet<QString> before = clipIdSet(m_controller);
    m_controller->splitClipAt(ref.track, ref.clip, at);
    const QSet<QString> afterIds = clipIdSet(m_controller);
    QJsonArray ids;
    ids.append(ref.id);
    for (const QString &id : afterIds) {
        if (!before.contains(id))
            ids.append(id);
    }
    if (ids.size() < 2)
        return err("bad_args", QStringLiteral("at=%1 is outside clip [%2, %3)").arg(at).arg(clipStart).arg(clipEnd));
    return ok({{QStringLiteral("clips"), ids}, {QStringLiteral("at"), at}});
}

QJsonObject McpDispatcher::opDeleteClip(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    m_controller->selectClip(ref.track, ref.clip);
    m_controller->deleteSelectedClip();
    if (m_controller->mcpLocateClip(ref.id).first >= 0)
        return err("bad_args", QStringLiteral("Delete refused"));
    return ok({{QStringLiteral("removed"), ref.id}});
}

QJsonObject McpDispatcher::opDuplicateClip(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QSet<QString> before = clipIdSet(m_controller);
    m_controller->selectClip(ref.track, ref.clip);
    m_controller->duplicateSelectedClip();
    const QString newId = findNewClipId(before, clipIdSet(m_controller));
    if (newId.isEmpty())
        return err("bad_args", QStringLiteral("Duplicate refused"));
    const QPair<int, int> loc = m_controller->mcpLocateClip(newId);
    return ok(clipFeedback({loc.first, loc.second, newId}));
}

QJsonObject McpDispatcher::opUndo()
{
    if (!m_controller->undoAvailable())
        return err("bad_args", QStringLiteral("Nothing to undo"));
    m_controller->undo();
    return ok({});
}

QJsonObject McpDispatcher::opRedo()
{
    if (!m_controller->redoAvailable())
        return err("bad_args", QStringLiteral("Nothing to redo"));
    m_controller->redo();
    return ok({});
}

QJsonObject McpDispatcher::opSetOverlap(const QJsonObject &args)
{
    if (!args.contains(QStringLiteral("enabled")))
        return err("bad_args", QStringLiteral("enabled required"));
    m_controller->setAllowClipOverlap(jsonBool(args.value(QStringLiteral("enabled"))));
    return ok({{QStringLiteral("overlap"), m_controller->allowClipOverlap()}});
}

QJsonObject McpDispatcher::opSetTransform(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    QVariantMap patch;
    for (const char *key : {"x", "y", "w", "h", "rotation", "opacity"}) {
        const QString k = QString::fromUtf8(key);
        if (args.contains(k))
            patch.insert(k, jsonNumber(args.value(k), 0));
    }
    if (patch.isEmpty())
        return err("bad_args", QStringLiteral("No transform fields"));
    if (!m_controller->mcpSetClipCanvas(ref.track, ref.clip, patch))
        return err("bad_args", QStringLiteral("Transform refused"));
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    return ok(clipFeedback(after));
}

QJsonObject McpDispatcher::opResetTransform(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    m_controller->resetClipTransform(ref.track, ref.clip);
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    return ok(clipFeedback(after));
}

QJsonObject McpDispatcher::opSeek(const QJsonObject &args)
{
    if (!args.contains(QStringLiteral("at")))
        return err("bad_args", QStringLiteral("at required"));
    m_controller->setPlayheadSeconds(jsonNumber(args.value(QStringLiteral("at")), 0));
    return ok({{QStringLiteral("at"), m_controller->playheadSeconds()}});
}

QJsonObject McpDispatcher::opPlay()
{
    m_controller->setPlaying(true);
    return ok({{QStringLiteral("playing"), m_controller->playing()}});
}

QJsonObject McpDispatcher::opPause()
{
    m_controller->setPlaying(false);
    return ok({{QStringLiteral("playing"), m_controller->playing()}});
}

QJsonObject McpDispatcher::opSetWorkArea(const QJsonObject &args)
{
    if (!args.contains(QStringLiteral("in")) || !args.contains(QStringLiteral("out")))
        return err("bad_args", QStringLiteral("in and out required"));
    const double inSeconds = jsonNumber(args.value(QStringLiteral("in")), -1);
    const double outSeconds = jsonNumber(args.value(QStringLiteral("out")), -1);
    if (!m_controller->mcpSetWorkArea(inSeconds, outSeconds))
        return err("bad_args", QStringLiteral("out must be greater than in"));
    return ok({{QStringLiteral("work_in"), m_controller->workAreaInSeconds()},
               {QStringLiteral("work_out"), m_controller->workAreaOutSeconds()}});
}

QJsonObject McpDispatcher::opClearWorkArea()
{
    m_controller->clearWorkArea();
    return ok({{QStringLiteral("cleared"), true}});
}

QJsonObject McpDispatcher::opAddText(const QJsonObject &args)
{
    QString text = args.value(QStringLiteral("text")).toString();
    if (text.trimmed().isEmpty())
        text = QStringLiteral("Text");
    const double at = args.contains(QStringLiteral("at"))
                          ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                          : m_controller->playheadSeconds();
    const QSet<QString> before = clipIdSet(m_controller);
    m_controller->addTextClip(text, at, args.value(QStringLiteral("preset")).toString());
    const QString newId = findNewClipId(before, clipIdSet(m_controller));
    if (newId.isEmpty())
        return err("bad_args", QStringLiteral("Text clip not added"));
    const QPair<int, int> loc = m_controller->mcpLocateClip(newId);
    const ClipRef ref{loc.first, loc.second, newId};
    moveClipToRequested(ref, at);
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), newId}});
    return ok(clipFeedback(after));
}

QJsonObject McpDispatcher::opSetText(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    if (args.contains(QStringLiteral("text")))
        m_controller->setClipTextContent(ref.track, ref.clip, args.value(QStringLiteral("text")).toString());
    if (args.contains(QStringLiteral("style")))
        m_controller->setTextStyle(ref.track, ref.clip, args.value(QStringLiteral("style")).toObject().toVariantMap());
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    return ok(clipFeedback(after));
}

QJsonObject McpDispatcher::opListEffects(const QJsonObject &args) const
{
    return catalogListing(m_controller->effectCatalog(), args, "effects", "effect", "list_effects");
}

QJsonObject McpDispatcher::opListAudioEffects(const QJsonObject &args) const
{
    return catalogListing(m_controller->audioEffectCatalog(), args, "effects", "audio effect", "list_audio_effects");
}

QJsonObject McpDispatcher::opListTransitions(const QJsonObject &args) const
{
    return catalogListing(m_controller->transitionKinds(), args, "transitions", "transition", "list_transitions");
}

QJsonObject McpDispatcher::opAddEffect(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString raw = args.value(QStringLiteral("effect")).toString();
    QStringList nearest;
    const QString effect = resolveCatalogId(raw, catalogIds(m_controller->effectCatalog()), &nearest);
    if (effect.isEmpty())
        return unknownCatalogId("effect", raw, nearest, "list_effects");
    const QVariantList before = m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("effects")).toList();
    m_controller->addEffect(ref.track, ref.clip, effect);
    const QVariantList after = m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("effects")).toList();
    if (after.size() <= before.size())
        return err("not_found", QStringLiteral("Unknown effect id"));
    return ok(clipFeedback(ref, effectHost(ref, {{QStringLiteral("index"), after.size() - 1},
                                                 {QStringLiteral("effect"), effect}})));
}

QJsonObject McpDispatcher::opRemoveEffect(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const int index = jsonInt(args.value(QStringLiteral("index")));
    const int before = m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("effects")).toList().size();
    m_controller->removeEffect(ref.track, ref.clip, index);
    const int after = m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("effects")).toList().size();
    if (after >= before)
        return err("not_found", QStringLiteral("No such effect index"));
    return ok(clipFeedback(ref, {{QStringLiteral("removed"), index}}));
}

QJsonObject McpDispatcher::opSetEffectParam(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    m_controller->setEffectParam(ref.track, ref.clip, jsonInt(args.value(QStringLiteral("index"))),
                                 args.value(QStringLiteral("key")).toString(),
                                 jsonNumber(args.value(QStringLiteral("value")), 0));
    return ok(clipFeedback(ref));
}

QJsonObject McpDispatcher::opAddAudioEffect(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString raw = args.value(QStringLiteral("effect")).toString();
    QStringList nearest;
    const QString effect = resolveCatalogId(raw, catalogIds(m_controller->audioEffectCatalog()), &nearest);
    if (effect.isEmpty())
        return unknownCatalogId("audio effect", raw, nearest, "list_audio_effects");
    const int before =
        m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("audioEffects")).toList().size();
    m_controller->addAudioEffect(ref.track, ref.clip, effect);
    const int after =
        m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("audioEffects")).toList().size();
    if (after <= before)
        return err("not_found", QStringLiteral("Unknown audio effect id"));
    return ok(clipFeedback(ref, effectHost(ref, {{QStringLiteral("index"), after - 1},
                                                 {QStringLiteral("effect"), effect}})));
}

// Effects live on an adjustment clip that hosts the target clip's stack (the app creates one on
// its own lane the first time). Every effect op still addresses the original clip, but the
// agent sees a new track and clip in inspect, so say where the stack went.
QJsonObject McpDispatcher::effectHost(const ClipRef &ref, QJsonObject extra) const
{
    const int hostTrack = m_controller->selectedTrack();
    const int hostClip = m_controller->selectedClip();
    if (hostTrack >= 0 && hostClip >= 0 && (hostTrack != ref.track || hostClip != ref.clip)) {
        extra.insert(QStringLiteral("host"),
                     QJsonObject{{QStringLiteral("track"), hostTrack},
                                 {QStringLiteral("index"), hostClip},
                                 {QStringLiteral("clip"), m_controller->mcpClipId(hostTrack, hostClip)},
                                 {QStringLiteral("kind"), QStringLiteral("adjustment")}});
    }
    return extra;
}

QJsonObject McpDispatcher::opRemoveAudioEffect(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const int index = jsonInt(args.value(QStringLiteral("index")));
    const int before =
        m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("audioEffects")).toList().size();
    m_controller->removeAudioEffect(ref.track, ref.clip, index);
    const int after =
        m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("audioEffects")).toList().size();
    if (after >= before)
        return err("not_found", QStringLiteral("No such audio effect index"));
    return ok(clipFeedback(ref, {{QStringLiteral("removed"), index}}));
}

QJsonObject McpDispatcher::opSetAudioEffectParam(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    m_controller->setAudioEffectParam(ref.track, ref.clip, jsonInt(args.value(QStringLiteral("index"))),
                                      args.value(QStringLiteral("key")).toString(),
                                      jsonNumber(args.value(QStringLiteral("value")), 0));
    return ok(clipFeedback(ref));
}

QJsonObject McpDispatcher::opAddTransition(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString raw = args.value(QStringLiteral("kind")).toString().trimmed();
    QString kind = QStringLiteral("crossfade");
    if (!raw.isEmpty()) {
        QStringList nearest;
        kind = resolveCatalogId(raw, catalogIds(m_controller->transitionKinds()), &nearest);
        if (kind.isEmpty())
            return unknownCatalogId("transition", raw, nearest, "list_transitions");
    }
    const double duration = args.contains(QStringLiteral("duration"))
                                ? jsonNumber(args.value(QStringLiteral("duration")), 0.5)
                                : 0.5;
    m_controller->addTransition(ref.track, ref.clip, kind, duration);
    const QVariantMap tr = m_controller->transitionBetweenClips(ref.track, ref.clip);
    if (tr.isEmpty())
        return err("bad_args", QStringLiteral("No neighbour clip for a transition"));
    return ok({{QStringLiteral("id"), tr.value(QStringLiteral("id")).toString()},
               {QStringLiteral("kind"), tr.value(QStringLiteral("kind")).toString()},
               {QStringLiteral("dur"), tr.value(QStringLiteral("duration")).toDouble()},
               {QStringLiteral("track"), ref.track}});
}

QJsonObject McpDispatcher::opRemoveTransition(const QJsonObject &args)
{
    const int track = jsonInt(args.value(QStringLiteral("track")));
    const QString id = args.value(QStringLiteral("id")).toString();
    if (track < 0 || id.isEmpty())
        return err("bad_args", QStringLiteral("track and id required"));
    m_controller->removeTransition(track, id);
    return ok({{QStringLiteral("removed"), id}});
}

QJsonObject McpDispatcher::opSetProjectSetup(const QJsonObject &args)
{
    const int width = jsonInt(args.value(QStringLiteral("width")));
    const int height = jsonInt(args.value(QStringLiteral("height")));
    const int fps = jsonInt(args.value(QStringLiteral("fps")));
    if (width <= 0 || height <= 0 || fps <= 0)
        return err("bad_args", QStringLiteral("width, height, fps required"));
    m_controller->setProjectSetup(width, height, fps);
    return ok({{QStringLiteral("w"), width},
               {QStringLiteral("h"), height},
               {QStringLiteral("fps"), fps}});
}

QJsonObject McpDispatcher::opSetBackground(const QJsonObject &args)
{
    QVariantMap patch;
    if (args.contains(QStringLiteral("kind")))
        patch.insert(QStringLiteral("kind"), args.value(QStringLiteral("kind")).toString());
    if (args.contains(QStringLiteral("color")))
        patch.insert(QStringLiteral("color"), args.value(QStringLiteral("color")).toString());
    if (args.contains(QStringLiteral("blurStrength")))
        patch.insert(QStringLiteral("blurStrength"), jsonNumber(args.value(QStringLiteral("blurStrength")), 0));
    if (patch.isEmpty())
        return err("bad_args", QStringLiteral("At least one of kind, color, blurStrength required"));
    m_controller->setBackground(patch);
    return ok(QJsonObject::fromVariantMap(m_controller->background()));
}

QJsonObject McpDispatcher::opSetMetadata(const QJsonObject &args)
{
    const QVariantMap current = m_controller->projectMetadata();
    const QString title =
        args.contains(QStringLiteral("title")) ? args.value(QStringLiteral("title")).toString()
                                               : current.value(QStringLiteral("title")).toString();
    const QString author =
        args.contains(QStringLiteral("author")) ? args.value(QStringLiteral("author")).toString()
                                               : current.value(QStringLiteral("author")).toString();
    const QString description = args.contains(QStringLiteral("description"))
                                    ? args.value(QStringLiteral("description")).toString()
                                    : current.value(QStringLiteral("description")).toString();
    m_controller->setProjectMetadata(title, author, description);
    return ok({{QStringLiteral("title"), title},
               {QStringLiteral("author"), author},
               {QStringLiteral("description"), description}});
}

QJsonObject McpDispatcher::opSaveProject(const QJsonObject &args)
{
    QString path = args.value(QStringLiteral("path")).toString();
    if (path.isEmpty())
        path = m_controller->currentProjectPath();
    if (path.isEmpty())
        return err("bad_args", QStringLiteral("path required — project has never been saved"));
    const QFileInfo info(path);
    if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath()))
        return err("bad_args", QStringLiteral("Could not create parent folder"));
    m_controller->saveProject(QUrl::fromLocalFile(path));
    return ok({{QStringLiteral("path"), path}});
}

QJsonObject McpDispatcher::opListExportOptions() const
{
    QJsonArray scales;
    for (const QVariant &v : m_controller->exportScaleOptions()) {
        const QVariantMap m = v.toMap();
        scales.append(QJsonObject{
            {QStringLiteral("id"), m.value(QStringLiteral("id")).toString()},
            {QStringLiteral("w"), m.value(QStringLiteral("width")).toInt()},
            {QStringLiteral("h"), m.value(QStringLiteral("height")).toInt()},
        });
    }
    QJsonArray fps;
    for (const QVariant &v : m_controller->exportFrameRateOptions()) {
        fps.append(QJsonObject{
            {QStringLiteral("id"), v.toMap().value(QStringLiteral("id")).toString()},
        });
    }
    return ok({
        {QStringLiteral("scales"), scales},
        {QStringLiteral("fps"), fps},
        {QStringLiteral("video"), compactAvailableCodecs(m_controller->exportVideoCodecs())},
        {QStringLiteral("audio"), compactAvailableCodecs(m_controller->exportAudioCodecs())},
        {QStringLiteral("gif"), m_controller->exportGifAvailable()},
        {QStringLiteral("folder"), m_controller->lastExportFolder()},
    });
}

QJsonObject McpDispatcher::opExportVideo(const QJsonObject &args)
{
    QJsonObject patched = args;
    patched.insert(QStringLiteral("wait"), false);
    return opExport(patched);
}

QJsonObject McpDispatcher::opExport(const QJsonObject &args)
{
    if (m_controller->exportInProgress())
        return err("export_busy", QStringLiteral("Export already in progress"));

    QString path = localExportPath(args.value(QStringLiteral("path")).toString());
    if (path.isEmpty())
        return err("bad_args", QStringLiteral("path required"));

    QVariantMap map = mergedExportSettings(m_controller);
    QString error;
    if (!applyExportPatch(m_controller, map, args, &error))
        return err("bad_args", error);

    const QJsonObject compact = compactExportSettings(m_controller, map);
    const QString suffix = compact.value(QStringLiteral("suffix")).toString();
    QFileInfo info(path);
    if (info.isDir() || path.endsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('\\'))) {
        const QString name = m_controller->projectName().isEmpty()
                                 ? QStringLiteral("export")
                                 : m_controller->projectName();
        path = QDir(path).filePath(name + QLatin1Char('.') + suffix);
        info.setFile(path);
    } else if (info.suffix().isEmpty() && !suffix.isEmpty()) {
        path += QLatin1Char('.') + suffix;
        info.setFile(path);
    }

    const QString folder = info.absolutePath();
    if (!folder.isEmpty() && !QDir().mkpath(folder))
        return err("bad_args", QStringLiteral("Could not create output folder"));

    const bool wait = jsonBool(args.value(QStringLiteral("wait")), true);
    const double timeoutSec = jsonNumber(args.value(QStringLiteral("timeout")), 0);

    bool finished = false;
    bool success = false;
    QEventLoop loop;
    const auto conn = QObject::connect(
        m_controller, &AppController::exportFinished, &loop, [&](bool ok) {
            finished = true;
            success = ok;
            loop.quit();
        });

    m_controller->exportWithSettings(QUrl::fromLocalFile(info.absoluteFilePath()), map);

    if (!wait) {
        QObject::disconnect(conn);
        if (finished && !success)
            return err("export_failed", m_controller->lastMessage());
        return ok({{QStringLiteral("started"), true},
                   {QStringLiteral("path"), info.absoluteFilePath()},
                   {QStringLiteral("busy"), m_controller->exportInProgress()}});
    }

    if (!finished) {
        QTimer timeout;
        if (timeoutSec > 0) {
            timeout.setSingleShot(true);
            QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
            timeout.start(static_cast<int>(timeoutSec * 1000.0));
        }
        loop.exec();
    }
    QObject::disconnect(conn);

    if (!finished)
        return err("export_timeout", QStringLiteral("Still encoding; call export_status or cancel_export"));
    if (!success)
        return err("export_failed", m_controller->lastMessage());

    QJsonObject extra = compact;
    extra.insert(QStringLiteral("path"), info.absoluteFilePath());
    extra.insert(QStringLiteral("bytes"), QFile(info.absoluteFilePath()).size());
    return ok(extra);
}

QJsonObject McpDispatcher::opExportStatus() const
{
    return ok({
        {QStringLiteral("busy"), m_controller->exportInProgress()},
        {QStringLiteral("progress"), m_controller->exportProgress()},
        {QStringLiteral("message"), m_controller->lastMessage()},
    });
}

QJsonArray McpDispatcher::speedPointsToJson(const QVariantList &points)
{
    QJsonArray out;
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        out.append(QJsonObject{
            {QStringLiteral("pos"), map.value(QStringLiteral("pos")).toDouble()},
            {QStringLiteral("speed"), map.value(QStringLiteral("speed"), 1.0).toDouble()},
            {QStringLiteral("inDx"), map.value(QStringLiteral("inDx")).toDouble()},
            {QStringLiteral("inDy"), map.value(QStringLiteral("inDy")).toDouble()},
            {QStringLiteral("outDx"), map.value(QStringLiteral("outDx")).toDouble()},
            {QStringLiteral("outDy"), map.value(QStringLiteral("outDy")).toDouble()},
            {QStringLiteral("corner"), map.value(QStringLiteral("corner")).toBool()},
        });
    }
    return out;
}

QVariantList McpDispatcher::speedPointsFromJson(const QJsonArray &points)
{
    QVariantList out;
    for (const QJsonValue &entry : points) {
        const QJsonObject map = entry.toObject();
        out.append(QVariantMap{
            {QStringLiteral("pos"), map.value(QStringLiteral("pos")).toDouble()},
            {QStringLiteral("speed"), map.contains(QStringLiteral("speed")) ? map.value(QStringLiteral("speed")).toDouble()
                                                                            : 1.0},
            {QStringLiteral("inDx"), map.value(QStringLiteral("inDx")).toDouble()},
            {QStringLiteral("inDy"), map.value(QStringLiteral("inDy")).toDouble()},
            {QStringLiteral("outDx"), map.value(QStringLiteral("outDx")).toDouble()},
            {QStringLiteral("outDy"), map.value(QStringLiteral("outDy")).toDouble()},
            {QStringLiteral("corner"), map.value(QStringLiteral("corner")).toBool()},
        });
    }
    return out;
}

QJsonObject McpDispatcher::opListAnimatedProperties(const QJsonObject &args) const
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QStringList props = m_controller->clipAnimatedProperties(ref.track, ref.clip);
    QJsonArray list;
    for (const QString &prop : props)
        list.append(prop);
    return ok({{QStringLiteral("props"), list}});
}

QJsonObject McpDispatcher::opListKeyframes(const QJsonObject &args) const
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString prop = args.value(QStringLiteral("prop")).toString().trimmed();
    if (prop.isEmpty())
        return err("bad_args", QStringLiteral("prop required"));
    const QVariantList keys = m_controller->clipKeyframes(ref.track, ref.clip, prop);
    QJsonArray points;
    for (const QVariant &entry : keys) {
        const QVariantMap map = entry.toMap();
        points.append(QJsonObject{
            {QStringLiteral("seconds"), map.value(QStringLiteral("seconds")).toDouble()},
            {QStringLiteral("value"), map.value(QStringLiteral("value")).toDouble()},
            {QStringLiteral("inDx"), map.value(QStringLiteral("inDx")).toDouble()},
            {QStringLiteral("inDy"), map.value(QStringLiteral("inDy")).toDouble()},
            {QStringLiteral("outDx"), map.value(QStringLiteral("outDx")).toDouble()},
            {QStringLiteral("outDy"), map.value(QStringLiteral("outDy")).toDouble()},
            {QStringLiteral("corner"), map.value(QStringLiteral("corner")).toBool()},
            {QStringLiteral("hold"), map.value(QStringLiteral("hold")).toBool()},
            {QStringLiteral("easing"), map.value(QStringLiteral("easing")).toString()},
            {QStringLiteral("custom"), map.value(QStringLiteral("custom")).toBool()},
        });
    }
    const bool enabled = m_controller->clipPropertyKeyframesEnabled(ref.track, ref.clip, prop);
    return ok({{QStringLiteral("prop"), prop}, {QStringLiteral("enabled"), enabled}, {QStringLiteral("keys"), points}});
}

QJsonObject McpDispatcher::opSetKeyframe(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString prop = args.value(QStringLiteral("prop")).toString().trimmed();
    if (prop.isEmpty())
        return err("bad_args", QStringLiteral("prop required"));
    if (!args.contains(QStringLiteral("at")) || !args.contains(QStringLiteral("value")))
        return err("bad_args", QStringLiteral("at and value required"));
    const double at = jsonNumber(args.value(QStringLiteral("at")), 0);
    const double value = jsonNumber(args.value(QStringLiteral("value")), 0);
    m_controller->setClipKeyframe(ref.track, ref.clip, prop, at, value);
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    return ok(clipFeedback(after, {{QStringLiteral("prop"), prop}, {QStringLiteral("at"), at}}));
}

QJsonObject McpDispatcher::opRemoveKeyframe(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString prop = args.value(QStringLiteral("prop")).toString().trimmed();
    if (prop.isEmpty() || !args.contains(QStringLiteral("at")))
        return err("bad_args", QStringLiteral("prop and at required"));
    m_controller->removeClipKeyframe(ref.track, ref.clip, prop, jsonNumber(args.value(QStringLiteral("at")), 0));
    return ok({{QStringLiteral("prop"), prop}});
}

QJsonObject McpDispatcher::opSetKeyframeInterpolation(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString prop = args.value(QStringLiteral("prop")).toString().trimmed();
    const QString mode = args.value(QStringLiteral("mode")).toString().trimmed().toLower();
    if (prop.isEmpty() || !args.contains(QStringLiteral("at")) || mode.isEmpty())
        return err("bad_args", QStringLiteral("prop, at, and mode required"));
    const double at = jsonNumber(args.value(QStringLiteral("at")), 0);
    m_controller->setPlayheadSeconds(at);
    m_controller->setKeyframeInterpolation(ref.track, ref.clip, prop, mode);
    return ok({{QStringLiteral("prop"), prop}, {QStringLiteral("at"), at}, {QStringLiteral("mode"), mode}});
}

QJsonObject McpDispatcher::opSetKeyframeTangents(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString prop = args.value(QStringLiteral("prop")).toString().trimmed();
    if (prop.isEmpty() || !args.contains(QStringLiteral("at")))
        return err("bad_args", QStringLiteral("prop and at required"));
    const double at = jsonNumber(args.value(QStringLiteral("at")), 0);
    m_controller->setKeyframeTangents(ref.track, ref.clip, prop, at,
                                    jsonNumber(args.value(QStringLiteral("inDx")), 0),
                                    jsonNumber(args.value(QStringLiteral("inDy")), 0),
                                    jsonNumber(args.value(QStringLiteral("outDx")), 0),
                                    jsonNumber(args.value(QStringLiteral("outDy")), 0),
                                    jsonBool(args.value(QStringLiteral("corner"))));
    return ok({{QStringLiteral("prop"), prop}, {QStringLiteral("at"), at}});
}

QJsonObject McpDispatcher::opSetKeyframeHold(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString prop = args.value(QStringLiteral("prop")).toString().trimmed();
    if (prop.isEmpty() || !args.contains(QStringLiteral("at")) || !args.contains(QStringLiteral("hold")))
        return err("bad_args", QStringLiteral("prop, at, and hold required"));
    m_controller->setKeyframeHold(ref.track, ref.clip, prop, jsonNumber(args.value(QStringLiteral("at")), 0),
                                  jsonBool(args.value(QStringLiteral("hold"))));
    return ok({{QStringLiteral("prop"), prop}});
}

QJsonObject McpDispatcher::opSetPropertyKeyframesEnabled(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QString prop = args.value(QStringLiteral("prop")).toString().trimmed();
    if (prop.isEmpty() || !args.contains(QStringLiteral("enabled")))
        return err("bad_args", QStringLiteral("prop and enabled required"));
    m_controller->setClipPropertyKeyframesEnabled(ref.track, ref.clip, prop,
                                                  jsonBool(args.value(QStringLiteral("enabled"))));
    return ok({{QStringLiteral("prop"), prop},
               {QStringLiteral("enabled"),
                m_controller->clipPropertyKeyframesEnabled(ref.track, ref.clip, prop)}});
}

QJsonObject McpDispatcher::opListSpeedCurve(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    if (m_controller->speedCurveSessionActive())
        m_controller->endSpeedCurveSession();
    m_controller->beginSpeedCurveSession(ref.track, ref.clip);
    if (!m_controller->speedCurveSessionActive())
        return err("bad_args", QStringLiteral("Clip cannot use a speed curve"));
    const QVariantMap clipMap = m_controller->clipAt(ref.track, ref.clip);
    const bool hasCurve = clipMap.value(QStringLiteral("hasSpeedCurve")).toBool();
    const QJsonArray points = speedPointsToJson(m_controller->speedCurvePoints());
    const double retimed = m_controller->speedCurveRetimedDuration();
    m_controller->endSpeedCurveSession();
    return ok({{QStringLiteral("hasCurve"), hasCurve},
               {QStringLiteral("points"), points},
               {QStringLiteral("retimedDuration"), retimed}});
}

QJsonObject McpDispatcher::opSetSpeedCurve(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    const QJsonArray pointArray = args.value(QStringLiteral("points")).toArray();
    if (pointArray.size() < 2)
        return err("bad_args", QStringLiteral("points needs at least two entries"));
    if (m_controller->speedCurveSessionActive())
        m_controller->endSpeedCurveSession();
    m_controller->beginSpeedCurveSession(ref.track, ref.clip);
    if (!m_controller->speedCurveSessionActive())
        return err("bad_args", QStringLiteral("Clip cannot use a speed curve"));
    m_controller->setSpeedCurvePoints(speedPointsFromJson(pointArray));
    m_controller->applySpeedCurve();
    const int track = m_controller->selectedTrack();
    const int clip = m_controller->selectedClip();
    const QString newId = m_controller->mcpClipId(track, clip);
    m_controller->endSpeedCurveSession();
    if (newId.isEmpty())
        return err("bad_args", QStringLiteral("Speed curve was not applied"));
    return ok({{QStringLiteral("id"), newId},
               {QStringLiteral("track"), track},
               {QStringLiteral("index"), clip},
               {QStringLiteral("retimedDuration"), m_controller->clipAt(track, clip).value(QStringLiteral("duration")).toDouble()}});
}

QJsonObject McpDispatcher::opClearSpeedCurve(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);
    m_controller->clearClipSpeedCurve(ref.track, ref.clip);
    const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
    return ok(clipFeedback(after));
}

QJsonObject McpDispatcher::opGetUiPreferences() const
{
    return ok({
        {QStringLiteral("theme"),
         QJsonObject{{QStringLiteral("overridden"), m_controller->darkModeOverridden()},
                     {QStringLiteral("dark"), m_controller->darkModePreferred()}}},
        {QStringLiteral("autoKey"), m_controller->autoKeyEnabled()},
        {QStringLiteral("mediaGrid"), m_controller->mediaGridMode()},
        {QStringLiteral("reopenLastProject"), m_controller->reopenLastProject()},
    });
}

QJsonObject McpDispatcher::opSetTheme(const QJsonObject &args)
{
    if (!args.contains(QStringLiteral("dark")))
        return err("bad_args", QStringLiteral("dark required"));
    m_controller->setDarkModePreference(jsonBool(args.value(QStringLiteral("dark"))));
    return ok({
        {QStringLiteral("overridden"), m_controller->darkModeOverridden()},
        {QStringLiteral("dark"), m_controller->darkModePreferred()},
    });
}

QJsonObject McpDispatcher::opListShortcuts(const QJsonObject &args) const
{
    const QString q = args.value(QStringLiteral("q")).toString().trimmed();
    QJsonArray actions;
    for (const QVariant &entry : m_controller->actions()) {
        const QVariantMap map = entry.toMap();
        const QString id = map.value(QStringLiteral("id")).toString();
        const QString label = map.value(QStringLiteral("label")).toString();
        const QString shortcut = map.value(QStringLiteral("shortcut")).toString();
        if (!q.isEmpty() && !id.contains(q, Qt::CaseInsensitive) && !label.contains(q, Qt::CaseInsensitive)
            && !shortcut.contains(q, Qt::CaseInsensitive))
            continue;
        actions.append(QJsonObject{
            {QStringLiteral("id"), id},
            {QStringLiteral("label"), label},
            {QStringLiteral("shortcut"), shortcut},
        });
    }
    return ok({{QStringLiteral("actions"), actions}, {QStringLiteral("n"), actions.size()}});
}

QJsonObject McpDispatcher::opSetShortcut(const QJsonObject &args)
{
    const QString action = args.value(QStringLiteral("action")).toString().trimmed();
    if (action.isEmpty())
        return err("bad_args", QStringLiteral("action required"));
    if (!args.contains(QStringLiteral("keys")))
        return err("bad_args", QStringLiteral("keys required"));
    const QString keys = args.value(QStringLiteral("keys")).toString();
    const QString conflict = m_controller->setShortcut(action, keys);
    if (!conflict.isEmpty())
        return err("conflict", conflict);
    return ok({{QStringLiteral("action"), action}, {QStringLiteral("keys"), keys}});
}

QJsonObject McpDispatcher::opResetShortcuts()
{
    m_controller->resetShortcuts();
    return ok({{QStringLiteral("reset"), true}});
}

} // namespace drift::mcp

