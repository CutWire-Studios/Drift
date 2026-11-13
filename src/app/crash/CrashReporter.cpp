#include "app/crash/CrashReporter.h"

#include "app/GraphicsApiChoice.h"
#include "app/crash/CrashBundle.h"
#include "app/crash/CrashReason.h"
#include "app/crash/LogScrubber.h"
#include "app/platform/CrashHandler.h"
#include "engine/addons/AddonRegistry.h"
#include "engine/gpu/device/GpuDevice.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QUrl>

#ifndef DRIFT_DISTRIBUTION
#define DRIFT_DISTRIBUTION "source"
#endif

using namespace drift::crash;

namespace {

const QString kEndpoint = QStringLiteral("https://crash-reporting.cutwire.org/v1/report");
const QString kSettingKey = QStringLiteral("privacy/sendCrashReports");

constexpr qint64 kMaxUploadDump = 512 * 1024;
constexpr qsizetype kMaxUploadLog = 256 * 1024;
constexpr int kUploadTimeoutMs = 30 * 1000;

QString dataRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString logDir()
{
    return dataRoot() + QStringLiteral("/logs");
}

ScrubContext scrubContext()
{
    ScrubContext context;
    context.homeDir = QDir::homePath();
    context.userName = QDir::home().dirName();
    context.appDir = QCoreApplication::applicationDirPath();
    return context;
}

QString readText(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

Reason reasonFor(const CrashInfo &crash)
{
    Reason reason = mapReason(crash.code, crash.module, crash.modulePath, drift::addon::addonsDir());
    if (!reason.addonId.isEmpty()) {
        if (const auto *addon = drift::addon::installedAddon(reason.addonId); addon && !addon->name.isEmpty())
            reason.text.replace(reason.addonId, addon->name);
    }
    return reason;
}

} // namespace

CrashReporter::CrashReporter(QObject *parent)
    : QObject(parent)
    , m_store(dataRoot())
{
}

bool CrashReporter::supported() const
{
#ifdef DRIFT_HAS_CRASH_HANDLER
    return true;
#else
    return false;
#endif
}

bool CrashReporter::automaticReports() const
{
    return QSettings().value(kSettingKey, true).toBool();
}

void CrashReporter::setAutomaticReports(bool enabled)
{
    if (automaticReports() == enabled)
        return;
    QSettings().setValue(kSettingKey, enabled);
    emit automaticReportsChanged();
}

bool CrashReporter::safeMode() const
{
    return QCoreApplication::arguments().contains(QStringLiteral("--safe-mode"));
}

void CrashReporter::begin()
{
    const qint64 pid = QCoreApplication::applicationPid();
    const CrashStore::Startup startup = m_store.scan(&drift::crash::processAlive);
    m_store.beginSession(pid);
    m_unshown = startup.crashes;

    if (!m_unshown.isEmpty()) {
        m_kind = QStringLiteral("crash");
        const CrashInfo &newest = m_unshown.constFirst();
        const Reason reason = reasonFor(newest);
        m_reasonKey = reason.key;
        m_reasonText = reason.text;
        m_reasonLinkUrl = reason.linkUrl;
        m_reasonLinkLabel = reason.linkLabel;

        // The crashed run's log is the previous one now; keep its scrubbed tail with the crash so a
        // retry on a later launch still sends the right text.
        const QString snapshot = newest.dir + QStringLiteral("/upload-log.txt");
        if (!QFile::exists(snapshot)) {
            const QByteArray tail = tailOfLog(scrubLog(readText(logDir() + QStringLiteral("/drift.prev.log")),
                                                       scrubContext()),
                                              kMaxUploadLog);
            QFile file(snapshot);
            if (file.open(QIODevice::WriteOnly))
                file.write(tail);
        }
    } else if (startup.uncleanSessions > 0) {
        m_kind = QStringLiteral("unclean");
    }

    QObject::connect(qApp, &QCoreApplication::aboutToQuit, this, [this, pid] { m_store.endSession(pid); });
}

void CrashReporter::startUploads()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    m_store.sweep(now);
    m_queue = m_store.pendingUploads(now);
    if (m_queue.isEmpty())
        return;
    if (!automaticReports()) {
        // Turning it back on later must not send what was crashed with it off.
        for (const CrashInfo &crash : std::as_const(m_queue))
            m_store.markDone(crash);
        m_queue.clear();
        return;
    }
    m_system = gatherSystemInfo();
    m_network = new QNetworkAccessManager(this);
    uploadNext();
}

void CrashReporter::uploadNext()
{
    if (m_queue.isEmpty())
        return;
    const CrashInfo crash = m_queue.takeFirst();
    m_store.recordAttempt(crash);

    const Reason reason = mapReason(crash.code, crash.module, crash.modulePath, drift::addon::addonsDir());
    const QJsonObject meta = buildMeta(crash, reason.key, m_system, crash.uptimeSeconds);

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    const auto addPart = [multi](const QString &name, const QString &type, const QByteArray &body,
                                 const QString &fileName) {
        QHttpPart part;
        QString disposition = QStringLiteral("form-data; name=\"%1\"").arg(name);
        if (!fileName.isEmpty())
            disposition += QStringLiteral("; filename=\"%1\"").arg(fileName);
        part.setHeader(QNetworkRequest::ContentDispositionHeader, disposition);
        part.setHeader(QNetworkRequest::ContentTypeHeader, type);
        part.setBody(body);
        multi->append(part);
    };
    addPart(QStringLiteral("meta"), QStringLiteral("application/json"),
            QJsonDocument(meta).toJson(QJsonDocument::Compact), {});

    QFile dump(crash.dir + QStringLiteral("/upload.dmp"));
    if (dump.exists() && dump.size() <= kMaxUploadDump && dump.open(QIODevice::ReadOnly))
        addPart(QStringLiteral("minidump"), QStringLiteral("application/octet-stream"), dump.readAll(),
                QStringLiteral("upload.dmp"));
    QFile log(crash.dir + QStringLiteral("/upload-log.txt"));
    if (log.open(QIODevice::ReadOnly))
        addPart(QStringLiteral("log"), QStringLiteral("text/plain"), log.readAll(), QStringLiteral("drift.log"));

    QNetworkRequest request{QUrl(kEndpoint)};
    request.setTransferTimeout(kUploadTimeoutMs);
    QNetworkReply *reply = m_network->post(request, multi);
    multi->setParent(reply);
    reply->setProperty("crashDir", crash.dir);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finishUpload(reply); });
}

void CrashReporter::finishUpload(QNetworkReply *reply)
{
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // 400 and 413 will not get better by sending the same thing again.
    if (status == 200 || status == 201 || status == 400 || status == 413)
        m_store.markDone(readCrashInfo(reply->property("crashDir").toString()));
    else
        qInfo("crash report not sent (HTTP %d); will retry on a later launch", status);
    uploadNext();
}

SystemInfo CrashReporter::gatherSystemInfo() const
{
    SystemInfo info;
    info.appVersion = QStringLiteral(DRIFT_VERSION);
    info.channel = channelFor(info.appVersion, QStringLiteral(DRIFT_DISTRIBUTION));
    info.arch = archFor(QSysInfo::buildCpuArchitecture());
    info.osName = osNameFor(QSysInfo::kernelType());
#ifdef Q_OS_MACOS
    info.osVersion = QSysInfo::productVersion();
#else
    info.osVersion = QSysInfo::kernelVersion();
#endif
    info.osDescription = QSysInfo::prettyProductName();
    info.cpuCores = QThread::idealThreadCount();
    info.locale = QLocale().name().replace(QLatin1Char('_'), QLatin1Char('-'));
    info.graphicsApi = drift::graphics::storedApi();
    for (const drift::gpu::Adapter &adapter : drift::gpu::enumerateAdapters())
        info.gpus.append({adapter.vendor, adapter.name, {}});
    for (const drift::addon::InstalledAddon &addon : drift::addon::installedAddons())
        info.addons.append({addon.id, addon.version});
    return info;
}

void CrashReporter::saveCrashFile()
{
    if (m_unshown.isEmpty())
        saveBundle(nullptr, QStringLiteral("drift-diagnostics"));
    else
        saveBundle(&m_unshown.constFirst(), QStringLiteral("drift-crash"));
}

void CrashReporter::saveDiagnostics()
{
    saveBundle(nullptr, QStringLiteral("drift-diagnostics"));
}

void CrashReporter::saveBundle(const CrashInfo *crash, const QString &prefix)
{
    m_saveMessage.clear();
    const QString folder = QFileDialog::getExistingDirectory(
        nullptr, tr("Choose where to save the file"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (folder.isEmpty())
        return;

    const QString path = QDir(folder).filePath(
        prefix + QLatin1Char('-') + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmmss"))
        + QStringLiteral(".tar.zst"));
    const QString info = systemInfoText(gatherSystemInfo(), crash, m_reasonKey);
    QString error;
    QString appleReport;
#ifdef Q_OS_MACOS
    if (crash)
        appleReport = newestAppleReport(QDir::homePath() + QStringLiteral("/Library/Logs/DiagnosticReports"),
                                        crash->crashedAt.addSecs(-crash->uptimeSeconds));
#endif
    if (writeTarZst(path, bundleItems(crash ? crash->dir : QString(), logDir(), info, appleReport), &error))
        m_saveMessage = tr("Saved to %1").arg(QDir::toNativeSeparators(path));
    else
        m_saveMessage = tr("Could not save the file: %1").arg(error);
    emit saveMessageChanged();
}

void CrashReporter::restartInSafeMode()
{
    QStringList arguments = QCoreApplication::arguments().mid(1);
    if (!arguments.contains(QStringLiteral("--safe-mode")))
        arguments.append(QStringLiteral("--safe-mode"));
    QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
    QCoreApplication::quit();
}

void CrashReporter::dismiss()
{
    for (const CrashInfo &crash : std::as_const(m_unshown))
        m_store.markShown(crash);
    m_unshown.clear();
    emit dismissed();
}
