#include "app/online/assets/DriftAssetStore.h"

#include "app/online/downloads/DownloadCenter.h"
#include "app/online/marketplace/MarketInstall.h"
#include "app/online/marketplace/MarketTransport.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

using namespace drift::market;

namespace {

bool validKey(const QString &key)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$"));
    return re.match(key).hasMatch();
}

QString kindOf(const QVariantMap &item)
{
    const QString kind = item.value(QStringLiteral("extra")).toMap().value(QStringLiteral("kind")).toString();
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9-]{0,31}$"));
    return re.match(kind).hasMatch() ? kind : QString();
}

} // namespace

struct DriftAssetStore::Install
{
    QVariantMap item;
    QString variantId;
    QString key;
    QString kind;
    QString title;
    QString finalDir;
    QString partialDir;
    QList<InstallFile> files;
    qint64 totalBytes = 0;
    qint64 doneBytes = 0;
    int next = 0;
    bool cancelled = false;
    QPointer<Call> call;
    QPointer<FileFetch> fetch;
};

DriftAssetStore::DriftAssetStore(QObject *parent, MarketTransport *transport, DownloadCenter *center)
    : QObject(parent)
    , m_transport(transport ? transport : MarketTransport::instance())
    , m_center(center ? center : DownloadCenter::instance())
{
    const int prefix = int(qstrlen("drift-asset:"));
    m_center->registerHandler(
        QStringLiteral("drift-asset"), [this, prefix](const QString &id) { cancel(id.mid(prefix)); },
        [this, prefix](const QString &id) {
            const auto it = m_retry.constFind(id.mid(prefix));
            if (it != m_retry.constEnd())
                install(it->first, it->second);
        });
}

QString DriftAssetStore::installRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/drift-assets");
}

QString DriftAssetStore::jobId(const QString &key)
{
    return QStringLiteral("drift-asset:") + key;
}

QString DriftAssetStore::installKey(const QVariantMap &item, const QString &variantId) const
{
    const QString id = item.value(QStringLiteral("id")).toString();
    const QVariantList variants = item.value(QStringLiteral("variants")).toList();
    if (variantId.isEmpty() || variants.isEmpty()
        || variants.first().toMap().value(QStringLiteral("id")).toString() == variantId)
        return id;
    return id + QStringLiteral("--") + variantId;
}

QString DriftAssetStore::kindDir(const QString &key) const
{
    const QDir root(installRoot());
    for (const QString &kind : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo(root.filePath(kind + QLatin1Char('/') + key)).isDir())
            return root.filePath(kind + QLatin1Char('/') + key);
    }
    return {};
}

QString DriftAssetStore::state(const QString &key) const
{
    const QString s = m_states.value(key);
    if (!s.isEmpty() && s != QLatin1String("installed"))
        return s;
    if (m_propLookup && m_propLookup(key))
        return QStringLiteral("installed");
    return kindDir(key).isEmpty() ? QStringLiteral("none") : QStringLiteral("installed");
}

QString DriftAssetStore::itemState(const QVariantMap &item, const QString &variantId) const
{
    return state(installKey(item, variantId));
}

QString DriftAssetStore::localPath(const QString &key) const
{
    const QString dir = kindDir(key);
    if (dir.isEmpty())
        return {};
    const QStringList names = QDir(dir).entryList(QDir::Files, QDir::Name);
    for (const char *suffix : {".glb", ".gltf", ".lottie", ".json"}) {
        for (const QString &name : names) {
            if (name.endsWith(QLatin1String(suffix), Qt::CaseInsensitive))
                return dir + QLatin1Char('/') + name;
        }
    }
    return names.isEmpty() ? QString() : dir + QLatin1Char('/') + names.first();
}

void DriftAssetStore::setState(const QString &key, const QString &state)
{
    m_states.insert(key, state);
    ++m_revision;
    emit revisionChanged();
    emit stateChanged(key);
}

void DriftAssetStore::publishStatus(const std::shared_ptr<Install> &install, const QString &status,
                                    const QString &phase, double progress, qint64 bytes, qint64 total,
                                    const QVariantMap &result, const QString &code,
                                    const QString &error, bool retryable)
{
    DownloadCenter::Job job;
    job.id = jobId(install->key);
    job.kind = QStringLiteral("drift-asset");
    job.title = install->title.isEmpty() ? install->key : install->title;
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

void DriftAssetStore::install(const QVariantMap &item, const QString &variantId)
{
    const QString id = item.value(QStringLiteral("id")).toString();
    const QString key = installKey(item, variantId);
    const QString kind = kindOf(item);
    if (!validKey(id) || !validKey(key) || kind.isEmpty() || m_installs.contains(key))
        return;
    m_retry.insert(key, {item, variantId});
    if (state(key) == QLatin1String("installed")) {
        const auto done = std::make_shared<Install>();
        done->key = key;
        done->kind = kind;
        finish(done);
        return;
    }

    auto install = std::make_shared<Install>();
    install->item = item;
    install->variantId = variantId;
    install->key = key;
    install->kind = kind;
    install->title = item.value(QStringLiteral("title")).toString();
    install->finalDir = installRoot() + QLatin1Char('/') + kind + QLatin1Char('/') + key;
    install->partialDir = install->finalDir + QStringLiteral(".partial");
    m_installs.insert(key, install);
    setState(key, QStringLiteral("installing"));
    begin(install);
}

void DriftAssetStore::begin(const std::shared_ptr<Install> &install)
{
    publishStatus(install, QStringLiteral("queued"), tr("Starting…"), 0);
    Call *call = requestInstall(m_transport, QStringLiteral("drift-assets"),
                                install->item.value(QStringLiteral("id")).toString(), install->variantId);
    install->call = call;
    connect(call, &Call::finished, this, [this, install](const Response &response) {
        if (install->cancelled)
            return;
        const InstallJob job = parseInstallJob(response);
        if (!job.ready()) {
            fail(install, job.errorCode.isEmpty() ? QStringLiteral("download_failed") : job.errorCode,
                 job.errorMessage, job.retryable);
            return;
        }
        for (const InstallFile &file : job.files) {
            if (safeFileName(file.name) != file.name) {
                fail(install, QStringLiteral("download_failed"), tr("That asset could not be installed."),
                     false);
                return;
            }
            install->totalBytes += file.size;
        }
        install->files = job.files;
        QDir(install->partialDir).removeRecursively();
        if (!QDir().mkpath(install->partialDir)) {
            fail(install, QStringLiteral("download_failed"), tr("Could not write to the app data folder."),
                 true);
            return;
        }
        fetchNext(install);
    });
}

void DriftAssetStore::fetchNext(const std::shared_ptr<Install> &install)
{
    if (install->next >= install->files.size()) {
        QDir(install->finalDir).removeRecursively();
        if (!QDir().rename(install->partialDir, install->finalDir)) {
            fail(install, QStringLiteral("download_failed"), tr("Could not write to the app data folder."),
                 true);
            return;
        }
        finish(install);
        return;
    }
    const InstallFile &file = install->files.at(install->next);
    auto *fetch = new FileFetch(m_transport, file, install->partialDir + QLatin1Char('/') + file.name, this);
    install->fetch = fetch;
    connect(fetch, &FileFetch::progress, this, [this, install](qint64 received, qint64) {
        const qint64 done = install->doneBytes + received;
        publishStatus(install, QStringLiteral("downloading"), tr("Downloading…"),
                      install->totalBytes > 0 ? double(done) / double(install->totalBytes) : 0, done,
                      install->totalBytes);
    });
    connect(fetch, &FileFetch::finished, this, [this, install](const QString &code, const QString &message) {
        if (install->cancelled)
            return;
        if (!code.isEmpty()) {
            fail(install, code, message, true);
            return;
        }
        install->doneBytes += install->files.at(install->next).size;
        ++install->next;
        fetchNext(install);
    });
    fetch->start();
}

void DriftAssetStore::finish(const std::shared_ptr<Install> &install)
{
    const QString key = install->key;
    m_installs.remove(key);
    if (install->kind == QLatin1String("face-prop")) {
        // A prop is installed once it is in the library; the staged copy has done its job.
        const QString staged = install->finalDir;
        if (QFileInfo::exists(staged)) {
            const QStringList added = m_propImporter ? m_propImporter(staged) : QStringList();
            QDir(staged).removeRecursively();
            if (!added.contains(key)) {
                fail(install, QStringLiteral("download_failed"), tr("Could not add that face prop."), true);
                return;
            }
        }
        setState(key, QStringLiteral("installed"));
        publishStatus(install, QStringLiteral("done"), tr("Added to face props"), 1);
        emit ready(key, QString());
        return;
    }

    publishStatus(install, QStringLiteral("importing"), tr("Importing…"), 1);
    const QString path = localPath(key);
    const QStringList ids = !path.isEmpty() && m_importer ? m_importer({path}) : QStringList();
    if (ids.isEmpty()) {
        fail(install, QStringLiteral("download_failed"), tr("Could not add that asset to the media bin."), true);
        return;
    }
    setState(key, QStringLiteral("installed"));
    publishStatus(install, QStringLiteral("done"), tr("In the media bin"), 1, 0, 0,
                  {{QStringLiteral("path"), path}, {QStringLiteral("assetId"), ids.first()}});
    emit ready(key, ids.first());
}

void DriftAssetStore::fail(const std::shared_ptr<Install> &install, const QString &code,
                           const QString &message, bool retryable)
{
    m_installs.remove(install->key);
    if (install->fetch)
        install->fetch->abort();
    QDir(install->partialDir).removeRecursively();
    setState(install->key, QStringLiteral("failed"));
    publishStatus(install, QStringLiteral("failed"), message, 0, 0, 0, {}, code, message, retryable);
    emit failed(install->key, message);
}

void DriftAssetStore::cancel(const QString &key)
{
    const auto install = m_installs.take(key);
    if (!install)
        return;
    install->cancelled = true;
    if (install->call)
        install->call->abort();
    if (install->fetch)
        install->fetch->abort();
    QDir(install->partialDir).removeRecursively();
    m_states.remove(key);
    ++m_revision;
    emit revisionChanged();
    emit stateChanged(key);
    publishStatus(install, QStringLiteral("cancelled"), tr("Cancelled"), 0, 0, 0, {}, {}, {}, true);
}
