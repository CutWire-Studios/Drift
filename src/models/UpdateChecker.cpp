#include "UpdateChecker.h"

#include "UpdateAsset.h"
#include "VersionCompare.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDnsLookup>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>

#include <functional>
#include <memory>

// Configured in CMakeLists.txt (DRIFT_UPDATE_FEED_URL) and injected as a compile definition, the
// same way the addon service is, so a fork points at its own repository without touching code.
// A translation unit built outside the `drift` target gets no feed rather than a stale one.
#ifndef DRIFT_UPDATE_FEED_URL
#define DRIFT_UPDATE_FEED_URL ""
#endif

#ifndef DRIFT_DISTRIBUTION
#define DRIFT_DISTRIBUTION "source"
#endif

namespace {

// Once a day. The feed is GitHub's own API and the check is a single conditional-ish GET, but
// there is no reason to ask more often than releases happen.
constexpr qint64 kCheckIntervalSeconds = 24 * 60 * 60;

// Long enough that the first frame, the project restore and the addon index refresh are all past
// before a socket is opened. Nothing in the app waits on this.
constexpr int kStartupDelayMs = 5000;

constexpr int kTransferTimeoutMs = 15000;

// Aborts a download that stops moving. Large installers are fine; a hung socket is not.
constexpr int kDownloadIdleTimeoutMs = 60 * 1000;

const QString kFeedUrl = QStringLiteral(DRIFT_UPDATE_FEED_URL);
const QString kCurrentVersion = QStringLiteral(DRIFT_VERSION);
const QString kDistribution = QStringLiteral(DRIFT_DISTRIBUTION);

// The newest stable version. A TXT record, so the check is one DNS query and does not depend on
// GitHub's API answering before we know whether anything changed.
const QString kVersionHost = QStringLiteral("drift-version.cutwire.org");

QString settingsKey(const char *name)
{
    return QLatin1String("updates/") + QLatin1String(name);
}

QString shellSingleQuote(const QString &text)
{
    QString out = QStringLiteral("'");
    for (const QChar c : text) {
        if (c == QLatin1Char('\''))
            out += QLatin1String("'\\''");
        else
            out += c;
    }
    out += QLatin1Char('\'');
    return out;
}

QString powerShellQuote(const QString &text)
{
    QString escaped = text;
    escaped.replace(QLatin1Char('\''), QLatin1String("''"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString updatePlatform()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return {};
#endif
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this))
{
    QSettings settings;
    m_skippedVersion = settings.value(settingsKey("skippedVersion")).toString();
    m_announcedVersion = settings.value(settingsKey("announcedVersion")).toString();
    restorePending();

    // The helper has to be started from aboutToQuit: the installer cannot replace a binary that
    // is still mapped, and the unsaved-changes prompt has to be allowed to cancel the quit.
    if (qApp) {
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { launchStagedInstall(); });
    }

    scheduleStartup();
}

UpdateChecker::~UpdateChecker()
{
    if (m_reply)
        m_reply->abort();
}

bool UpdateChecker::supported() const
{
    return !kFeedUrl.isEmpty();
}

bool UpdateChecker::installSupported() const
{
#if defined(Q_OS_WIN)
    return supported() && kDistribution == QLatin1String("windows");
#elif defined(Q_OS_MACOS)
    return supported() && kDistribution == QLatin1String("macos");
#else
    return false;
#endif
}

bool UpdateChecker::enabled() const
{
    return QSettings().value(settingsKey("enabled"), true).toBool();
}

void UpdateChecker::setEnabled(bool enabled)
{
    if (enabled == this->enabled())
        return;
    QSettings().setValue(settingsKey("enabled"), enabled);
    emit enabledChanged();
}

bool UpdateChecker::checking() const
{
    return m_checking;
}

bool UpdateChecker::downloading() const
{
    return m_downloading;
}

bool UpdateChecker::preparing() const
{
    return m_preparing;
}

bool UpdateChecker::readyToInstall() const
{
    return m_readyToInstall;
}

qreal UpdateChecker::progress() const
{
    return m_progress;
}

bool UpdateChecker::updateAvailable() const
{
    return !m_latestVersion.isEmpty() && m_latestVersion != m_skippedVersion;
}

bool UpdateChecker::canInstall() const
{
    return installSupported() && !m_assetUrl.isEmpty() && !m_assetName.isEmpty();
}

QString UpdateChecker::currentVersion() const
{
    return kCurrentVersion;
}

QString UpdateChecker::latestVersion() const
{
    return m_latestVersion;
}

QString UpdateChecker::releaseNotes() const
{
    return m_releaseNotes;
}

QString UpdateChecker::releaseUrl() const
{
    return m_releaseUrl;
}

QString UpdateChecker::status() const
{
    return m_status;
}

QString UpdateChecker::error() const
{
    return m_error;
}

void UpdateChecker::setChecking(bool checking)
{
    if (m_checking == checking)
        return;
    m_checking = checking;
    emit activityChanged();
}

void UpdateChecker::setDownloading(bool downloading)
{
    if (m_downloading == downloading)
        return;
    m_downloading = downloading;
    emit activityChanged();
}

void UpdateChecker::setPreparing(bool preparing)
{
    if (m_preparing == preparing)
        return;
    m_preparing = preparing;
    emit activityChanged();
}

void UpdateChecker::setReadyToInstall(bool ready)
{
    if (m_readyToInstall == ready)
        return;
    m_readyToInstall = ready;
    emit activityChanged();
}

void UpdateChecker::setProgress(qreal progress)
{
    if (qAbs(m_progress - progress) < 0.005 && progress < 1.0)
        return;
    m_progress = progress;
    emit progressChanged();
}

void UpdateChecker::setStatus(const QString &status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void UpdateChecker::setError(const QString &error)
{
    if (m_error == error && (error.isEmpty() || m_status == error))
        return;
    m_error = error;
    if (!error.isEmpty())
        m_status = error;
    emit statusChanged();
}

void UpdateChecker::scheduleStartup()
{
    if (!supported() || !enabled())
        return;

    const QDateTime last = QSettings().value(settingsKey("lastCheck")).toDateTime();
    const bool due = !last.isValid()
            || last.secsTo(QDateTime::currentDateTimeUtc()) >= kCheckIntervalSeconds;

    QTimer::singleShot(kStartupDelayMs, this, [this, due] {
        if (due)
            check(false);
        else
            announceIfNeeded();
    });
}

void UpdateChecker::checkNow()
{
    // Asking explicitly un-skips: the user wants to be told about whatever is out there.
    if (!m_skippedVersion.isEmpty()) {
        m_skippedVersion.clear();
        QSettings().remove(settingsKey("skippedVersion"));
        emit resultChanged();
    }
    check(true);
}

void UpdateChecker::skipVersion()
{
    cancelDownload();
    m_installOnQuit = false;
    setReadyToInstall(false);
    if (m_latestVersion.isEmpty())
        return;
    m_skippedVersion = m_latestVersion;
    QSettings().setValue(settingsKey("skippedVersion"), m_skippedVersion);
    emit resultChanged();
}

void UpdateChecker::openDownloadPage()
{
    if (!m_releaseUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(m_releaseUrl));
}

void UpdateChecker::downloadAndInstall()
{
    if (!canInstall() || m_downloading || m_preparing)
        return;
    if (m_readyToInstall && !m_stagedPath.isEmpty()) {
        requestQuit();
        return;
    }
    beginDownload();
}

void UpdateChecker::requestQuit()
{
    if (!m_readyToInstall || m_stagedPath.isEmpty())
        return;
    m_installOnQuit = true;
    m_deferQuit = false;
    emit quitRequested();
}

void UpdateChecker::setInstallPromptOpen(bool open)
{
    m_deferQuit = !open;
}

void UpdateChecker::markAnnounced()
{
    if (m_latestVersion.isEmpty() || m_latestVersion == m_announcedVersion)
        return;
    m_announcedVersion = m_latestVersion;
    QSettings().setValue(settingsKey("announcedVersion"), m_announcedVersion);
}

void UpdateChecker::check(bool manual)
{
    if (m_checking || m_downloading || m_preparing || !supported())
        return;

    setChecking(true);
    if (manual)
        setStatus(QString());
    setError(QString());

    auto *dns = new QDnsLookup(QDnsLookup::TXT, kVersionHost, this);
    connect(dns, &QDnsLookup::finished, this, [this, dns, manual] {
        dns->deleteLater();

        if (dns->error() != QDnsLookup::NoError) {
            setChecking(false);
            // A background check that failed says nothing: being offline is not an error the user
            // asked about. Only record the attempt when the record was actually read, so a laptop
            // that launches offline all week still checks the day it has a connection.
            if (manual)
                setStatus(tr("Couldn’t check for updates: %1").arg(dns->errorString()));
            else
                announceIfNeeded();
            return;
        }

        QString version;
        const auto records = dns->textRecords();
        for (const QDnsTextRecord &record : records) {
            QByteArray joined;
            const auto values = record.values();
            for (const QByteArray &part : values)
                joined += part;
            version = drift::parseVersionText(joined);
            if (!version.isEmpty())
                break;
        }
        if (version.isEmpty()) {
            setChecking(false);
            if (manual)
                setStatus(tr("Couldn’t check for updates: unexpected response."));
            return;
        }

        QSettings().setValue(settingsKey("lastCheck"), QDateTime::currentDateTimeUtc());
        if (drift::compareVersions(kCurrentVersion, version) >= 0) {
            setChecking(false);
            clearRelease();
            if (manual)
                setStatus(tr("Drift %1 is the latest version.").arg(kCurrentVersion));
            return;
        }

        fetchRelease(version, manual);
    });
    dns->lookup();
}

void UpdateChecker::fetchRelease(const QString &version, bool manual)
{
    const QString api = drift::releaseTagApiUrl(kFeedUrl, version);
    if (api.isEmpty()) {
        publishFromVersion(version, manual);
        return;
    }

    QNetworkRequest request{QUrl(api)};
    // GitHub's API rejects requests that send no User-Agent, and pins response shape to an API
    // version so a future default cannot change the fields parsed below.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QLatin1String("Drift/") + kCurrentVersion);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual, version] {
        reply->deleteLater();
        setChecking(false);

        // The version already came from DNS. A missing release body still leaves a download
        // built from that version; notes and the checksum are the only things this request adds.
        if (reply->error() != QNetworkReply::NoError
            || !applyRelease(reply->readAll(), manual, version))
            publishFromVersion(version, manual);
    });
}

void UpdateChecker::publishFromVersion(const QString &version, bool manual)
{
    setChecking(false);
    const QString fileName = drift::installerFileName(updatePlatform(),
                                                      QSysInfo::currentCpuArchitecture(), version);
    publishRelease(version, QString(), drift::releasePageUrl(kFeedUrl, version), fileName,
                   drift::releaseDownloadUrl(kFeedUrl, version, fileName), QString(), 0);
    if (manual)
        setStatus(tr("Drift %1 is available.").arg(version));
    announceIfNeeded();
}

bool UpdateChecker::applyRelease(const QByteArray &json, bool manual, const QString &expectedVersion)
{
    const QJsonObject release = QJsonDocument::fromJson(json).object();

    // The tag URL already names one release; these checks refuse a draft or pre-release that
    // was published at the version the DNS record is advertising.
    const QString tag = release.value(QStringLiteral("tag_name")).toString();
    const QString version = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
    const bool usable = version == expectedVersion && !release.value(QStringLiteral("draft")).toBool()
            && !release.value(QStringLiteral("prerelease")).toBool();
    if (!usable)
        return false;

    const drift::ReleaseAsset asset = drift::selectReleaseAsset(
            release.value(QStringLiteral("assets")).toArray(), updatePlatform(),
            QSysInfo::currentCpuArchitecture());
    const QString assetName = asset.name.isEmpty()
            ? drift::installerFileName(updatePlatform(), QSysInfo::currentCpuArchitecture(), version)
            : asset.name;
    const QString assetUrl = asset.url.isEmpty()
            ? drift::releaseDownloadUrl(kFeedUrl, version, assetName)
            : asset.url;
    publishRelease(version, release.value(QStringLiteral("body")).toString(),
                   release.value(QStringLiteral("html_url")).toString(), assetName, assetUrl,
                   asset.sha256, asset.size);
    if (manual)
        setStatus(tr("Drift %1 is available.").arg(version));
    announceIfNeeded();
    return true;
}

void UpdateChecker::publishRelease(const QString &version, const QString &notes, const QString &htmlUrl,
                                   const QString &assetName, const QString &assetUrl,
                                   const QString &sha256, qint64 assetSize)
{
    m_latestVersion = version;
    m_releaseNotes = notes;
    m_releaseUrl = htmlUrl;
    m_assetName = assetName;
    m_assetUrl = assetUrl;
    m_assetSha256 = sha256;
    m_assetSize = assetSize;
    storePending();
    emit resultChanged();
}

void UpdateChecker::clearRelease()
{
    m_latestVersion.clear();
    m_releaseNotes.clear();
    m_releaseUrl.clear();
    m_assetName.clear();
    m_assetUrl.clear();
    m_assetSha256.clear();
    m_assetSize = 0;
    clearPending();
    emit resultChanged();
}

void UpdateChecker::restorePending()
{
    QSettings settings;
    const QString version = settings.value(settingsKey("pendingVersion")).toString();
    if (version.isEmpty() || version == m_skippedVersion)
        return;
    if (drift::compareVersions(kCurrentVersion, version) >= 0) {
        clearPending();
        return;
    }
    m_latestVersion = version;
    m_releaseNotes = settings.value(settingsKey("pendingNotes")).toString();
    m_releaseUrl = settings.value(settingsKey("pendingUrl")).toString();
    m_assetName = settings.value(settingsKey("pendingAssetName")).toString();
    m_assetUrl = settings.value(settingsKey("pendingAssetUrl")).toString();
    m_assetSha256 = settings.value(settingsKey("pendingAssetSha256")).toString();
    m_assetSize = settings.value(settingsKey("pendingAssetSize")).toLongLong();
}

void UpdateChecker::storePending()
{
    QSettings settings;
    settings.setValue(settingsKey("pendingVersion"), m_latestVersion);
    settings.setValue(settingsKey("pendingNotes"), m_releaseNotes);
    settings.setValue(settingsKey("pendingUrl"), m_releaseUrl);
    settings.setValue(settingsKey("pendingAssetName"), m_assetName);
    settings.setValue(settingsKey("pendingAssetUrl"), m_assetUrl);
    settings.setValue(settingsKey("pendingAssetSha256"), m_assetSha256);
    settings.setValue(settingsKey("pendingAssetSize"), m_assetSize);
}

void UpdateChecker::clearPending()
{
    QSettings settings;
    settings.remove(settingsKey("pendingVersion"));
    settings.remove(settingsKey("pendingNotes"));
    settings.remove(settingsKey("pendingUrl"));
    settings.remove(settingsKey("pendingAssetName"));
    settings.remove(settingsKey("pendingAssetUrl"));
    settings.remove(settingsKey("pendingAssetSha256"));
    settings.remove(settingsKey("pendingAssetSize"));
}

void UpdateChecker::announceIfNeeded()
{
    if (!installSupported() || !updateAvailable())
        return;
    if (m_latestVersion == m_announcedVersion)
        return;
    emit updateReady();
}

QString UpdateChecker::updatesDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::tempPath();
    const QString dir = base + QStringLiteral("/updates");
    QDir().mkpath(dir);
    return dir;
}

QString UpdateChecker::macBundleName() const
{
    if (kCurrentVersion.contains(QLatin1String("-nightly.")))
        return QStringLiteral("Drift Nightly");
    return QStringLiteral("Drift");
}

void UpdateChecker::cancelDownload()
{
    m_downloadCancelled = true;
    if (m_reply) {
        m_reply->abort();
        m_reply = nullptr;
    }
}

void UpdateChecker::beginDownload()
{
    setError(QString());
    setReadyToInstall(false);
    m_installOnQuit = false;
    m_stagedPath.clear();
    m_packagePath = updatesDir() + QLatin1Char('/') + m_assetName;

    // A previous attempt that finished is reused. A partial file is not: the hash is only known
    // once the whole body has been written.
    if (m_assetSize > 0 && QFileInfo(m_packagePath).size() == m_assetSize) {
        stagePackage();
        return;
    }
    QFile::remove(m_packagePath);

    m_partialPath = m_packagePath + QStringLiteral(".part");
    QFile::remove(m_partialPath);

    m_downloadFile = new QFile(m_partialPath, this);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) {
        delete m_downloadFile;
        m_downloadFile = nullptr;
        setError(tr("Couldn’t download the update: the cache isn’t writable."));
        return;
    }

    delete m_hasher;
    m_hasher = new QCryptographicHash(QCryptographicHash::Sha256);
    m_downloadCancelled = false;
    setProgress(0);
    setDownloading(true);

    QNetworkRequest request{QUrl(m_assetUrl)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QLatin1String("Drift/") + kCurrentVersion);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kDownloadIdleTimeoutMs);

    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_reply || !m_downloadFile)
            return;
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray chunk = m_reply->readAll();
        if (status != 0 && (status < 200 || status >= 300))
            return;
        if (m_downloadFile->write(chunk) != chunk.size()) {
            m_downloadCancelled = true;
            m_reply->abort();
            return;
        }
        if (m_hasher)
            m_hasher->addData(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 expected = total > 0 ? total : m_assetSize;
        if (expected > 0)
            setProgress(qMin(1.0, double(received) / double(expected)));
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] { finishDownload(); });
}

void UpdateChecker::finishDownload()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (reply)
        reply->deleteLater();

    const bool cancelled = m_downloadCancelled;
    const int status = reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
    if (reply && !cancelled && (status == 0 || (status >= 200 && status < 300))) {
        const QByteArray tail = reply->readAll();
        if (m_downloadFile && m_downloadFile->write(tail) == tail.size() && m_hasher)
            m_hasher->addData(tail);
    }

    const QByteArray digest = m_hasher ? m_hasher->result().toHex() : QByteArray();
    if (m_downloadFile) {
        m_downloadFile->close();
        m_downloadFile->deleteLater();
        m_downloadFile = nullptr;
    }
    delete m_hasher;
    m_hasher = nullptr;

    setDownloading(false);

    if (cancelled || !reply || reply->error() != QNetworkReply::NoError) {
        QFile::remove(m_partialPath);
        if (!cancelled)
            setError(tr("Couldn’t download the update: %1")
                             .arg(reply ? reply->errorString() : tr("the download was interrupted.")));
        return;
    }

    if (!m_assetSha256.isEmpty() && QString::fromLatin1(digest) != m_assetSha256) {
        QFile::remove(m_partialPath);
        setError(tr("Couldn’t download the update: the file didn’t match the release."));
        return;
    }

    QFile::remove(m_packagePath);
    if (!QFile::rename(m_partialPath, m_packagePath)) {
        QFile::remove(m_partialPath);
        setError(tr("Couldn’t download the update: the cache isn’t writable."));
        return;
    }

    setProgress(1);
    stagePackage();
}

void UpdateChecker::stagePackage()
{
#if defined(Q_OS_MACOS)
    stageMacApp();
#else
    m_stagedPath = m_packagePath;
    finishStage();
#endif
}

void UpdateChecker::runStageStep(const QString &program, const QStringList &arguments,
                                 const std::function<void(int, const QString &)> &done)
{
    auto *process = new QProcess(this);
    auto state = std::make_shared<bool>(false);
    std::function<void(int)> finish = [process, done, state](int code) {
        if (*state)
            return;
        *state = true;
        const QString detail = QString::fromLocal8Bit(process->readAllStandardError());
        process->deleteLater();
        done(code, detail);
    };
    connect(process, &QProcess::finished, this,
            [finish](int code, QProcess::ExitStatus status) {
                finish(status == QProcess::NormalExit ? code : -1);
            });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(-1);
    });
    process->start(program, arguments);
}

void UpdateChecker::stageMacApp()
{
    setPreparing(true);
    m_mountPoint = updatesDir() + QStringLiteral("/mount");
    QDir(m_mountPoint).removeRecursively();

    runStageStep(QStringLiteral("hdiutil"),
                 {QStringLiteral("attach"), QStringLiteral("-nobrowse"), QStringLiteral("-readonly"),
                  QStringLiteral("-mountpoint"), m_mountPoint, m_packagePath},
                 [this](int code, const QString &) {
                     if (code != 0) {
                         m_mountPoint.clear();
                         failPrepare(tr("Couldn’t prepare the update."));
                         return;
                     }
                     copyMacApp();
                 });
}

void UpdateChecker::copyMacApp()
{
    const QString bundle = macBundleName() + QStringLiteral(".app");
    QString source = m_mountPoint + QLatin1Char('/') + bundle;
    if (!QFileInfo::exists(source)) {
        const QStringList apps = QDir(m_mountPoint).entryList({QStringLiteral("*.app")}, QDir::Dirs);
        if (apps.size() == 1)
            source = m_mountPoint + QLatin1Char('/') + apps.first();
    }
    if (!QFileInfo::exists(source)) {
        detachMacImage(false);
        return;
    }

    const QString stagedDir = updatesDir() + QStringLiteral("/staged");
    QDir().mkpath(stagedDir);
    const QString dest = stagedDir + QLatin1Char('/') + QFileInfo(source).fileName();
    QDir(dest).removeRecursively();

    runStageStep(QStringLiteral("ditto"), {source, dest}, [this, dest](int code, const QString &) {
        if (code != 0) {
            detachMacImage(false);
            return;
        }
        m_stagedPath = dest;
        detachMacImage(true);
    });
}

void UpdateChecker::detachMacImage(bool thenInstall)
{
    const QString mount = m_mountPoint;
    m_mountPoint.clear();
    if (mount.isEmpty()) {
        if (thenInstall)
            finishStage();
        else
            failPrepare(tr("Couldn’t prepare the update."));
        return;
    }

    runStageStep(QStringLiteral("hdiutil"), {QStringLiteral("detach"), mount},
                 [this, thenInstall](int, const QString &) {
                     if (thenInstall)
                         finishStage();
                     else
                         failPrepare(tr("Couldn’t prepare the update."));
                 });
}

void UpdateChecker::failPrepare(const QString &message)
{
    setPreparing(false);
    setDownloading(false);
    m_stagedPath.clear();
    setError(message);
}

void UpdateChecker::finishStage()
{
    setDownloading(false);
    setPreparing(false);
    m_installOnQuit = true;
    setReadyToInstall(true);
    setProgress(1);
    if (m_deferQuit)
        setStatus(tr("Drift %1 will install when you quit.").arg(m_latestVersion));
    else
        emit quitRequested();
}

void UpdateChecker::launchStagedInstall()
{
    if (!m_installOnQuit || m_stagedPath.isEmpty() || !QFileInfo::exists(m_stagedPath))
        return;
    m_installOnQuit = false;

    const QString dir = updatesDir();
    const qint64 pid = QCoreApplication::applicationPid();

#if defined(Q_OS_WIN)
    const QString scriptPath = dir + QStringLiteral("/install-update.ps1");
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    // The helper waits out this process, then runs the Inno installer. /DRIFTUPDATE=1 is what
    // makes a silent install launch Drift again; without it skipifsilent would leave the user
    // with the app closed. -Verb RunAs is the UAC prompt — the installer writes to Program Files.
    const QString body = QStringLiteral(
            "$processId = %1\r\n"
            "$installer = %2\r\n"
            "while (Get-Process -Id $processId -ErrorAction SilentlyContinue) {\r\n"
            "    Start-Sleep -Milliseconds 400\r\n"
            "}\r\n"
            "try {\r\n"
            "    Start-Process -FilePath $installer -ArgumentList "
            "'/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/DRIFTUPDATE=1' "
            "-Verb RunAs -Wait\r\n"
            "} catch {\r\n"
            "    exit 1\r\n"
            "}\r\n")
                                 .arg(pid)
                                 .arg(powerShellQuote(QDir::toNativeSeparators(m_stagedPath)));
    script.write(body.toUtf8());
    script.close();

    QProcess helper;
    helper.setProgram(QStringLiteral("powershell.exe"));
    helper.setArguments({QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                         QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"),
                         QStringLiteral("Hidden"), QStringLiteral("-File"), scriptPath});
    helper.startDetached();
#elif defined(Q_OS_MACOS)
    const QString dest = QStringLiteral("/Applications/") + macBundleName() + QStringLiteral(".app");
    const QString copyPath = dir + QStringLiteral("/install-copy.sh");
    const QString waitPath = dir + QStringLiteral("/install-update.sh");

    QFile copyScript(copyPath);
    if (!copyScript.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    const QString copyBody = QStringLiteral(
            "#!/bin/bash\n"
            "set -euo pipefail\n"
            "rm -rf %1\n"
            "ditto %2 %1\n"
            "xattr -dr com.apple.quarantine %1 2>/dev/null || true\n")
                                     .arg(shellSingleQuote(dest), shellSingleQuote(m_stagedPath));
    copyScript.write(copyBody.toUtf8());
    copyScript.close();
    copyScript.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QFile waitScript(waitPath);
    if (!waitScript.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    // Copying into /Applications fails when that app is owned by root (the usual case after an
    // admin install). The password dialog is the fallback, and it has to happen after we have
    // exited or ditto is replacing a bundle that is still running.
    const QString waitBody = QStringLiteral(
            "#!/bin/bash\n"
            "set -u\n"
            "while kill -0 %1 2>/dev/null; do\n"
            "  sleep 0.4\n"
            "done\n"
            "if ! /bin/bash %2; then\n"
            "  /usr/bin/osascript -e \"do shell script \\\"/bin/bash %3\\\" with administrator privileges\"\n"
            "fi\n"
            "open %4\n")
                                     .arg(QString::number(pid), shellSingleQuote(copyPath),
                                          shellSingleQuote(copyPath), shellSingleQuote(dest));
    waitScript.write(waitBody.toUtf8());
    waitScript.close();
    waitScript.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);

    QProcess::startDetached(QStringLiteral("/bin/bash"), {waitPath});
#else
    Q_UNUSED(dir);
    Q_UNUSED(pid);
#endif
}
