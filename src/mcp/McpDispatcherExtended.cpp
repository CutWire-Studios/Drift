#include "mcp/McpCatalog.h"
#include "mcp/McpDispatcher.h"

#include "core/TextAnimationPreset.h"
#include "core/TextLook.h"
#include "mcp/McpJson.h"
#include "mcp/McpMarket.h"

#include "models/AppController.h"
#include "models/AssetLibrary.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <memory>

namespace drift::mcp {
namespace {

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

QString argString(const QJsonObject &args, const QString &a, const QString &b = {})
{
    if (args.contains(a) && args.value(a).isString())
        return args.value(a).toString().trimmed();
    if (!b.isEmpty() && args.contains(b) && args.value(b).isString())
        return args.value(b).toString().trimmed();
    return {};
}

// Same pair as in McpDispatcher.cpp: several ops mint clips through void controller methods and
// have to discover the new id by diffing. Duplicated rather than shared for the same reason the
// json helpers above are — this file is a continuation of that one, not a client of it.
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

QString findNewClipId(const QSet<QString> &before, const QSet<QString> &after)
{
    for (const QString &id : after) {
        if (!before.contains(id))
            return id;
    }
    return {};
}

QJsonObject replyMintedClips(AppController *c, const QSet<QString> &before)
{
    QJsonArray ids;
    QString first;
    int track = -1;
    int index = -1;
    const QSet<QString> after = clipIdSet(c);
    for (const QString &id : after) {
        if (before.contains(id))
            continue;
        ids.append(id);
        if (first.isEmpty()) {
            first = id;
            const QPair<int, int> loc = c->mcpLocateClip(id);
            track = loc.first;
            index = loc.second;
        }
    }
    QJsonObject extra;
    if (!first.isEmpty()) {
        extra.insert(QStringLiteral("id"), first);
        extra.insert(QStringLiteral("track"), track);
        extra.insert(QStringLiteral("index"), index);
    }
    extra.insert(QStringLiteral("ids"), ids);
    extra.insert(QStringLiteral("n"), ids.size());
    return ok(extra);
}

QString localPath(const QString &raw)
{
    const QString trimmed = raw.trimmed();
    if (trimmed.startsWith(QLatin1String("file:")))
        return QUrl(trimmed).toLocalFile();
    return trimmed;
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

QJsonArray compactCatalogList(const QVariantList &list)
{
    QJsonArray out;
    for (const QVariant &v : list)
        out.append(compactCatalogItem(v.toMap()));
    return out;
}

QJsonObject compactEmojiItem(const QVariantMap &item)
{
    return {{QStringLiteral("id"), item.value(QStringLiteral("emoji")).toString()},
            {QStringLiteral("label"), item.value(QStringLiteral("name")).toString()},
            {QStringLiteral("cat"), item.value(QStringLiteral("group")).toString()}};
}

QJsonObject compactFontItem(const QVariantMap &item)
{
    return {{QStringLiteral("id"), item.value(QStringLiteral("id")).toString()},
            {QStringLiteral("label"), item.value(QStringLiteral("family")).toString()},
            {QStringLiteral("cat"), item.value(QStringLiteral("category")).toString()}};
}

// q is matched against every string field of the source row (so emoji keywords count even
// though the compact row drops them); limit ≤ 0 means unlimited.
QJsonObject filterCatalog(const QVariantList &list, const QString &q, int limit, const char *key,
                          QJsonObject (*compact)(const QVariantMap &) = compactCatalogItem)
{
    QJsonArray out;
    int n = 0;
    for (const QVariant &v : list) {
        const QVariantMap item = v.toMap();
        if (!q.isEmpty()) {
            bool hit = false;
            for (auto it = item.begin(); it != item.end() && !hit; ++it)
                hit = it.value().typeId() == QMetaType::QString && it.value().toString().contains(q, Qt::CaseInsensitive);
            if (!hit)
                continue;
        }
        ++n;
        if (limit <= 0 || out.size() < limit)
            out.append(compact(item));
    }
    return ok({{QString::fromUtf8(key), out}, {QStringLiteral("n"), n}});
}

QVariantList fadeCurvePointsFromJson(const QJsonArray &points)
{
    QVariantList out;
    for (const QJsonValue &entry : points) {
        const QJsonObject map = entry.toObject();
        out.append(QVariantMap{
            {QStringLiteral("t"), jsonNumber(map.value(QStringLiteral("t")), 0)},
            {QStringLiteral("g"), jsonNumber(map.value(QStringLiteral("g")), 0)},
        });
    }
    return out;
}

QJsonArray fadeCurvePointsToJson(const QVariantList &points)
{
    QJsonArray out;
    for (const QVariant &entry : points) {
        const QVariantMap map = entry.toMap();
        out.append(QJsonObject{
            {QStringLiteral("t"), map.value(QStringLiteral("t")).toDouble()},
            {QStringLiteral("g"), map.value(QStringLiteral("g")).toDouble()},
        });
    }
    return out;
}

QVariantList segmentationPointsFromJson(const QJsonArray &points)
{
    QVariantList out;
    for (const QJsonValue &entry : points) {
        const QJsonObject map = entry.toObject();
        out.append(QVariantMap{
            {QStringLiteral("x"), jsonNumber(map.value(QStringLiteral("x")), 0)},
            {QStringLiteral("y"), jsonNumber(map.value(QStringLiteral("y")), 0)},
            {QStringLiteral("include"), jsonBool(map.value(QStringLiteral("include")), true)},
        });
    }
    return out;
}

QVariantList subtitleCuesFromJson(const QJsonArray &cues)
{
    QVariantList out;
    for (const QJsonValue &entry : cues) {
        const QJsonObject cue = entry.toObject();
        out.append(QVariantMap{
            {QStringLiteral("start"), jsonNumber(cue.value(QStringLiteral("start")), 0)},
            {QStringLiteral("end"), jsonNumber(cue.value(QStringLiteral("end")), 0)},
            {QStringLiteral("text"), cue.value(QStringLiteral("text")).toString()},
        });
    }
    return out;
}

} // namespace

// Splits back to front. Every cut mints a new clip for the right-hand side and shifts the
// indices after it, but the piece before a cut keeps both its position and its id — so working
// from the last time backwards, re-resolving by UUID each pass, needs no index bookkeeping. The
// id the caller passed ends up naming the first piece.
QJsonObject McpDispatcher::opSplitOnBeats(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);

    const QString unit = argString(args, QStringLiteral("unit"));
    // Read the grid before touching anything: finishEdit drops the beat analysis as soon as the
    // mix changes, so the first cut can take the rest of the grid with it.
    const QList<double> times = m_controller->mcpBeatTimes(
        unit.isEmpty() ? QStringLiteral("beat") : unit,
        jsonNumber(args.value(QStringLiteral("min_strength")), 0.0));
    if (times.isEmpty())
        return err("not_found", QStringLiteral("No beat analysis yet — call detect_beats first"));

    const double minGap = qMax(0.0, jsonNumber(args.value(QStringLiteral("min_gap")), 0.1));
    const QVariantMap before = m_controller->mcpCompactClip(ref.track, ref.clip);
    const double start = before.value(QStringLiteral("start")).toDouble();
    const double end = start + before.value(QStringLiteral("duration")).toDouble();

    QList<double> cuts;
    for (double t : times) {
        if (t - start < minGap || end - t < minGap)
            continue;
        if (!cuts.isEmpty() && t - cuts.last() < minGap)
            continue;
        cuts.append(t);
    }
    if (cuts.isEmpty())
        return err("bad_args", QStringLiteral("No grid times fall inside that clip"));

    m_controller->mcpBeginBatch();
    QJsonArray at;
    QStringList newIds;
    for (int i = cuts.size() - 1; i >= 0; --i) {
        const ClipRef here = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
        if (!here.valid())
            break;
        const QSet<QString> idsBefore = clipIdSet(m_controller);
        m_controller->splitClipAt(here.track, here.clip, cuts.at(i));
        const QString minted = findNewClipId(idsBefore, clipIdSet(m_controller));
        if (minted.isEmpty())
            continue; // refused (locked track, or the time landed on an edge after rounding)
        newIds.prepend(minted);
        at.prepend(cuts.at(i));
    }
    m_controller->mcpEndBatch(QStringLiteral("Split on beats"), !newIds.isEmpty());

    if (newIds.isEmpty())
        return err("apply_failed", QStringLiteral("No cuts were made"));

    QJsonArray clips;
    clips.append(ref.id); // the original id now names the first piece
    for (const QString &id : std::as_const(newIds))
        clips.append(id);
    return ok({{QStringLiteral("clips"), clips},
               {QStringLiteral("at"), at},
               {QStringLiteral("n"), clips.size()}});
}

QJsonObject McpDispatcher::opSplitOnScenes(const QJsonObject &args)
{
    const ClipRef ref = resolveClip(args);
    if (!ref.valid())
        return clipRefError(args);

    // Read the boundaries before touching anything. Unlike beats the analysis survives edits,
    // but the times are relative to the clip being cut, and each cut renumbers what follows.
    const QList<double> times =
        m_controller->mcpSceneCutTimes(jsonNumber(args.value(QStringLiteral("min_score")), 0.0),
                                       argString(args, QStringLiteral("label")));
    if (times.isEmpty()) {
        return err("not_found",
                   QStringLiteral("No scene analysis for that clip — call detect_scenes first"));
    }

    const double minGap = qMax(0.0, jsonNumber(args.value(QStringLiteral("min_gap")), 0.1));
    const QVariantMap before = m_controller->mcpCompactClip(ref.track, ref.clip);
    const double start = before.value(QStringLiteral("start")).toDouble();
    const double end = start + before.value(QStringLiteral("duration")).toDouble();

    QList<double> cuts;
    for (double t : times) {
        if (t - start < minGap || end - t < minGap)
            continue;
        if (!cuts.isEmpty() && t - cuts.last() < minGap)
            continue;
        cuts.append(t);
    }
    if (cuts.isEmpty())
        return err("bad_args", QStringLiteral("No scene boundaries fall inside that clip"));

    m_controller->mcpBeginBatch();
    QJsonArray at;
    QStringList newIds;
    // Back to front, so the id the caller passed keeps naming a real clip throughout and ends
    // up on the first piece.
    for (int i = cuts.size() - 1; i >= 0; --i) {
        const ClipRef here = resolveClip(QJsonObject{{QStringLiteral("clip"), ref.id}});
        if (!here.valid())
            break;
        const QSet<QString> idsBefore = clipIdSet(m_controller);
        m_controller->splitClipAt(here.track, here.clip, cuts.at(i));
        const QString minted = findNewClipId(idsBefore, clipIdSet(m_controller));
        if (minted.isEmpty())
            continue; // refused (locked track, or the time landed on an edge after rounding)
        newIds.prepend(minted);
        at.prepend(cuts.at(i));
    }
    m_controller->mcpEndBatch(QStringLiteral("Split on scenes"), !newIds.isEmpty());

    if (newIds.isEmpty())
        return err("apply_failed", QStringLiteral("No cuts were made"));

    QJsonArray clips;
    clips.append(ref.id); // the original id now names the first piece
    for (const QString &id : std::as_const(newIds))
        clips.append(id);
    return ok({{QStringLiteral("clips"), clips},
               {QStringLiteral("at"), at},
               {QStringLiteral("n"), clips.size()}});
}

QJsonObject McpDispatcher::opSnapClipsToBeats(const QJsonObject &args)
{
    const QString unit = argString(args, QStringLiteral("unit"));
    const QList<double> times = m_controller->mcpBeatTimes(
        unit.isEmpty() ? QStringLiteral("beat") : unit,
        jsonNumber(args.value(QStringLiteral("min_strength")), 0.0));
    if (times.isEmpty())
        return err("not_found", QStringLiteral("No beat analysis yet — call detect_beats first"));

    // Resolve to UUIDs up front: the first move renumbers everything after it on the track.
    QStringList targets;
    const QJsonArray requested = args.value(QStringLiteral("clips")).toArray();
    if (!requested.isEmpty()) {
        for (const QJsonValue &v : requested) {
            const QString id = v.toString().trimmed();
            if (!id.isEmpty())
                targets.append(id);
        }
    } else if (args.contains(QStringLiteral("track"))) {
        const int t = jsonInt(args.value(QStringLiteral("track")));
        if (t < 0 || t >= m_controller->tracks().size())
            return err("not_found", QStringLiteral("No such track"));
        const QVariantList clips = m_controller->tracks().at(t).toMap()
                                       .value(QStringLiteral("clips")).toList();
        for (int c = 0; c < clips.size(); ++c)
            targets.append(m_controller->mcpClipId(t, c));
    } else {
        return err("bad_args", QStringLiteral("clips or track required"));
    }
    if (targets.isEmpty())
        return err("not_found", QStringLiteral("No clips to quantise"));

    const double maxDistance =
        qMax(0.0, jsonNumber(args.value(QStringLiteral("max_distance")), 0.25));

    // Ascending start order, so a clip is never shoved by the gap-resolve of a later one.
    struct Target {
        QString id;
        double start = 0.0;
    };
    QList<Target> ordered;
    for (const QString &id : std::as_const(targets)) {
        const ClipRef ref = resolveClip(QJsonObject{{QStringLiteral("clip"), id}});
        if (!ref.valid())
            continue;
        ordered.append({id, m_controller->mcpCompactClip(ref.track, ref.clip)
                                .value(QStringLiteral("start")).toDouble()});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const Target &a, const Target &b) { return a.start < b.start; });

    m_controller->mcpBeginBatch();
    QJsonArray moved;
    QJsonArray skipped;
    for (const Target &target : std::as_const(ordered)) {
        double nearest = times.first();
        for (double t : times) {
            if (qAbs(t - target.start) < qAbs(nearest - target.start))
                nearest = t;
        }
        if (qAbs(nearest - target.start) > maxDistance) {
            skipped.append(QJsonObject{{QStringLiteral("clip"), target.id},
                                       {QStringLiteral("reason"), QStringLiteral("too_far")}});
            continue;
        }

        const ClipRef ref = resolveClip(QJsonObject{{QStringLiteral("clip"), target.id}});
        if (!ref.valid()) {
            skipped.append(QJsonObject{{QStringLiteral("clip"), target.id},
                                       {QStringLiteral("reason"), QStringLiteral("not_found")}});
            continue;
        }
        m_controller->setClipStart(ref.track, ref.clip, nearest);

        // Report where it landed, not where it was aimed: with overlap off, resolveClipStart
        // pushes a clip to the next free gap and the two differ.
        const ClipRef after = resolveClip(QJsonObject{{QStringLiteral("clip"), target.id}});
        const double placed = after.valid()
                                  ? m_controller->mcpCompactClip(after.track, after.clip)
                                        .value(QStringLiteral("start")).toDouble()
                                  : target.start;
        if (qAbs(placed - target.start) < 0.0005) {
            skipped.append(QJsonObject{{QStringLiteral("clip"), target.id},
                                       {QStringLiteral("reason"), QStringLiteral("unchanged")}});
            continue;
        }
        moved.append(QJsonObject{{QStringLiteral("clip"), target.id},
                                 {QStringLiteral("from"), target.start},
                                 {QStringLiteral("to"), placed}});
    }
    m_controller->mcpEndBatch(QStringLiteral("Snap clips to beats"), !moved.isEmpty());

    return ok({{QStringLiteral("moved"), moved},
               {QStringLiteral("skipped"), skipped},
               {QStringLiteral("n"), moved.size()}});
}

QJsonObject McpDispatcher::applyOneExtended(const QString &tool, const QJsonObject &args)
{
    // --- media ---
    if (tool == QLatin1String("remove_asset")) {
        const int index = resolveAsset(args.value(QStringLiteral("asset")));
        if (index < 0)
            return err("not_found", QStringLiteral("Unknown asset"));
        if (!m_controller->removeAsset(index))
            return err("bad_args", QStringLiteral("Remove refused"));
        return ok({{QStringLiteral("removed"), index}});
    }

    if (tool == QLatin1String("replace_asset")) {
        const int index = resolveAsset(args.value(QStringLiteral("asset")));
        const QString path = localPath(argString(args, QStringLiteral("path")));
        if (index < 0)
            return err("not_found", QStringLiteral("Unknown asset"));
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path required"));
        if (!QFileInfo::exists(path))
            return err("not_found", QStringLiteral("File not found"));
        if (!m_controller->replaceAssetSource(index, QUrl::fromLocalFile(path)))
            return err("bad_args", QStringLiteral("Replace refused"));
        return ok({{QStringLiteral("index"), index}, {QStringLiteral("path"), path}});
    }

    if (tool == QLatin1String("export_asset_image")) {
        const int index = resolveAsset(args.value(QStringLiteral("asset")));
        const QString path = localPath(argString(args, QStringLiteral("path")));
        if (index < 0)
            return err("not_found", QStringLiteral("Unknown asset"));
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path required"));
        const QFileInfo info(path);
        if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath()))
            return err("bad_args", QStringLiteral("Could not create parent folder"));
        if (!m_controller->exportAssetImage(index, QUrl::fromLocalFile(path)))
            return err("bad_args", QStringLiteral("Export refused"));
        return ok({{QStringLiteral("index"), index}, {QStringLiteral("path"), path}});
    }

    if (tool == QLatin1String("import_media_bytes")) {
        AssetLibrary *lib = m_controller->assetLibrary();
        if (!lib)
            return err("not_found", QStringLiteral("No media bin"));
        const QByteArray data = QByteArray::fromBase64(args.value(QStringLiteral("data")).toString().toUtf8());
        if (data.isEmpty())
            return err("bad_args", QStringLiteral("data required (base64)"));
        QString writePath = localPath(argString(args, QStringLiteral("path")));
        std::unique_ptr<QTemporaryDir> tempDir;
        if (writePath.isEmpty()) {
            const QString name = argString(args, QStringLiteral("name"));
            if (name.isEmpty())
                return err("bad_args", QStringLiteral("name required when path omitted"));
            tempDir = std::make_unique<QTemporaryDir>();
            if (!tempDir->isValid())
                return err("bad_args", QStringLiteral("Could not create temp directory"));
            writePath = tempDir->filePath(name);
        } else {
            const QFileInfo info(writePath);
            if (!info.absoluteDir().exists() && !QDir().mkpath(info.absolutePath()))
                return err("bad_args", QStringLiteral("Could not create parent folder"));
        }
        QFile file(writePath);
        if (!file.open(QIODevice::WriteOnly))
            return err("bad_args", QStringLiteral("Could not write file"));
        if (file.write(data) != data.size())
            return err("bad_args", QStringLiteral("Write incomplete"));
        file.close();
        const QStringList ids = lib->importLocalPaths({writePath});
        if (ids.isEmpty())
            return err("import_failed", QStringLiteral("Import refused"));
        return waitImport(ids);
    }

    // --- project ---
    if (tool == QLatin1String("load_project")) {
        const QString path = localPath(argString(args, QStringLiteral("path")));
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path required"));
        m_controller->loadProject(QUrl::fromLocalFile(path));
        return ok({{QStringLiteral("path"), path}});
    }

    if (tool == QLatin1String("new_project")) {
        m_controller->newProject();
        return ok({});
    }

    if (tool == QLatin1String("package_project")) {
        const QString path = localPath(argString(args, QStringLiteral("path")));
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path required"));
        m_controller->packageProject(QUrl::fromLocalFile(path));
        return ok({{QStringLiteral("started"), true}, {QStringLiteral("path"), path}});
    }

    if (tool == QLatin1String("cancel_package")) {
        m_controller->cancelPackage();
        return ok({{QStringLiteral("cancelled"), true}});
    }

    if (tool == QLatin1String("cancel_export")) {
        m_controller->cancelExport();
        return ok({{QStringLiteral("cancelled"), true}});
    }

    if (tool == QLatin1String("apply_canvas_crop")) {
        const double x = jsonNumber(args.value(QStringLiteral("x")), 0);
        const double y = jsonNumber(args.value(QStringLiteral("y")), 0);
        const double w = jsonNumber(args.value(QStringLiteral("width")), 0);
        const double h = jsonNumber(args.value(QStringLiteral("height")), 0);
        if (w <= 0 || h <= 0)
            return err("bad_args", QStringLiteral("width and height must be > 0"));
        m_controller->applyCanvasCrop(x, y, w, h);
        return ok({{QStringLiteral("x"), x},
                   {QStringLiteral("y"), y},
                   {QStringLiteral("width"), w},
                   {QStringLiteral("height"), h}});
    }

    // --- timeline ---
    if (tool == QLatin1String("move_track")) {
        const int from = jsonInt(args.value(QStringLiteral("from")));
        const int to = jsonInt(args.value(QStringLiteral("to")));
        if (from < 0 || to < 0)
            return err("bad_args", QStringLiteral("from and to required"));
        m_controller->moveTrack(from, to);
        return ok({{QStringLiteral("from"), from}, {QStringLiteral("to"), to}});
    }

    if (tool == QLatin1String("set_track_waveform")) {
        const int track = jsonInt(args.value(QStringLiteral("track")));
        if (track < 0 || track >= m_controller->tracks().size())
            return err("not_found", QStringLiteral("No such track"));
        if (!args.contains(QStringLiteral("show")))
            return err("bad_args", QStringLiteral("show required"));
        const bool show = jsonBool(args.value(QStringLiteral("show")));
        m_controller->setTrackShowWaveform(track, show);
        return ok({{QStringLiteral("track"), track}, {QStringLiteral("show"), show}});
    }

    if (tool == QLatin1String("set_track_height")) {
        const int track = jsonInt(args.value(QStringLiteral("track")));
        if (track < 0 || track >= m_controller->tracks().size())
            return err("not_found", QStringLiteral("No such track"));
        if (!args.contains(QStringLiteral("scale")))
            return err("bad_args", QStringLiteral("scale required"));
        const double scale = jsonNumber(args.value(QStringLiteral("scale")), 1.0);
        m_controller->setTrackHeightScale(track, scale);
        return ok({{QStringLiteral("track"), track}, {QStringLiteral("scale"), m_controller->trackHeightScale(track)}});
    }

    if (tool == QLatin1String("select_clip")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->selectClip(ref.track, ref.clip);
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("clear_selection")) {
        m_controller->clearSelection();
        return ok({});
    }

    if (tool == QLatin1String("select_all_clips")) {
        m_controller->selectAllClips();
        return ok({});
    }

    if (tool == QLatin1String("copy_selection")) {
        m_controller->copySelection();
        return ok({});
    }

    if (tool == QLatin1String("cut_selection")) {
        m_controller->cutSelection();
        return ok({});
    }

    if (tool == QLatin1String("paste_at_playhead")) {
        const QSet<QString> before = clipIdSet(m_controller);
        m_controller->pasteAtPlayhead();
        return replyMintedClips(m_controller, before);
    }

    if (tool == QLatin1String("separate_audio")) {
        if (!m_controller->canSeparateAudioSelection())
            return err("bad_args", QStringLiteral("No separable clip selected"));
        m_controller->separateAudioFromSelection();
        return ok({});
    }

    if (tool == QLatin1String("unlink_audio")) {
        if (!m_controller->canUnlinkSelection())
            return err("bad_args", QStringLiteral("No linked clips selected"));
        m_controller->unlinkSelectedClips();
        return ok({});
    }

    if (tool == QLatin1String("merge_clips")) {
        if (!m_controller->canMergeSelection())
            return err("bad_args", QStringLiteral("Cannot merge selection"));
        m_controller->mergeSelectedClips();
        return ok({});
    }

    if (tool == QLatin1String("align_clip_left")) {
        m_controller->alignSelectedClipLeft();
        return ok({});
    }

    if (tool == QLatin1String("align_clip_right")) {
        m_controller->alignSelectedClipRight();
        return ok({});
    }

    if (tool == QLatin1String("set_clip_name")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString name = args.value(QStringLiteral("name")).toString();
        if (name.trimmed().isEmpty())
            return err("bad_args", QStringLiteral("name required"));
        m_controller->setClipName(ref.track, ref.clip, name);
        return ok(clipFeedback(ref, {{QStringLiteral("name"), name}}));
    }

    if (tool == QLatin1String("add_bookmark")) {
        if (!args.contains(QStringLiteral("at")))
            return err("bad_args", QStringLiteral("at required"));
        const double at = jsonNumber(args.value(QStringLiteral("at")), 0);
        const QString label = args.value(QStringLiteral("label")).toString();
        m_controller->addBookmark(at, label);
        return ok({{QStringLiteral("at"), at}, {QStringLiteral("label"), label}});
    }

    if (tool == QLatin1String("remove_bookmark")) {
        const int index = jsonInt(args.value(QStringLiteral("index")));
        if (index < 0)
            return err("bad_args", QStringLiteral("index required"));
        m_controller->removeBookmark(index);
        return ok({{QStringLiteral("removed"), index}});
    }

    if (tool == QLatin1String("update_bookmark")) {
        const int index = jsonInt(args.value(QStringLiteral("index")));
        if (index < 0)
            return err("bad_args", QStringLiteral("index required"));
        const double at = args.contains(QStringLiteral("at"))
                              ? jsonNumber(args.value(QStringLiteral("at")), 0)
                              : m_controller->bookmarks().at(index).toMap().value(QStringLiteral("at")).toDouble();
        const QString label = args.contains(QStringLiteral("label"))
                                  ? args.value(QStringLiteral("label")).toString()
                                  : m_controller->bookmarks().at(index).toMap().value(QStringLiteral("label")).toString();
        m_controller->updateBookmark(index, at, label);
        return ok({{QStringLiteral("index"), index}, {QStringLiteral("at"), at}, {QStringLiteral("label"), label}});
    }

    if (tool == QLatin1String("go_to_bookmark")) {
        const int index = jsonInt(args.value(QStringLiteral("index")));
        if (index < 0)
            return err("bad_args", QStringLiteral("index required"));
        m_controller->goToBookmark(index);
        return ok({{QStringLiteral("index"), index}});
    }

    if (tool == QLatin1String("freeze_frame")) {
        const QSet<QString> before = clipIdSet(m_controller);
        m_controller->freezeFrameAtPlayhead();
        return replyMintedClips(m_controller, before);
    }

    // --- canvas ---
    if (tool == QLatin1String("set_flip")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (!args.contains(QStringLiteral("flipH")) && !args.contains(QStringLiteral("flipV")))
            return err("bad_args", QStringLiteral("flipH or flipV required"));
        const bool flipH = args.contains(QStringLiteral("flipH")) ? jsonBool(args.value(QStringLiteral("flipH")))
                                                                  : m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("flipH")).toBool();
        const bool flipV = args.contains(QStringLiteral("flipV")) ? jsonBool(args.value(QStringLiteral("flipV")))
                                                                  : m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("flipV")).toBool();
        m_controller->setClipFlip(ref.track, ref.clip, flipH, flipV);
        return ok(clipFeedback(ref, {{QStringLiteral("flipH"), flipH}, {QStringLiteral("flipV"), flipV}}));
    }

    if (tool == QLatin1String("set_blend_mode")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString mode = args.value(QStringLiteral("mode")).toString().trimmed();
        if (mode.isEmpty())
            return err("bad_args", QStringLiteral("mode required"));
        m_controller->setClipBlendMode(ref.track, ref.clip, mode);
        return ok(clipFeedback(ref, {{QStringLiteral("mode"), mode}}));
    }

    if (tool == QLatin1String("set_mask")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->setClipMask(ref.track, ref.clip, args.value(QStringLiteral("mask")).toObject().toVariantMap());
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("set_fade")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QVariantMap clip = m_controller->clipAt(ref.track, ref.clip);
        const double fadeIn = args.contains(QStringLiteral("in")) ? jsonNumber(args.value(QStringLiteral("in")), 0)
                                                                  : clip.value(QStringLiteral("fadeIn")).toDouble();
        const double fadeOut = args.contains(QStringLiteral("out")) ? jsonNumber(args.value(QStringLiteral("out")), 0)
                                                                    : clip.value(QStringLiteral("fadeOut")).toDouble();
        m_controller->setClipFade(ref.track, ref.clip, fadeIn, fadeOut);
        return ok(clipFeedback(ref, {{QStringLiteral("in"), fadeIn}, {QStringLiteral("out"), fadeOut}}));
    }

    if (tool == QLatin1String("set_fade_curve")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (args.contains(QStringLiteral("points"))) {
            const QJsonArray pointArray = args.value(QStringLiteral("points")).toArray();
            if (pointArray.size() < 2)
                return err("bad_args", QStringLiteral("points needs at least two entries"));
            if (m_controller->fadeCurveSessionActive())
                m_controller->endFadeCurveSession();
            m_controller->beginFadeCurveSession(ref.track, ref.clip);
            if (!m_controller->fadeCurveSessionActive())
                return err("bad_args", QStringLiteral("Clip cannot use a custom fade curve"));
            m_controller->setFadeCurvePoints(fadeCurvePointsFromJson(pointArray));
            m_controller->applyFadeCurve();
            m_controller->endFadeCurveSession();
            return ok(clipFeedback(ref));
        }
        const QString curve = args.value(QStringLiteral("curve")).toString().trimmed();
        if (curve.isEmpty())
            return err("bad_args", QStringLiteral("curve or points required"));
        m_controller->setClipFadeCurve(ref.track, ref.clip, curve);
        return ok(clipFeedback(ref, {{QStringLiteral("curve"), curve}}));
    }

    if (tool == QLatin1String("set_clip_speed")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (!args.contains(QStringLiteral("speed")))
            return err("bad_args", QStringLiteral("speed required"));
        const double speed = jsonNumber(args.value(QStringLiteral("speed")), 1.0);
        m_controller->setClipSpeed(ref.track, ref.clip, speed);
        return ok(clipFeedback(ref, {{QStringLiteral("speed"), speed}}));
    }

    if (tool == QLatin1String("set_clip_reverse")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (!args.contains(QStringLiteral("reverse")))
            return err("bad_args", QStringLiteral("reverse required"));
        const bool reverse = jsonBool(args.value(QStringLiteral("reverse")));
        m_controller->setClipReverse(ref.track, ref.clip, reverse);
        return ok(clipFeedback(ref, {{QStringLiteral("reverse"), reverse}}));
    }

    if (tool == QLatin1String("cancel_reverse_render")) {
        m_controller->cancelReverseRender();
        return ok({{QStringLiteral("cancelled"), true}});
    }

    if (tool == QLatin1String("set_animation")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString which = args.value(QStringLiteral("which")).toString().trimmed();
        if (which != QLatin1String("animIn") && which != QLatin1String("animOut"))
            return err("bad_args", QStringLiteral("which must be animIn or animOut"));
        QVariantMap patch;
        if (args.contains(QStringLiteral("kind")))
            patch.insert(QStringLiteral("kind"), args.value(QStringLiteral("kind")).toString());
        if (args.contains(QStringLiteral("duration")))
            patch.insert(QStringLiteral("duration"), jsonNumber(args.value(QStringLiteral("duration")), 0));
        if (args.contains(QStringLiteral("ease")))
            patch.insert(QStringLiteral("ease"), args.value(QStringLiteral("ease")).toString());
        if (args.contains(QStringLiteral("curve")))
            patch.insert(QStringLiteral("curve"), args.value(QStringLiteral("curve")).toString());
        if (patch.isEmpty())
            return err("bad_args", QStringLiteral("At least one animation field required"));
        m_controller->setClipAnimation(ref.track, ref.clip, which, patch);
        return ok(clipFeedback(ref, {{QStringLiteral("which"), which}}));
    }

    // The *_text_layer and *_shape_layer spellings are one set of tools: the shading stack is
    // the same on both clip kinds, and the controller dispatches on the clip.
    if (tool == QLatin1String("add_text_layer") || tool == QLatin1String("add_shape_layer")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const int at = args.contains(QStringLiteral("at")) ? int(jsonNumber(args.value(QStringLiteral("at")), -1)) : -1;
        const QString id = m_controller->addStyleLayer(ref.track, ref.clip, args.value(QStringLiteral("kind")).toString(), at);
        if (id.isEmpty())
            return err("bad_args", QStringLiteral("not a text or shape clip"));
        // `id` in the feedback is the clip's; the new layer's goes under its own key.
        return ok(clipFeedback(ref, {{QStringLiteral("layerId"), id}}));
    }

    if (tool == QLatin1String("set_text_layer") || tool == QLatin1String("set_shape_layer")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        QVariantMap layer = args.value(QStringLiteral("layer")).toObject().toVariantMap();
        if (args.contains(QStringLiteral("id")))
            layer.insert(QStringLiteral("id"), args.value(QStringLiteral("id")).toString());
        else if (args.contains(QStringLiteral("index")))
            layer.insert(QStringLiteral("index"), int(jsonNumber(args.value(QStringLiteral("index")), 0)));
        else
            return err("bad_args", QStringLiteral("id or index required"));
        const QString id = layer.value(QStringLiteral("id")).toString();
        if (!id.isEmpty()) {
            layer.remove(QStringLiteral("id"));
            m_controller->setStyleLayer(ref.track, ref.clip, id, layer);
        } else if (m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("kind")).toString() == QLatin1String("shape")) {
            m_controller->setShapeStyle(ref.track, ref.clip, QVariantMap{{QStringLiteral("layer"), layer}});
        } else {
            m_controller->setTextStyle(ref.track, ref.clip, QVariantMap{{QStringLiteral("layer"), layer}});
        }
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("remove_text_layer") || tool == QLatin1String("remove_shape_layer")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (!m_controller->removeStyleLayer(ref.track, ref.clip, args.value(QStringLiteral("id")).toString()))
            return err("bad_args", QStringLiteral("no such layer"));
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("move_text_layer") || tool == QLatin1String("move_shape_layer")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->moveStyleLayer(ref.track, ref.clip, args.value(QStringLiteral("id")).toString(),
                                     int(jsonNumber(args.value(QStringLiteral("to")), 0)));
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("list_text_looks")) {
        return ok({{QStringLiteral("looks"), QJsonArray::fromVariantList(m_controller->textLooks())}});
    }

    if (tool == QLatin1String("apply_text_look")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString look = args.value(QStringLiteral("look")).toString();
        if (!drift::textLookForId(look))
            return err("bad_args", QStringLiteral("unknown look '%1'").arg(look));
        m_controller->applyTextLook(ref.track, ref.clip, look, args.value(QStringLiteral("params")).toObject().toVariantMap());
        return ok(clipFeedback(ref, {{QStringLiteral("look"), look}}));
    }

    if (tool == QLatin1String("list_text_animations")) {
        const QString q = args.value(QStringLiteral("q")).toString().trimmed().toLower();
        QJsonArray presets;
        for (const QVariant &v : m_controller->textAnimationPresets(args.value(QStringLiteral("which")).toString())) {
            const QVariantMap m = v.toMap();
            if (!q.isEmpty() && !m.value(QStringLiteral("id")).toString().contains(q)
                && !m.value(QStringLiteral("label")).toString().toLower().contains(q))
                continue;
            presets.append(QJsonObject::fromVariantMap(m));
        }
        return ok({{QStringLiteral("presets"), presets}, {QStringLiteral("n"), presets.size()}});
    }

    if (tool == QLatin1String("set_text_animation")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString which = args.value(QStringLiteral("which")).toString().trimmed();
        if (which != QLatin1String("in") && which != QLatin1String("out") && which != QLatin1String("loop"))
            return err("bad_args", QStringLiteral("which must be in, out or loop"));
        QVariantMap patch = args.toVariantMap();
        patch.remove(QStringLiteral("which"));
        patch.remove(QStringLiteral("clip"));
        patch.remove(QStringLiteral("track"));
        patch.remove(QStringLiteral("index"));
        if (patch.contains(QStringLiteral("preset"))) {
            const QString preset = patch.value(QStringLiteral("preset")).toString();
            if (!preset.isEmpty() && preset != QLatin1String("none")
                && !drift::TextAnimationPresetCatalog::instance().presetForId(preset))
                return err("bad_args", QStringLiteral("unknown preset '%1'; see list_text_animations").arg(preset));
        }
        m_controller->setTextAnimationSlot(ref.track, ref.clip, which, patch);
        return ok(clipFeedback(ref, {{QStringLiteral("which"), which}}));
    }

    if (tool == QLatin1String("clear_text_animation")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->clearTextAnimationSlot(ref.track, ref.clip, args.value(QStringLiteral("which")).toString());
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("import_text_animation")) {
        const QString path = args.value(QStringLiteral("path")).toString();
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path required"));
        const QVariantMap result = m_controller->importTextAnimationPreset(QUrl::fromLocalFile(path),
                                                                           args.value(QStringLiteral("which")).toString());
        if (!result.value(QStringLiteral("ok")).toBool())
            return err("bad_args", result.value(QStringLiteral("error")).toString());
        return ok({{QStringLiteral("id"), result.value(QStringLiteral("id")).toString()},
                   {QStringLiteral("unsupported"), QJsonArray::fromStringList(result.value(QStringLiteral("unsupported")).toStringList())}});
    }

    if (tool == QLatin1String("apply_text_style_to_all")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const int changed = m_controller->applyTextStyleToCaptions(ref.track, ref.clip,
                                                                   args.value(QStringLiteral("scope")).toString(QStringLiteral("track")));
        return ok(clipFeedback(ref, {{QStringLiteral("changed"), changed}}));
    }

    if (tool == QLatin1String("set_shape_style")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->setShapeStyle(ref.track, ref.clip, args.value(QStringLiteral("style")).toObject().toVariantMap());
        return ok(clipFeedback(ref));
    }

    // --- motion (Lottie / SVG vector clips) ---
    if (tool == QLatin1String("add_lottie") || tool == QLatin1String("add_svg")) {
        const bool svg = tool == QLatin1String("add_svg");
        const QString source = argString(args, svg ? QStringLiteral("svg") : QStringLiteral("json"));
        if (source.isEmpty())
            return err("bad_args", svg ? QStringLiteral("svg required") : QStringLiteral("json required"));
        if (!m_controller->vectorSupportAvailable())
            return err("unsupported", QStringLiteral("this build has no vector renderer"));
        const double at = args.contains(QStringLiteral("at"))
                              ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                              : m_controller->playheadSeconds();
        QVariantMap opts;
        opts.insert(QStringLiteral("kind"), svg ? QStringLiteral("svg") : QStringLiteral("lottie"));
        for (const char *key : {"duration", "fit", "loop", "offset", "name", "slots"}) {
            if (args.contains(QLatin1String(key)))
                opts.insert(QString::fromUtf8(key), args.value(QLatin1String(key)).toVariant());
        }
        const QVariantMap reply = m_controller->addVectorClip(
            source, jsonInt(args.value(QStringLiteral("track"))), at, opts);
        if (!reply.value(QStringLiteral("ok")).toBool())
            return err("bad_args", reply.value(QStringLiteral("error")).toString());
        QJsonObject out = QJsonObject::fromVariantMap(reply);
        out.insert(QStringLiteral("n"), 1);
        return out;
    }

    if (tool == QLatin1String("inspect_lottie")) {
        if (args.contains(QStringLiteral("clip")) || args.contains(QStringLiteral("track"))
            || args.contains(QStringLiteral("index"))) {
            const ClipRef ref = resolveClip(args);
            if (!ref.valid())
                return clipRefError(args);
            const QVariantMap report = m_controller->inspectVectorClip(ref.track, ref.clip);
            if (!report.value(QStringLiteral("ok")).toBool())
                return err("bad_args", report.value(QStringLiteral("error")).toString());
            return QJsonObject::fromVariantMap(report);
        }
        QString source = argString(args, QStringLiteral("json"));
        QString kind = QStringLiteral("lottie");
        if (source.isEmpty()) {
            source = argString(args, QStringLiteral("svg"));
            kind = QStringLiteral("svg");
        }
        if (source.isEmpty()) {
            source = argString(args, QStringLiteral("path"));
            kind.clear();
        }
        if (source.isEmpty())
            return err("bad_args", QStringLiteral("json, svg, path or clip required"));
        const QVariantMap report = m_controller->inspectVector(source, kind);
        if (!report.value(QStringLiteral("ok")).toBool())
            return err("bad_args", report.value(QStringLiteral("error")).toString());
        return QJsonObject::fromVariantMap(report);
    }

    if (tool == QLatin1String("set_lottie_source")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        QString source = argString(args, QStringLiteral("json"));
        QString kind = QStringLiteral("lottie");
        if (source.isEmpty()) {
            source = argString(args, QStringLiteral("svg"));
            kind = QStringLiteral("svg");
        }
        if (source.isEmpty()) {
            source = argString(args, QStringLiteral("path"));
            kind.clear();
        }
        if (source.isEmpty())
            return err("bad_args", QStringLiteral("json, svg or path required"));
        const QVariantMap reply = m_controller->setVectorSource(ref.track, ref.clip, source,
                                                                {{QStringLiteral("kind"), kind}});
        if (!reply.value(QStringLiteral("ok")).toBool())
            return err("bad_args", reply.value(QStringLiteral("error")).toString());
        QJsonObject out = QJsonObject::fromVariantMap(reply);
        out.insert(QStringLiteral("track"), ref.track);
        out.insert(QStringLiteral("index"), ref.clip);
        return out;
    }

    if (tool == QLatin1String("set_lottie_options")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        QVariantMap opts;
        for (const char *key : {"fit", "loop", "offset", "name"}) {
            if (args.contains(QLatin1String(key)))
                opts.insert(QString::fromUtf8(key), args.value(QLatin1String(key)).toVariant());
        }
        const QString error = m_controller->setVectorOptions(ref.track, ref.clip, opts);
        if (!error.isEmpty())
            return err("bad_args", error);
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("set_lottie_slot")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString name = argString(args, QStringLiteral("name"));
        if (name.isEmpty())
            return err("bad_args", QStringLiteral("name required"));
        const QJsonValue value = args.value(QStringLiteral("value"));
        const QString error = m_controller->setVectorSlot(
            ref.track, ref.clip, name, value.isUndefined() || value.isNull() ? QVariant() : value.toVariant());
        if (!error.isEmpty())
            return err("bad_args", error);
        return ok(clipFeedback(ref, {{QStringLiteral("name"), name},
                                     {QStringLiteral("cleared"), value.isUndefined() || value.isNull()}}));
    }

    if (tool == QLatin1String("list_lottie_slots")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (m_controller->clipAt(ref.track, ref.clip).value(QStringLiteral("kind")).toString() != QLatin1String("vector"))
            return err("bad_args", QStringLiteral("not a vector clip"));
        const QVariantList rows = m_controller->vectorSlots(ref.track, ref.clip);
        return ok({{QStringLiteral("slots"), QJsonArray::fromVariantList(rows)},
                   {QStringLiteral("n"), rows.size()}});
    }

    if (tool == QLatin1String("get_lottie_source")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QVariantMap clip = m_controller->clipAt(ref.track, ref.clip);
        if (clip.value(QStringLiteral("kind")).toString() != QLatin1String("vector"))
            return err("bad_args", QStringLiteral("not a vector clip"));
        const QVariantMap vector = clip.value(QStringLiteral("vector")).toMap();
        return ok({{QStringLiteral("kind"), vector.value(QStringLiteral("kind")).toString()},
                   {QStringLiteral("inline"), vector.value(QStringLiteral("inline")).toBool()},
                   {QStringLiteral("path"), vector.value(QStringLiteral("path")).toString()},
                   {QStringLiteral("hash"), vector.value(QStringLiteral("hash")).toString()},
                   {QStringLiteral("source"), m_controller->vectorSourceText(ref.track, ref.clip)}});
    }

    // --- shapes ---
    if (tool == QLatin1String("list_shapes"))
        return filterCatalog(m_controller->builtinShapes(), argString(args, QStringLiteral("q")), 0, "shapes");

    if (tool == QLatin1String("list_stickers"))
        return filterCatalog(m_controller->builtinStickers(), argString(args, QStringLiteral("q")), 0, "stickers");

    if (tool == QLatin1String("list_emoji")) {
        const QString q = argString(args, QStringLiteral("q"));
        const QString group = argString(args, QStringLiteral("group"));
        if (q.isEmpty() && group.isEmpty())
            return err("bad_args", QStringLiteral("q or group required; groups: %1")
                                       .arg(m_controller->emojiGroups().join(QStringLiteral(", "))));
        QVariantList pool;
        for (const QVariant &v : m_controller->emojiCatalog()) {
            if (group.isEmpty() || v.toMap().value(QStringLiteral("group")).toString().compare(group, Qt::CaseInsensitive) == 0)
                pool.append(v);
        }
        QJsonObject out = filterCatalog(pool, q, qBound(1, jsonInt(args.value(QStringLiteral("limit")), 50), 2000),
                                        "emoji", compactEmojiItem);
        out.insert(QStringLiteral("groups"), QJsonArray::fromStringList(m_controller->emojiGroups()));
        return out;
    }

    if (tool == QLatin1String("list_text_presets"))
        return filterCatalog(m_controller->textPresets(), argString(args, QStringLiteral("q")), 0, "presets");

    if (tool == QLatin1String("list_fonts"))
        return filterCatalog(m_controller->fontCatalog(), argString(args, QStringLiteral("q")),
                             qBound(1, jsonInt(args.value(QStringLiteral("limit")), 100), 2000), "fonts", compactFontItem);

    if (tool == QLatin1String("add_shape")) {
        const QString shapeId = argString(args, QStringLiteral("shape"));
        if (shapeId.isEmpty())
            return err("bad_args", QStringLiteral("shape required"));
        const double at = args.contains(QStringLiteral("at"))
                              ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                              : m_controller->playheadSeconds();
        const QSet<QString> before = clipIdSet(m_controller);
        if (args.contains(QStringLiteral("track")))
            m_controller->addShapeClipAt(shapeId, jsonInt(args.value(QStringLiteral("track"))), at);
        else
            m_controller->addShapeClip(shapeId, at);
        return replyMintedClips(m_controller, before);
    }

    if (tool == QLatin1String("add_sticker")) {
        const QString stickerId = argString(args, QStringLiteral("sticker"));
        if (stickerId.isEmpty())
            return err("bad_args", QStringLiteral("sticker required"));
        const double at = args.contains(QStringLiteral("at"))
                              ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                              : m_controller->playheadSeconds();
        const QSet<QString> before = clipIdSet(m_controller);
        m_controller->addStickerClip(stickerId, at);
        return replyMintedClips(m_controller, before);
    }

    if (tool == QLatin1String("add_emoji")) {
        const QString emoji = argString(args, QStringLiteral("emoji"));
        if (emoji.isEmpty())
            return err("bad_args", QStringLiteral("emoji required"));
        const QString name = argString(args, QStringLiteral("name"));
        const double at = args.contains(QStringLiteral("at"))
                              ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                              : m_controller->playheadSeconds();
        const QSet<QString> before = clipIdSet(m_controller);
        m_controller->addEmojiClip(emoji, name, at);
        return replyMintedClips(m_controller, before);
    }

    // --- subtitles ---
    if (tool == QLatin1String("add_subtitle_clip")) {
        const double at = args.contains(QStringLiteral("at"))
                              ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                              : m_controller->playheadSeconds();
        const QSet<QString> before = clipIdSet(m_controller);
        m_controller->addSubtitleClip(at);
        return replyMintedClips(m_controller, before);
    }

    if (tool == QLatin1String("import_subtitle_file")) {
        const QString path = localPath(argString(args, QStringLiteral("path")));
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path required"));
        const double at = args.contains(QStringLiteral("at"))
                              ? jsonNumber(args.value(QStringLiteral("at")), -1.0)
                              : -1.0;
        const QSet<QString> before = clipIdSet(m_controller);
        if (!m_controller->importSubtitleFile(QUrl::fromLocalFile(path), at))
            return err("bad_args", QStringLiteral("Import refused"));
        return replyMintedClips(m_controller, before);
    }

    if (tool == QLatin1String("import_subtitle_into_clip")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString path = localPath(argString(args, QStringLiteral("path")));
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path required"));
        if (!m_controller->importSubtitleFileIntoClip(ref.track, ref.clip, QUrl::fromLocalFile(path)))
            return err("bad_args", QStringLiteral("Import refused"));
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("export_subtitle_file")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString path = localPath(argString(args, QStringLiteral("path")));
        if (path.isEmpty())
            return err("bad_args", QStringLiteral("path required"));
        if (!m_controller->exportSubtitleFile(ref.track, ref.clip, QUrl::fromLocalFile(path)))
            return err("bad_args", QStringLiteral("Export refused"));
        return ok(clipFeedback(ref, {{QStringLiteral("path"), path}}));
    }

    if (tool == QLatin1String("set_subtitle_cues")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QJsonArray cueArray = args.value(QStringLiteral("cues")).toArray();
        if (cueArray.isEmpty())
            return err("bad_args", QStringLiteral("cues required"));
        m_controller->setSubtitleCues(ref.track, ref.clip, subtitleCuesFromJson(cueArray));
        return ok(clipFeedback(ref, {{QStringLiteral("n"), cueArray.size()}}));
    }

    if (tool == QLatin1String("upsert_subtitle_cue")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString text = args.value(QStringLiteral("text")).toString();
        if (text.trimmed().isEmpty())
            return err("bad_args", QStringLiteral("text required"));
        m_controller->upsertSubtitleCueAtPlayhead(ref.track, ref.clip, text);
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("list_whisper_languages"))
        return ok({{QStringLiteral("languages"), compactCatalogList(m_controller->whisperLanguages())}});

    if (tool == QLatin1String("generate_subtitles")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString language = args.value(QStringLiteral("language")).toString();
        const int maxWords =
            static_cast<int>(jsonNumber(args.value(QStringLiteral("max_words_per_cue")), 0.0));
        m_controller->generateSubtitlesForClip(ref.track, ref.clip, language, maxWords);
        return ok(clipFeedback(ref, {{QStringLiteral("started"), true}}));
    }

    if (tool == QLatin1String("cancel_subtitle_generation")) {
        m_controller->cancelSubtitleGeneration();
        return ok({{QStringLiteral("cancelled"), true}});
    }

    // --- effects ---
    if (tool == QLatin1String("set_effect_enabled")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const int index = jsonInt(args.value(QStringLiteral("index")));
        if (index < 0 || !args.contains(QStringLiteral("enabled")))
            return err("bad_args", QStringLiteral("index and enabled required"));
        const bool enabled = jsonBool(args.value(QStringLiteral("enabled")));
        m_controller->setEffectEnabled(ref.track, ref.clip, index, enabled);
        return ok(clipFeedback(ref, {{QStringLiteral("index"), index}, {QStringLiteral("enabled"), enabled}}));
    }

    if (tool == QLatin1String("move_effect")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const int from = jsonInt(args.value(QStringLiteral("from")));
        const int to = jsonInt(args.value(QStringLiteral("to")));
        if (from < 0 || to < 0)
            return err("bad_args", QStringLiteral("from and to required"));
        m_controller->moveEffect(ref.track, ref.clip, from, to);
        return ok(clipFeedback(ref, {{QStringLiteral("from"), from}, {QStringLiteral("to"), to}}));
    }

    if (tool == QLatin1String("set_effect_color_param")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const int index = jsonInt(args.value(QStringLiteral("index")));
        const QString key = args.value(QStringLiteral("key")).toString().trimmed();
        const QString value = args.value(QStringLiteral("value")).toString().trimmed();
        if (index < 0 || key.isEmpty() || value.isEmpty())
            return err("bad_args", QStringLiteral("index, key, and value required"));
        m_controller->setEffectColorParam(ref.track, ref.clip, index, key, value);
        return ok(clipFeedback(ref, {{QStringLiteral("index"), index}, {QStringLiteral("key"), key}}));
    }

    if (tool == QLatin1String("set_audio_effect_enabled")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const int index = jsonInt(args.value(QStringLiteral("index")));
        if (index < 0 || !args.contains(QStringLiteral("enabled")))
            return err("bad_args", QStringLiteral("index and enabled required"));
        const bool enabled = jsonBool(args.value(QStringLiteral("enabled")));
        m_controller->setAudioEffectEnabled(ref.track, ref.clip, index, enabled);
        return ok(clipFeedback(ref, {{QStringLiteral("index"), index}, {QStringLiteral("enabled"), enabled}}));
    }

    if (tool == QLatin1String("move_audio_effect")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const int from = jsonInt(args.value(QStringLiteral("from")));
        const int to = jsonInt(args.value(QStringLiteral("to")));
        if (from < 0 || to < 0)
            return err("bad_args", QStringLiteral("from and to required"));
        m_controller->moveAudioEffect(ref.track, ref.clip, from, to);
        return ok(clipFeedback(ref, {{QStringLiteral("from"), from}, {QStringLiteral("to"), to}}));
    }

    if (tool == QLatin1String("list_effect_templates"))
        return ok({{QStringLiteral("templates"), compactCatalogList(m_controller->effectTemplateCatalog())}});

    if (tool == QLatin1String("apply_effect_template")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString templateId = argString(args, QStringLiteral("template"));
        if (templateId.isEmpty())
            return err("bad_args", QStringLiteral("template required"));
        m_controller->applyEffectTemplate(ref.track, ref.clip, templateId);
        return ok(clipFeedback(ref, {{QStringLiteral("template"), templateId}}));
    }

    if (tool == QLatin1String("set_transition_kind")) {
        const int track = jsonInt(args.value(QStringLiteral("track")));
        const QString id = args.value(QStringLiteral("id")).toString().trimmed();
        const QString kind = args.value(QStringLiteral("kind")).toString().trimmed();
        if (track < 0 || id.isEmpty() || kind.isEmpty())
            return err("bad_args", QStringLiteral("track, id, and kind required"));
        m_controller->setTransitionKind(track, id, kind);
        return ok({{QStringLiteral("track"), track}, {QStringLiteral("id"), id}, {QStringLiteral("kind"), kind}});
    }

    if (tool == QLatin1String("set_transition_duration")) {
        const int track = jsonInt(args.value(QStringLiteral("track")));
        const QString id = args.value(QStringLiteral("id")).toString().trimmed();
        if (track < 0 || id.isEmpty() || !args.contains(QStringLiteral("duration")))
            return err("bad_args", QStringLiteral("track, id, and duration required"));
        const double duration = jsonNumber(args.value(QStringLiteral("duration")), 0);
        m_controller->setTransitionDuration(track, id, duration);
        return ok({{QStringLiteral("track"), track}, {QStringLiteral("id"), id}, {QStringLiteral("duration"), duration}});
    }

    if (tool == QLatin1String("set_transition_param")) {
        const int track = jsonInt(args.value(QStringLiteral("track")));
        const QString id = args.value(QStringLiteral("id")).toString().trimmed();
        const QString key = args.value(QStringLiteral("key")).toString().trimmed();
        if (track < 0 || id.isEmpty() || key.isEmpty() || !args.contains(QStringLiteral("value")))
            return err("bad_args", QStringLiteral("track, id, key, and value required"));
        const double value = jsonNumber(args.value(QStringLiteral("value")), 0);
        m_controller->setTransitionParam(track, id, key, value);
        return ok({{QStringLiteral("track"), track}, {QStringLiteral("id"), id}, {QStringLiteral("key"), key}});
    }

    // --- keyframes ---
    if (tool == QLatin1String("move_keyframe")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString prop = args.value(QStringLiteral("prop")).toString().trimmed();
        if (prop.isEmpty() || !args.contains(QStringLiteral("from")) || !args.contains(QStringLiteral("to")))
            return err("bad_args", QStringLiteral("prop, from, and to required"));
        const double fromSec = jsonNumber(args.value(QStringLiteral("from")), 0);
        const double toSec = jsonNumber(args.value(QStringLiteral("to")), 0);
        const double value = args.contains(QStringLiteral("value"))
                                 ? jsonNumber(args.value(QStringLiteral("value")), 0)
                                 : m_controller->propertyValueAt(ref.track, ref.clip, prop, fromSec, 0);
        m_controller->previewMoveClipKeyframe(ref.track, ref.clip, prop, fromSec, toSec, value);
        m_controller->commitPreviewDrag();
        return ok(clipFeedback(ref, {{QStringLiteral("prop"), prop}, {QStringLiteral("from"), fromSec}, {QStringLiteral("to"), toSec}}));
    }

    // --- speed (custom fade curve) ---
    if (tool == QLatin1String("list_fade_curve")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (m_controller->fadeCurveSessionActive())
            m_controller->endFadeCurveSession();
        m_controller->beginFadeCurveSession(ref.track, ref.clip);
        if (!m_controller->fadeCurveSessionActive())
            return err("bad_args", QStringLiteral("Clip cannot use a custom fade curve"));
        const QJsonArray points = fadeCurvePointsToJson(m_controller->fadeCurvePoints());
        m_controller->endFadeCurveSession();
        return ok({{QStringLiteral("points"), points}});
    }

    // --- segmentation ---
    if (tool == QLatin1String("segmentation_status")) {
        return ok({{QStringLiteral("available"), m_controller->segmentationAvailable()},
                   {QStringLiteral("model"), m_controller->segmentationModelVariant()},
                   {QStringLiteral("backends"),
                    QJsonArray::fromStringList(m_controller->segmentationBackends())}});
    }

    if (tool == QLatin1String("begin_segmentation_session")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const double at = args.contains(QStringLiteral("at"))
                              ? jsonNumber(args.value(QStringLiteral("at")), m_controller->playheadSeconds())
                              : m_controller->playheadSeconds();
        const bool forTemplate = jsonBool(args.value(QStringLiteral("forTemplate")));
        m_controller->beginSegmentationSession(ref.track, ref.clip, at, forTemplate);
        return ok(clipFeedback(ref, {{QStringLiteral("at"), at}, {QStringLiteral("forTemplate"), forTemplate}}));
    }

    if (tool == QLatin1String("end_segmentation_session")) {
        m_controller->endSegmentationSession();
        return ok({});
    }

    if (tool == QLatin1String("set_segmentation_frame")) {
        if (!args.contains(QStringLiteral("at")))
            return err("bad_args", QStringLiteral("at required"));
        const double at = jsonNumber(args.value(QStringLiteral("at")), 0);
        m_controller->setSegmentationFrame(at);
        return ok({{QStringLiteral("at"), at}});
    }

    if (tool == QLatin1String("add_segmentation_point")) {
        if (!args.contains(QStringLiteral("x")) || !args.contains(QStringLiteral("y")))
            return err("bad_args", QStringLiteral("x and y required"));
        const double x = jsonNumber(args.value(QStringLiteral("x")), 0);
        const double y = jsonNumber(args.value(QStringLiteral("y")), 0);
        const bool include = jsonBool(args.value(QStringLiteral("include")), true);
        m_controller->addSegmentationPoint(x, y, include);
        return ok({{QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("include"), include}});
    }

    if (tool == QLatin1String("remove_segmentation_point")) {
        const int index = jsonInt(args.value(QStringLiteral("index")));
        if (index < 0)
            return err("bad_args", QStringLiteral("index required"));
        m_controller->removeSegmentationPoint(index);
        return ok({{QStringLiteral("removed"), index}});
    }

    if (tool == QLatin1String("clear_segmentation_points")) {
        m_controller->clearSegmentationPoints();
        return ok({});
    }

    if (tool == QLatin1String("run_segmentation")) {
        const QString output = argString(args, QStringLiteral("output"));
        const QString backend = argString(args, QStringLiteral("backend"));
        if (!backend.isEmpty())
            m_controller->setSegmentationBackend(backend);
        m_controller->runSegmentationSession(output.isEmpty() ? QStringLiteral("adjustment") : output);
        return ok({{QStringLiteral("output"), output.isEmpty() ? QStringLiteral("adjustment") : output},
                   {QStringLiteral("backend"), m_controller->segmentBackend()}});
    }

    if (tool == QLatin1String("segment_clip")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QJsonArray pointArray = args.value(QStringLiteral("points")).toArray();
        const QString backend = argString(args, QStringLiteral("backend"));
        const bool rvm = backend == QLatin1String("rvm");
        // RVM takes no prompt, so requiring points there would be a lie about what it needs.
        if (!rvm && pointArray.isEmpty())
            return err("bad_args", QStringLiteral("points required for the sam2 backend"));
        const QString output = argString(args, QStringLiteral("output"));
        m_controller->segmentClip(ref.track, ref.clip, segmentationPointsFromJson(pointArray),
                                  output.isEmpty() ? QStringLiteral("adjustment") : output, backend);
        return ok(clipFeedback(ref, {{QStringLiteral("output"), output.isEmpty() ? QStringLiteral("adjustment") : output},
                                     {QStringLiteral("backend"), rvm ? QStringLiteral("rvm") : QStringLiteral("sam2")}}));
    }

    if (tool == QLatin1String("cancel_segmentation")) {
        m_controller->cancelSegmentation();
        return ok({{QStringLiteral("cancelled"), true}});
    }

    // --- ai ---
    if (tool == QLatin1String("denoise_status"))
        return ok({{QStringLiteral("available"), m_controller->denoiseAvailable()}});

    if (tool == QLatin1String("apply_denoise")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->applyDenoise(ref.track, ref.clip);
        return ok(clipFeedback(ref, {{QStringLiteral("started"), true}}));
    }

    if (tool == QLatin1String("cancel_denoise")) {
        m_controller->cancelDenoise();
        return ok({{QStringLiteral("cancelled"), true}});
    }

    if (tool == QLatin1String("face_detection_status"))
        return ok({{QStringLiteral("available"), m_controller->faceDetectionAvailable()}});

    if (tool == QLatin1String("detect_faces")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->detectFacesForClip(ref.track, ref.clip);
        return ok(clipFeedback(ref, {{QStringLiteral("started"), true}}));
    }

    if (tool == QLatin1String("cancel_face_detection")) {
        m_controller->cancelFaceDetection();
        return ok({{QStringLiteral("cancelled"), true}});
    }

    if (tool == QLatin1String("clear_face_track")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->clearFaceTrack(ref.track, ref.clip);
        return ok(clipFeedback(ref));
    }

    // --- audio ---
    if (tool == QLatin1String("audio_summary"))
        return m_controller->mcpAudioSummary();

    if (tool == QLatin1String("get_waveform")) {
        const int buckets = qBound(1, jsonInt(args.value(QStringLiteral("buckets")), 400), 4096);
        const bool image = jsonBool(args.value(QStringLiteral("image")));
        const auto waveformImage = [&](const QString &mode, int track, int clipIndex,
                                       const QString &assetId, double start, double duration) {
            return m_controller->mcpWaveformImage(
                mode, track, clipIndex, assetId, start, duration,
                jsonInt(args.value(QStringLiteral("width")), 1400),
                jsonInt(args.value(QStringLiteral("height")), 300),
                jsonBool(args.value(QStringLiteral("spectrogram"))),
                jsonInt(args.value(QStringLiteral("summary_buckets")), 50));
        };

        // Mode is chosen by what was passed, most specific first: a clip reference names one
        // clip's source window, an asset names a whole file, and neither means the mix.
        if (args.contains(QStringLiteral("clip")) || args.contains(QStringLiteral("index"))
            || (args.contains(QStringLiteral("track")) && !args.contains(QStringLiteral("asset")))) {
            const ClipRef ref = resolveClip(args);
            if (!ref.valid())
                return clipRefError(args);
            if (image)
                return waveformImage(QStringLiteral("clip"), ref.track, ref.clip, {}, 0.0, 0.0);
            return m_controller->mcpWaveformForClip(ref.track, ref.clip, buckets);
        }

        if (args.contains(QStringLiteral("asset"))) {
            const int index = resolveAsset(args.value(QStringLiteral("asset")));
            if (index < 0)
                return err("not_found", QStringLiteral("Unknown asset"));
            AssetLibrary *lib = m_controller->assetLibrary();
            const QString assetId = lib ? lib->assetIdAt(index) : QString();
            const double start = jsonNumber(args.value(QStringLiteral("start")), 0.0);
            const double duration = jsonNumber(args.value(QStringLiteral("duration")), 0.0);
            if (image)
                return waveformImage(QStringLiteral("asset"), -1, -1, assetId, start, duration);
            return m_controller->mcpWaveformForAsset(assetId, start, duration, buckets);
        }

        if (!args.contains(QStringLiteral("duration"))) {
            return err("bad_args",
                       QStringLiteral("duration required for a timeline waveform (or pass clip/asset)"));
        }
        if (image) {
            return waveformImage(QStringLiteral("timeline"), -1, -1, {},
                                 jsonNumber(args.value(QStringLiteral("start")), 0.0),
                                 jsonNumber(args.value(QStringLiteral("duration")), 0.0));
        }
        return m_controller->mcpWaveformForTimeline(
            jsonNumber(args.value(QStringLiteral("start")), 0.0),
            jsonNumber(args.value(QStringLiteral("duration")), 0.0), buckets);
    }

    if (tool == QLatin1String("detect_beats")) {
        if (!args.contains(QStringLiteral("duration")))
            return err("bad_args", QStringLiteral("duration required"));
        return m_controller->mcpDetectBeats(jsonNumber(args.value(QStringLiteral("start")), 0.0),
                                            jsonNumber(args.value(QStringLiteral("duration")), 0.0),
                                            jsonBool(args.value(QStringLiteral("force"))));
    }

    if (tool == QLatin1String("set_beat_layers")) {
        return m_controller->mcpSetBeatLayers(
            jsonBool(args.value(QStringLiteral("grid")), true),
            jsonBool(args.value(QStringLiteral("onsets")), false));
    }

    if (tool == QLatin1String("bookmark_beats")) {
        const QString unit = argString(args, QStringLiteral("unit"));
        const int added = m_controller->mcpBookmarkBeats(
            jsonNumber(args.value(QStringLiteral("start")), 0.0),
            jsonNumber(args.value(QStringLiteral("duration")), 0.0),
            unit.isEmpty() ? QStringLiteral("beat") : unit,
            jsonNumber(args.value(QStringLiteral("min_strength")), 0.0),
            argString(args, QStringLiteral("label")).isEmpty() ? QStringLiteral("Beat")
                                                               : argString(args, QStringLiteral("label")));
        if (added == 0 && m_controller->mcpBeatTimes(unit, 0.0).isEmpty())
            return err("not_found", QStringLiteral("No beat analysis yet — call detect_beats first"));
        return ok({{QStringLiteral("added"), added}});
    }

    if (tool == QLatin1String("split_on_beats"))
        return opSplitOnBeats(args);

    if (tool == QLatin1String("snap_clips_to_beats"))
        return opSnapClipsToBeats(args);

    // --- scene ---
    if (tool == QLatin1String("detect_scenes")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        return m_controller->mcpDetectScenes(
            ref.track, ref.clip, jsonNumber(args.value(QStringLiteral("threshold")), 0.0),
            jsonNumber(args.value(QStringLiteral("min_scene")), 0.0),
            jsonBool(args.value(QStringLiteral("with_objects"))));
    }

    if (tool == QLatin1String("list_scenes") || tool == QLatin1String("describe_clip")) {
        int track = -1;
        int clipIndex = -1;
        if (args.contains(QStringLiteral("clip")) || args.contains(QStringLiteral("track"))
            || args.contains(QStringLiteral("index"))) {
            const ClipRef ref = resolveClip(args);
            if (!ref.valid())
                return clipRefError(args);
            track = ref.track;
            clipIndex = ref.clip;
        }
        if (tool == QLatin1String("describe_clip"))
            return m_controller->mcpDescribeClip(int(jsonNumber(args.value(QStringLiteral("top")), 5)), track, clipIndex);
        return m_controller->mcpListScenes(
            argString(args, QStringLiteral("label")),
            jsonNumber(args.value(QStringLiteral("min_score")), 0.0),
            argString(args, QStringLiteral("sort")),
            int(jsonNumber(args.value(QStringLiteral("limit")), 200)), track, clipIndex);
    }

    if (tool == QLatin1String("find_scenes")) {
        return m_controller->mcpFindScenes(
            argString(args, QStringLiteral("label")),
            jsonNumber(args.value(QStringLiteral("min_score")), 0.0),
            args.contains(QStringLiteral("track"))
                ? int(jsonNumber(args.value(QStringLiteral("track")), -1))
                : -1,
            int(jsonNumber(args.value(QStringLiteral("limit")), 50)));
    }

    if (tool == QLatin1String("split_on_scenes"))
        return opSplitOnScenes(args);

    if (tool == QLatin1String("bookmark_scenes")) {
        const int added = m_controller->mcpBookmarkScenes(
            jsonNumber(args.value(QStringLiteral("min_score")), 0.0),
            argString(args, QStringLiteral("label")),
            args.contains(QStringLiteral("prefix"))
                ? argString(args, QStringLiteral("prefix"))
                : QStringLiteral("Scene"));
        if (added == 0) {
            return err("not_found",
                       QStringLiteral("No scene analysis to mark — call detect_scenes first"));
        }
        QJsonArray at;
        for (double t : m_controller->mcpSceneCutTimes(
                 jsonNumber(args.value(QStringLiteral("min_score")), 0.0),
                 argString(args, QStringLiteral("label")))) {
            at.append(t);
        }
        return ok({{QStringLiteral("added"), added}, {QStringLiteral("at"), at}});
    }

    if (tool == QLatin1String("ai_capabilities"))
        return m_controller->mcpAiCapabilities();

    if (tool == QLatin1String("set_volume")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (!args.contains(QStringLiteral("value")))
            return err("bad_args", QStringLiteral("value required"));
        const bool atGiven = args.contains(QStringLiteral("at"));
        return m_controller->mcpSetClipVolume(
            ref.track, ref.clip, jsonNumber(args.value(QStringLiteral("value")), 1.0), atGiven,
            jsonNumber(args.value(QStringLiteral("at")), 0.0));
    }

    // --- ui ---
    if (tool == QLatin1String("set_ui_preferences")) {
        if (args.contains(QStringLiteral("autoKey")))
            m_controller->setAutoKeyEnabled(jsonBool(args.value(QStringLiteral("autoKey"))));
        if (args.contains(QStringLiteral("mediaGrid")))
            m_controller->setMediaGridMode(jsonBool(args.value(QStringLiteral("mediaGrid"))));
        if (args.contains(QStringLiteral("reopenLastProject")))
            m_controller->setReopenLastProject(jsonBool(args.value(QStringLiteral("reopenLastProject"))));
        if (args.contains(QStringLiteral("followSystem")) && jsonBool(args.value(QStringLiteral("followSystem"))))
            m_controller->clearDarkModePreference();
        return ok({
            {QStringLiteral("autoKey"), m_controller->autoKeyEnabled()},
            {QStringLiteral("mediaGrid"), m_controller->mediaGridMode()},
            {QStringLiteral("reopenLastProject"), m_controller->reopenLastProject()},
            {QStringLiteral("theme"),
             QJsonObject{{QStringLiteral("overridden"), m_controller->darkModeOverridden()},
                         {QStringLiteral("dark"), m_controller->darkModePreferred()}}},
        });
    }

    if (tool == QLatin1String("list_history"))
        return m_controller->mcpListHistory(qBound(1, jsonInt(args.value(QStringLiteral("limit")), 20), 10000));

    if (tool == QLatin1String("undo_to")) {
        const QString hash = argString(args, QStringLiteral("hash"));
        if (!hash.isEmpty())
            return m_controller->mcpUndoTo(-1, hash);
        if (!args.contains(QStringLiteral("index")))
            return err("bad_args", QStringLiteral("index or hash required"));
        return m_controller->mcpUndoTo(jsonInt(args.value(QStringLiteral("index")), -1), {});
    }

    if (tool == QLatin1String("take_snapshot"))
        return m_controller->mcpTakeSnapshot(argString(args, QStringLiteral("label")));

    if (tool == QLatin1String("list_snapshots"))
        return m_controller->mcpListSnapshots();

    if (tool == QLatin1String("restore_snapshot")) {
        const QString hash = argString(args, QStringLiteral("hash"));
        if (hash.isEmpty())
            return err("bad_args", QStringLiteral("hash required"));
        return m_controller->mcpRestoreSnapshot(hash);
    }

    if (tool == QLatin1String("set_ripple")) {
        if (!args.contains(QStringLiteral("enabled")))
            return err("bad_args", QStringLiteral("enabled required"));
        m_controller->setRippleEnabled(jsonBool(args.value(QStringLiteral("enabled"))));
        return ok({{QStringLiteral("ripple"), m_controller->rippleEnabled()}});
    }

    if (tool == QLatin1String("close_gap")) {
        if (!args.contains(QStringLiteral("track")) || !args.contains(QStringLiteral("at")))
            return err("bad_args", QStringLiteral("track and at required"));
        m_controller->closeGap(jsonInt(args.value(QStringLiteral("track"))),
                               jsonNumber(args.value(QStringLiteral("at")), 0.0));
        return ok({});
    }

    if (tool == QLatin1String("set_snap")) {
        if (!args.contains(QStringLiteral("enabled")))
            return err("bad_args", QStringLiteral("enabled required"));
        m_controller->setSnapEnabled(jsonBool(args.value(QStringLiteral("enabled"))));
        return ok({{QStringLiteral("snap"), m_controller->snapEnabled()}});
    }

    if (tool == QLatin1String("set_guides")) {
        if (!args.contains(QStringLiteral("enabled")) && !args.contains(QStringLiteral("type")))
            return err("bad_args", QStringLiteral("enabled or type required"));
        if (args.contains(QStringLiteral("enabled")))
            m_controller->setGuidesEnabled(jsonBool(args.value(QStringLiteral("enabled"))));
        if (args.contains(QStringLiteral("type")))
            m_controller->setGuideType(argString(args, QStringLiteral("type")));
        return ok({{QStringLiteral("enabled"), m_controller->guidesEnabled()},
                   {QStringLiteral("type"), m_controller->guideType()}});
    }

    if (tool == QLatin1String("set_loop_work_area")) {
        if (!args.contains(QStringLiteral("enabled")))
            return err("bad_args", QStringLiteral("enabled required"));
        m_controller->setLoopWorkAreaEnabled(jsonBool(args.value(QStringLiteral("enabled"))));
        return ok({{QStringLiteral("loop"), m_controller->loopWorkAreaEnabled()}});
    }

    if (tool == QLatin1String("select_clips")) {
        const QJsonArray ids = args.value(QStringLiteral("clips")).toArray();
        if (ids.isEmpty())
            return err("bad_args", QStringLiteral("clips required"));
        QVariantList pairs;
        for (const QJsonValue &v : ids) {
            const QString id = v.toString().trimmed();
            const QPair<int, int> loc = m_controller->mcpLocateClip(id);
            if (loc.first < 0)
                return clipRefError(QJsonObject{{QStringLiteral("clip"), id}});
            pairs.append(QVariantMap{{QStringLiteral("track"), loc.first},
                                     {QStringLiteral("clip"), loc.second}});
        }
        m_controller->setSelection(pairs);
        return ok({{QStringLiteral("n"), pairs.size()}});
    }

    if (tool == QLatin1String("copy_clip_effects")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->copyClipEffectsToClipboard(ref.track, ref.clip);
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("paste_clip_effects")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (!m_controller->clipboardHasEffects())
            return err("bad_args", QStringLiteral("Effect clipboard is empty"));
        m_controller->pasteEffectsFromClipboard(ref.track, ref.clip);
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("set_effect_string_param")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString key = argString(args, QStringLiteral("key"));
        if (key.isEmpty() || !args.contains(QStringLiteral("index")))
            return err("bad_args", QStringLiteral("index and key required"));
        const QString value = argString(args, QStringLiteral("value"));
        const QUrl url = value.isEmpty() ? QUrl() : QUrl::fromLocalFile(localPath(value));
        m_controller->setEffectStringParam(ref.track, ref.clip,
                                           jsonInt(args.value(QStringLiteral("index"))), key, url);
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("apply_text_preset") || tool == QLatin1String("apply_user_text_preset")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString preset = argString(args, QStringLiteral("preset"));
        if (preset.isEmpty())
            return err("bad_args", QStringLiteral("preset required"));
        m_controller->applyTextPreset(ref.track, ref.clip, preset);
        return ok(clipFeedback(ref, {{QStringLiteral("preset"), preset}}));
    }

    if (tool == QLatin1String("list_user_text_presets"))
        return ok({{QStringLiteral("presets"), compactCatalogList(m_controller->userTextPresets())}});

    if (tool == QLatin1String("save_text_preset")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString label = argString(args, QStringLiteral("label"));
        if (label.isEmpty())
            return err("bad_args", QStringLiteral("label required"));
        const QString id = m_controller->saveTextStyleAsPreset(ref.track, ref.clip, label);
        if (id.isEmpty())
            return err("bad_args", QStringLiteral("Could not save text preset"));
        return ok({{QStringLiteral("id"), id}, {QStringLiteral("label"), label}});
    }

    if (tool == QLatin1String("list_user_effect_presets")) {
        QJsonArray presets;
        for (const QVariant &v : m_controller->userEffectPresets()) {
            const QVariantMap m = v.toMap();
            presets.append(QJsonObject{
                {QStringLiteral("id"), m.value(QStringLiteral("id")).toString()},
                {QStringLiteral("label"), m.value(QStringLiteral("label")).toString()},
                {QStringLiteral("effectCount"), m.value(QStringLiteral("effectCount")).toInt()},
                {QStringLiteral("audioEffectCount"), m.value(QStringLiteral("audioEffectCount")).toInt()},
                {QStringLiteral("labels"),
                 QJsonArray::fromStringList(m.value(QStringLiteral("labels")).toStringList())},
            });
        }
        return ok({{QStringLiteral("presets"), presets}});
    }

    if (tool == QLatin1String("apply_effect_preset")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString preset = argString(args, QStringLiteral("preset"));
        if (preset.isEmpty())
            return err("bad_args", QStringLiteral("preset required"));
        m_controller->applyEffectPreset(ref.track, ref.clip, preset);
        return ok(clipFeedback(ref, {{QStringLiteral("preset"), preset}}));
    }

    if (tool == QLatin1String("save_effect_preset")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        const QString label = argString(args, QStringLiteral("label"));
        if (label.isEmpty())
            return err("bad_args", QStringLiteral("label required"));
        const QString id = m_controller->saveClipEffectsAsPreset(ref.track, ref.clip, label);
        if (id.isEmpty())
            return err("bad_args", QStringLiteral("Could not save effect preset"));
        return ok({{QStringLiteral("id"), id}, {QStringLiteral("label"), label}});
    }

    if (tool == QLatin1String("list_export_presets"))
        return ok({{QStringLiteral("presets"), compactCatalogList(m_controller->exportPresets())}});

    if (tool == QLatin1String("export_with_preset")) {
        if (m_controller->exportInProgress())
            return err("export_busy", QStringLiteral("Export already in progress"));
        const QString path = localPath(argString(args, QStringLiteral("path")));
        const QString preset = argString(args, QStringLiteral("preset"));
        if (path.isEmpty() || preset.isEmpty())
            return err("bad_args", QStringLiteral("path and preset required"));
        m_controller->exportWithPreset(QUrl::fromLocalFile(path), preset);
        return ok({{QStringLiteral("started"), true},
                   {QStringLiteral("path"), path},
                   {QStringLiteral("preset"), preset}});
    }

    if (tool == QLatin1String("stabilize_clip")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        if (args.contains(QStringLiteral("smoothing")))
            m_controller->setClipStabilizeSmoothing(ref.track, ref.clip,
                                                    jsonInt(args.value(QStringLiteral("smoothing")), 15));
        if (args.contains(QStringLiteral("tripod")))
            m_controller->setClipStabilizeTripod(ref.track, ref.clip,
                                                 jsonBool(args.value(QStringLiteral("tripod"))));
        if (args.contains(QStringLiteral("mode")))
            m_controller->setClipStabilizeMode(ref.track, ref.clip,
                                               argString(args, QStringLiteral("mode")));
        m_controller->stabilizeClip(ref.track, ref.clip);
        return ok(clipFeedback(ref, {{QStringLiteral("started"), true}}));
    }

    if (tool == QLatin1String("cancel_stabilize")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->cancelClipStabilization(ref.track, ref.clip);
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("remove_stabilize")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        m_controller->removeClipStabilization(ref.track, ref.clip);
        return ok(clipFeedback(ref));
    }

    if (tool == QLatin1String("list_addons"))
        return m_controller->mcpListAddons();

    if (tool == QLatin1String("install_addon"))
        return m_controller->mcpInstallAddon(argString(args, QStringLiteral("id")));

    if (tool == QLatin1String("cancel_addon_install"))
        return m_controller->mcpCancelAddonInstall(argString(args, QStringLiteral("id")));

    if (tool == QLatin1String("set_acceleration"))
        return m_controller->mcpSetAcceleration(argString(args, QStringLiteral("variant")));

    if (tool == QLatin1String("list_face_track")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        return m_controller->mcpListFaceTrack(ref.track, ref.clip);
    }

    if (tool == QLatin1String("auto_reframe")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        double aspect = jsonNumber(args.value(QStringLiteral("aspect")), 0.0);
        if (aspect <= 0.01) {
            const double h = jsonNumber(args.value(QStringLiteral("height")), 0.0);
            if (h > 0.0)
                aspect = jsonNumber(args.value(QStringLiteral("width")), 0.0) / h;
        }
        const QString mode = args.contains(QStringLiteral("mode"))
                                 ? argString(args, QStringLiteral("mode"))
                                 : QStringLiteral("face");
        return m_controller->mcpAutoReframe(ref.track, ref.clip, aspect, mode);
    }

    if (tool == QLatin1String("set_scene_threshold")) {
        if (!args.contains(QStringLiteral("threshold")))
            return err("bad_args", QStringLiteral("threshold required"));
        m_controller->setSceneThreshold(jsonNumber(args.value(QStringLiteral("threshold")), 0.0));
        return ok({{QStringLiteral("threshold"), jsonNumber(args.value(QStringLiteral("threshold")), 0.0)}});
    }

    if (tool == QLatin1String("clear_scenes")) {
        m_controller->clearScenes();
        return ok({});
    }

    if (tool == QLatin1String("cancel_scene_detection")) {
        m_controller->cancelSceneDetection();
        return ok({});
    }

    if (tool == QLatin1String("clear_beat_analysis")) {
        m_controller->clearBeatAnalysis();
        return ok({});
    }

    if (tool == QLatin1String("detect_silence")) {
        const ClipRef ref = resolveClip(args);
        const double threshold = jsonNumber(args.value(QStringLiteral("threshold")), 0.02);
        const double minDuration = jsonNumber(args.value(QStringLiteral("min_duration")), 0.35);
        const double padding = jsonNumber(args.value(QStringLiteral("padding")), 0.08);
        if (ref.valid()) {
            return m_controller->mcpDetectSilence(ref.track, ref.clip, 0, 0, threshold, minDuration,
                                                  padding);
        }
        if (!args.contains(QStringLiteral("start")) || !args.contains(QStringLiteral("duration")))
            return err("bad_args", QStringLiteral("clip, or start+duration, required"));
        return m_controller->mcpDetectSilence(-1, -1,
                                              jsonNumber(args.value(QStringLiteral("start")), 0.0),
                                              jsonNumber(args.value(QStringLiteral("duration")), 0.0),
                                              threshold, minDuration, padding);
    }

    if (tool == QLatin1String("remove_silence")) {
        const ClipRef ref = resolveClip(args);
        const double threshold = jsonNumber(args.value(QStringLiteral("threshold")), 0.02);
        const double minDuration = jsonNumber(args.value(QStringLiteral("min_duration")), 0.35);
        const double padding = jsonNumber(args.value(QStringLiteral("padding")), 0.08);
        if (ref.valid())
            return m_controller->mcpRemoveSilence(ref.track, ref.clip, threshold, minDuration, padding);
        if (!args.contains(QStringLiteral("track")))
            return err("bad_args", QStringLiteral("clip or track required"));
        return m_controller->mcpRemoveSilence(jsonInt(args.value(QStringLiteral("track"))), -1,
                                              threshold, minDuration, padding);
    }

    if (tool == QLatin1String("analyze_loudness")) {
        const ClipRef ref = resolveClip(args);
        if (ref.valid())
            return m_controller->mcpAnalyzeLoudness(ref.track, ref.clip, 0, 0);
        if (!args.contains(QStringLiteral("start")) || !args.contains(QStringLiteral("duration")))
            return err("bad_args", QStringLiteral("clip, or start+duration, required"));
        return m_controller->mcpAnalyzeLoudness(-1, -1,
                                                jsonNumber(args.value(QStringLiteral("start")), 0.0),
                                                jsonNumber(args.value(QStringLiteral("duration")), 0.0));
    }

    if (tool == QLatin1String("normalize_volume")) {
        const ClipRef ref = resolveClip(args);
        if (!ref.valid())
            return clipRefError(args);
        return m_controller->mcpNormalizeVolume(
            ref.track, ref.clip, jsonNumber(args.value(QStringLiteral("target_lufs")), -16.0));
    }

    if (tool == QLatin1String("duck_under")) {
        const ClipRef music = resolveClip(args);
        if (!music.valid())
            return err("not_found", QStringLiteral("Unknown music clip"));
        QStringList overClips;
        for (const QJsonValue &v : args.value(QStringLiteral("over_clips")).toArray())
            overClips.append(v.toString().trimmed());
        const int overTrack = args.contains(QStringLiteral("over_track"))
                                  ? jsonInt(args.value(QStringLiteral("over_track")))
                                  : -1;
        return m_controller->mcpDuckUnder(
            music.track, music.clip, overTrack, overClips,
            jsonNumber(args.value(QStringLiteral("amount")), 0.3),
            jsonNumber(args.value(QStringLiteral("attack")), 0.12),
            jsonNumber(args.value(QStringLiteral("release")), 0.25));
    }

    if (tool == QLatin1String("setup_multicam")) {
        if (!m_controller->beginMulticamSession())
            return err("bad_args",
                       QStringLiteral("Need two video clips on different tracks, or two video assets"));
        if (m_controller->multicamAngles().size() < 2)
            m_controller->setUpMulticamFromAssets();
        if (!m_controller->multicamActive() || m_controller->multicamAngles().size() < 2) {
            return err("bad_args",
                       QStringLiteral("Need two video clips on different tracks, or two video assets"));
        }
        return ok({{QStringLiteral("active"), true},
                   {QStringLiteral("angles"),
                    QJsonArray::fromVariantList(m_controller->multicamAngles())}});
    }

    if (tool == QLatin1String("switch_angle")) {
        if (!args.contains(QStringLiteral("angle")))
            return err("bad_args", QStringLiteral("angle required"));
        m_controller->switchMulticamAngle(jsonInt(args.value(QStringLiteral("angle"))));
        return ok({{QStringLiteral("angle"), m_controller->multicamActiveAngle()}});
    }

    if (tool == QLatin1String("end_multicam")) {
        m_controller->endMulticamSession();
        return ok({{QStringLiteral("active"), m_controller->multicamActive()}});
    }

    if (tool == QLatin1String("save_multicam_separate")) {
        m_controller->saveMulticamAsSeparateTracks();
        return ok({{QStringLiteral("active"), m_controller->multicamActive()}});
    }

    if (tool == QLatin1String("save_multicam_combined")) {
        m_controller->saveMulticamCombined();
        return ok({{QStringLiteral("active"), m_controller->multicamActive()}});
    }

    if (tool.startsWith(QLatin1String("market_"))) {
        MarketClient *market = m_controller->marketClient();
        if (tool == QLatin1String("market_status"))
            return marketStatus(market);
        if (tool == QLatin1String("market_search"))
            return marketSearch(market, args);
        if (tool == QLatin1String("market_resolve"))
            return marketResolve(market, argString(args, QStringLiteral("url")));
        if (tool == QLatin1String("market_item"))
            return marketItem(market, argString(args, QStringLiteral("id")));
        if (tool == QLatin1String("market_download"))
            return marketDownload(market, args);
        if (tool == QLatin1String("market_downloads"))
            return marketDownloads(market, jsonBool(args.value(QStringLiteral("clear"))));
        if (tool == QLatin1String("market_cancel_download"))
            return marketCancelDownload(market, argString(args, QStringLiteral("id")));
    }

    return unknownOpError(tool);
}

} // namespace drift::mcp
