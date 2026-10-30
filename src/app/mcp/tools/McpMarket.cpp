#include "app/mcp/tools/McpMarket.h"
#include "app/mcp/protocol/McpJson.h"
#include "app/online/downloads/DownloadCenter.h"
#include "app/online/marketplace/MarketSourceBrowser.h"
#include "app/online/marketplace/StockInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <functional>

namespace drift::mcp {
namespace {

constexpr int kSearchWaitMs = 30000;
constexpr int kResolveWaitMs = 90000;
constexpr int kMaxDownloadWaitSeconds = 600;

// Pumps the event loop until `done` or the deadline. The client's replies arrive on this
// thread, so a blocking wait without the loop would never see them.
bool waitUntil(const std::function<bool()> &done, int timeoutMs)
{
    if (done())
        return true;
    QElapsedTimer clock;
    clock.start();
    QEventLoop loop;
    QTimer tick;
    tick.setInterval(25);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&] {
        if (done() || clock.elapsed() > timeoutMs)
            loop.quit();
    });
    tick.start();
    loop.exec();
    return done();
}

// The agent's own browsing session: one listing, paged with more:true, and the items it has
// shown so market_item and market_download can find them again.
MarketSourceBrowser *browser()
{
    static MarketSourceBrowser *shared = new MarketSourceBrowser(QCoreApplication::instance());
    return shared;
}

QHash<QString, QVariantMap> &seenItems()
{
    static QHash<QString, QVariantMap> items;
    return items;
}

QJsonObject gate(StockInstaller *client)
{
    if (!client || !client->configured())
        return err("market_unavailable", QStringLiteral("This build has no marketplace service configured"));
    return {};
}

QJsonObject compactItem(const QVariantMap &item, bool full)
{
    const QVariantMap extra = item.value(QStringLiteral("extra")).toMap();
    QJsonObject out{{QStringLiteral("id"), item.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("title"), item.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("type"), item.value(QStringLiteral("mediaKind")).toString()},
                    {QStringLiteral("source"), item.value(QStringLiteral("source")).toString()},
                    {QStringLiteral("provider"), extra.value(QStringLiteral("provider")).toString()}};
    if (item.value(QStringLiteral("durationMs")).toDouble() > 0)
        out.insert(QStringLiteral("dur"), item.value(QStringLiteral("durationMs")).toDouble() / 1000.0);
    if (item.value(QStringLiteral("width")).toInt() > 0) {
        out.insert(QStringLiteral("w"), item.value(QStringLiteral("width")).toInt());
        out.insert(QStringLiteral("h"), item.value(QStringLiteral("height")).toInt());
    }
    if (extra.contains(QStringLiteral("downloadable")) && !extra.value(QStringLiteral("downloadable")).toBool())
        out.insert(QStringLiteral("downloadable"), false);
    const QString by = extra.value(QStringLiteral("creator")).toString();
    if (!by.isEmpty())
        out.insert(QStringLiteral("by"), by);
    const QString thumb = item.value(QStringLiteral("thumbUrl")).toString();
    if (!thumb.isEmpty())
        out.insert(QStringLiteral("thumb"), thumb);
    const QVariantList variants = item.value(QStringLiteral("variants")).toList();
    if (full) {
        const QString preview = item.value(QStringLiteral("previewUrl")).toString();
        if (!preview.isEmpty())
            out.insert(QStringLiteral("preview"), preview);
        const QVariantMap license = item.value(QStringLiteral("license")).toMap();
        if (!license.isEmpty())
            out.insert(QStringLiteral("license"), QJsonObject::fromVariantMap(license));
        QJsonArray rows;
        for (const QVariant &v : variants) {
            const QVariantMap variant = v.toMap();
            QJsonObject row{{QStringLiteral("id"), variant.value(QStringLiteral("id")).toString()},
                            {QStringLiteral("label"), variant.value(QStringLiteral("label")).toString()}};
            if (variant.value(QStringLiteral("width")).toInt() > 0) {
                row.insert(QStringLiteral("w"), variant.value(QStringLiteral("width")).toInt());
                row.insert(QStringLiteral("h"), variant.value(QStringLiteral("height")).toInt());
            }
            rows.append(row);
        }
        if (!rows.isEmpty())
            out.insert(QStringLiteral("variants"), rows);
    } else if (!variants.isEmpty()) {
        out.insert(QStringLiteral("variants"), variants.size());
    }
    return out;
}

QJsonObject compactJob(const QVariantMap &job)
{
    QJsonObject out{{QStringLiteral("id"), job.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("title"), job.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("status"), job.value(QStringLiteral("status")).toString()},
                    {QStringLiteral("progress"), job.value(QStringLiteral("progress")).toDouble()}};
    const QString kind = job.value(QStringLiteral("mediaKind")).toString();
    if (!kind.isEmpty())
        out.insert(QStringLiteral("kind"), kind);
    const QString phase = job.value(QStringLiteral("phase")).toString();
    if (!phase.isEmpty())
        out.insert(QStringLiteral("phase"), phase);
    if (job.value(QStringLiteral("status")).toString() == QLatin1String("failed")) {
        out.insert(QStringLiteral("error"),
                   QJsonObject{{QStringLiteral("code"), job.value(QStringLiteral("errorCode")).toString()},
                               {QStringLiteral("message"), job.value(QStringLiteral("errorMessage")).toString()},
                               {QStringLiteral("retryable"), job.value(QStringLiteral("retryable")).toBool()}});
    }
    const QString path = job.value(QStringLiteral("filePath")).toString();
    if (!path.isEmpty())
        out.insert(QStringLiteral("path"), path);
    const QString asset = job.value(QStringLiteral("assetId")).toString();
    if (!asset.isEmpty())
        out.insert(QStringLiteral("asset"), asset);
    return out;
}

QString jobIdFor(const QVariantMap &item)
{
    return StockInstaller::jobId(item.value(QStringLiteral("source")).toString(),
                                 item.value(QStringLiteral("id")).toString());
}

} // namespace

QJsonObject marketStatus(StockInstaller *client)
{
    QJsonObject out = ok({{QStringLiteral("configured"), client && client->configured()}});
    if (!client || !client->configured()) {
        out.insert(QStringLiteral("hint"), QStringLiteral("This build has no marketplace service configured"));
        return out;
    }
    out.insert(QStringLiteral("active_downloads"), DownloadCenter::instance()->activeCount());
    return out;
}

QJsonObject marketSearch(StockInstaller *client, const QJsonObject &args)
{
    if (const QJsonObject blocked = gate(client); !blocked.isEmpty())
        return blocked;

    MarketSourceBrowser *list = browser();
    const bool more = args.value(QStringLiteral("more")).toBool();
    const int limit = args.value(QStringLiteral("limit")).toInt(30);
    int offset = 0;
    if (more) {
        if (!list->hasMore())
            return err("not_found", QStringLiteral("No more results for the last search"));
        offset = list->items().size();
        list->loadMore();
    } else {
        const QString type = args.value(QStringLiteral("type")).toString(QStringLiteral("video"));
        if (type != QLatin1String("video") && type != QLatin1String("audio") && type != QLatin1String("image"))
            return err("bad_args", QStringLiteral("type must be one of video, audio, image"));
        list->setSource(QStringLiteral("stock.") + type);
        list->setProvider(args.value(QStringLiteral("provider")).toString());
        list->setQuery(args.value(QStringLiteral("q")).toString());
        list->setSort(args.value(QStringLiteral("sort")).toString());
        list->setFilters(args.value(QStringLiteral("filters")).toObject().toVariantMap());
        list->reload();
    }
    if (!waitUntil([list] { return !list->loading(); }, kSearchWaitMs)) {
        list->cancel();
        return err("market_error", QStringLiteral("The marketplace did not answer in time"));
    }
    if (!list->error().isEmpty()) {
        const QString code = list->errorCode();
        return err(code.isEmpty() ? "market_error" : code.toUtf8().constData(), list->error());
    }
    const QVariantList rows = list->items();
    QJsonArray items;
    for (int i = offset; i < rows.size(); ++i) {
        const QVariantMap row = rows.at(i).toMap();
        seenItems().insert(row.value(QStringLiteral("id")).toString(), row);
        if (limit > 0 && items.size() >= limit)
            continue;
        items.append(compactItem(row, false));
    }
    QJsonObject out = ok({{QStringLiteral("type"), list->source().mid(int(qstrlen("stock.")))},
                          {QStringLiteral("provider"), list->provider()},
                          {QStringLiteral("items"), items},
                          {QStringLiteral("n"), items.size()},
                          {QStringLiteral("offset"), offset},
                          {QStringLiteral("has_more"), list->hasMore()}});
    if (!list->quota().isEmpty())
        out.insert(QStringLiteral("quota"), QJsonObject::fromVariantMap(list->quota()));
    return out;
}

QJsonObject marketResolve(StockInstaller *client, const QString &url)
{
    if (const QJsonObject blocked = gate(client); !blocked.isEmpty())
        return blocked;
    if (url.trimmed().isEmpty())
        return err("bad_args", QStringLiteral("url required"));
    client->resolveUrl(url.trimmed());
    if (!waitUntil([client] { return !client->resolving(); }, kResolveWaitMs)) {
        client->cancelResolve();
        return err("market_error", QStringLiteral("Resolving that link did not finish in time"));
    }
    if (!client->resolveError().isEmpty()) {
        const QString code = client->resolveErrorCode();
        return err(code.isEmpty() ? "market_error" : code.toUtf8().constData(), client->resolveError());
    }
    const QVariantMap item = client->resolvedItem();
    if (item.isEmpty())
        return err("not_found", QStringLiteral("Nothing could be resolved from that link"));
    seenItems().insert(item.value(QStringLiteral("id")).toString(), item);
    return ok({{QStringLiteral("item"), compactItem(item, true)}});
}

QJsonObject marketItem(StockInstaller *client, const QString &id)
{
    if (const QJsonObject blocked = gate(client); !blocked.isEmpty())
        return blocked;
    const QVariantMap item = seenItems().value(id);
    if (item.isEmpty())
        return err("not_found", QStringLiteral("Item %1 is not in the last search or resolve result").arg(id));
    return ok({{QStringLiteral("item"), compactItem(item, true)}});
}

QJsonObject marketDownload(StockInstaller *client, const QJsonObject &args)
{
    if (const QJsonObject blocked = gate(client); !blocked.isEmpty())
        return blocked;
    const QString id = args.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return err("bad_args", QStringLiteral("id required — an item id from market_search or market_resolve"));
    const QVariantMap item = seenItems().value(id);
    if (item.isEmpty())
        return err("not_found", QStringLiteral("Item %1 is not in the last market_search / market_resolve result").arg(id));
    const QVariantMap extra = item.value(QStringLiteral("extra")).toMap();
    if (extra.contains(QStringLiteral("downloadable")) && !extra.value(QStringLiteral("downloadable")).toBool())
        return err("bad_args", QStringLiteral("Item %1 is not downloadable").arg(id));
    DownloadCenter *center = DownloadCenter::instance();
    const QString jobId = jobIdFor(item);
    if (center->contains(jobId) && !DownloadCenter::isFinished(center->job(jobId).status))
        return err("conflict", QStringLiteral("Item %1 is already downloading").arg(id));

    QString dir = args.value(QStringLiteral("dir")).toString();
    if (dir.startsWith(QLatin1String("file://")))
        dir = QUrl(dir).toLocalFile();
    if (!dir.isEmpty() && !QDir::isAbsolutePath(dir))
        return err("bad_args", QStringLiteral("dir must be an absolute path"));
    client->download(item.value(QStringLiteral("source")).toString(), id,
                     args.value(QStringLiteral("variant")).toString(),
                     dir.isEmpty() ? QUrl() : QUrl::fromLocalFile(dir),
                     item.value(QStringLiteral("title")).toString(),
                     item.value(QStringLiteral("mediaKind")).toString());
    if (!center->contains(jobId))
        return err("market_error", QStringLiteral("The download could not be started"));

    const int waitSeconds = qBound(0, args.value(QStringLiteral("wait")).toInt(0), kMaxDownloadWaitSeconds);
    if (waitSeconds > 0) {
        waitUntil([center, jobId] { return DownloadCenter::isFinished(center->job(jobId).status); },
                  waitSeconds * 1000);
    }
    const QVariantMap info = center->jobMap(jobId);
    QJsonObject out = compactJob(info);
    const QString status = info.value(QStringLiteral("status")).toString();
    if (waitSeconds > 0 && (status == QLatin1String("failed") || status == QLatin1String("cancelled"))) {
        out.insert(QStringLiteral("ok"), false);
        const QString code = info.value(QStringLiteral("errorCode")).toString();
        out.insert(QStringLiteral("error"), status == QLatin1String("cancelled") ? QStringLiteral("cancelled")
                                            : code.isEmpty()                     ? QStringLiteral("download_failed")
                                                                                 : code);
        out.insert(QStringLiteral("detail"), info.value(QStringLiteral("errorMessage")).toString());
        out.insert(QStringLiteral("job"), compactJob(info));
        return out;
    }
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("started"), true);
    return out;
}

QJsonObject marketDownloads(StockInstaller *client, bool clear)
{
    if (!client || !client->configured())
        return err("market_unavailable", QStringLiteral("This build has no marketplace service configured"));
    DownloadCenter *center = DownloadCenter::instance();
    if (clear)
        center->clearFinished();
    QJsonArray jobs;
    for (const QVariant &row : center->jobs())
        jobs.append(compactJob(row.toMap()));
    return ok({{QStringLiteral("jobs"), jobs},
               {QStringLiteral("active"), center->activeCount()},
               {QStringLiteral("n"), jobs.size()}});
}

QJsonObject marketCancelDownload(StockInstaller *client, const QString &id)
{
    if (!client || !client->configured())
        return err("market_unavailable", QStringLiteral("This build has no marketplace service configured"));
    DownloadCenter *center = DownloadCenter::instance();
    QString jobId = id;
    if (!center->contains(jobId)) {
        const QVariantMap item = seenItems().value(id);
        jobId = item.isEmpty() ? id : jobIdFor(item);
    }
    if (!center->contains(jobId))
        return err("not_found", QStringLiteral("No download for item %1").arg(id));
    const QVariantMap info = center->jobMap(jobId);
    if (info.value(QStringLiteral("finished")).toBool())
        return err("conflict", QStringLiteral("Download %1 already finished (%2)")
                                   .arg(id, info.value(QStringLiteral("status")).toString()));
    center->cancel(jobId);
    return ok({{QStringLiteral("id"), id}, {QStringLiteral("cancelled"), true}});
}

} // namespace drift::mcp
