#include "app/online/marketplace/CommunityInstaller.h"

#include "app/online/downloads/DownloadCenter.h"
#include "app/online/marketplace/MarketInstall.h"
#include "app/online/marketplace/MarketTransport.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

using namespace drift::market;

namespace {

QString settingsKey(const QString &itemId)
{
    return QStringLiteral("marketplace/community/") + itemId;
}

QString versionOf(const QVariantMap &item)
{
    return item.value(QStringLiteral("extra")).toMap().value(QStringLiteral("version")).toString();
}

QString cacheRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/marketplace/community");
}

} // namespace

struct CommunityInstaller::Install
{
    QString source;
    QString kind;
    QString itemId;
    QString title;
    QString version;
    QString dir;
    bool cancelled = false;
    QPointer<Call> call;
    QPointer<FileFetch> fetch;
};

CommunityInstaller::CommunityInstaller(QObject *parent, MarketTransport *transport, DownloadCenter *center)
    : QObject(parent)
    , m_transport(transport ? transport : MarketTransport::instance())
    , m_center(center ? center : DownloadCenter::instance())
{
    m_center->registerHandler(
        QStringLiteral("community"), [this](const QString &id) { cancel(id); },
        [this](const QString &id) {
            for (const QVariantMap &item : std::as_const(m_items)) {
                if (jobId(item.value(QStringLiteral("source")).toString(),
                          item.value(QStringLiteral("id")).toString()) == id)
                    install(item);
            }
        });
}

void CommunityInstaller::setHandler(const QString &kind, Handler handler)
{
    m_handlers.insert(kind, std::move(handler));
}

QString CommunityInstaller::kindOf(const QString &source)
{
    static const QString prefix = QStringLiteral("community.");
    return source.startsWith(prefix) ? source.mid(prefix.size()) : QString();
}

QString CommunityInstaller::jobId(const QString &source, const QString &itemId)
{
    return source + QLatin1Char('/') + itemId;
}

QString CommunityInstaller::installedVersion(const QString &itemId) const
{
    return QSettings().value(settingsKey(itemId)).toString();
}

QString CommunityInstaller::state(const QVariantMap &item) const
{
    const QString id = item.value(QStringLiteral("id")).toString();
    const QString s = m_states.value(id);
    if (s == QLatin1String("installing") || s == QLatin1String("failed"))
        return s;
    const QString have = installedVersion(id);
    if (have.isEmpty())
        return QStringLiteral("none");
    const QString latest = versionOf(item);
    return !latest.isEmpty() && latest != have ? QStringLiteral("update") : QStringLiteral("installed");
}

QVariantMap CommunityInstaller::result(const QVariantMap &item) const
{
    return m_results.value(item.value(QStringLiteral("id")).toString());
}

void CommunityInstaller::setState(const std::shared_ptr<Install> &install, const QString &state)
{
    m_states.insert(install->itemId, state);
    ++m_revision;
    emit revisionChanged();
    emit stateChanged(install->source, install->itemId);
}

void CommunityInstaller::publish(const std::shared_ptr<Install> &install, const QString &status,
                                 const QString &phase, double progress, qint64 bytes, qint64 total,
                                 const QVariantMap &result, const QString &code, const QString &error,
                                 bool retryable)
{
    DownloadCenter::Job job;
    job.id = jobId(install->source, install->itemId);
    job.kind = QStringLiteral("community");
    job.title = install->title.isEmpty() ? install->itemId : install->title;
    job.mediaKind = install->kind;
    job.status = status;
    job.phase = phase;
    job.progress = progress;
    job.bytesReceived = bytes;
    job.bytesTotal = total;
    job.result = result;
    job.errorCode = code;
    job.error = error;
    job.retryable = retryable;
    job.cancellable = true;
    m_center->upsert(job);
}

void CommunityInstaller::install(const QVariantMap &item)
{
    const QString source = item.value(QStringLiteral("source")).toString();
    const QString itemId = item.value(QStringLiteral("id")).toString();
    const QString kind = kindOf(source);
    if (kind.isEmpty() || itemId.isEmpty() || m_installs.contains(itemId))
        return;
    m_items.insert(itemId, item);

    auto install = std::make_shared<Install>();
    install->source = source;
    install->kind = kind;
    install->itemId = itemId;
    install->title = item.value(QStringLiteral("title")).toString();
    install->version = versionOf(item);
    QString safeId = itemId;
    safeId.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    install->dir = cacheRoot() + QLatin1Char('/') + safeId;
    m_installs.insert(itemId, install);
    setState(install, QStringLiteral("installing"));
    publish(install, QStringLiteral("queued"), tr("Starting…"), 0);

    Call *call = requestInstall(m_transport, source, itemId, {});
    install->call = call;
    connect(call, &Call::finished, this, [this, install](const Response &response) {
        if (install->cancelled)
            return;
        const InstallJob job = parseInstallJob(response);
        const InstallFile *file = job.ready() ? job.mainFile() : nullptr;
        if (!file) {
            fail(install, job.errorCode.isEmpty() ? QStringLiteral("download_failed") : job.errorCode,
                 job.errorMessage, job.retryable);
            return;
        }
        QDir(install->dir).removeRecursively();
        const QString path = install->dir + QLatin1Char('/') + safeFileName(file->name);
        auto *fetch = new FileFetch(m_transport, *file, path, this);
        install->fetch = fetch;
        connect(fetch, &FileFetch::progress, this, [this, install](qint64 received, qint64 total) {
            publish(install, QStringLiteral("downloading"), tr("Downloading…"),
                    total > 0 ? double(received) / double(total) : 0, received, total);
        });
        connect(fetch, &FileFetch::finished, this,
                [this, install, path](const QString &code, const QString &message) {
            if (install->cancelled)
                return;
            if (!code.isEmpty()) {
                fail(install, code, message, true);
                return;
            }
            finishDownload(install, path);
        });
        fetch->start();
    });
}

void CommunityInstaller::finishDownload(const std::shared_ptr<Install> &install, const QString &path)
{
    publish(install, QStringLiteral("installing"), tr("Installing…"), 1);
    const Handler handler = m_handlers.value(install->kind);
    QString error;
    QString installedId;
    if (!handler)
        error = tr("This build cannot install that kind of item.");
    else
        installedId = handler(path, &error);
    QDir(install->dir).removeRecursively();
    if (!error.isEmpty()) {
        fail(install, QStringLiteral("install_failed"), error, false);
        return;
    }

    QSettings().setValue(settingsKey(install->itemId), install->version.isEmpty() ? QStringLiteral("0") : install->version);
    const QVariantMap result{{QStringLiteral("kind"), install->kind},
                             {QStringLiteral("itemId"), install->itemId},
                             {QStringLiteral("id"), installedId.isEmpty() ? install->itemId : installedId}};
    m_installs.remove(install->itemId);
    m_results.insert(install->itemId, result);
    setState(install, QStringLiteral("installed"));
    publish(install, QStringLiteral("done"), tr("Installed"), 1, 0, 0, result);
    emit installed(install->source, install->itemId, result);
}

void CommunityInstaller::fail(const std::shared_ptr<Install> &install, const QString &code,
                              const QString &message, bool retryable)
{
    m_installs.remove(install->itemId);
    if (install->fetch)
        install->fetch->abort();
    QDir(install->dir).removeRecursively();
    setState(install, QStringLiteral("failed"));
    publish(install, QStringLiteral("failed"), message, 0, 0, 0, {}, code, message, retryable);
    emit failed(install->source, install->itemId, code, message);
}

void CommunityInstaller::cancel(const QString &id)
{
    const auto installs = m_installs.values();
    for (const auto &install : installs) {
        if (jobId(install->source, install->itemId) != id)
            continue;
        install->cancelled = true;
        m_installs.remove(install->itemId);
        if (install->call)
            install->call->abort();
        if (install->fetch)
            install->fetch->abort();
        QDir(install->dir).removeRecursively();
        m_states.remove(install->itemId);
        ++m_revision;
        emit revisionChanged();
        emit stateChanged(install->source, install->itemId);
        publish(install, QStringLiteral("cancelled"), tr("Cancelled"), 0, 0, 0, {}, {}, {}, true);
        return;
    }
}
