#include "mcp/McpMarket.h"
#include "mcp/McpJson.h"
#include "models/MarketClient.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <functional>

namespace drift::mcp {
namespace {

constexpr int kCatalogWaitMs = 20000;
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

QJsonObject gate(MarketClient *client)
{
    if (!client || !client->configured())
        return err("market_unavailable", QStringLiteral("This build has no marketplace service configured"));
    if (!client->consented()) {
        return err("consent_required",
                   QStringLiteral("The user has not accepted the marketplace terms. They accept them in the "
                                  "app (Assets → Market); an agent cannot accept on their behalf"));
    }
    return {};
}

bool ensureCatalog(MarketClient *client)
{
    if (!client->types().isEmpty())
        return true;
    if (!client->catalogLoading())
        client->refreshCatalog();
    waitUntil([client] { return !client->catalogLoading(); }, kCatalogWaitMs);
    return !client->types().isEmpty();
}

QJsonObject quotaJson(const QVariantMap &quota)
{
    QJsonObject out;
    for (const char *key : {"limit", "remaining", "window", "scope", "reset_at"}) {
        const QVariant v = quota.value(QLatin1String(key));
        if (v.isValid())
            out.insert(QLatin1String(key), QJsonValue::fromVariant(v));
    }
    return out;
}

QJsonObject compactItem(const QVariantMap &item, bool full)
{
    QJsonObject out{{QStringLiteral("id"), item.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("title"), item.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("type"), item.value(QStringLiteral("type")).toString()},
                    {QStringLiteral("provider"), item.value(QStringLiteral("provider")).toString()}};
    if (item.contains(QStringLiteral("duration_ms")))
        out.insert(QStringLiteral("dur"), item.value(QStringLiteral("duration_ms")).toDouble() / 1000.0);
    if (item.value(QStringLiteral("width")).toInt() > 0) {
        out.insert(QStringLiteral("w"), item.value(QStringLiteral("width")).toInt());
        out.insert(QStringLiteral("h"), item.value(QStringLiteral("height")).toInt());
    }
    if (item.value(QStringLiteral("price_coins")).toInt() > 0)
        out.insert(QStringLiteral("coins"), item.value(QStringLiteral("price_coins")).toInt());
    if (item.contains(QStringLiteral("downloadable")) && !item.value(QStringLiteral("downloadable")).toBool())
        out.insert(QStringLiteral("downloadable"), false);
    const QString by = item.value(QStringLiteral("creator")).toMap().value(QStringLiteral("name")).toString();
    if (!by.isEmpty())
        out.insert(QStringLiteral("by"), by);
    const QString thumb = item.value(QStringLiteral("thumb_url")).toString();
    if (!thumb.isEmpty())
        out.insert(QStringLiteral("thumb"), thumb);
    const QVariantList variants = item.value(QStringLiteral("variants")).toList();
    if (full) {
        const QString preview = item.value(QStringLiteral("preview_url")).toString();
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
            if (variant.value(QStringLiteral("price_coins")).toInt() > 0)
                row.insert(QStringLiteral("coins"), variant.value(QStringLiteral("price_coins")).toInt());
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
    QJsonObject out{{QStringLiteral("id"), job.value(QStringLiteral("itemId")).toString()},
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

QStringList idsOf(const QVariantList &rows)
{
    QStringList ids;
    for (const QVariant &row : rows)
        ids.append(row.toMap().value(QStringLiteral("id")).toString());
    return ids;
}

QJsonObject searchOutcome(MarketClient *client, int limit)
{
    if (!client->searchError().isEmpty()) {
        const QString code = client->searchErrorCode();
        return err(code.isEmpty() ? "market_error" : code.toUtf8().constData(), client->searchError());
    }
    QJsonArray items;
    for (const QVariant &row : client->items()) {
        if (limit > 0 && items.size() >= limit)
            break;
        items.append(compactItem(row.toMap(), false));
    }
    QJsonObject out = ok({{QStringLiteral("type"), client->activeTypeId()},
                          {QStringLiteral("provider"), client->activeProviderId()},
                          {QStringLiteral("items"), items},
                          {QStringLiteral("n"), items.size()},
                          {QStringLiteral("has_more"), client->hasMore()}});
    const QJsonObject quota = quotaJson(client->quota());
    if (!quota.isEmpty())
        out.insert(QStringLiteral("quota"), quota);
    return out;
}

bool jobFinished(const QVariantMap &info)
{
    const QString status = info.value(QStringLiteral("status")).toString();
    return status.isEmpty() || status == QLatin1String("done") || status == QLatin1String("failed")
           || status == QLatin1String("cancelled");
}

} // namespace

QJsonObject marketStatus(MarketClient *client)
{
    QJsonObject out = ok({{QStringLiteral("configured"), client && client->configured()},
                          {QStringLiteral("consented"), client && client->consented()}});
    if (!client || !client->configured()) {
        out.insert(QStringLiteral("hint"), QStringLiteral("This build has no marketplace service configured"));
        return out;
    }
    if (!client->consented()) {
        out.insert(QStringLiteral("hint"),
                   QStringLiteral("The user must accept the marketplace terms in the app (Assets → Market) "
                                  "before search or download work; an agent cannot accept for them"));
        return out;
    }
    out.insert(QStringLiteral("authenticated"), client->authenticated());
    if (client->authenticated()) {
        out.insert(QStringLiteral("account"), client->accountName());
        out.insert(QStringLiteral("coins"), client->coins());
    }
    out.insert(QStringLiteral("active_downloads"), client->activeDownloadCount());
    if (!ensureCatalog(client)) {
        out.insert(QStringLiteral("catalog_error"), client->catalogError());
        return out;
    }
    QJsonArray types;
    for (const QVariant &typeRow : client->types()) {
        const QVariantMap type = typeRow.toMap();
        QJsonArray providers;
        for (const QVariant &providerRow : type.value(QStringLiteral("providers")).toList()) {
            const QVariantMap provider = providerRow.toMap();
            QJsonObject p{{QStringLiteral("id"), provider.value(QStringLiteral("id")).toString()},
                          {QStringLiteral("label"), provider.value(QStringLiteral("label")).toString()},
                          {QStringLiteral("capabilities"),
                           QJsonArray::fromStringList(provider.value(QStringLiteral("capabilities")).toStringList())}};
            const QJsonObject quota = quotaJson(provider.value(QStringLiteral("quota")).toMap());
            if (!quota.isEmpty())
                p.insert(QStringLiteral("quota"), quota);
            QJsonArray filters;
            for (const QVariant &filterRow : provider.value(QStringLiteral("filters")).toList()) {
                const QVariantMap filter = filterRow.toMap();
                QJsonObject f{{QStringLiteral("id"), filter.value(QStringLiteral("id")).toString()},
                              {QStringLiteral("type"), filter.value(QStringLiteral("type")).toString()},
                              {QStringLiteral("label"), filter.value(QStringLiteral("label")).toString()}};
                const QStringList options = idsOf(filter.value(QStringLiteral("options")).toList());
                if (!options.isEmpty())
                    f.insert(QStringLiteral("options"), QJsonArray::fromStringList(options));
                filters.append(f);
            }
            if (!filters.isEmpty())
                p.insert(QStringLiteral("filters"), filters);
            providers.append(p);
        }
        types.append(QJsonObject{{QStringLiteral("id"), type.value(QStringLiteral("id")).toString()},
                                 {QStringLiteral("label"), type.value(QStringLiteral("label")).toString()},
                                 {QStringLiteral("providers"), providers}});
    }
    out.insert(QStringLiteral("types"), types);
    return out;
}

QJsonObject marketSearch(MarketClient *client, const QJsonObject &args)
{
    if (const QJsonObject blocked = gate(client); !blocked.isEmpty())
        return blocked;
    if (!ensureCatalog(client))
        return err("market_error", client->catalogError().isEmpty()
                                       ? QStringLiteral("The marketplace catalog is empty")
                                       : client->catalogError());

    const bool more = args.value(QStringLiteral("more")).toBool();
    const int limit = args.value(QStringLiteral("limit")).toInt(30);
    if (more) {
        if (!client->hasMore())
            return err("not_found", QStringLiteral("No more results for the last search"));
        client->loadMore();
    } else {
        const QString type = args.value(QStringLiteral("type")).toString();
        if (!type.isEmpty()) {
            const QStringList known = idsOf(client->types());
            if (!known.contains(type))
                return err("bad_args", QStringLiteral("type must be one of %1").arg(known.join(QStringLiteral(", "))));
            client->setActiveTypeId(type);
        }
        const QString provider = args.value(QStringLiteral("provider")).toString();
        if (!provider.isEmpty()) {
            const QStringList known = idsOf(client->providers());
            if (!known.contains(provider))
                return err("bad_args", QStringLiteral("provider must be one of %1 for type %2")
                                           .arg(known.join(QStringLiteral(", ")), client->activeTypeId()));
            client->setActiveProviderId(provider);
        }
        if (!client->canSearch()) {
            return err("bad_args", QStringLiteral("provider %1 has no browsable catalog — pass a page URL to "
                                                  "market_resolve instead")
                                       .arg(client->activeProviderId()));
        }
        const QString q = args.value(QStringLiteral("q")).toString();
        const QVariantMap filters = args.value(QStringLiteral("filters")).toObject().toVariantMap();
        client->search(q, filters);
    }
    if (!waitUntil([client] { return !client->searching(); }, kSearchWaitMs)) {
        client->cancelSearch();
        return err("market_error", QStringLiteral("The marketplace did not answer in time"));
    }
    return searchOutcome(client, limit);
}

QJsonObject marketResolve(MarketClient *client, const QString &url)
{
    if (const QJsonObject blocked = gate(client); !blocked.isEmpty())
        return blocked;
    if (url.trimmed().isEmpty())
        return err("bad_args", QStringLiteral("url required"));
    if (!ensureCatalog(client))
        return err("market_error", QStringLiteral("The marketplace catalog is empty"));
    if (!client->canResolve())
        return err("bad_args", QStringLiteral("No provider in the catalog resolves pasted links"));
    client->resolveUrl(url.trimmed());
    if (!waitUntil([client] { return !client->searching(); }, kResolveWaitMs)) {
        client->cancelSearch();
        return err("market_error", QStringLiteral("Resolving that link did not finish in time"));
    }
    if (!client->searchError().isEmpty()) {
        const QString code = client->searchErrorCode();
        return err(code.isEmpty() ? "market_error" : code.toUtf8().constData(), client->searchError());
    }
    if (client->items().isEmpty())
        return err("not_found", QStringLiteral("Nothing could be resolved from that link"));
    QJsonObject out = ok({{QStringLiteral("item"), compactItem(client->items().first().toMap(), true)}});
    const QJsonObject quota = quotaJson(client->quota());
    if (!quota.isEmpty())
        out.insert(QStringLiteral("quota"), quota);
    return out;
}

QJsonObject marketItem(MarketClient *client, const QString &id)
{
    if (const QJsonObject blocked = gate(client); !blocked.isEmpty())
        return blocked;
    const QVariantMap item = client->itemById(id);
    if (item.isEmpty())
        return err("not_found", QStringLiteral("Item %1 is not in the last search or resolve result").arg(id));
    return ok({{QStringLiteral("item"), compactItem(item, true)}});
}

QJsonObject marketDownload(MarketClient *client, const QJsonObject &args)
{
    if (const QJsonObject blocked = gate(client); !blocked.isEmpty())
        return blocked;
    const QString id = args.value(QStringLiteral("id")).toString();
    if (id.isEmpty())
        return err("bad_args", QStringLiteral("id required — an item id from market_search or market_resolve"));
    const QVariantMap item = client->itemById(id);
    if (item.contains(QStringLiteral("downloadable")) && !item.value(QStringLiteral("downloadable")).toBool())
        return err("bad_args", QStringLiteral("Item %1 is not downloadable").arg(id));
    const QVariantMap running = client->downloadInfo(id);
    if (!jobFinished(running))
        return err("conflict", QStringLiteral("Item %1 is already downloading").arg(id));

    const QString dir = args.value(QStringLiteral("dir")).toString();
    client->download(id, args.value(QStringLiteral("variant")).toString(),
                     dir.isEmpty() ? QUrl() : QUrl::fromLocalFile(dir),
                     item.value(QStringLiteral("title")).toString(),
                     item.value(QStringLiteral("type")).toString());
    if (client->downloadInfo(id).isEmpty())
        return err("market_error", QStringLiteral("The download could not be started"));

    const int waitSeconds = qBound(0, args.value(QStringLiteral("wait")).toInt(0), kMaxDownloadWaitSeconds);
    if (waitSeconds > 0)
        waitUntil([client, id] { return jobFinished(client->downloadInfo(id)); }, waitSeconds * 1000);
    QVariantMap info;
    for (const QVariant &row : client->downloads()) {
        if (row.toMap().value(QStringLiteral("itemId")).toString() == id)
            info = row.toMap();
    }
    QJsonObject out = compactJob(info);
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("started"), true);
    return out;
}

QJsonObject marketDownloads(MarketClient *client, bool clear)
{
    if (!client || !client->configured())
        return err("market_unavailable", QStringLiteral("This build has no marketplace service configured"));
    if (clear)
        client->clearFinishedDownloads();
    QJsonArray jobs;
    for (const QVariant &row : client->downloads())
        jobs.append(compactJob(row.toMap()));
    return ok({{QStringLiteral("jobs"), jobs},
               {QStringLiteral("active"), client->activeDownloadCount()},
               {QStringLiteral("n"), jobs.size()}});
}

QJsonObject marketCancelDownload(MarketClient *client, const QString &id)
{
    if (!client || !client->configured())
        return err("market_unavailable", QStringLiteral("This build has no marketplace service configured"));
    const QVariantMap info = client->downloadInfo(id);
    if (info.isEmpty())
        return err("not_found", QStringLiteral("No download for item %1").arg(id));
    if (jobFinished(info))
        return err("conflict", QStringLiteral("Download %1 already finished (%2)")
                                   .arg(id, info.value(QStringLiteral("status")).toString()));
    client->cancelDownload(id);
    return ok({{QStringLiteral("id"), id}, {QStringLiteral("cancelled"), true}});
}

} // namespace drift::mcp
