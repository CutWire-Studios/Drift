#include "MarketClient.h"

#include "AssetLibrary.h"
#include "MarketEndpoint.h"
#include "MarketIdentity.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

#include <utility>

using namespace drift::market;

namespace {

constexpr int kApiTimeoutMs = 20000;
constexpr int kFileTimeoutMs = 10 * 60 * 1000;
constexpr int kPollIntervalMs = 1000;
constexpr int kSearchLimit = 30;
constexpr qint64 kTokenRefreshSkewSecs = 60;

QString settingsKey(const char *name)
{
    return QLatin1String("market/") + QLatin1String(name);
}

QString downloadsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/marketplace/downloads");
}

QString sanitizeFileName(QString name)
{
    name = QFileInfo(name).fileName();
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (name == QLatin1String(".") || name == QLatin1String("..") || name.isEmpty())
        name = QStringLiteral("download");
    return name;
}

QString extensionForMime(const QString &mime)
{
    if (mime.contains(QLatin1String("mp4")))
        return QStringLiteral(".mp4");
    if (mime.contains(QLatin1String("wav")))
        return QStringLiteral(".wav");
    if (mime.contains(QLatin1String("aac")))
        return QStringLiteral(".m4a");
    if (mime.contains(QLatin1String("png")))
        return QStringLiteral(".png");
    if (mime.contains(QLatin1String("webp")))
        return QStringLiteral(".webp");
    if (mime.contains(QLatin1String("jpeg")) || mime.contains(QLatin1String("jpg")))
        return QStringLiteral(".jpg");
    if (mime.contains(QLatin1String("mpeg")))
        return QStringLiteral(".mp3");
    return {};
}

QVariantMap objectToMap(const QJsonObject &object)
{
    return object.toVariantMap();
}

} // namespace

struct MarketClient::Job
{
    QString itemId;
    QString jobId;
    QString status;
    double progress = 0;
    QString phase;
    QString errorCode;
    QString errorMessage;
    QPointer<QNetworkReply> reply;
    bool cancelled = false;
};

MarketClient::MarketClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_pollTimer(new QTimer(this))
{
    auto *cache = new QNetworkDiskCache(this);
    cache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + QStringLiteral("/marketplace"));
    cache->setMaximumCacheSize(256 * 1024 * 1024);
    m_network->setCache(cache);

    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &MarketClient::pollJobs);

    loadStoredAuth();
    if (configured()) {
        QDir().mkpath(downloadsRoot());
        if (!m_refreshToken.isEmpty())
            fetchMe();
    }
}

MarketClient::~MarketClient() = default;

void MarketClient::setAssetLibrary(AssetLibrary *library)
{
    m_library = library;
}

bool MarketClient::configured() const
{
    return marketServiceConfigured();
}

QVariantList MarketClient::providers() const
{
    return activeType().value(QStringLiteral("providers")).toList();
}

QVariantList MarketClient::filters() const
{
    return activeProvider().value(QStringLiteral("filters")).toList();
}

bool MarketClient::hasCapability(const QString &name) const
{
    const QVariantList caps = activeProvider().value(QStringLiteral("capabilities")).toList();
    for (const QVariant &value : caps) {
        if (value.toString() == name)
            return true;
    }
    return false;
}

bool MarketClient::canSearch() const
{
    const QVariantList caps = activeProvider().value(QStringLiteral("capabilities")).toList();
    // Older catalogs omit capabilities; treat that as a normal searchable source.
    if (caps.isEmpty())
        return true;
    return hasCapability(QStringLiteral("search")) || hasCapability(QStringLiteral("featured"));
}

bool MarketClient::canResolve() const
{
    return hasCapability(QStringLiteral("resolve"));
}

void MarketClient::abortInFlightSearch()
{
    if (!m_searchReply)
        return;
    m_searchReply->abort();
    m_searchReply->deleteLater();
    m_searchReply.clear();
}

void MarketClient::setActiveTypeId(const QString &id)
{
    if (id == m_activeTypeId)
        return;
    m_activeTypeId = id;
    const QVariantList list = providers();
    QString providerId;
    if (!list.isEmpty())
        providerId = list.first().toMap().value(QStringLiteral("id")).toString();
    emit activeTypeIdChanged();
    emit providersChanged();
    setActiveProviderId(providerId);
}

void MarketClient::setActiveProviderId(const QString &id)
{
    if (id == m_activeProviderId && !id.isEmpty()) {
        emit providersChanged();
        const QVariantMap q = activeProvider().value(QStringLiteral("quota")).toMap();
        if (q != m_quota) {
            m_quota = q;
            emit quotaChanged();
        }
        return;
    }
    m_activeProviderId = id;
    abortInFlightSearch();
    m_items.clear();
    m_itemIndex.clear();
    m_nextCursor.clear();
    setSearchError({});
    emit activeProviderIdChanged();
    emit providersChanged();
    emit itemsChanged();
    const QVariantMap q = activeProvider().value(QStringLiteral("quota")).toMap();
    if (q != m_quota) {
        m_quota = q;
        emit quotaChanged();
    }
}

void MarketClient::refreshCatalog()
{
    if (!configured()) {
        setCatalogError(tr("Marketplace is not available in this build."));
        return;
    }
    setCatalogLoading(true);
    setCatalogError({});
    QNetworkReply *reply = get(apiUrl(QStringLiteral("/catalog")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setCatalogLoading(false);
        if (reply->error() != QNetworkReply::NoError) {
            QString code;
            setCatalogError(parseProblem(reply->readAll(), reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                                         &code));
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        applyCatalog(root.value(QStringLiteral("types")).toArray());
    });
}

void MarketClient::search(const QString &query, const QVariantMap &filterValues)
{
    m_query = query;
    m_filterValues = filterValues;
    m_nextCursor.clear();
    startSearch(false);
}

void MarketClient::resolveUrl(const QString &url)
{
    if (!configured()) {
        setSearchError(tr("Marketplace is not available in this build."));
        return;
    }
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty())
        return;

    abortInFlightSearch();
    m_nextCursor.clear();

    QJsonObject body;
    body.insert(QStringLiteral("url"), trimmed);
    if (!m_activeTypeId.isEmpty())
        body.insert(QStringLiteral("type"), m_activeTypeId);

    setSearching(true);
    setSearchError({});
    QNetworkReply *reply =
        post(apiUrl(QStringLiteral("/resolve")), QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_searchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_searchReply == reply)
            m_searchReply.clear();
        setSearching(false);
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString code;
            setSearchError(parseProblem(payload, status, &code));
            m_items.clear();
            m_itemIndex.clear();
            emit itemsChanged();
            return;
        }
        applyResolvedItem(QJsonDocument::fromJson(payload).object());
    });
}

void MarketClient::loadMore()
{
    if (m_searching || m_nextCursor.isEmpty())
        return;
    startSearch(true);
}

void MarketClient::download(const QString &itemId, const QString &variantId)
{
    if (!configured() || itemId.isEmpty())
        return;
    if (m_jobs.contains(itemId))
        return;

    auto job = std::make_shared<Job>();
    job->itemId = itemId;
    job->status = QStringLiteral("queued");
    job->phase = tr("Starting…");
    m_jobs.insert(itemId, job);
    bumpDownloads();
    emit downloadProgress(itemId, 0, job->phase);

    QJsonObject body;
    body.insert(QStringLiteral("item_id"), itemId);
    if (!variantId.isEmpty())
        body.insert(QStringLiteral("variant_id"), variantId);

    QNetworkReply *reply = post(apiUrl(QStringLiteral("/downloads")),
                                QJsonDocument(body).toJson(QJsonDocument::Compact));
    job->reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, itemId, reply] {
        reply->deleteLater();
        const auto job = m_jobs.value(itemId);
        if (!job || job->cancelled)
            return;
        job->reply.clear();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && status != 201) {
            QString code;
            failJob(itemId, code, parseProblem(payload, status, &code));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        job->jobId = obj.value(QStringLiteral("id")).toString();
        job->status = obj.value(QStringLiteral("status")).toString(QStringLiteral("queued"));
        job->progress = obj.value(QStringLiteral("progress")).toDouble();
        if (job->status == QLatin1String("ready")) {
            finishJobFile(job.get(), obj.value(QStringLiteral("file")).toObject(),
                          obj.value(QStringLiteral("attribution")).toString());
            return;
        }
        if (job->status == QLatin1String("failed")) {
            const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
            failJob(itemId, err.value(QStringLiteral("code")).toString(),
                    err.value(QStringLiteral("detail")).toString());
            return;
        }
        job->phase = tr("Preparing…");
        bumpDownloads();
        if (!m_pollTimer->isActive())
            m_pollTimer->start();
    });
}

void MarketClient::cancelDownload(const QString &itemId)
{
    const auto job = m_jobs.value(itemId);
    if (!job)
        return;
    job->cancelled = true;
    if (job->reply)
        job->reply->abort();
    m_jobs.remove(itemId);
    bumpDownloads();
}

QVariantMap MarketClient::downloadInfo(const QString &itemId) const
{
    const auto job = m_jobs.value(itemId);
    if (!job)
        return {};
    return QVariantMap{
        {QStringLiteral("status"), job->status},
        {QStringLiteral("progress"), job->progress},
        {QStringLiteral("phase"), job->phase},
        {QStringLiteral("errorCode"), job->errorCode},
        {QStringLiteral("errorMessage"), job->errorMessage},
    };
}

QVariantMap MarketClient::itemById(const QString &itemId) const
{
    const int at = m_itemIndex.value(itemId, -1);
    if (at < 0 || at >= m_items.size())
        return {};
    return m_items.at(at).toMap();
}

bool MarketClient::handleIncomingUrl(const QUrl &url)
{
    if (!isAuthCallbackUrl(url))
        return false;
    const QUrlQuery query(url);
    const QString code = query.queryItemValue(QStringLiteral("code"));
    const QString state = query.queryItemValue(QStringLiteral("state"));
    if (code.isEmpty()) {
        emit authFinished(false, tr("Could not connect the marketplace account."));
        return true;
    }
    exchangeCode(code, state);
    return true;
}

void MarketClient::disconnectAccount()
{
    const QString refresh = m_refreshToken;
    clearAuth();
    emit authChanged();
    if (!configured() || refresh.isEmpty())
        return;
    QJsonObject body;
    body.insert(QStringLiteral("refresh_token"), refresh);
    QNetworkReply *reply =
        post(apiUrl(QStringLiteral("/auth/logout")), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
}

QUrl MarketClient::apiUrl(const QString &path) const
{
    QString base = kApiUrl;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QString rel = path;
    if (!rel.startsWith(QLatin1Char('/')))
        rel.prepend(QLatin1Char('/'));
    return QUrl(base + rel);
}

void MarketClient::sign(QNetworkRequest *request, const QByteArray &method, const QByteArray &body) const
{
    const QString timestamp = QString::number(QDateTime::currentSecsSinceEpoch());
    const QString nonce = makeNonce();
    const QString id = clientId();
    const QString target = pathAndQuery(request->url());
    const QString signature =
        signRequest(hmacKeyBytes(), timestamp, nonce, id, method, target, body);
    request->setRawHeader("X-Cutwire-Client", id.toUtf8());
    request->setRawHeader("X-Cutwire-Timestamp", timestamp.toUtf8());
    request->setRawHeader("X-Cutwire-Nonce", nonce.toUtf8());
    request->setRawHeader("X-Cutwire-Signature", signature.toUtf8());
    request->setRawHeader("X-Cutwire-App", appHeader().toUtf8());
    request->setHeader(QNetworkRequest::UserAgentHeader, appHeader());
    applyAuthHeader(request);
}

void MarketClient::applyAuthHeader(QNetworkRequest *request) const
{
    if (m_accessToken.isEmpty())
        return;
    request->setRawHeader("Authorization", QByteArray("Bearer ") + m_accessToken.toUtf8());
}

QNetworkReply *MarketClient::get(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(kApiTimeoutMs);
    sign(&request, "GET", {});
    return m_network->get(request);
}

QNetworkReply *MarketClient::post(const QUrl &url, const QByteArray &body)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kApiTimeoutMs);
    sign(&request, "POST", body);
    return m_network->post(request, body);
}

void MarketClient::setCatalogLoading(bool loading)
{
    if (m_catalogLoading == loading)
        return;
    m_catalogLoading = loading;
    emit catalogLoadingChanged();
}

void MarketClient::setCatalogError(const QString &error)
{
    if (m_catalogError == error)
        return;
    m_catalogError = error;
    emit catalogErrorChanged();
}

void MarketClient::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    emit searchingChanged();
}

void MarketClient::setSearchError(const QString &error)
{
    if (m_searchError == error)
        return;
    m_searchError = error;
    emit searchErrorChanged();
}

void MarketClient::bumpDownloads()
{
    ++m_downloadsRevision;
    emit downloadsRevisionChanged();
}

QVariantMap MarketClient::activeType() const
{
    for (const QVariant &row : m_types) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toString() == m_activeTypeId)
            return map;
    }
    return {};
}

QVariantMap MarketClient::activeProvider() const
{
    for (const QVariant &row : providers()) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toString() == m_activeProviderId)
            return map;
    }
    return {};
}

void MarketClient::applyCatalog(const QJsonArray &types)
{
    QVariantList filtered;
    for (const QJsonValue &value : types) {
        QVariantMap type = objectToMap(value.toObject());
        const QString delivery = type.value(QStringLiteral("delivery")).toString();
        if (!delivery.isEmpty() && delivery != QLatin1String("media"))
            continue;
        filtered.append(type);
    }
    m_types = filtered;
    emit catalogChanged();

    QString typeId = m_activeTypeId;
    bool typeOk = false;
    for (const QVariant &row : m_types) {
        if (row.toMap().value(QStringLiteral("id")).toString() == typeId) {
            typeOk = true;
            break;
        }
    }
    if (!typeOk)
        typeId = m_types.isEmpty() ? QString()
                                   : m_types.first().toMap().value(QStringLiteral("id")).toString();
    // Force provider refresh even when the type id is unchanged (catalog reload).
    const QString previousType = m_activeTypeId;
    m_activeTypeId.clear();
    if (previousType != typeId)
        emit activeTypeIdChanged();
    setActiveTypeId(typeId);
}

void MarketClient::applySearchPage(const QJsonObject &page, bool append)
{
    if (page.contains(QStringLiteral("quota"))) {
        m_quota = objectToMap(page.value(QStringLiteral("quota")).toObject());
        emit quotaChanged();
    }
    m_nextCursor = page.value(QStringLiteral("next_cursor")).toString();
    const QJsonArray items = page.value(QStringLiteral("items")).toArray();
    if (!append) {
        m_items.clear();
        m_itemIndex.clear();
    }
    for (const QJsonValue &value : items) {
        const QVariantMap item = objectToMap(value.toObject());
        const QString id = item.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        m_itemIndex.insert(id, m_items.size());
        m_items.append(item);
    }
    emit itemsChanged();
}

void MarketClient::applyResolvedItem(const QJsonObject &item)
{
    const QVariantMap map = objectToMap(item);
    m_items.clear();
    m_itemIndex.clear();
    m_nextCursor.clear();
    const QString id = map.value(QStringLiteral("id")).toString();
    if (!id.isEmpty()) {
        m_itemIndex.insert(id, 0);
        m_items.append(map);
    }
    emit itemsChanged();

    const QString provider = map.value(QStringLiteral("provider")).toString();
    if (provider.isEmpty() || provider == m_activeProviderId)
        return;
    bool known = false;
    for (const QVariant &row : providers()) {
        if (row.toMap().value(QStringLiteral("id")).toString() == provider) {
            known = true;
            break;
        }
    }
    if (!known)
        return;
    m_activeProviderId = provider;
    emit activeProviderIdChanged();
    emit providersChanged();
    const QVariantMap q = activeProvider().value(QStringLiteral("quota")).toMap();
    if (q != m_quota) {
        m_quota = q;
        emit quotaChanged();
    }
}

void MarketClient::startSearch(bool append)
{
    if (!configured()) {
        setSearchError(tr("Marketplace is not available in this build."));
        return;
    }
    if (!canSearch())
        return;
    if (m_activeTypeId.isEmpty() || m_activeProviderId.isEmpty()) {
        setSearchError(tr("Nothing is available from the marketplace right now."));
        return;
    }
    abortInFlightSearch();

    QUrl url = apiUrl(QStringLiteral("/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), m_activeTypeId);
    query.addQueryItem(QStringLiteral("provider"), m_activeProviderId);
    if (!m_query.trimmed().isEmpty())
        query.addQueryItem(QStringLiteral("q"), m_query.trimmed());
    query.addQueryItem(QStringLiteral("limit"), QString::number(kSearchLimit));
    if (append && !m_nextCursor.isEmpty())
        query.addQueryItem(QStringLiteral("cursor"), m_nextCursor);
    for (auto it = m_filterValues.cbegin(); it != m_filterValues.cend(); ++it) {
        const QString value = it.value().toString();
        if (value.isEmpty())
            continue;
        query.addQueryItem(it.key(), value);
    }
    url.setQuery(query);

    setSearching(true);
    setSearchError({});
    QNetworkReply *reply = get(url);
    m_searchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, append] {
        reply->deleteLater();
        if (m_searchReply == reply)
            m_searchReply.clear();
        setSearching(false);
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString code;
            setSearchError(parseProblem(payload, status, &code));
            return;
        }
        applySearchPage(QJsonDocument::fromJson(payload).object(), append);
    });
}

void MarketClient::pollJobs()
{
    bool any = false;
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        const auto &job = it.value();
        if (!job || job->cancelled || job->jobId.isEmpty())
            continue;
        if (job->status == QLatin1String("ready") || job->status == QLatin1String("failed")
            || job->status == QLatin1String("downloading") || job->status == QLatin1String("importing"))
            continue;
        any = true;
        if (job->reply)
            continue;
        QNetworkReply *reply = get(apiUrl(QStringLiteral("/downloads/") + job->jobId));
        job->reply = reply;
        const QString itemId = job->itemId;
        connect(reply, &QNetworkReply::finished, this, [this, itemId, reply] {
            reply->deleteLater();
            const auto job = m_jobs.value(itemId);
            if (!job || job->cancelled)
                return;
            job->reply.clear();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray payload = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                QString code;
                failJob(itemId, code, parseProblem(payload, status, &code));
                return;
            }
            const QJsonObject obj = QJsonDocument::fromJson(payload).object();
            job->status = obj.value(QStringLiteral("status")).toString(job->status);
            job->progress = obj.value(QStringLiteral("progress")).toDouble(job->progress);
            if (job->status == QLatin1String("ready")) {
                finishJobFile(job.get(), obj.value(QStringLiteral("file")).toObject(),
                              obj.value(QStringLiteral("attribution")).toString());
                return;
            }
            if (job->status == QLatin1String("failed")) {
                const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
                failJob(itemId,
                        err.value(QStringLiteral("code")).toString(QStringLiteral("download_failed")),
                        err.value(QStringLiteral("detail")).toString());
                return;
            }
            job->phase = tr("Preparing…");
            bumpDownloads();
            emit downloadProgress(itemId, job->progress, job->phase);
        });
    }
    if (!any)
        m_pollTimer->stop();
}

void MarketClient::finishJobFile(Job *job, const QJsonObject &file, const QString &attribution)
{
    Q_UNUSED(attribution);
    if (!job)
        return;
    const QString url = file.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        failJob(job->itemId, QStringLiteral("download_failed"),
                userMessageForCode(QStringLiteral("download_failed"), {}));
        return;
    }

    job->status = QStringLiteral("downloading");
    job->phase = tr("Downloading…");
    job->progress = 0;
    bumpDownloads();
    emit downloadProgress(job->itemId, 0, job->phase);

    QString name = sanitizeFileName(file.value(QStringLiteral("filename")).toString());
    if (QFileInfo(name).suffix().isEmpty())
        name += extensionForMime(file.value(QStringLiteral("mime")).toString());
    const QString dir = downloadsRoot() + QLatin1Char('/') + job->itemId;
    QDir().mkpath(dir);
    const QString path = dir + QLatin1Char('/') + name;
    const QString expectedSha = file.value(QStringLiteral("sha256")).toString().trimmed().toLower();

    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kFileTimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    job->reply = reply;
    const QString itemId = job->itemId;
    auto *out = new QSaveFile(path);
    if (!out->open(QIODevice::WriteOnly)) {
        delete out;
        failJob(itemId, QStringLiteral("download_failed"), tr("Could not save that file."));
        return;
    }
    auto *hasher = new QCryptographicHash(QCryptographicHash::Sha256);
    connect(reply, &QNetworkReply::readyRead, this, [reply, out, hasher] {
        const QByteArray chunk = reply->readAll();
        out->write(chunk);
        hasher->addData(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, itemId](qint64 received, qint64 total) {
                const auto job = m_jobs.value(itemId);
                if (!job || total <= 0)
                    return;
                job->progress = double(received) / double(total);
                emit downloadProgress(itemId, job->progress, job->phase);
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, itemId, reply, out, hasher, expectedSha, name, path] {
        reply->deleteLater();
        hasher->addData(reply->readAll());
        const auto job = m_jobs.value(itemId);
        if (!job || job->cancelled) {
            out->cancelWriting();
            delete out;
            delete hasher;
            return;
        }
        job->reply.clear();
        if (reply->error() != QNetworkReply::NoError) {
            out->cancelWriting();
            delete out;
            delete hasher;
            failJob(itemId, QStringLiteral("download_failed"),
                    userMessageForCode(QStringLiteral("download_failed"), {}));
            return;
        }
        const QString actual = QString::fromLatin1(hasher->result().toHex());
        delete hasher;
        if (!expectedSha.isEmpty() && actual != expectedSha) {
            out->cancelWriting();
            delete out;
            failJob(itemId, QStringLiteral("download_failed"),
                    tr("The downloaded file did not match what the marketplace sent."));
            return;
        }
        if (!out->commit()) {
            delete out;
            failJob(itemId, QStringLiteral("download_failed"), tr("Could not save that file."));
            return;
        }
        delete out;
        importReadyFile(itemId, path, QFileInfo(name).completeBaseName());
    });
}

void MarketClient::failJob(const QString &itemId, const QString &code, const QString &message)
{
    const auto job = m_jobs.value(itemId);
    const QString resolvedCode = code.isEmpty() ? QStringLiteral("download_failed") : code;
    const QString resolved = userMessageForCode(resolvedCode, message);
    if (job) {
        job->status = QStringLiteral("failed");
        job->errorCode = resolvedCode;
        job->errorMessage = resolved;
        job->phase = resolved;
    }
    bumpDownloads();
    emit downloadFailed(itemId, resolvedCode, resolved);
}

void MarketClient::importReadyFile(const QString &itemId, const QString &path,
                                   const QString &displayName)
{
    const auto job = m_jobs.value(itemId);
    if (job) {
        job->status = QStringLiteral("importing");
        job->phase = tr("Importing…");
        job->progress = 1;
        bumpDownloads();
    }
    if (!m_library) {
        failJob(itemId, QStringLiteral("download_failed"), tr("Could not import that file."));
        return;
    }
    const QStringList ids = m_library->importLocalPaths({path});
    m_jobs.remove(itemId);
    bumpDownloads();
    if (ids.isEmpty()) {
        emit downloadFailed(itemId, QStringLiteral("download_failed"),
                            tr("Could not import that file."));
        return;
    }
    emit downloadImported(itemId, displayName);
}

void MarketClient::loadStoredAuth()
{
    QSettings settings;
    m_accessToken = settings.value(settingsKey("accessToken")).toString();
    m_refreshToken = settings.value(settingsKey("refreshToken")).toString();
    m_tokenExpiresAt = settings.value(settingsKey("expiresAt")).toLongLong();
    m_accountId = settings.value(settingsKey("accountId")).toString();
    m_accountName = settings.value(settingsKey("accountName")).toString();
    m_coins = settings.value(settingsKey("coins")).toInt();
}

void MarketClient::storeAuth()
{
    QSettings settings;
    settings.setValue(settingsKey("accessToken"), m_accessToken);
    settings.setValue(settingsKey("refreshToken"), m_refreshToken);
    settings.setValue(settingsKey("expiresAt"), m_tokenExpiresAt);
    settings.setValue(settingsKey("accountId"), m_accountId);
    settings.setValue(settingsKey("accountName"), m_accountName);
    settings.setValue(settingsKey("coins"), m_coins);
}

void MarketClient::clearAuth()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_tokenExpiresAt = 0;
    m_accountId.clear();
    m_accountName.clear();
    m_coins = 0;
    QSettings settings;
    settings.remove(settingsKey("accessToken"));
    settings.remove(settingsKey("refreshToken"));
    settings.remove(settingsKey("expiresAt"));
    settings.remove(settingsKey("accountId"));
    settings.remove(settingsKey("accountName"));
    settings.remove(settingsKey("coins"));
}

void MarketClient::applyAccount(const QJsonObject &account)
{
    if (account.isEmpty())
        return;
    m_accountId = account.value(QStringLiteral("id")).toString(m_accountId);
    m_accountName = account.value(QStringLiteral("display_name")).toString(m_accountName);
    if (account.contains(QStringLiteral("coins")))
        m_coins = account.value(QStringLiteral("coins")).toInt();
}

void MarketClient::exchangeCode(const QString &code, const QString &state)
{
    if (!configured()) {
        emit authFinished(false, tr("Marketplace is not available in this build."));
        return;
    }
    QJsonObject body;
    body.insert(QStringLiteral("code"), code);
    if (!state.isEmpty())
        body.insert(QStringLiteral("state"), state);
    QNetworkReply *reply =
        post(apiUrl(QStringLiteral("/auth/token")), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString errorCode;
            emit authFinished(false, parseProblem(payload, status, &errorCode));
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(payload).object();
        m_accessToken = obj.value(QStringLiteral("access_token")).toString();
        m_refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
        const int expiresIn = obj.value(QStringLiteral("expires_in")).toInt();
        m_tokenExpiresAt = QDateTime::currentSecsSinceEpoch() + qMax(0, expiresIn);
        applyAccount(obj.value(QStringLiteral("account")).toObject());
        storeAuth();
        emit authChanged();
        emit authFinished(true, tr("Marketplace account connected."));
        refreshCatalog();
    });
}

void MarketClient::refreshAccessToken(const std::function<void(bool)> &then)
{
    if (m_refreshToken.isEmpty() || !configured()) {
        then(false);
        return;
    }
    if (m_refreshingToken) {
        then(false);
        return;
    }
    m_refreshingToken = true;
    QJsonObject body;
    body.insert(QStringLiteral("refresh_token"), m_refreshToken);
    QNetworkReply *reply =
        post(apiUrl(QStringLiteral("/auth/refresh")), QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, then] {
        reply->deleteLater();
        m_refreshingToken = false;
        if (reply->error() != QNetworkReply::NoError) {
            clearAuth();
            emit authChanged();
            then(false);
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        m_accessToken = obj.value(QStringLiteral("access_token")).toString();
        const QString refresh = obj.value(QStringLiteral("refresh_token")).toString();
        if (!refresh.isEmpty())
            m_refreshToken = refresh;
        const int expiresIn = obj.value(QStringLiteral("expires_in")).toInt();
        m_tokenExpiresAt = QDateTime::currentSecsSinceEpoch() + qMax(0, expiresIn);
        applyAccount(obj.value(QStringLiteral("account")).toObject());
        storeAuth();
        emit authChanged();
        then(true);
    });
}

void MarketClient::fetchMe()
{
    if (m_accessToken.isEmpty())
        return;
    const auto go = [this] {
        QNetworkReply *reply = get(apiUrl(QStringLiteral("/me")));
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 401) {
                refreshAccessToken([this](bool ok) {
                    if (ok)
                        fetchMe();
                });
                return;
            }
            if (reply->error() != QNetworkReply::NoError)
                return;
            applyAccount(QJsonDocument::fromJson(reply->readAll()).object());
            storeAuth();
            emit authChanged();
        });
    };
    if (m_tokenExpiresAt > 0
        && QDateTime::currentSecsSinceEpoch() + kTokenRefreshSkewSecs >= m_tokenExpiresAt) {
        refreshAccessToken([go](bool) { go(); });
        return;
    }
    go();
}

QString MarketClient::userMessageForCode(const QString &code, const QString &fallback)
{
    if (code == QLatin1String("rate_limited"))
        return tr("Daily limit reached for this source. Try again later.");
    if (code == QLatin1String("auth_required"))
        return tr("This item needs a connected account.");
    if (code == QLatin1String("payment_required"))
        return tr("Not enough coins.");
    if (code == QLatin1String("provider_unavailable"))
        return tr("This source is temporarily unavailable.");
    if (code == QLatin1String("not_found"))
        return tr("That item is no longer available.");
    if (code == QLatin1String("invalid_client"))
        return tr("Could not reach the marketplace.");
    if (code == QLatin1String("download_failed"))
        return tr("Could not prepare that file.");
    if (!fallback.isEmpty())
        return fallback;
    return tr("Could not complete that request.");
}

QString MarketClient::parseProblem(const QByteArray &body, int httpStatus, QString *codeOut)
{
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    QString code = obj.value(QStringLiteral("code")).toString();
    if (code.isEmpty()) {
        if (httpStatus == 429)
            code = QStringLiteral("rate_limited");
        else if (httpStatus == 401 || httpStatus == 403)
            code = QStringLiteral("auth_required");
        else if (httpStatus == 402)
            code = QStringLiteral("payment_required");
        else if (httpStatus == 404)
            code = QStringLiteral("not_found");
        else if (httpStatus == 503)
            code = QStringLiteral("provider_unavailable");
        else
            code = QStringLiteral("download_failed");
    }
    if (codeOut)
        *codeOut = code;
    const QString detail = obj.value(QStringLiteral("detail")).toString();
    return userMessageForCode(code, detail);
}
