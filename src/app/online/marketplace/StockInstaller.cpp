#include "app/online/marketplace/StockInstaller.h"

#include "app/online/downloads/DownloadCenter.h"
#include "app/online/marketplace/MarketInstall.h"
#include "app/online/marketplace/MarketTransport.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

using namespace drift::market;

namespace {

constexpr int kPollIntervalMs = 1000;
// Three at once: enough that a handful of clips do not run one behind the other, few enough
// that a click-happy session does not open a dozen sockets against a source that is already
// rate-limiting per client. The rest wait in "waiting" and start as slots free.
constexpr int kMaxConcurrentDownloads = 3;

QString downloadsRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/marketplace/downloads");
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

} // namespace

struct StockInstaller::Job
{
    QString source;
    QString itemId;
    QString serverJobId;
    QString title;
    QString mediaKind;
    QString variantId;
    QString destinationDir;
    QString filePath;
    QString assetId;
    QString status;
    QString phase;
    QString errorCode;
    QString error;
    bool retryable = false;
    bool cancelled = false;
    double progress = 0;
    qint64 bytesReceived = 0;
    qint64 bytesTotal = 0;
    QPointer<Call> call;
    QPointer<FileFetch> fetch;

    QString id() const { return StockInstaller::jobId(source, itemId); }
    bool running() const
    {
        return status == QLatin1String("queued") || status == QLatin1String("processing")
            || status == QLatin1String("downloading") || status == QLatin1String("importing");
    }
};

StockInstaller::StockInstaller(QObject *parent, MarketTransport *transport, DownloadCenter *center)
    : QObject(parent)
    , m_transport(transport ? transport : MarketTransport::instance())
    , m_center(center ? center : DownloadCenter::instance())
    , m_pollTimer(new QTimer(this))
    , m_resolveTimer(new QTimer(this))
{
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &StockInstaller::pollJobs);
    m_resolveTimer->setInterval(kPollIntervalMs);
    connect(m_resolveTimer, &QTimer::timeout, this, &StockInstaller::pollResolve);

    m_lastDownloadDir = QSettings().value(QStringLiteral("market/lastDownloadDir")).toUrl();
    if (configured())
        QDir().mkpath(downloadsRoot());

    m_center->registerHandler(
        QStringLiteral("stock"), [this](const QString &id) { cancelDownload(id); },
        [this](const QString &id) { retryDownload(id); });
}

StockInstaller::~StockInstaller() = default;

bool StockInstaller::configured() const
{
    return MarketTransport::configured();
}

void StockInstaller::setLastDownloadDir(const QUrl &dir)
{
    if (dir == m_lastDownloadDir)
        return;
    m_lastDownloadDir = dir;
    QSettings().setValue(QStringLiteral("market/lastDownloadDir"), dir);
    emit lastDownloadDirChanged();
}

int StockInstaller::maxConcurrentDownloads() const
{
    return kMaxConcurrentDownloads;
}

QString StockInstaller::jobId(const QString &source, const QString &itemId)
{
    return source + QLatin1Char('/') + itemId;
}

void StockInstaller::publish(const Job &job)
{
    DownloadCenter::Job out;
    out.id = job.id();
    out.kind = QStringLiteral("stock");
    out.title = job.title.isEmpty() ? job.itemId : job.title;
    out.mediaKind = job.mediaKind;
    out.status = job.status;
    out.phase = job.phase;
    out.progress = job.progress;
    out.bytesReceived = job.bytesReceived;
    out.bytesTotal = job.bytesTotal;
    out.error = job.error;
    out.errorCode = job.errorCode;
    out.retryable = job.retryable;
    out.cancellable = !DownloadCenter::isFinished(job.status);
    if (!job.filePath.isEmpty())
        out.result.insert(QStringLiteral("path"), job.filePath);
    if (!job.assetId.isEmpty())
        out.result.insert(QStringLiteral("assetId"), job.assetId);
    out.result.insert(QStringLiteral("destinationDir"), job.destinationDir);
    m_center->upsert(out);
}

void StockInstaller::download(const QString &source, const QString &itemId, const QString &variantId,
                              const QUrl &destinationDir, const QString &title,
                              const QString &mediaKind)
{
    if (!configured() || itemId.isEmpty() || source.isEmpty())
        return;
    const QString id = jobId(source, itemId);
    if (const auto existing = m_jobs.value(id)) {
        if (!DownloadCenter::isFinished(existing->status))
            return;
        m_jobs.remove(id);
        m_order.removeAll(id);
    }

    auto job = std::make_shared<Job>();
    job->source = source;
    job->itemId = itemId;
    job->title = title;
    job->mediaKind = mediaKind;
    job->variantId = variantId;
    job->destinationDir = destinationDir.isLocalFile() ? destinationDir.toLocalFile() : QString();
    job->status = QStringLiteral("waiting");
    job->phase = tr("Waiting…");
    m_jobs.insert(id, job);
    m_order.append(id);
    publish(*job);
    pump();
}

QVariantMap StockInstaller::downloadInfo(const QString &id) const
{
    auto job = m_jobs.value(id);
    if (!job) {
        for (const auto &candidate : m_jobs) {
            if (candidate->itemId == id)
                job = candidate;
        }
    }
    if (!job)
        return {};
    return {
        {QStringLiteral("status"), job->status},
        {QStringLiteral("progress"), job->progress},
        {QStringLiteral("phase"), job->phase},
        {QStringLiteral("errorCode"), job->errorCode},
        {QStringLiteral("errorMessage"), job->error},
        {QStringLiteral("filePath"), job->filePath},
        {QStringLiteral("assetId"), job->assetId},
        {QStringLiteral("destinationDir"), job->destinationDir},
    };
}

void StockInstaller::pump()
{
    int running = 0;
    for (const auto &job : m_jobs) {
        if (job->running())
            ++running;
    }
    for (const QString &id : std::as_const(m_order)) {
        if (running >= kMaxConcurrentDownloads)
            return;
        const auto job = m_jobs.value(id);
        if (!job || job->status != QLatin1String("waiting") || job->cancelled)
            continue;
        begin(job);
        ++running;
    }
}

void StockInstaller::begin(const std::shared_ptr<Job> &job)
{
    job->status = QStringLiteral("queued");
    job->phase = tr("Starting…");
    publish(*job);

    Call *call = requestInstall(m_transport, job->source, job->itemId, job->variantId);
    job->call = call;
    connect(call, &Call::finished, this, [this, job](const Response &response) {
        if (job->cancelled)
            return;
        onInstallResponse(job, parseInstallJob(response));
    });
}

void StockInstaller::onInstallResponse(const std::shared_ptr<Job> &job, const InstallJob &install)
{
    if (install.failed()) {
        fail(job, install.errorCode, install.errorMessage, install.retryable);
        return;
    }
    job->serverJobId = install.id;
    job->status = install.ready() ? QStringLiteral("processing") : install.status;
    job->progress = install.progress;
    if (install.ready()) {
        if (const InstallFile *file = install.mainFile())
            fetchFile(job, *file);
        return;
    }
    job->phase = tr("Preparing…");
    publish(*job);
    if (!m_pollTimer->isActive())
        m_pollTimer->start();
}

void StockInstaller::pollJobs()
{
    bool any = false;
    for (const auto &job : std::as_const(m_jobs)) {
        if (job->cancelled || job->serverJobId.isEmpty() || DownloadCenter::isFinished(job->status))
            continue;
        if (job->status != QLatin1String("queued") && job->status != QLatin1String("processing"))
            continue;
        any = true;
        if (job->call)
            continue;
        Call *call = pollInstall(m_transport, job->serverJobId);
        job->call = call;
        connect(call, &Call::finished, this, [this, job](const Response &response) {
            if (job->cancelled)
                return;
            const InstallJob install = parseInstallJob(response);
            if (!install.failed() && !install.ready()) {
                job->status = install.status;
                job->progress = install.progress;
                job->phase = tr("Preparing…");
                publish(*job);
                return;
            }
            onInstallResponse(job, install);
        });
    }
    if (!any)
        m_pollTimer->stop();
}

void StockInstaller::fetchFile(const std::shared_ptr<Job> &job, const InstallFile &file)
{
    job->status = QStringLiteral("downloading");
    job->phase = tr("Downloading…");
    job->progress = 0;
    publish(*job);

    QString name = safeFileName(file.name);
    if (QFileInfo(name).suffix().isEmpty())
        name += extensionForMime(file.mime);
    // A folder the user picked is written to directly; without one the file goes to the
    // per-item app data area. Their own folder may already hold a file of this name, and
    // silently writing over it would destroy something we did not put there.
    QString dir = job->destinationDir;
    if (dir.isEmpty()) {
        QString safeId = job->itemId;
        safeId.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
        dir = downloadsRoot() + QLatin1Char('/') + safeId;
    }
    QDir().mkpath(dir);
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

    auto *fetch = new FileFetch(m_transport, file, path, this);
    job->fetch = fetch;
    connect(fetch, &FileFetch::progress, this, [this, job](qint64 received, qint64 total) {
        job->bytesReceived = received;
        job->bytesTotal = total;
        job->progress = total > 0 ? double(received) / double(total) : 0;
        publish(*job);
    });
    connect(fetch, &FileFetch::finished, this, [this, job](const QString &code, const QString &message) {
        if (job->cancelled)
            return;
        if (!code.isEmpty()) {
            fail(job, QStringLiteral("download_failed"), message, true);
            return;
        }
        importFile(job);
    });
    fetch->start();
}

void StockInstaller::importFile(const std::shared_ptr<Job> &job)
{
    job->status = QStringLiteral("importing");
    job->phase = tr("Importing…");
    job->progress = 1;
    publish(*job);
    const QStringList ids = m_importer ? m_importer({job->filePath}) : QStringList();
    if (ids.isEmpty()) {
        fail(job, QStringLiteral("download_failed"), tr("Could not import that file."), true);
        return;
    }
    job->status = QStringLiteral("done");
    job->phase = tr("In the media bin");
    job->assetId = ids.first();
    publish(*job);
    pump();
    emit downloadImported(job->id(), QFileInfo(job->filePath).completeBaseName());
}

void StockInstaller::fail(const std::shared_ptr<Job> &job, const QString &code, const QString &message,
                          bool retryable)
{
    job->status = QStringLiteral("failed");
    job->errorCode = code.isEmpty() ? QStringLiteral("download_failed") : code;
    job->error = message.trimmed().isEmpty() ? MarketTransport::fallbackMessageForCode(job->errorCode)
                                             : message.trimmed();
    job->phase = job->error;
    job->retryable = retryable;
    publish(*job);
    pump();
    emit downloadFailed(job->id(), job->errorCode, job->error);
}

void StockInstaller::cancelDownload(const QString &id)
{
    auto job = m_jobs.value(id);
    if (!job) {
        for (const auto &candidate : std::as_const(m_jobs)) {
            if (candidate->itemId == id)
                job = candidate;
        }
    }
    if (!job || DownloadCenter::isFinished(job->status))
        return;
    job->cancelled = true;
    job->status = QStringLiteral("cancelled");
    job->phase = tr("Cancelled");
    job->retryable = true;
    if (job->call)
        job->call->abort();
    if (job->fetch)
        job->fetch->abort();
    publish(*job);
    pump();
}

void StockInstaller::retryDownload(const QString &id)
{
    auto job = m_jobs.value(id);
    if (!job) {
        for (const auto &candidate : std::as_const(m_jobs)) {
            if (candidate->itemId == id)
                job = candidate;
        }
    }
    if (!job || !DownloadCenter::isFinished(job->status))
        return;
    job->cancelled = false;
    job->serverJobId.clear();
    job->errorCode.clear();
    job->error.clear();
    job->retryable = false;
    job->progress = 0;
    job->bytesReceived = 0;
    job->bytesTotal = 0;
    job->status = QStringLiteral("waiting");
    job->phase = tr("Waiting…");
    publish(*job);
    pump();
}

void StockInstaller::setResolveState(bool resolving, const QVariantMap &item, const QString &error,
                                     const QString &code, bool retryable)
{
    m_resolving = resolving;
    m_resolvedItem = item;
    m_resolveError = error;
    m_resolveErrorCode = code;
    m_resolveErrorRetryable = retryable;
    emit resolveChanged();
}

void StockInstaller::resolveUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    if (!configured() || trimmed.isEmpty())
        return;
    cancelResolve();
    setResolveState(true, {});
    const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("url"), trimmed}}).toJson(QJsonDocument::Compact);
    Call *call = m_transport->send("POST", QStringLiteral("/marketplace/resolve"), {}, body);
    m_resolveCall = call;
    connect(call, &Call::finished, this, [this](const Response &response) {
        if (!response.ok()) {
            const Problem problem = response.problem();
            setResolveState(false, {}, problem.message, problem.code, problem.retryable);
            return;
        }
        const QJsonObject job = QJsonDocument::fromJson(response.body).object();
        m_resolveJobId = job.value(QStringLiteral("id")).toString();
        const QString status = job.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("ready")) {
            m_resolveJobId.clear();
            setResolveState(false, job.value(QStringLiteral("item")).toObject().toVariantMap());
            return;
        }
        if (m_resolveJobId.isEmpty()) {
            setResolveState(false, {}, MarketTransport::fallbackMessageForCode(QStringLiteral("not_found")),
                            QStringLiteral("not_found"), false);
            return;
        }
        m_resolveTimer->start();
    });
}

void StockInstaller::cancelResolve()
{
    m_resolveTimer->stop();
    m_resolveJobId.clear();
    if (Call *call = m_resolveCall.data())
        call->abort();
    if (m_resolving)
        setResolveState(false, {});
}

void StockInstaller::pollResolve()
{
    if (m_resolveJobId.isEmpty()) {
        m_resolveTimer->stop();
        return;
    }
    if (m_resolveCall)
        return;
    Call *call = m_transport->send("GET", QStringLiteral("/marketplace/resolve/") + m_resolveJobId, {});
    m_resolveCall = call;
    connect(call, &Call::finished, this, [this](const Response &response) {
        if (!response.ok()) {
            m_resolveTimer->stop();
            m_resolveJobId.clear();
            const Problem problem = response.problem();
            setResolveState(false, {}, problem.message, problem.code, problem.retryable);
            return;
        }
        const QJsonObject job = QJsonDocument::fromJson(response.body).object();
        const QString status = job.value(QStringLiteral("status")).toString();
        if (status == QLatin1String("ready")) {
            m_resolveTimer->stop();
            m_resolveJobId.clear();
            setResolveState(false, job.value(QStringLiteral("item")).toObject().toVariantMap());
        } else if (status == QLatin1String("failed")) {
            m_resolveTimer->stop();
            m_resolveJobId.clear();
            const QJsonObject err = job.value(QStringLiteral("error")).toObject();
            const QString code = err.value(QStringLiteral("code")).toString();
            const QString detail = err.value(QStringLiteral("detail")).toString().trimmed();
            setResolveState(false, {}, detail.isEmpty() ? MarketTransport::fallbackMessageForCode(code) : detail,
                            code, MarketTransport::isRetryable(code, QString()));
        }
    });
}
