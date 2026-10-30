#include "app/online/assets/SfxLibrary.h"

#include "app/online/downloads/DownloadCenter.h"
#include "app/online/marketplace/MarketInstall.h"
#include "app/online/marketplace/MarketTransport.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

using namespace drift::market;

namespace {

bool validId(const QString &id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$"));
    return re.match(id).hasMatch();
}

} // namespace

struct SfxLibrary::Install
{
    QString id;
    QString title;
    QString dir;
    QString path;
    bool cancelled = false;
    QPointer<Call> call;
    QPointer<FileFetch> fetch;
};

SfxLibrary::SfxLibrary(QObject *parent, MarketTransport *transport, DownloadCenter *center)
    : QObject(parent)
    , m_transport(transport ? transport : MarketTransport::instance())
    , m_center(center ? center : DownloadCenter::instance())
{
    const int prefix = int(qstrlen("sfx:"));
    m_center->registerHandler(
        QStringLiteral("sfx"), [this, prefix](const QString &id) { cancel(id.mid(prefix)); },
        [this, prefix](const QString &id) { install(m_items.value(id.mid(prefix))); });
}

QString SfxLibrary::installRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/sfx");
}

QString SfxLibrary::jobId(const QString &id)
{
    return QStringLiteral("sfx:") + id;
}

QString SfxLibrary::localPath(const QString &id) const
{
    if (!validId(id))
        return {};
    const QDir dir(installRoot() + QLatin1Char('/') + id);
    const QStringList names = dir.entryList({QStringLiteral("*"), QStringLiteral("!*.partial")}, QDir::Files, QDir::Name);
    return names.isEmpty() ? QString() : dir.filePath(names.first());
}

QString SfxLibrary::state(const QString &id) const
{
    const QString s = m_states.value(id);
    if (!s.isEmpty() && s != QLatin1String("installed"))
        return s;
    return localPath(id).isEmpty() ? QStringLiteral("none") : QStringLiteral("installed");
}

QVariantList SfxLibrary::peaksOf(const QVariantMap &item) const
{
    QVariantList peaks;
    const QString encoded = item.value(QStringLiteral("extra")).toMap().value(QStringLiteral("peaks")).toString();
    for (const char b : QByteArray::fromBase64(encoded.toLatin1()))
        peaks.append(static_cast<unsigned char>(b) / 255.0);
    return peaks;
}

void SfxLibrary::setState(const QString &id, const QString &state)
{
    m_states.insert(id, state);
    ++m_revision;
    emit revisionChanged();
    emit stateChanged(id);
}

void SfxLibrary::publishStatus(const std::shared_ptr<Install> &install, const QString &status,
                               const QString &phase, double progress, qint64 bytes, qint64 total,
                               const QVariantMap &result, const QString &code, const QString &error,
                               bool retryable)
{
    DownloadCenter::Job job;
    job.id = jobId(install->id);
    job.kind = QStringLiteral("sfx");
    job.title = install->title.isEmpty() ? install->id : install->title;
    job.mediaKind = QStringLiteral("audio");
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

void SfxLibrary::install(const QVariantMap &item)
{
    const QString id = item.value(QStringLiteral("id")).toString();
    if (!validId(id) || m_installs.contains(id))
        return;
    m_items.insert(id, item);

    auto install = std::make_shared<Install>();
    install->id = id;
    install->title = item.value(QStringLiteral("title")).toString();
    install->dir = installRoot() + QLatin1Char('/') + id;
    if (!localPath(id).isEmpty()) {
        install->path = localPath(id);
        finish(install);
        return;
    }

    m_installs.insert(id, install);
    setState(id, QStringLiteral("installing"));
    publishStatus(install, QStringLiteral("queued"), tr("Starting…"), 0);
    Call *call = requestInstall(m_transport, QStringLiteral("sfx"), id, {});
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
        install->path = install->dir + QLatin1Char('/') + safeFileName(file->name);
        auto *fetch = new FileFetch(m_transport, *file, install->path + QStringLiteral(".partial"), this);
        install->fetch = fetch;
        connect(fetch, &FileFetch::progress, this, [this, install](qint64 received, qint64 total) {
            publishStatus(install, QStringLiteral("downloading"), tr("Downloading…"),
                          total > 0 ? double(received) / double(total) : 0, received, total);
        });
        connect(fetch, &FileFetch::finished, this, [this, install](const QString &code, const QString &message) {
            if (install->cancelled)
                return;
            const QString partial = install->path + QStringLiteral(".partial");
            if (!code.isEmpty()) {
                QFile::remove(partial);
                fail(install, code, message, true);
                return;
            }
            QFile::remove(install->path);
            if (!QFile::rename(partial, install->path)) {
                QFile::remove(partial);
                fail(install, QStringLiteral("download_failed"), tr("Could not write to the app data folder."),
                     true);
                return;
            }
            m_installs.remove(install->id);
            finish(install);
        });
        fetch->start();
    });
}

void SfxLibrary::finish(const std::shared_ptr<Install> &install)
{
    publishStatus(install, QStringLiteral("importing"), tr("Importing…"), 1);
    const QStringList ids = m_importer ? m_importer({install->path}) : QStringList();
    if (ids.isEmpty()) {
        fail(install, QStringLiteral("download_failed"), tr("Could not add that sound to the media bin."), true);
        return;
    }
    setState(install->id, QStringLiteral("installed"));
    publishStatus(install, QStringLiteral("done"), tr("In the media bin"), 1, 0, 0,
                  {{QStringLiteral("path"), install->path}, {QStringLiteral("assetId"), ids.first()}});
    emit ready(install->id, ids.first());
}

void SfxLibrary::fail(const std::shared_ptr<Install> &install, const QString &code, const QString &message,
                      bool retryable)
{
    m_installs.remove(install->id);
    if (install->fetch)
        install->fetch->abort();
    setState(install->id, QStringLiteral("failed"));
    publishStatus(install, QStringLiteral("failed"), message, 0, 0, 0, {}, code, message, retryable);
    emit failed(install->id, message);
}

void SfxLibrary::cancel(const QString &id)
{
    const auto install = m_installs.take(id);
    if (!install)
        return;
    install->cancelled = true;
    if (install->call)
        install->call->abort();
    if (install->fetch)
        install->fetch->abort();
    QFile::remove(install->path + QStringLiteral(".partial"));
    m_states.remove(id);
    ++m_revision;
    emit revisionChanged();
    emit stateChanged(id);
    publishStatus(install, QStringLiteral("cancelled"), tr("Cancelled"), 0, 0, 0, {}, {}, {}, true);
}
