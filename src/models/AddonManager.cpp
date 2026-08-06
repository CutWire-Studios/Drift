#include "AddonManager.h"

#include "AddonEndpoint.h"
#include "VersionCompare.h"
#include "engine/AddonPackage.h"
#include "engine/AddonRegistry.h"
#include "engine/AudioEffectCatalog.h"
#include "engine/EffectCatalog.h"
#include "engine/EffectTemplateCatalog.h"
#include "engine/EmojiCatalog.h"
#include "engine/FontCatalog.h"
#include "engine/FacePropCatalog.h"
#include "engine/OrtRuntime.h"
#include "engine/StickerCatalog.h"
#include "engine/TransitionCatalog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QSettings>
#include <QtConcurrent>

#include <atomic>
#include <utility>

using namespace drift::addon;

namespace {

constexpr qint64 kIndexMaxAgeSeconds = 6 * 60 * 60;

// The packs every install is nudged to keep as addons so shader/audio fixes can ship without an
// app release. Order matches the Extras category order (video → transitions → audio).
const QStringList kEssentialAddonIds = {
    QStringLiteral("effects.core"),
    QStringLiteral("transitions.core"),
    QStringLiteral("audioeffects.core"),
};

QString cachedIndexPath()
{
    return QDir(addonsDir()).filePath(QStringLiteral("index.json"));
}

QString settingsKey(const char *name)
{
    return QLatin1String("addons/") + QLatin1String(name);
}

QStringList kindsOf(const QJsonObject &addon)
{
    QStringList kinds;
    for (const QJsonValue &value : addon.value(QStringLiteral("provides")).toArray()) {
        const QString kind = value.toObject().value(QStringLiteral("kind")).toString();
        if (!kind.isEmpty())
            kinds.append(kind);
    }
    if (kinds.isEmpty())
        kinds.append(addon.value(QStringLiteral("kind")).toString());
    return kinds;
}

} // namespace

// Everything in flight for one addon. Held by shared_ptr so the extraction lambda can read the
// cancel flag after the manager has dropped its own reference.
struct AddonManager::Transfer
{
    QString id;
    QString version;
    QString packagePath;
    QPointer<QNetworkReply> reply;
    QFile file;
    std::atomic_bool cancelled{false};
    bool extracting = false;
};

AddonManager::AddonManager(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    QDir().mkpath(addonDownloadCacheDir());
    sweepDownloadCache();

    // Serve whatever we already know immediately, then go and check.
    QFile cached(cachedIndexPath());
    if (cached.open(QIODevice::ReadOnly))
        applyIndex(cached.readAll(), true);

    refresh();
}

AddonManager::~AddonManager() = default;

QString AddonManager::status() const
{
    return m_status;
}

bool AddonManager::refreshing() const
{
    return m_refreshing;
}

bool AddonManager::remindEssential() const
{
    return QSettings().value(settingsKey("remindEssential"), true).toBool();
}

void AddonManager::setRemindEssential(bool remind)
{
    if (remind == remindEssential())
        return;
    QSettings().setValue(settingsKey("remindEssential"), remind);
    emit remindEssentialChanged();
}

bool AddonManager::remindUpdates() const
{
    return QSettings().value(settingsKey("remindUpdates"), true).toBool();
}

void AddonManager::setRemindUpdates(bool remind)
{
    if (remind == remindUpdates())
        return;
    QSettings().setValue(settingsKey("remindUpdates"), remind);
    emit remindUpdatesChanged();
}

void AddonManager::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void AddonManager::setRefreshing(bool refreshing)
{
    if (m_refreshing == refreshing)
        return;
    m_refreshing = refreshing;
    emit refreshingChanged();
}

QVariantList AddonManager::catalog() const
{
    QVariantList rows;
    for (const QJsonObject &addon : m_remote) {
        const QString id = addon.value(QStringLiteral("id")).toString();
        const QString version = addon.value(QStringLiteral("version")).toString();
        const InstalledAddon *installed = installedAddon(id);

        QString state = QStringLiteral("available");
        if (const auto transfer = m_transfers.value(id))
            state = transfer->extracting ? QStringLiteral("installing") : QStringLiteral("downloading");
        else if (m_failures.contains(id))
            state = QStringLiteral("failed");
        else if (installed)
            state = drift::compareVersions(installed->version, version) < 0
                        ? QStringLiteral("update-available")
                        : QStringLiteral("installed");

        int items = 0;
        for (const QJsonValue &value : addon.value(QStringLiteral("provides")).toArray())
            items += value.toObject().value(QStringLiteral("items")).toInt();

        const QJsonArray platforms = addon.value(QStringLiteral("platforms")).toArray();
        const QString platform = platforms.isEmpty() ? QString() : platforms.at(0).toString();

        rows.append(QVariantMap{
            {QStringLiteral("id"), id},
            {QStringLiteral("name"), addon.value(QStringLiteral("name")).toString()},
            {QStringLiteral("description"), addon.value(QStringLiteral("description")).toString()},
            {QStringLiteral("details"), addon.value(QStringLiteral("details")).toString()},
            {QStringLiteral("author"), addon.value(QStringLiteral("author")).toString()},
            {QStringLiteral("license"), addon.value(QStringLiteral("license")).toString()},
            {QStringLiteral("kind"), addon.value(QStringLiteral("kind")).toString()},
            // A pack can provide several kinds; `kind` is only the headline one. The category
            // filter needs all of them or an Acceleration pack that also ships an EP disappears.
            {QStringLiteral("kinds"), kindsOf(addon)},
            {QStringLiteral("version"), version},
            {QStringLiteral("installedVersion"), installed ? installed->version : QString()},
            {QStringLiteral("downloadSize"), addon.value(QStringLiteral("downloadSize")).toDouble()},
            {QStringLiteral("installedSize"), addon.value(QStringLiteral("installedSize")).toDouble()},
            {QStringLiteral("items"), items},
            {QStringLiteral("state"), state},
            {QStringLiteral("error"), m_failures.value(id)},
            {QStringLiteral("platform"), platform},
        });
    }
    return rows;
}

bool AddonManager::hasKind(const QString &kind) const
{
    return !addonRootsForKind(kind).isEmpty();
}

QString AddonManager::firstAddonForKind(const QString &kind) const
{
    for (const QJsonObject &addon : m_remote) {
        if (kindsOf(addon).contains(kind))
            return addon.value(QStringLiteral("id")).toString();
    }
    return {};
}

QVariantList AddonManager::missingEssentialAddons() const
{
    QVariantList rows;
    for (const QString &id : kEssentialAddonIds) {
        if (installedAddon(id))
            continue;
        for (const QJsonObject &addon : m_remote) {
            if (addon.value(QStringLiteral("id")).toString() != id)
                continue;
            rows.append(QVariantMap{
                {QStringLiteral("id"), id},
                {QStringLiteral("name"), addon.value(QStringLiteral("name")).toString()},
                {QStringLiteral("version"), addon.value(QStringLiteral("version")).toString()},
                {QStringLiteral("description"), addon.value(QStringLiteral("description")).toString()},
                {QStringLiteral("downloadSize"), addon.value(QStringLiteral("downloadSize")).toDouble()},
            });
            break;
        }
    }
    return rows;
}

QVariantList AddonManager::updatableAddons() const
{
    QVariantList rows;
    for (const QVariant &row : catalog()) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("state")).toString() != QLatin1String("update-available"))
            continue;
        rows.append(QVariantMap{
            {QStringLiteral("id"), map.value(QStringLiteral("id"))},
            {QStringLiteral("name"), map.value(QStringLiteral("name"))},
            {QStringLiteral("version"), map.value(QStringLiteral("version"))},
            {QStringLiteral("installedVersion"), map.value(QStringLiteral("installedVersion"))},
            {QStringLiteral("downloadSize"), map.value(QStringLiteral("downloadSize"))},
        });
    }
    return rows;
}

bool AddonManager::runtimeAvailable() const
{
    return drift::ort::available();
}

QVariantList AddonManager::accelerationOptions() const
{
    // "auto" and "cpu" are always offered: auto is the default, and CPU is what every runtime can
    // do, so being able to pick it is how a user rules the GPU out when chasing a bad result.
    const QStringList installed = drift::ort::selectableVariants();
    const auto row = [&](const QString &value, const QString &label) {
        return QVariantMap{{QStringLiteral("value"), value},
                           {QStringLiteral("label"), label},
                           {QStringLiteral("available"), value == QLatin1String("auto")
                                                             || installed.contains(value)}};
    };

    QVariantList rows{row(QStringLiteral("auto"), tr("Automatic (recommended)")),
                      row(QStringLiteral("cpu"), tr("This computer"))};
    for (const QString &variant : installed) {
        if (variant == QLatin1String("cpu"))
            continue;
        if (variant == QLatin1String("cuda"))
            rows.append(row(variant, tr("NVIDIA graphics (faster)")));
        else if (variant == QLatin1String("webgpu"))
            rows.append(row(variant, tr("Graphics card (faster)")));
        else
            rows.append(row(variant, variant.toUpper()));
    }
    return rows;
}

QString AddonManager::acceleration() const
{
    return drift::ort::preferredVariant();
}

void AddonManager::setAcceleration(const QString &variant)
{
    if (variant == drift::ort::preferredVariant())
        return;
    drift::ort::setPreferredVariant(variant);

    // Sessions are built per use and read the preference then, so switching to a plugin EP takes
    // effect immediately — it layers onto the core already loaded. Switching to a *different core*
    // does not: that library is loaded once per process.
    const QString active = drift::ort::activeVariant();
    if (!active.isEmpty() && variant != active && variant != QLatin1String("auto")) {
        for (const drift::ort::RuntimeInfo &runtime : drift::ort::installedRuntimes()) {
            if (runtime.variant == variant) {
                m_runtimeRestartRequired = true;
                break;
            }
        }
    }
    emit kindChanged(QString::fromLatin1(drift::ort::kRuntimeKind));
}

bool AddonManager::runtimeRestartRequired() const
{
    return m_runtimeRestartRequired;
}

void AddonManager::refresh(bool force)
{
    if (m_refreshing)
        return;

    if (!addonServiceConfigured()) {
        setStatus(QStringLiteral("Downloads aren’t available in this version."));
        return;
    }

    if (!force) {
        const QFileInfo cached(cachedIndexPath());
        if (cached.exists() && cached.lastModified().secsTo(QDateTime::currentDateTime()) < kIndexMaxAgeSeconds)
            return;
    }

    setRefreshing(true);
    QNetworkRequest request{QUrl(kIndexUrl)};
    request.setRawHeader("X-Drift-Client", kClientToken.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        setRefreshing(false);

        if (reply->error() != QNetworkReply::NoError) {
            // Non-fatal: whatever is installed keeps working, and the cached index still lists it.
            setStatus(QStringLiteral("Couldn’t reach the download store: %1").arg(reply->errorString()));
            return;
        }

        const QByteArray body = reply->readAll();
        QSaveFile cache(cachedIndexPath());
        if (cache.open(QIODevice::WriteOnly)) {
            cache.write(body);
            cache.commit();
        }
        applyIndex(body, false);
        setStatus(QString());

        const QStringList waiting = std::exchange(m_awaitingFreshIndex, {});
        for (const QString &id : waiting)
            startDownload(id);
    });
}

void AddonManager::applyIndex(const QByteArray &json, bool fromCache)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.value(QStringLiteral("schema")).toInt() != 1) {
        if (!fromCache)
            setStatus(QStringLiteral("The download store returned something this version can’t read."));
        return;
    }

    // One index serves every platform, so rows that carry native code declare which ones they are
    // for and the rest are dropped before they ever reach the UI. An absent or empty list means
    // content that runs anywhere, which is everything except the Acceleration addons.
    const QString platform = currentPlatform();
    m_remote.clear();
    for (const QJsonValue &value : root.value(QStringLiteral("addons")).toArray()) {
        const QJsonObject addon = value.toObject();
        const QJsonArray platforms = addon.value(QStringLiteral("platforms")).toArray();
        if (!platforms.isEmpty() && !platforms.contains(QJsonValue(platform)))
            continue;
        m_remote.append(addon);
    }

    emit catalogChanged();
}

void AddonManager::install(const QString &id)
{
    if (m_transfers.contains(id) || !addonServiceConfigured())
        return;

    m_failures.remove(id);
    startDownload(id);
}

void AddonManager::startDownload(const QString &id)
{
    QJsonObject addon;
    for (const QJsonObject &candidate : m_remote) {
        if (candidate.value(QStringLiteral("id")).toString() == id) {
            addon = candidate;
            break;
        }
    }

    const QString url = addon.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        if (!m_awaitingFreshIndex.contains(id))
            m_awaitingFreshIndex.append(id);
        refresh(true);
        return;
    }

    auto transfer = std::make_shared<Transfer>();
    transfer->id = id;
    transfer->version = addon.value(QStringLiteral("version")).toString();
    transfer->packagePath = QDir(addonDownloadCacheDir())
                                .filePath(QStringLiteral("%1-%2.driftpkg").arg(id, transfer->version));

    // Resume where a previous attempt stopped rather than re-fetching hundreds of megabytes.
    const QString partial = transfer->packagePath + QStringLiteral(".part");
    const qint64 have = QFileInfo(partial).size();
    transfer->file.setFileName(partial);
    if (!transfer->file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        m_failures.insert(id, QStringLiteral("Cannot write to the download cache"));
        emit catalogChanged();
        return;
    }

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (have > 0)
        request.setRawHeader("Range", QByteArrayLiteral("bytes=") + QByteArray::number(have) + "-");

    transfer->reply = m_network->get(request);
    m_transfers.insert(id, transfer);
    emit catalogChanged();

    QNetworkReply *reply = transfer->reply;
    const double total = addon.value(QStringLiteral("downloadSize")).toDouble();

    connect(reply, &QNetworkReply::readyRead, this, [this, transfer] {
        if (transfer->reply)
            transfer->file.write(transfer->reply->readAll());
    });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, id, have, total](qint64 received, qint64) {
                const double done = double(have + received);
                emit progressChanged(id, total > 0 ? qMin(1.0, done / total) : 0.0,
                                     QStringLiteral("Downloading"));
            });
    connect(reply, &QNetworkReply::finished, this, [this, id] { finishDownload(id); });
}

void AddonManager::finishDownload(const QString &id)
{
    const auto transfer = m_transfers.value(id);
    if (!transfer)
        return;

    QNetworkReply *reply = transfer->reply;
    if (!reply)
        return;
    reply->deleteLater();

    transfer->file.write(reply->readAll());
    transfer->file.close();

    if (transfer->cancelled) {
        m_transfers.remove(id);
        emit catalogChanged();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        m_transfers.remove(id);

        // A rejected ticket means the index we started from has gone stale, not that anything is
        // wrong with the addon — fetch a fresh one and pick the download back up where it stopped.
        if (status == 403 && !m_retried.contains(id)) {
            m_retried.insert(id);
            m_awaitingFreshIndex.append(id);
            emit catalogChanged();
            refresh(true);
            return;
        }

        m_transfers.insert(id, transfer);
        // The .part file is deliberately left in place so the next attempt resumes.
        failTransfer(id, reply->errorString());
        return;
    }
    m_retried.remove(id);

    const QString partial = transfer->packagePath + QStringLiteral(".part");
    QFile::remove(transfer->packagePath);
    if (!QFile::rename(partial, transfer->packagePath)) {
        failTransfer(id, QStringLiteral("Could not finalise the download"));
        return;
    }

    beginExtract(id, transfer->packagePath);
}

void AddonManager::beginExtract(const QString &id, const QString &packagePath)
{
    const auto transfer = m_transfers.value(id);
    if (!transfer)
        return;

    transfer->extracting = true;
    emit catalogChanged();
    emit progressChanged(id, 0.0, QStringLiteral("Installing"));

    const QString destination = addonInstallDir(id);

    // Verification and decompression are seconds of work on a large model, so they never run on
    // the GUI thread. The lambda owns a copy of the shared_ptr, so the cancel flag stays alive.
    auto *watcher = new QFutureWatcher<QPair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<bool, QString>>::finished, this,
            [this, id, packagePath, watcher] {
                watcher->deleteLater();
                const auto [ok, message] = watcher->result();
                const auto finished = m_transfers.value(id);
                m_transfers.remove(id);
                QFile::remove(packagePath);

                if (!ok) {
                    if (finished && finished->cancelled) {
                        emit catalogChanged();
                        emit transferFailed(id, QStringLiteral("Cancelled"));
                        return;
                    }
                    m_failures.insert(id, message);
                    setStatus(message);
                    emit catalogChanged();
                    emit transferFailed(id, message);
                    return;
                }

                reloadAddonRegistry();
                const InstalledAddon *installed = installedAddon(id);
                QStringList kinds;
                if (installed) {
                    for (const InstalledProvide &provide : installed->provides)
                        kinds.append(provide.kind);
                }
                reloadForKinds(kinds);
                emit catalogChanged();
                emit transferSucceeded(id);
            });

    watcher->setFuture(QtConcurrent::run([transfer, packagePath, destination]() -> QPair<bool, QString> {
        PackageInfo info;
        QString error;
        const bool ok = drift::addon::install(
            packagePath, destination,
            [transfer](qint64, qint64) { return !transfer->cancelled; }, &info, &error);
        if (!ok)
            return {false, error};
        if (!recordInstalledAddon(info, &error))
            return {false, error};
        return {true, QString()};
    }));
}

void AddonManager::failTransfer(const QString &id, const QString &message)
{
    m_transfers.remove(id);
    m_failures.insert(id, message);
    setStatus(message);
    emit catalogChanged();
    emit transferFailed(id, message);
}

void AddonManager::cancel(const QString &id)
{
    const auto transfer = m_transfers.value(id);
    if (!transfer)
        return;

    transfer->cancelled = true;
    if (transfer->reply)
        transfer->reply->abort(); // the finished handler tidies up
}

void AddonManager::uninstall(const QString &id)
{
    const InstalledAddon *installed = installedAddon(id);
    if (!installed)
        return;

    QStringList kinds;
    for (const InstalledProvide &provide : installed->provides)
        kinds.append(provide.kind);

    QString error;
    QDir(addonInstallDir(id)).removeRecursively();
    forgetInstalledAddon(id, &error);
    reloadAddonRegistry();
    reloadForKinds(kinds);
    m_failures.remove(id);
    emit catalogChanged();
}

void AddonManager::reloadForKinds(const QStringList &kinds)
{
    for (const QString &kind : kinds) {
        // reloadFontCatalog touches QFontDatabase, so it has to be here on the GUI thread rather
        // than on the extraction worker — same constraint as the call in main().
        if (kind == QLatin1String("fonts"))
            reloadFontCatalog();
        else if (kind == QLatin1String("stickers"))
            reloadStickerCatalog();
        else if (kind == QLatin1String("face-props"))
            reloadFacePropCatalog();
        else if (kind == QLatin1String("emoji-font"))
            reloadEmojiCatalog();
        else if (kind == QLatin1String("effects"))
            reloadEffectCatalog();
        else if (kind == QLatin1String("transitions"))
            reloadTransitionCatalog();
        else if (kind == QLatin1String("audio-effects"))
            reloadAudioEffectCatalog();
        else if (kind == QLatin1String("effect-templates"))
            reloadEffectTemplateCatalog();
        else if (kind == QLatin1String(drift::ort::kRuntimeKind)
                 || kind == QLatin1String(drift::ort::kPluginEpKind)) {
            // Nothing to reload. But a runtime that has already been loaded stays loaded for the
            // life of the process, so an install or removal only lands on the next launch —
            // whereas installing the first one, before anything has loaded, works immediately.
            if (!drift::ort::activeVariant().isEmpty())
                m_runtimeRestartRequired = true;
        }
        // whisper-model, sam2-model and face-model need nothing: sessions are created lazily on
        // next use.
        emit kindChanged(kind);
    }
}

void AddonManager::sweepDownloadCache()
{
    // Completed .driftpkg files are removed after a successful install, so anything left here is
    // from a crash. Half-finished .part files are kept — the next install resumes them.
    QDir cache(addonDownloadCacheDir());
    const QStringList stale = cache.entryList({QStringLiteral("*.driftpkg")}, QDir::Files);
    for (const QString &name : stale)
        QFile::remove(cache.filePath(name));
}
