#include "app/online/market/MarketClient.h"

#include "app/bin/AssetLibrary.h"
#include "app/online/market/MarketEndpoint.h"
#include "app/online/market/MarketIdentity.h"
#include "app/online/marketplace/MarketTransport.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace drift::market;

namespace {

constexpr int kFileTimeoutMs = 10 * 60 * 1000;
constexpr int kPollIntervalMs = 1000;
// Three at once: enough that a handful of clips do not run one behind the other, few enough
// that a click-happy session does not open a dozen sockets against a source that is already
// rate-limiting per client. The rest wait in "waiting" and start as slots free.
constexpr int kMaxConcurrentDownloads = 3;
// Speed is sampled rather than averaged over the whole transfer, so it tracks a source that
// slows down instead of reporting the mean of a stall and a burst. The same tick throttles
// the model rebuild: downloadProgress fires per network chunk, and rebuilding the job list
// that often would have three transfers respinning it hundreds of times a second.
constexpr int kSpeedSampleMs = 400;
constexpr int kSearchLimit = 30;

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
    QString title;
    // video | audio | image, straight from the catalog item. Kept on the job because the
    // manager outlives the search results the item came from.
    QString mediaKind;
    QString variantId;
    QString destinationDir;
    QString filePath;
    QString assetId;
    // waiting -> queued/processing -> downloading -> importing -> done, or failed/cancelled.
    // "waiting" is ours (behind the concurrency cap); the middle ones come from the service.
    QString status;
    double progress = 0;
    QString phase;
    QString errorCode;
    QString errorMessage;
    qint64 bytesReceived = 0;
    qint64 bytesTotal = 0;
    double speedBytesPerSec = 0;
    QDateTime startedAt;
    QDateTime finishedAt;
    // Sampling window for the speed readout.
    QElapsedTimer sampleClock;
    qint64 sampleBytes = 0;
    QPointer<QNetworkReply> reply;
    bool cancelled = false;
};

bool MarketClient::jobIsRunning(const Job &job)
{
    return job.status == QLatin1String("queued") || job.status == QLatin1String("processing")
        || job.status == QLatin1String("downloading") || job.status == QLatin1String("importing");
}

bool MarketClient::jobIsFinished(const Job &job)
{
    return job.status == QLatin1String("done") || job.status == QLatin1String("failed")
        || job.status == QLatin1String("cancelled");
}

MarketClient::MarketClient(QObject *parent)
    : QObject(parent)
    , m_transport(MarketTransport::instance())
    , m_network(m_transport->network())
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &MarketClient::pollJobs);

    m_resolvePollTimer = new QTimer(this);
    m_resolvePollTimer->setInterval(kPollIntervalMs);
    connect(m_resolvePollTimer, &QTimer::timeout, this, &MarketClient::pollResolve);

    m_lastDownloadDir = QSettings().value(settingsKey("lastDownloadDir")).toUrl();

    for (const char *key : {"accessToken", "refreshToken", "expiresAt", "accountId", "accountName", "coins",
                            "consented"})
        QSettings().remove(settingsKey(key));
    if (configured())
        QDir().mkpath(downloadsRoot());
}

MarketClient::~MarketClient() = default;

void MarketClient::setAssetLibrary(AssetLibrary *library)
{
    m_library = library;
}

bool MarketClient::configured() const
{
    return MarketTransport::configured();
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
    if (searchingAll())
        return canSearchAll();
    const QVariantList caps = activeProvider().value(QStringLiteral("capabilities")).toList();
    // Older catalogs omit capabilities; treat that as a normal searchable source.
    if (caps.isEmpty())
        return true;
    return hasCapability(QStringLiteral("search")) || hasCapability(QStringLiteral("featured"));
}

bool MarketClient::canResolve() const
{
    if (!searchingAll())
        return hasCapability(QStringLiteral("resolve"));
    for (const QVariant &row : providers()) {
        if (row.toMap().value(QStringLiteral("capabilities")).toList().contains(QStringLiteral("resolve")))
            return true;
    }
    return false;
}

QStringList MarketClient::allProviderIds() const
{
    QStringList ids;
    for (const QVariant &row : providers()) {
        const QVariantMap p = row.toMap();
        const QVariantList caps = p.value(QStringLiteral("capabilities")).toList();
        const bool searchable = caps.isEmpty() || caps.contains(QStringLiteral("search"))
            || caps.contains(QStringLiteral("featured"));
        // The catalog omits quota for an unlimited source.
        if (searchable && !p.contains(QStringLiteral("quota")))
            ids.append(p.value(QStringLiteral("id")).toString());
    }
    return ids;
}

bool MarketClient::hasMore() const
{
    if (!searchingAll())
        return !m_nextCursor.isEmpty();
    for (const QString &cursor : m_fanoutCursors) {
        if (!cursor.isEmpty())
            return true;
    }
    return false;
}

void MarketClient::setLastDownloadDir(const QUrl &dir)
{
    if (dir == m_lastDownloadDir)
        return;
    m_lastDownloadDir = dir;
    QSettings().setValue(settingsKey("lastDownloadDir"), dir);
    emit lastDownloadDirChanged();
}

void MarketClient::abortInFlightSearch()
{
    // abort() emits finished() synchronously, and that handler clears m_searchReply — so
    // touching the member again after the call dereferenced a null QPointer and took the
    // whole app down. Switching type mid-search did exactly that, because setActiveTypeId
    // lands here through setActiveProviderId. Detach first, then work from the local.
    // A resolve in progress is a job on the server; stop chasing it before dropping the reply.
    if (m_resolvePollTimer)
        m_resolvePollTimer->stop();
    m_resolveJobId.clear();

    ++m_fanoutGeneration;
    const QList<QPointer<QNetworkReply>> fanout = m_fanoutReplies;
    m_fanoutReplies.clear();
    for (const QPointer<QNetworkReply> &r : fanout) {
        if (!r)
            continue;
        r->setProperty("driftAborted", true);
        r->abort();
    }

    const QPointer<QNetworkReply> reply = m_searchReply;
    m_searchReply.clear();
    if (!reply)
        return;
    // A transfer timeout is also delivered as OperationCanceledError, so the finished
    // handler cannot tell a timeout from a cancellation by error code alone — and it used
    // to treat both as ours and return silently, which left a timed-out search showing no
    // results and no reason. Mark the ones we really do cancel.
    reply->setProperty("driftAborted", true);
    reply->abort();
    if (reply)
        reply->deleteLater();
}

void MarketClient::cancelSearch()
{
    // Between resolve poll ticks there is a job but no reply, and Cancel has to work then
    // too — that gap is most of the wait on a slow extraction.
    if (!m_searchReply && m_resolveJobId.isEmpty() && m_fanoutReplies.isEmpty())
        return;
    abortInFlightSearch();
    // The finished handler normally clears this, but it does not run when the reply was
    // already gone, and a spinner left turning is worse than a redundant assignment.
    setSearching(false);
    setSearchError({});
}

void MarketClient::setActiveTypeId(const QString &id)
{
    if (id == m_activeTypeId)
        return;
    m_activeTypeId = id;
    const QVariantList list = providers();
    QString providerId;
    if (canSearchAll())
        providerId = QStringLiteral("*");
    else if (!list.isEmpty())
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
    m_fanoutCursors.clear();
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
    QNetworkReply *reply = m_transport->get(MarketTransport::apiUrl(QStringLiteral("/catalog")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setCatalogLoading(false);
        if (reply->error() != QNetworkReply::NoError) {
            QString code;
            QString reason;
            bool retryable = true;
            const QString message =
                MarketTransport::parseProblem(reply->readAll(),
                             reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),
                             &code, &reason, &retryable, int(reply->error()));
            setCatalogError(message, retryable);
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
        m_transport->post(MarketTransport::apiUrl(QStringLiteral("/resolve")), QJsonDocument(body).toJson(QJsonDocument::Compact));
    m_searchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_searchReply == reply)
            m_searchReply.clear();
        if (reply->property("driftAborted").toBool()) {
            setSearching(false);
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        // Fast failures stay on the POST: a link nobody owns, or one whose provider is
        // switched off. Those are answered here and never become a job.
        if (reply->error() != QNetworkReply::NoError) {
            setSearching(false);
            QString code;
            QString reason;
            bool retryable = true;
            const QString message =
                MarketTransport::parseProblem(payload, status, &code, &reason, &retryable, int(reply->error()));
            setSearchError(message, retryable, code);
            m_items.clear();
            m_itemIndex.clear();
            emit itemsChanged();
            return;
        }
        const QJsonObject job = QJsonDocument::fromJson(payload).object();
        m_resolveJobId = job.value(QStringLiteral("id")).toString();
        if (m_resolveJobId.isEmpty()) {
            setSearching(false);
            setSearchError(MarketTransport::fallbackMessageForCode(QStringLiteral("not_found")), false,
                           QStringLiteral("not_found"));
            return;
        }
        // Extraction may already be done by the time the POST returns.
        if (job.value(QStringLiteral("status")).toString() == QLatin1String("ready")) {
            m_resolveJobId.clear();
            setSearching(false);
            applyResolvedItem(job.value(QStringLiteral("item")).toObject());
            return;
        }
        m_resolvePollTimer->start();
    });
}

void MarketClient::finishResolveFailure(const QJsonObject &problem)
{
    const QString code = problem.value(QStringLiteral("code")).toString();
    const QString reason = problem.value(QStringLiteral("reason")).toString();
    const QString detail = problem.value(QStringLiteral("detail")).toString().trimmed();
    setSearchError(detail.isEmpty() ? MarketTransport::fallbackMessageForCode(code) : detail,
                   MarketTransport::isRetryable(code, reason), code);
    m_items.clear();
    m_itemIndex.clear();
    emit itemsChanged();
}

void MarketClient::pollResolve()
{
    if (m_resolveJobId.isEmpty()) {
        m_resolvePollTimer->stop();
        return;
    }
    // One request in flight at a time; a slow answer must not stack up behind the tick.
    if (m_searchReply)
        return;

    QNetworkReply *reply = m_transport->get(MarketTransport::apiUrl(QStringLiteral("/resolve/") + m_resolveJobId));
    m_searchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (m_searchReply == reply)
            m_searchReply.clear();
        // Cancelling or switching provider aborts the poll; the job is then not ours.
        if (reply->property("driftAborted").toBool())
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            m_resolvePollTimer->stop();
            m_resolveJobId.clear();
            setSearching(false);
            QString code;
            QString reason;
            bool retryable = true;
            const QString message =
                MarketTransport::parseProblem(payload, status, &code, &reason, &retryable, int(reply->error()));
            setSearchError(message, retryable, code);
            return;
        }
        const QJsonObject job = QJsonDocument::fromJson(payload).object();
        const QString state = job.value(QStringLiteral("status")).toString();
        if (state == QLatin1String("ready")) {
            m_resolvePollTimer->stop();
            m_resolveJobId.clear();
            setSearching(false);
            applyResolvedItem(job.value(QStringLiteral("item")).toObject());
            return;
        }
        if (state == QLatin1String("failed")) {
            m_resolvePollTimer->stop();
            m_resolveJobId.clear();
            setSearching(false);
            // The job carries the same Problem shape the HTTP errors use, so a source that
            // timed out during extraction reads as a reason rather than a bare failure.
            finishResolveFailure(job.value(QStringLiteral("error")).toObject());
            return;
        }
    });
}

void MarketClient::loadMore()
{
    if (m_searching || !hasMore())
        return;
    startSearch(true);
}

void MarketClient::download(const QString &itemId, const QString &variantId,
                            const QUrl &destinationDir, const QString &title,
                            const QString &mediaKind)
{
    if (!configured() || itemId.isEmpty())
        return;
    // A finished job for the same item is history, not a conflict: replace it so the item
    // can be fetched again. One still running is a genuine duplicate.
    if (const auto existing = m_jobs.value(itemId)) {
        if (!jobIsFinished(*existing) )
            return;
        m_jobs.remove(itemId);
        m_jobOrder.removeAll(itemId);
    }

    auto job = std::make_shared<Job>();
    job->itemId = itemId;
    job->title = title;
    job->mediaKind = mediaKind;
    job->variantId = variantId;
    // Anything that is not a local folder falls back to the app data area rather than
    // being pasted into a filesystem path it is not.
    job->destinationDir = destinationDir.isLocalFile() ? destinationDir.toLocalFile() : QString();
    job->status = QStringLiteral("waiting");
    job->phase = tr("Waiting…");
    job->startedAt = QDateTime::currentDateTime();
    m_jobs.insert(itemId, job);
    m_jobOrder.append(itemId);
    emit downloadStarted(itemId);
    bumpDownloads();
    emit downloadProgress(itemId, 0, job->phase);
    pumpDownloadQueue();
}

void MarketClient::pumpDownloadQueue()
{
    int running = 0;
    for (const auto &job : m_jobs) {
        if (job && jobIsRunning(*job))
            ++running;
    }
    // Oldest first, so the queue drains in the order the user asked for.
    for (const QString &itemId : std::as_const(m_jobOrder)) {
        if (running >= kMaxConcurrentDownloads)
            return;
        const auto job = m_jobs.value(itemId);
        if (!job || job->status != QLatin1String("waiting") || job->cancelled)
            continue;
        beginJob(job);
        ++running;
    }
}

void MarketClient::beginJob(const std::shared_ptr<Job> &job)
{
    const QString itemId = job->itemId;
    const QString variantId = job->variantId;
    job->status = QStringLiteral("queued");
    job->phase = tr("Starting…");
    bumpDownloads();
    emit downloadProgress(itemId, 0, job->phase);

    QJsonObject body;
    body.insert(QStringLiteral("item_id"), itemId);
    if (!variantId.isEmpty())
        body.insert(QStringLiteral("variant_id"), variantId);

    QNetworkReply *reply = m_transport->post(MarketTransport::apiUrl(QStringLiteral("/downloads")),
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
            failJob(itemId, code,
                    MarketTransport::parseProblem(payload, status, &code, nullptr, nullptr, int(reply->error())));
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
            const QString code = err.value(QStringLiteral("code")).toString();
            const QString detail = err.value(QStringLiteral("detail")).toString().trimmed();
            failJob(itemId, code, detail.isEmpty() ? MarketTransport::fallbackMessageForCode(code) : detail);
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
    job->status = QStringLiteral("cancelled");
    job->phase = tr("Cancelled");
    job->finishedAt = QDateTime::currentDateTime();
    job->speedBytesPerSec = 0;
    if (job->reply)
        job->reply->abort();
    bumpDownloads();
    pumpDownloadQueue();
}

int MarketClient::maxConcurrentDownloads() const
{
    return kMaxConcurrentDownloads;
}

int MarketClient::activeDownloadCount() const
{
    int n = 0;
    for (const auto &job : m_jobs) {
        // Waiting counts: from the user's side it is a download they asked for and have
        // not got yet, and a badge that ignored the queue would undercount.
        if (job && !jobIsFinished(*job))
            ++n;
    }
    return n;
}

QVariantMap MarketClient::jobToMap(const Job &job) const
{
    return QVariantMap{
        {QStringLiteral("itemId"), job.itemId},
        {QStringLiteral("title"), job.title.isEmpty() ? job.itemId : job.title},
        {QStringLiteral("mediaKind"), job.mediaKind},
        {QStringLiteral("status"), job.status},
        {QStringLiteral("phase"), job.phase},
        {QStringLiteral("progress"), job.progress},
        {QStringLiteral("bytesReceived"), job.bytesReceived},
        {QStringLiteral("bytesTotal"), job.bytesTotal},
        {QStringLiteral("speed"), job.speedBytesPerSec},
        {QStringLiteral("filePath"), job.filePath},
        {QStringLiteral("assetId"), job.assetId},
        {QStringLiteral("destinationDir"), job.destinationDir},
        {QStringLiteral("errorCode"), job.errorCode},
        {QStringLiteral("errorMessage"), job.errorMessage},
        {QStringLiteral("startedAt"), job.startedAt},
        {QStringLiteral("finishedAt"), job.finishedAt},
        {QStringLiteral("running"), jobIsRunning(job)},
        {QStringLiteral("finished"), jobIsFinished(job)},
        // No reason is kept per job, so this is the code-level answer; isRetryable falls
        // back to exactly that when a reason is absent.
        {QStringLiteral("retryable"), job.status == QLatin1String("failed")
                                      && MarketTransport::isRetryable(job.errorCode, QString())},
    };
}

QVariantList MarketClient::downloads() const
{
    QVariantList out;
    out.reserve(m_jobOrder.size());
    for (const QString &itemId : m_jobOrder) {
        if (const auto job = m_jobs.value(itemId))
            out.append(jobToMap(*job));
    }
    return out;
}

void MarketClient::clearFinishedDownloads()
{
    QStringList kept;
    for (const QString &itemId : std::as_const(m_jobOrder)) {
        const auto job = m_jobs.value(itemId);
        if (job && jobIsFinished(*job)) {
            m_jobs.remove(itemId);
            continue;
        }
        kept.append(itemId);
    }
    if (kept.size() == m_jobOrder.size())
        return;
    m_jobOrder = kept;
    bumpDownloads();
}

void MarketClient::retryDownload(const QString &itemId)
{
    const auto job = m_jobs.value(itemId);
    if (!job || !jobIsFinished(*job))
        return;
    // Reuses the row rather than appending a second one for the same item, so a flaky
    // source retried three times does not read as three separate downloads.
    job->cancelled = false;
    job->jobId.clear();
    job->errorCode.clear();
    job->errorMessage.clear();
    job->progress = 0;
    job->bytesReceived = 0;
    job->bytesTotal = 0;
    job->speedBytesPerSec = 0;
    job->sampleClock.invalidate();
    job->finishedAt = QDateTime();
    job->startedAt = QDateTime::currentDateTime();
    job->status = QStringLiteral("waiting");
    job->phase = tr("Waiting…");
    bumpDownloads();
    pumpDownloadQueue();
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

void MarketClient::setCatalogLoading(bool loading)
{
    if (m_catalogLoading == loading)
        return;
    m_catalogLoading = loading;
    emit catalogLoadingChanged();
}

void MarketClient::setCatalogError(const QString &error, bool retryable)
{
    if (m_catalogError == error && m_catalogErrorRetryable == retryable)
        return;
    m_catalogError = error;
    m_catalogErrorRetryable = retryable;
    emit catalogErrorChanged();
}

void MarketClient::setSearching(bool searching)
{
    if (m_searching == searching)
        return;
    m_searching = searching;
    emit searchingChanged();
}

void MarketClient::setSearchError(const QString &error, bool retryable, const QString &code)
{
    if (m_searchError == error && m_searchErrorRetryable == retryable && m_searchErrorCode == code)
        return;
    m_searchError = error;
    m_searchErrorRetryable = retryable;
    m_searchErrorCode = code;
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
    if (searchingAll()) {
        startFanoutSearch(append);
        return;
    }
    abortInFlightSearch();

    QUrl url = MarketTransport::apiUrl(QStringLiteral("/search"));
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
    QNetworkReply *reply = m_transport->get(url);
    m_searchReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, append] {
        reply->deleteLater();
        if (m_searchReply == reply)
            m_searchReply.clear();
        setSearching(false);
        if (reply->property("driftAborted").toBool())
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            QString code;
            QString reason;
            bool retryable = true;
            const QString message =
                MarketTransport::parseProblem(payload, status, &code, &reason, &retryable, int(reply->error()));
            setSearchError(message, retryable, code);
            return;
        }
        applySearchPage(QJsonDocument::fromJson(payload).object(), append);
    });
}

void MarketClient::startFanoutSearch(bool append)
{
    abortInFlightSearch();
    QStringList ids;
    for (const QString &id : allProviderIds()) {
        if (!append || !m_fanoutCursors.value(id).isEmpty())
            ids.append(id);
    }
    if (ids.isEmpty())
        return;

    struct Fanout
    {
        int remaining = 0;
        QHash<QString, QJsonArray> pages;
        QString firstError;
        bool firstRetryable = true;
        QString firstCode;
    };
    auto state = std::make_shared<Fanout>();
    state->remaining = int(ids.size());
    const int generation = m_fanoutGeneration;
    // Enough per source that the merged page is about one ordinary page long.
    const int perSource = std::max(12, int(std::ceil(double(kSearchLimit) / ids.size())));

    setSearching(true);
    setSearchError({});
    for (const QString &providerId : ids) {
        QUrl url = MarketTransport::apiUrl(QStringLiteral("/search"));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("type"), m_activeTypeId);
        query.addQueryItem(QStringLiteral("provider"), providerId);
        if (!m_query.trimmed().isEmpty())
            query.addQueryItem(QStringLiteral("q"), m_query.trimmed());
        query.addQueryItem(QStringLiteral("limit"), QString::number(perSource));
        if (append)
            query.addQueryItem(QStringLiteral("cursor"), m_fanoutCursors.value(providerId));
        url.setQuery(query);

        QNetworkReply *reply = m_transport->get(url);
        m_fanoutReplies.append(reply);
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, providerId, ids, state, generation, append] {
            reply->deleteLater();
            m_fanoutReplies.removeAll(reply);
            if (generation != m_fanoutGeneration || reply->property("driftAborted").toBool())
                return;
            const QByteArray payload = reply->readAll();
            if (reply->error() != QNetworkReply::NoError) {
                m_fanoutCursors.insert(providerId, QString());
                if (state->firstError.isEmpty()) {
                    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    QString reason;
                    state->firstError = MarketTransport::parseProblem(payload, status, &state->firstCode, &reason,
                                                     &state->firstRetryable, int(reply->error()));
                }
            } else {
                const QJsonObject page = QJsonDocument::fromJson(payload).object();
                m_fanoutCursors.insert(providerId, page.value(QStringLiteral("next_cursor")).toString());
                state->pages.insert(providerId, page.value(QStringLiteral("items")).toArray());
            }
            if (--state->remaining > 0)
                return;

            // Round-robin, so the first screen shows every source rather than one source's
            // whole page followed by the next.
            if (!append) {
                m_items.clear();
                m_itemIndex.clear();
            }
            for (int row = 0;; ++row) {
                bool any = false;
                for (const QString &id : ids) {
                    const QJsonArray page = state->pages.value(id);
                    if (row >= page.size())
                        continue;
                    any = true;
                    const QVariantMap item = objectToMap(page.at(row).toObject());
                    const QString itemId = item.value(QStringLiteral("id")).toString();
                    if (itemId.isEmpty() || m_itemIndex.contains(itemId))
                        continue;
                    m_itemIndex.insert(itemId, m_items.size());
                    m_items.append(item);
                }
                if (!any)
                    break;
            }
            setSearching(false);
            if (state->pages.isEmpty())
                setSearchError(state->firstError, state->firstRetryable, state->firstCode);
            emit itemsChanged();
        });
    }
}

void MarketClient::pollJobs()
{
    bool any = false;
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        const auto &job = it.value();
        if (!job || job->cancelled || job->jobId.isEmpty())
            continue;
        // Finished jobs stay in m_jobs now so the download manager can list them. They keep
        // their jobId, so without this they stayed pollable: the service still answers
        // "ready" for a completed job, and polling one re-ran finishJobFile, which fetched
        // the file and imported it into the bin again on every tick.
        if (jobIsFinished(*job))
            continue;
        if (job->status == QLatin1String("ready") || job->status == QLatin1String("downloading")
            || job->status == QLatin1String("importing"))
            continue;
        any = true;
        if (job->reply)
            continue;
        QNetworkReply *reply = m_transport->get(MarketTransport::apiUrl(QStringLiteral("/downloads/") + job->jobId));
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
                failJob(itemId, code,
                        MarketTransport::parseProblem(payload, status, &code, nullptr, nullptr, int(reply->error())));
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
                const QString code =
                    err.value(QStringLiteral("code")).toString(QStringLiteral("download_failed"));
                const QString detail = err.value(QStringLiteral("detail")).toString().trimmed();
                failJob(itemId, code, detail.isEmpty() ? MarketTransport::fallbackMessageForCode(code) : detail);
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
    // Fetching the file is not idempotent — it writes to disk and imports into the bin — so
    // it must not be re-entered for a job that is already past this point, whatever route
    // asked for it.
    if (job->status == QLatin1String("downloading") || job->status == QLatin1String("importing")
        || jobIsFinished(*job))
        return;
    const QString url = file.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        failJob(job->itemId, QStringLiteral("download_failed"),
                MarketTransport::fallbackMessageForCode(QStringLiteral("download_failed")));
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
    // A folder the user picked is written to directly; without one the file goes to the
    // per-item app data area it always used.
    const QString dir = job->destinationDir.isEmpty()
                        ? downloadsRoot() + QLatin1Char('/') + job->itemId
                        : job->destinationDir;
    QDir().mkpath(dir);
    // Their own folder may already hold a file of this name, and silently writing over it
    // would destroy something we did not put there.
    QString path = dir + QLatin1Char('/') + name;
    if (!job->destinationDir.isEmpty() && QFileInfo::exists(path)) {
        const QString base = QFileInfo(name).completeBaseName();
        const QString suffix = QFileInfo(name).suffix();
        const QString dotted = suffix.isEmpty() ? QString() : QLatin1Char('.') + suffix;
        for (int n = 2; n < 1000; ++n) {
            const QString candidate =
                QStringLiteral("%1/%2 (%3)%4").arg(dir, base, QString::number(n), dotted);
            if (!QFileInfo::exists(candidate)) {
                path = candidate;
                break;
            }
        }
    }
    job->filePath = path;
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
                if (!job)
                    return;
                job->bytesReceived = received;
                job->bytesTotal = total;
                // A source that sends no Content-Length leaves total at -1; the manager
                // hides the bar rather than showing a wrong percentage.
                job->progress = total > 0 ? double(received) / double(total) : 0;
                emit downloadProgress(itemId, job->progress, job->phase);

                if (!job->sampleClock.isValid()) {
                    job->sampleClock.start();
                    job->sampleBytes = received;
                    return;
                }
                if (job->sampleClock.elapsed() < kSpeedSampleMs)
                    return;
                const qint64 delta = received - job->sampleBytes;
                const qint64 ms = job->sampleClock.restart();
                if (ms > 0 && delta >= 0)
                    job->speedBytesPerSec = double(delta) * 1000.0 / double(ms);
                job->sampleBytes = received;
                bumpDownloads();
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
                    MarketTransport::fallbackMessageForCode(QStringLiteral("download_failed")));
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
    const QString trimmed = message.trimmed();
    const QString resolved = trimmed.isEmpty() ? MarketTransport::fallbackMessageForCode(resolvedCode) : trimmed;
    if (job) {
        job->status = QStringLiteral("failed");
        job->errorCode = resolvedCode;
        job->errorMessage = resolved;
        job->phase = resolved;
        job->finishedAt = QDateTime::currentDateTime();
        job->speedBytesPerSec = 0;
    }
    bumpDownloads();
    pumpDownloadQueue();
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
    if (ids.isEmpty()) {
        failJob(itemId, QStringLiteral("download_failed"), tr("Could not import that file."));
        pumpDownloadQueue();
        return;
    }
    if (job) {
        job->status = QStringLiteral("done");
        job->phase = tr("In the media bin");
        job->progress = 1;
        job->filePath = path;
        job->assetId = ids.first();
        job->finishedAt = QDateTime::currentDateTime();
        job->speedBytesPerSec = 0;
        if (job->title.isEmpty())
            job->title = displayName;
    }
    bumpDownloads();
    // A finished job frees a slot for whatever is waiting behind it.
    pumpDownloadQueue();
    emit downloadImported(itemId, displayName);
}
