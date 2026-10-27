#include "app/platform/RunningInstances.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSet>
#include <QStandardPaths>

#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <signal.h>
#endif

namespace drift::instances {
namespace {

QString markerDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("instances"));
}

} // namespace

bool processAlive(qint64 pid)
{
    // Out of range would truncate, and kill(0, 0) probes our own process group.
    if (pid <= 0 || pid > std::numeric_limits<qint32>::max())
        return false;
#ifdef Q_OS_WIN
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
#else
    // EPERM: it exists but belongs to someone else, which still means the pid is taken.
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

void registerThisInstance()
{
    const QString dir = markerDir();
    if (dir.isEmpty())
        return;
    // Per location rather than once per process: tests switch the application name, and with
    // it AppData, between cases.
    static QSet<QString> registered;
    if (registered.contains(dir))
        return;
    if (!QDir().mkpath(dir))
        return;
    const QString path =
        QDir(dir).filePath(QString::number(QCoreApplication::applicationPid()));
    QFile marker(path);
    if (!marker.open(QIODevice::WriteOnly))
        return;
    marker.close();
    registered.insert(dir);
    if (QCoreApplication *app = QCoreApplication::instance())
        QObject::connect(app, &QCoreApplication::aboutToQuit, [path] { QFile::remove(path); });
}

std::unique_ptr<QLockFile> lockStartup(int timeoutMs)
{
    const QString dir = markerDir();
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return nullptr;
    auto lock = std::make_unique<QLockFile>(QDir(dir).filePath(QStringLiteral("startup.lock")));
    // No age limit: a sweep over a large projects/ can take a while, and a holder that
    // crashed is still recognised by its pid.
    lock->setStaleLockTime(0);
    if (timeoutMs >= 0) {
        if (!lock->tryLock(timeoutMs))
            return nullptr;
        return lock;
    }

    constexpr int kWarnAfterMs = 5000;
    if (lock->tryLock(kWarnAfterMs))
        return lock;
    if (lock->error() != QLockFile::LockFailedError)
        return nullptr;
    qWarning("Waiting for another Drift instance to finish its startup cleanup");
    if (!lock->tryLock(-1))
        return nullptr;
    return lock;
}

bool otherInstanceRunning()
{
    const QString dir = markerDir();
    if (dir.isEmpty())
        return false;
    const qint64 self = QCoreApplication::applicationPid();
    bool other = false;
    const QStringList names = QDir(dir).entryList(QDir::Files);
    for (const QString &name : names) {
        bool ok = false;
        const qint64 pid = name.toLongLong(&ok);
        if (!ok || pid == self)
            continue;
        if (processAlive(pid))
            other = true;
        else
            QFile::remove(QDir(dir).filePath(name));
    }
    return other;
}

} // namespace drift::instances
