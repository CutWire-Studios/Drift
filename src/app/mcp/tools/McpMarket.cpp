#include "app/mcp/tools/McpMarket.h"
#include "app/mcp/McpDetail.h"
#include "app/mcp/protocol/McpJson.h"
#include "app/online/assets/DriftAssetStore.h"
#include "app/online/assets/SfxLibrary.h"
#include "app/online/downloads/DownloadCenter.h"
#include "app/online/marketplace/CommunityInstaller.h"
#include "app/online/marketplace/MarketInstallRouter.h"
#include "app/online/marketplace/MarketSourceBrowser.h"
#include "app/online/marketplace/MarketTransport.h"
#include "app/online/marketplace/MarketplaceManifest.h"
#include "app/online/marketplace/StockInstaller.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QPainter>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>

using drift::market::Call;
using drift::market::MarketTransport;
using drift::market::Problem;
using drift::market::Query;
using drift::market::Response;

namespace drift::mcp {
namespace {

constexpr int kSearchWaitMs = 30000;
constexpr int kItemWaitMs = 20000;
constexpr int kPreviewWaitMs = 30000;
constexpr int kRefreshWaitMs = 15000;
constexpr int kMaxCachedItems = 200;
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

bool awaitCall(Call *call, Response *out, int timeoutMs)
{
    const auto finished = std::make_shared<bool>(false);
    const auto reply = std::make_shared<Response>();
    QPointer<Call> guard(call);
    QObject::connect(call, &Call::finished, call, [finished, reply](const Response &response) {
        *reply = response;
        *finished = true;
    });
    if (!waitUntil([finished] { return *finished; }, timeoutMs)) {
        if (guard)
            guard->abort();
        return false;
    }
    *out = *reply;
    return true;
}

QJsonObject unavailable()
{
    return err("market_unavailable", QStringLiteral("This build has no marketplace service configured"));
}

QJsonObject gate(const McpMarketContext &ctx)
{
    return ctx.valid() && ctx.stock->configured() ? QJsonObject() : unavailable();
}

QJsonObject problemError(const QString &code, const QString &message)
{
    QJsonObject out{{QStringLiteral("ok"), false},
                    {QStringLiteral("error"), code.isEmpty() ? QStringLiteral("market_error") : code}};
    if (!message.isEmpty())
        out.insert(QStringLiteral("detail"), message);
    return out;
}

QJsonObject problemError(const Problem &problem)
{
    return problemError(problem.code, problem.message);
}

QString encoded(const QString &part)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(part));
}

struct ItemCache
{
    QHash<QString, QVariantMap> items;
    QStringList order;
};

ItemCache &itemCache()
{
    static ItemCache cache;
    return cache;
}

QString itemKey(const QString &source, const QString &id)
{
    return source + QLatin1Char('\n') + id;
}

void remember(const QVariantMap &item)
{
    ItemCache &cache = itemCache();
    const QString key = itemKey(item.value(QStringLiteral("source")).toString(),
                                item.value(QStringLiteral("id")).toString());
    cache.order.removeOne(key);
    cache.order.append(key);
    cache.items.insert(key, item);
    while (cache.order.size() > kMaxCachedItems)
        cache.items.remove(cache.order.takeFirst());
}

QVariantMap fetchItem(const QString &source, const QString &id, QJsonObject *error)
{
    Call *call = MarketTransport::instance()->send(
        "GET", QStringLiteral("/marketplace/sources/") + encoded(source) + QStringLiteral("/items/") + encoded(id),
        Query());
    Response response;
    if (!awaitCall(call, &response, kItemWaitMs)) {
        *error = err("market_error", QStringLiteral("The marketplace did not answer in time"));
        return {};
    }
    if (!response.ok()) {
        *error = problemError(response.problem());
        return {};
    }
    QVariantMap item = QJsonDocument::fromJson(response.body).object().toVariantMap();
    if (item.isEmpty()) {
        *error = err("not_found", QStringLiteral("Item %1 not found in %2").arg(id, source));
        return {};
    }
    item.insert(QStringLiteral("source"), source);
    if (item.value(QStringLiteral("id")).toString().isEmpty())
        item.insert(QStringLiteral("id"), id);
    remember(item);
    return item;
}

QVariantMap lookupItem(const QString &source, const QString &id, QJsonObject *error)
{
    const QVariantMap cached = itemCache().items.value(itemKey(source, id));
    return cached.isEmpty() ? fetchItem(source, id, error) : cached;
}

QJsonValue json(const QVariant &value)
{
    return QJsonValue::fromVariant(value);
}

QString installState(const McpMarketContext &ctx, const QVariantMap &item)
{
    return ctx.router->installState(item);
}

QJsonObject compactItem(const McpMarketContext &ctx, const QVariantMap &item)
{
    const QVariantMap extra = item.value(QStringLiteral("extra")).toMap();
    QJsonObject out{{QStringLiteral("id"), item.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("source"), item.value(QStringLiteral("source")).toString()},
                    {QStringLiteral("title"), item.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("kind"), item.value(QStringLiteral("mediaKind")).toString()},
                    {QStringLiteral("installed"), installState(ctx, item) == QLatin1String("installed")}};
    if (item.value(QStringLiteral("durationMs")).toDouble() > 0)
        out.insert(QStringLiteral("dur"), item.value(QStringLiteral("durationMs")).toDouble() / 1000.0);
    if (item.value(QStringLiteral("width")).toInt() > 0) {
        out.insert(QStringLiteral("w"), item.value(QStringLiteral("width")).toInt());
        out.insert(QStringLiteral("h"), item.value(QStringLiteral("height")).toInt());
    }
    if (extra.contains(QStringLiteral("downloadable")) && !extra.value(QStringLiteral("downloadable")).toBool())
        out.insert(QStringLiteral("downloadable"), false);
    const QString provider = extra.value(QStringLiteral("provider")).toString();
    if (!provider.isEmpty())
        out.insert(QStringLiteral("provider"), provider);
    const QString by = extra.value(QStringLiteral("creator")).toString();
    if (!by.isEmpty())
        out.insert(QStringLiteral("by"), by);
    out.insert(QStringLiteral("tags"), QJsonArray::fromStringList(item.value(QStringLiteral("tags")).toStringList()));
    const QString category = item.value(QStringLiteral("category")).toString();
    if (!category.isEmpty())
        out.insert(QStringLiteral("category"), category);
    const QString thumb = item.value(QStringLiteral("thumbUrl")).toString();
    if (!thumb.isEmpty())
        out.insert(QStringLiteral("thumb"), thumb);
    const QString preview = item.value(QStringLiteral("previewUrl")).toString();
    if (!preview.isEmpty())
        out.insert(QStringLiteral("preview"), preview);
    out.insert(QStringLiteral("variants"), int(item.value(QStringLiteral("variants")).toList().size()));
    return out;
}

QJsonObject fullItem(const McpMarketContext &ctx, const QVariantMap &item)
{
    QJsonObject out = compactItem(ctx, item);
    QJsonArray rows;
    for (const QVariant &v : item.value(QStringLiteral("variants")).toList()) {
        const QVariantMap variant = v.toMap();
        QJsonObject row{{QStringLiteral("id"), variant.value(QStringLiteral("id")).toString()},
                        {QStringLiteral("label"), variant.value(QStringLiteral("label")).toString()}};
        if (variant.value(QStringLiteral("width")).toInt() > 0) {
            row.insert(QStringLiteral("w"), variant.value(QStringLiteral("width")).toInt());
            row.insert(QStringLiteral("h"), variant.value(QStringLiteral("height")).toInt());
        }
        rows.append(row);
    }
    out.insert(QStringLiteral("variants"), rows);
    out.insert(QStringLiteral("install_state"), installState(ctx, item));
    const auto copy = [&](const char *from, const char *to) {
        const QVariant value = item.value(QString::fromLatin1(from));
        if (value.isValid() && !value.toString().isEmpty())
            out.insert(QString::fromLatin1(to), json(value));
    };
    copy("description", "description");
    copy("webUrl", "web_url");
    copy("minAppVersion", "min_app_version");
    const QVariantMap license = item.value(QStringLiteral("license")).toMap();
    if (!license.isEmpty())
        out.insert(QStringLiteral("license"), QJsonObject::fromVariantMap(license));
    const QVariantMap extra = item.value(QStringLiteral("extra")).toMap();
    if (!extra.isEmpty())
        out.insert(QStringLiteral("extra"), QJsonObject::fromVariantMap(extra));
    return out;
}

QJsonObject sourceSummary(const QString &id, const QVariantMap &source, bool full)
{
    if (full) {
        QJsonObject out = QJsonObject::fromVariantMap(source);
        out.insert(QStringLiteral("id"), id);
        return out;
    }
    QJsonObject out{{QStringLiteral("id"), id}};
    for (const char *key : {"kind", "label", "mediaKind", "searchable", "canResolve", "categories", "providers", "sorts"}) {
        if (source.contains(QString::fromLatin1(key)))
            out.insert(QString::fromLatin1(key), json(source.value(QString::fromLatin1(key))));
    }
    return out;
}

QJsonObject compactJob(const QVariantMap &job)
{
    QJsonObject out{{QStringLiteral("id"), job.value(QStringLiteral("id")).toString()},
                    {QStringLiteral("kind"), job.value(QStringLiteral("kind")).toString()},
                    {QStringLiteral("title"), job.value(QStringLiteral("title")).toString()},
                    {QStringLiteral("status"), job.value(QStringLiteral("status")).toString()},
                    {QStringLiteral("phase"), job.value(QStringLiteral("phase")).toString()},
                    {QStringLiteral("progress"), job.value(QStringLiteral("progress")).toDouble()},
                    {QStringLiteral("bytes"), job.value(QStringLiteral("bytesReceived")).toLongLong()},
                    {QStringLiteral("total"), job.value(QStringLiteral("bytesTotal")).toLongLong()},
                    {QStringLiteral("speed"), job.value(QStringLiteral("speed")).toDouble()},
                    {QStringLiteral("retryable"), job.value(QStringLiteral("retryable")).toBool()},
                    {QStringLiteral("cancellable"), job.value(QStringLiteral("cancellable")).toBool()}};
    const QString error = job.value(QStringLiteral("error")).toString();
    if (!error.isEmpty()) {
        out.insert(QStringLiteral("error"), error);
        const QString code = job.value(QStringLiteral("errorCode")).toString();
        if (!code.isEmpty())
            out.insert(QStringLiteral("error_code"), code);
    }
    const QVariantMap result = job.value(QStringLiteral("result")).toMap();
    if (!result.isEmpty())
        out.insert(QStringLiteral("result"), QJsonObject::fromVariantMap(result));
    return out;
}

MarketSourceBrowser *browserFor(const QString &source)
{
    static QHash<QString, MarketSourceBrowser *> browsers;
    MarketSourceBrowser *&browser = browsers[source];
    if (!browser) {
        browser = new MarketSourceBrowser(QCoreApplication::instance());
        browser->setSource(source);
    }
    return browser;
}

QString &lastSource()
{
    static QString source;
    return source;
}

QString stringArg(const QJsonObject &args, const char *key)
{
    return args.value(QString::fromLatin1(key)).toString().trimmed();
}

// A section's own binding (sort, category, filters, page size) supplies the defaults a listing
// of that section would use; whatever the caller passed wins.
QJsonObject applySection(const McpMarketContext &ctx, const QString &ref, QJsonObject args, QJsonObject *error)
{
    const int slash = ref.indexOf(QLatin1Char('/'));
    const QVariantMap page = slash > 0 ? ctx.manifest->page(ref.left(slash)) : QVariantMap();
    QVariantMap section;
    for (const QVariant &row : page.value(QStringLiteral("sections")).toList()) {
        if (row.toMap().value(QStringLiteral("id")).toString() == ref.mid(slash + 1))
            section = row.toMap();
    }
    if (section.isEmpty() || !section.value(QStringLiteral("source")).toMap().contains(QStringLiteral("id"))) {
        *error = err("not_found", QStringLiteral("No source-bound section %1 — see market_status({layout:true})").arg(ref));
        return {};
    }
    const QVariantMap binding = section.value(QStringLiteral("source")).toMap();
    const auto fill = [&](const char *key) {
        const QString value = binding.value(QString::fromLatin1(key)).toString();
        if (!args.contains(QString::fromLatin1(key)) && !value.isEmpty())
            args.insert(QString::fromLatin1(key), value);
    };
    fill("category");
    fill("sort");
    fill("period");
    fill("provider");
    fill("q");
    if (!args.contains(QStringLiteral("source")))
        args.insert(QStringLiteral("source"), binding.value(QStringLiteral("id")).toString());
    if (!args.contains(QStringLiteral("limit")) && section.value(QStringLiteral("limit")).toInt() > 0)
        args.insert(QStringLiteral("limit"), section.value(QStringLiteral("limit")).toInt());
    QVariantMap filters = binding.value(QStringLiteral("filters")).toMap();
    const QVariantMap given = args.value(QStringLiteral("filters")).toObject().toVariantMap();
    for (auto it = given.cbegin(); it != given.cend(); ++it)
        filters.insert(it.key(), it.value());
    if (!filters.isEmpty())
        args.insert(QStringLiteral("filters"), QJsonObject::fromVariantMap(filters));
    return args;
}

QString driftAssetKey(const QVariantMap &item, const QString &variantId)
{
    const QString id = item.value(QStringLiteral("id")).toString();
    const QVariantList variants = item.value(QStringLiteral("variants")).toList();
    if (variantId.isEmpty() || variants.isEmpty()
        || variants.first().toMap().value(QStringLiteral("id")).toString() == variantId)
        return id;
    return id + QStringLiteral("--") + variantId;
}

QString installJobId(const QString &family, const QVariantMap &item, const QString &variantId)
{
    const QString source = item.value(QStringLiteral("source")).toString();
    const QString id = item.value(QStringLiteral("id")).toString();
    if (family == QLatin1String("stock"))
        return StockInstaller::jobId(source, id);
    if (family == QLatin1String("community"))
        return CommunityInstaller::jobId(source, id);
    if (family == QLatin1String("sfx"))
        return SfxLibrary::jobId(id);
    return DriftAssetStore::jobId(driftAssetKey(item, variantId));
}

QJsonObject installedResult(const QString &family, const QVariantMap &item, const QVariantMap &raw)
{
    QJsonObject out;
    if (family == QLatin1String("community")) {
        const QString kind = raw.value(QStringLiteral("kind")).toString();
        const QString id = raw.value(QStringLiteral("id")).toString();
        const char *key = kind == QLatin1String("theme")             ? "theme"
                          : kind == QLatin1String("look")            ? "look"
                          : kind == QLatin1String("effect-template") ? "template"
                                                                      : "effect";
        out.insert(QString::fromLatin1(key), id);
        return out;
    }
    if (family == QLatin1String("drift-assets")
        && item.value(QStringLiteral("extra")).toMap().value(QStringLiteral("kind")).toString() == QLatin1String("face-prop")) {
        out.insert(QStringLiteral("prop"), item.value(QStringLiteral("id")).toString());
        return out;
    }
    const QString asset = raw.value(QStringLiteral("assetId")).toString();
    if (!asset.isEmpty())
        out.insert(QStringLiteral("asset"), asset);
    const QString path = raw.value(QStringLiteral("path")).toString();
    if (!path.isEmpty())
        out.insert(QStringLiteral("path"), path);
    return out;
}

QString previewCacheDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/marketplace/previews");
}

QString cachedPreview(const QString &url, QString *mime, QJsonObject *error)
{
    const QString dir = previewCacheDir();
    const QString base = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex());
    const QStringList hits = QDir(dir).entryList({base + QStringLiteral(".*")}, QDir::Files);
    if (!hits.isEmpty()) {
        const QString path = dir + QLatin1Char('/') + hits.first();
        *mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
        return path;
    }

    QPointer<QNetworkReply> reply = MarketTransport::instance()->fetchSigned(QUrl(url));
    if (!waitUntil([reply] { return !reply || reply->isFinished(); }, kPreviewWaitMs)) {
        if (reply) {
            reply->abort();
            reply->deleteLater();
        }
        *error = err("market_error", QStringLiteral("The preview did not download in time"));
        return {};
    }
    if (!reply) {
        *error = err("market_error", QStringLiteral("The preview request was dropped"));
        return {};
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || status >= 400) {
        *error = err("market_error", QStringLiteral("Could not fetch the preview: %1").arg(reply->errorString()));
        reply->deleteLater();
        return {};
    }
    const QByteArray body = reply->readAll();
    *mime = QString::fromLatin1(reply->header(QNetworkRequest::ContentTypeHeader).toByteArray())
                .section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    reply->deleteLater();
    QString suffix = QMimeDatabase().mimeTypeForName(*mime).preferredSuffix();
    if (suffix.isEmpty())
        suffix = QFileInfo(QUrl(url).path()).suffix();
    if (suffix.isEmpty())
        suffix = QStringLiteral("bin");
    if (mime->isEmpty() || *mime == QLatin1String("application/octet-stream"))
        *mime = QMimeDatabase().mimeTypeForFile(QStringLiteral("x.") + suffix, QMimeDatabase::MatchExtension).name();

    const QString path = dir + QLatin1Char('/') + base + QLatin1Char('.') + suffix;
    QDir().mkpath(dir);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(body) != body.size() || !out.commit()) {
        *error = err("io_error", QStringLiteral("Could not write %1").arg(path));
        return {};
    }
    return path;
}

} // namespace

QJsonObject marketStatus(const McpMarketContext &ctx, const QJsonObject &args)
{
    if (!ctx.valid() || !ctx.stock->configured()) {
        QJsonObject out = ok({{QStringLiteral("configured"), false}});
        out.insert(QStringLiteral("hint"), QStringLiteral("This build has no marketplace service configured"));
        return out;
    }
    if (args.value(QStringLiteral("refresh")).toBool()) {
        ctx.manifest->refresh();
        waitUntil([&ctx] { return !ctx.manifest->loading(); }, kRefreshWaitMs);
    }

    const QString only = stringArg(args, "source");
    const QVariantMap sources = ctx.manifest->sources();
    if (!only.isEmpty() && !sources.contains(only))
        return err("not_found", QStringLiteral("Unknown source %1").arg(only));
    QJsonArray rows;
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        if (only.isEmpty() || it.key() == only)
            rows.append(sourceSummary(it.key(), it.value().toMap(), !only.isEmpty()));
    }
    QJsonObject out = ok({{QStringLiteral("configured"), true},
                          {QStringLiteral("offline"), ctx.manifest->offline()},
                          {QStringLiteral("revision"), ctx.manifest->revision()},
                          {QStringLiteral("features"), QJsonObject::fromVariantMap(ctx.manifest->features())},
                          {QStringLiteral("sources"), rows}});
    if (!ctx.manifest->error().isEmpty())
        out.insert(QStringLiteral("error"), ctx.manifest->error());
    if (args.value(QStringLiteral("layout")).toBool()) {
        QJsonArray pages;
        for (const QVariant &row : ctx.manifest->pages()) {
            const QVariantMap page = row.toMap();
            QJsonArray sections;
            for (const QVariant &s : page.value(QStringLiteral("sections")).toList()) {
                const QVariantMap section = s.toMap();
                QJsonObject entry{{QStringLiteral("id"), section.value(QStringLiteral("id")).toString()},
                                  {QStringLiteral("type"), section.value(QStringLiteral("type")).toString()}};
                if (!section.value(QStringLiteral("title")).toString().isEmpty())
                    entry.insert(QStringLiteral("title"), section.value(QStringLiteral("title")).toString());
                const QString source = section.value(QStringLiteral("source")).toMap().value(QStringLiteral("id")).toString();
                if (!source.isEmpty())
                    entry.insert(QStringLiteral("source"), source);
                sections.append(entry);
            }
            pages.append(QJsonObject{{QStringLiteral("id"), page.value(QStringLiteral("id")).toString()},
                                     {QStringLiteral("title"), page.value(QStringLiteral("title")).toString()},
                                     {QStringLiteral("sections"), sections}});
        }
        out.insert(QStringLiteral("pages"), pages);
    }
    return out;
}

QJsonObject marketSearch(const McpMarketContext &ctx, const QJsonObject &input)
{
    if (const QJsonObject blocked = gate(ctx); !blocked.isEmpty())
        return blocked;

    QJsonObject args = input;
    const QString section = stringArg(args, "section");
    if (!section.isEmpty()) {
        QJsonObject error;
        args = applySection(ctx, section, args, &error);
        if (!error.isEmpty())
            return error;
    }
    const bool more = args.value(QStringLiteral("more")).toBool();
    QString source = stringArg(args, "source");
    if (source.isEmpty() && !stringArg(args, "type").isEmpty()) {
        source = QStringLiteral("stock.") + stringArg(args, "type");
        if (ctx.manifest->source(source).isEmpty())
            return err("bad_args", QStringLiteral("type %1 is not a stock source — use source from market_status")
                                       .arg(stringArg(args, "type")));
    }
    if (source.isEmpty() && more)
        source = lastSource();
    if (source.isEmpty())
        return err("bad_args", QStringLiteral("source or section required — see market_status"));
    if (ctx.manifest->source(source).isEmpty())
        return err("not_found", QStringLiteral("Unknown source %1 — see market_status").arg(source));

    MarketSourceBrowser *list = browserFor(source);
    int offset = 0;
    if (more) {
        if (!list->hasMore())
            return err("not_found", QStringLiteral("No more results for the last %1 search").arg(source));
        offset = list->items().size();
        list->loadMore();
    } else {
        list->setCategory(stringArg(args, "category"));
        list->setQuery(stringArg(args, "q"));
        list->setSort(stringArg(args, "sort"));
        list->setPeriod(stringArg(args, "period"));
        list->setProvider(stringArg(args, "provider"));
        list->setFilters(args.value(QStringLiteral("filters")).toObject().toVariantMap());
        list->setLimit(qBound(1, args.value(QStringLiteral("limit")).toInt(24), 50));
        const QString cursor = stringArg(args, "cursor");
        if (cursor.isEmpty())
            list->reload();
        else
            list->loadFrom(cursor);
    }
    if (!waitUntil([list] { return !list->loading(); }, kSearchWaitMs)) {
        list->cancel();
        return err("market_error", QStringLiteral("The marketplace did not answer in time"));
    }
    if (!list->error().isEmpty())
        return problemError(list->errorCode(), list->error());

    lastSource() = source;
    const QVariantList rows = list->items();
    QJsonArray items;
    for (int i = offset; i < rows.size(); ++i) {
        QVariantMap row = rows.at(i).toMap();
        row.insert(QStringLiteral("source"), source);
        remember(row);
        items.append(compactItem(ctx, row));
    }
    QJsonObject out = ok({{QStringLiteral("source"), source},
                          {QStringLiteral("items"), items},
                          {QStringLiteral("n"), items.size()},
                          {QStringLiteral("offset"), offset},
                          {QStringLiteral("has_more"), list->hasMore()}});
    if (list->hasMore())
        out.insert(QStringLiteral("next_cursor"), list->nextCursor());
    if (!list->quota().isEmpty())
        out.insert(QStringLiteral("quota"), QJsonObject::fromVariantMap(list->quota()));
    if (!list->warnings().isEmpty())
        out.insert(QStringLiteral("warnings"), QJsonArray::fromStringList(list->warnings()));
    return out;
}

QJsonObject marketItem(const McpMarketContext &ctx, const QJsonObject &args)
{
    if (const QJsonObject blocked = gate(ctx); !blocked.isEmpty())
        return blocked;
    const QString source = stringArg(args, "source");
    const QString id = stringArg(args, "id");
    if (source.isEmpty() || id.isEmpty())
        return err("bad_args", QStringLiteral("source and id required"));
    QJsonObject error;
    const QVariantMap item = fetchItem(source, id, &error);
    if (item.isEmpty())
        return error;
    return ok({{QStringLiteral("item"), fullItem(ctx, item)}});
}

QJsonObject marketResolve(const McpMarketContext &ctx, const QJsonObject &args)
{
    if (const QJsonObject blocked = gate(ctx); !blocked.isEmpty())
        return blocked;
    const QString url = stringArg(args, "url");
    if (url.isEmpty())
        return err("bad_args", QStringLiteral("url required"));
    const int waitMs = qBound(1, args.value(QStringLiteral("wait")).toInt(90), 300) * 1000;
    ctx.stock->resolveUrl(url);
    if (!waitUntil([&ctx] { return !ctx.stock->resolving(); }, waitMs)) {
        ctx.stock->cancelResolve();
        return err("market_error", QStringLiteral("Resolving that link did not finish in time"));
    }
    if (!ctx.stock->resolveError().isEmpty())
        return problemError(ctx.stock->resolveErrorCode(), ctx.stock->resolveError());
    QVariantMap item = ctx.stock->resolvedItem();
    if (item.isEmpty())
        return err("not_found", QStringLiteral("Nothing could be resolved from that link"));
    remember(item);
    return ok({{QStringLiteral("item"), fullItem(ctx, item)}});
}

QJsonObject marketPreview(const McpMarketContext &ctx, const QJsonObject &args)
{
    if (const QJsonObject blocked = gate(ctx); !blocked.isEmpty())
        return blocked;
    const QString source = stringArg(args, "source");
    const QString id = stringArg(args, "id");
    if (source.isEmpty() || id.isEmpty())
        return err("bad_args", QStringLiteral("source and id required"));
    const QString what = args.value(QStringLiteral("what")).toString(QStringLiteral("thumb"));
    if (what != QLatin1String("thumb") && what != QLatin1String("preview"))
        return err("bad_args", QStringLiteral("what must be thumb or preview"));

    QJsonObject error;
    const QVariantMap item = lookupItem(source, id, &error);
    if (item.isEmpty())
        return error;
    const QString url = item.value(QString::fromLatin1(what == QLatin1String("thumb") ? "thumbUrl" : "previewUrl")).toString();
    if (url.isEmpty())
        return err("not_found", QStringLiteral("Item %1 has no %2").arg(id, what));

    QString mime;
    const QString path = cachedPreview(url, &mime, &error);
    if (path.isEmpty())
        return error;
    QJsonObject meta{{QStringLiteral("source"), source}, {QStringLiteral("id"), id},
                     {QStringLiteral("what"), what},     {QStringLiteral("path"), path},
                     {QStringLiteral("mime"), mime}};

    if (!mime.startsWith(QLatin1String("image/"))) {
        if (item.value(QStringLiteral("durationMs")).toDouble() > 0)
            meta.insert(QStringLiteral("dur"), item.value(QStringLiteral("durationMs")).toDouble() / 1000.0);
        return ok(meta);
    }
    const QImage image(path);
    if (image.isNull())
        return err("market_error", QStringLiteral("The %1 is not an image Drift can decode (%2)").arg(what, mime));
    meta.insert(QStringLiteral("w"), image.width());
    meta.insert(QStringLiteral("h"), image.height());
    if (!args.value(QStringLiteral("image")).toBool(true))
        return ok(meta);

    const int maxPx = qBound(128, args.value(QStringLiteral("max")).toInt(512), 1024);
    QImage scaled = image;
    if (qMax(image.width(), image.height()) > maxPx)
        scaled = image.scaled(maxPx, maxPx, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage flat(scaled.size(), QImage::Format_RGB32);
    flat.fill(Qt::white);
    QPainter painter(&flat);
    painter.drawImage(0, 0, scaled);
    painter.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!flat.save(&buffer, "JPEG", 80))
        return err("market_error", QStringLiteral("Could not encode the preview"));
    return drift::mcpdetail::imageResult(meta, bytes, QStringLiteral("image/jpeg"));
}

QJsonObject marketInstall(const McpMarketContext &ctx, const QJsonObject &args)
{
    if (const QJsonObject blocked = gate(ctx); !blocked.isEmpty())
        return blocked;
    const QString source = stringArg(args, "source");
    const QString id = stringArg(args, "id");
    const QString variant = stringArg(args, "variant");
    if (source.isEmpty() || id.isEmpty())
        return err("bad_args", QStringLiteral("source and id required — an item from market_search or market_resolve"));
    const QString family = MarketInstallRouter::family(source);
    if (family.isEmpty())
        return err("bad_args", QStringLiteral("Source %1 cannot be installed").arg(source));

    QString dir = stringArg(args, "dir");
    if (dir.startsWith(QLatin1String("file://")))
        dir = QUrl(dir).toLocalFile();
    if (!dir.isEmpty() && family != QLatin1String("stock"))
        return err("bad_args", QStringLiteral("dir only applies to stock sources"));
    if (!dir.isEmpty() && !QDir::isAbsolutePath(dir))
        return err("bad_args", QStringLiteral("dir must be an absolute path"));

    QJsonObject error;
    const QVariantMap item = lookupItem(source, id, &error);
    if (item.isEmpty())
        return error;
    const QVariantMap extra = item.value(QStringLiteral("extra")).toMap();
    if (extra.contains(QStringLiteral("downloadable")) && !extra.value(QStringLiteral("downloadable")).toBool())
        return err("bad_args", QStringLiteral("Item %1 is not downloadable").arg(id));

    DownloadCenter *center = ctx.downloads;
    const QString jobId = installJobId(family, item, variant);
    if (center->contains(jobId) && !DownloadCenter::isFinished(center->job(jobId).status))
        return err("conflict", QStringLiteral("Item %1 is already installing").arg(id));

    ctx.router->install(item, variant, dir);
    if (!center->contains(jobId) && ctx.router->installState(item, variant) != QLatin1String("installed"))
        return err("market_error", QStringLiteral("The install could not be started"));

    const int waitSeconds = qBound(0, args.value(QStringLiteral("wait")).toInt(0), kMaxDownloadWaitSeconds);
    if (waitSeconds > 0) {
        waitUntil([center, jobId] { return !center->contains(jobId) || DownloadCenter::isFinished(center->job(jobId).status); },
                  waitSeconds * 1000);
    }
    const QVariantMap info = center->jobMap(jobId);
    const QJsonObject job = info.isEmpty() ? QJsonObject() : compactJob(info);
    if (waitSeconds == 0)
        return ok({{QStringLiteral("job"), job}, {QStringLiteral("installed"), info.isEmpty()}});
    const QString status = info.value(QStringLiteral("status")).toString();
    if (!info.isEmpty() && !DownloadCenter::isFinished(status)) {
        QJsonObject out = err("install_timeout", QStringLiteral("Still %1 after %2 s — poll market_downloads").arg(status).arg(waitSeconds));
        out.insert(QStringLiteral("job"), job);
        return out;
    }
    if (status == QLatin1String("failed") || status == QLatin1String("cancelled")) {
        const QString code = info.value(QStringLiteral("errorCode")).toString();
        QJsonObject out = problemError(status == QLatin1String("cancelled") ? QStringLiteral("cancelled")
                                       : code.isEmpty()                      ? QStringLiteral("download_failed")
                                                                             : code,
                                       info.value(QStringLiteral("errorMessage")).toString());
        out.insert(QStringLiteral("job"), job);
        return out;
    }
    QVariantMap raw = info.value(QStringLiteral("result")).toMap();
    if (raw.isEmpty())
        raw = ctx.router->installResult(item, variant);
    QJsonObject out = ok({{QStringLiteral("result"), installedResult(family, item, raw)}});
    if (!job.isEmpty())
        out.insert(QStringLiteral("job"), job);
    return out;
}

QJsonObject marketDownloads(const McpMarketContext &ctx, const QJsonObject &args)
{
    if (!ctx.valid())
        return unavailable();
    if (args.value(QStringLiteral("clear")).toBool())
        ctx.downloads->clearFinished();
    const QString kind = stringArg(args, "kind");
    QJsonArray jobs;
    int active = 0;
    for (const QVariant &row : ctx.downloads->jobs()) {
        const QVariantMap job = row.toMap();
        if (!kind.isEmpty() && job.value(QStringLiteral("kind")).toString() != kind)
            continue;
        if (!job.value(QStringLiteral("finished")).toBool())
            ++active;
        jobs.append(compactJob(job));
    }
    return ok({{QStringLiteral("active"), active}, {QStringLiteral("jobs"), jobs}, {QStringLiteral("n"), jobs.size()}});
}

QJsonObject marketCancelDownload(const McpMarketContext &ctx, const QString &id)
{
    if (!ctx.valid())
        return unavailable();
    if (!ctx.downloads->contains(id))
        return err("not_found", QStringLiteral("No download %1 — ids come from market_downloads").arg(id));
    const QVariantMap info = ctx.downloads->jobMap(id);
    if (info.value(QStringLiteral("finished")).toBool())
        return err("conflict", QStringLiteral("Download %1 already finished (%2)")
                                   .arg(id, info.value(QStringLiteral("status")).toString()));
    if (!info.value(QStringLiteral("cancellable")).toBool())
        return err("conflict", QStringLiteral("Download %1 cannot be cancelled now").arg(id));
    ctx.downloads->cancel(id);
    return ok({{QStringLiteral("job"), compactJob(ctx.downloads->jobMap(id))}});
}

QJsonObject marketRetryDownload(const McpMarketContext &ctx, const QString &id)
{
    if (!ctx.valid())
        return unavailable();
    if (!ctx.downloads->contains(id))
        return err("not_found", QStringLiteral("No download %1 — ids come from market_downloads").arg(id));
    if (!ctx.downloads->jobMap(id).value(QStringLiteral("retryable")).toBool())
        return err("conflict", QStringLiteral("Download %1 is not failed or cancelled, or cannot be retried").arg(id));
    ctx.downloads->retry(id);
    return ok({{QStringLiteral("job"), compactJob(ctx.downloads->jobMap(id))}});
}

QJsonObject marketOpen(const McpMarketContext &ctx, const QJsonObject &args)
{
    if (!ctx.valid())
        return unavailable();
    QString page = stringArg(args, "page");
    const QString tab = stringArg(args, "tab");
    QString source;
    QString itemId;
    if (args.contains(QStringLiteral("item"))) {
        const QJsonObject item = args.value(QStringLiteral("item")).toObject();
        source = item.value(QStringLiteral("source")).toString();
        itemId = item.value(QStringLiteral("id")).toString();
        if (source.isEmpty() || itemId.isEmpty())
            return err("bad_args", QStringLiteral("item needs source and id"));
        page = QStringLiteral("item");
    } else if (!page.isEmpty() && ctx.manifest->page(page).isEmpty()) {
        return err("not_found", QStringLiteral("No page %1 — see market_status({layout:true})").arg(page));
    }
    ctx.manifest->requestOpen(page, tab, source, itemId);
    return ok({{QStringLiteral("opened"), true}});
}

} // namespace drift::mcp
