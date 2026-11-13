#include "app/crash/CrashStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace drift::crash {

namespace {

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void touch(const QString &path)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.close();
}

} // namespace

CrashInfo readCrashInfo(const QString &crashDir)
{
    const QJsonObject json = readJson(crashDir + QStringLiteral("/crash.json"));
    CrashInfo info;
    info.dir = crashDir;
    info.id = QFileInfo(crashDir).fileName();
    info.code = json.value(QStringLiteral("code")).toString();
    info.module = json.value(QStringLiteral("module")).toString();
    info.modulePath = json.value(QStringLiteral("modulePath")).toString();
    info.moduleVersion = json.value(QStringLiteral("moduleVersion")).toString();
    info.offset = json.value(QStringLiteral("offset")).toString();
    info.crashedAt = QDateTime::fromString(json.value(QStringLiteral("crashedAt")).toString(), Qt::ISODate);
    info.uptimeSeconds = json.value(QStringLiteral("uptimeSeconds")).toInt();
    return info;
}

CrashStore::CrashStore(const QString &root)
    : m_root(root)
{
}

void CrashStore::beginSession(qint64 pid) const
{
    QDir().mkpath(sessionsDir());
    touch(sessionsDir() + QLatin1Char('/') + QString::number(pid) + QStringLiteral(".running"));
}

void CrashStore::endSession(qint64 pid) const
{
    QFile::remove(sessionsDir() + QLatin1Char('/') + QString::number(pid) + QStringLiteral(".running"));
}

CrashStore::Startup CrashStore::scan(const std::function<bool(qint64)> &alive) const
{
    Startup result;

    const QFileInfoList markers = QDir(sessionsDir()).entryInfoList({QStringLiteral("*.running")}, QDir::Files);
    for (const QFileInfo &marker : markers) {
        if (alive(marker.baseName().toLongLong()))
            continue;
        QFile::remove(marker.absoluteFilePath());
        ++result.uncleanSessions;
    }

    const QFileInfoList dirs = QDir(crashesDir()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &dir : dirs) {
        if (QFile::exists(dir.absoluteFilePath() + QStringLiteral("/shown")))
            continue;
        const CrashInfo info = readCrashInfo(dir.absoluteFilePath());
        if (info.valid())
            result.crashes.append(info);
    }
    std::sort(result.crashes.begin(), result.crashes.end(),
              [](const CrashInfo &a, const CrashInfo &b) { return a.crashedAt > b.crashedAt; });
    return result;
}

void CrashStore::markShown(const CrashInfo &crash) const
{
    touch(crash.dir + QStringLiteral("/shown"));
}

int CrashStore::attempts(const CrashInfo &crash) const
{
    return readJson(crash.dir + QStringLiteral("/upload.json")).value(QStringLiteral("attempts")).toInt();
}

void CrashStore::recordAttempt(const CrashInfo &crash) const
{
    QJsonObject state = readJson(crash.dir + QStringLiteral("/upload.json"));
    state.insert(QStringLiteral("attempts"), state.value(QStringLiteral("attempts")).toInt() + 1);
    writeJson(crash.dir + QStringLiteral("/upload.json"), state);
}

void CrashStore::markDone(const CrashInfo &crash) const
{
    QJsonObject state = readJson(crash.dir + QStringLiteral("/upload.json"));
    state.insert(QStringLiteral("done"), true);
    writeJson(crash.dir + QStringLiteral("/upload.json"), state);
}

bool CrashStore::finished(const CrashInfo &crash, const QDateTime &now) const
{
    const QJsonObject state = readJson(crash.dir + QStringLiteral("/upload.json"));
    if (state.value(QStringLiteral("done")).toBool())
        return true;
    if (state.value(QStringLiteral("attempts")).toInt() >= kMaxUploadAttempts)
        return true;
    return crash.crashedAt.isValid() && crash.crashedAt.daysTo(now) >= kUploadGiveUpDays;
}

QList<CrashInfo> CrashStore::pendingUploads(const QDateTime &now) const
{
    QList<CrashInfo> pending;
    const QFileInfoList dirs = QDir(crashesDir()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &dir : dirs) {
        const CrashInfo info = readCrashInfo(dir.absoluteFilePath());
        if (info.valid() && !finished(info, now))
            pending.append(info);
    }
    std::sort(pending.begin(), pending.end(),
              [](const CrashInfo &a, const CrashInfo &b) { return a.crashedAt < b.crashedAt; });
    return pending;
}

void CrashStore::sweep(const QDateTime &now) const
{
    const QFileInfoList dirs = QDir(crashesDir()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &dir : dirs) {
        const CrashInfo info = readCrashInfo(dir.absoluteFilePath());
        const bool shown = QFile::exists(dir.absoluteFilePath() + QStringLiteral("/shown"));
        if (!info.valid() || (shown && finished(info, now)))
            QDir(dir.absoluteFilePath()).removeRecursively();
    }
}

} // namespace drift::crash
