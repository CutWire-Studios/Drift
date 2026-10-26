#include "app/mcp/protocol/McpSession.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
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

namespace drift::mcp {
namespace {

QString defaultSessionDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(base).filePath(QStringLiteral("drift/mcp-sessions"));
}

QString pinnedSessionPath()
{
    return qEnvironmentVariable("DRIFT_MCP_SESSION_PATH");
}

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

bool parseSession(const QString &path, SessionInfo *info)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return false;
    const QJsonObject o = doc.object();
    const int port = o.value(QStringLiteral("port")).toInt();
    const QString token = o.value(QStringLiteral("token")).toString();
    if (port <= 0 || port > 65535 || token.isEmpty())
        return false;
    info->port = static_cast<quint16>(port);
    info->token = token;
    info->pid = o.value(QStringLiteral("pid")).toInteger();
    info->startedAt = o.value(QStringLiteral("startedAt")).toInteger();
    info->headless = o.value(QStringLiteral("headless")).toBool();
    info->project = o.value(QStringLiteral("project")).toString();
    info->projectName = o.value(QStringLiteral("projectName")).toString();
    info->file = path;
    return true;
}

} // namespace

QString sessionDirPath()
{
    const QString pinned = pinnedSessionPath();
    if (!pinned.isEmpty())
        return QFileInfo(pinned).absolutePath();
    const QString override = qEnvironmentVariable("DRIFT_MCP_SESSION_DIR");
    if (!override.isEmpty())
        return override;
    return defaultSessionDir();
}

QString sessionFilePath()
{
    const QString pinned = pinnedSessionPath();
    if (!pinned.isEmpty())
        return pinned;
    return QDir(sessionDirPath())
        .filePath(QStringLiteral("%1.json").arg(QCoreApplication::applicationPid()));
}

bool writeSessionFile(const SessionInfo &info)
{
    const QString path = sessionFilePath();
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir))
        return false;
    // The records carry bearer tokens; with the directory closed off there is no moment
    // in which one sits world-readable before the file's own permissions are narrowed.
    if (pinnedSessionPath().isEmpty()) {
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                       | QFileDevice::ExeOwner);
    }

    const QJsonObject body{
        {QStringLiteral("port"), static_cast<int>(info.port)},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:%1/mcp").arg(info.port)},
        {QStringLiteral("token"), info.token},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("startedAt"), info.startedAt},
        {QStringLiteral("headless"), info.headless},
        {QStringLiteral("project"), info.project},
        {QStringLiteral("projectName"), info.projectName},
    };

    // Rewritten on every token rotation and project switch while bridges may be reading
    // it, so it is replaced whole rather than truncated in place.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(body).toJson(QJsonDocument::Compact));
    file.write("\n");
    if (!file.commit())
        return false;
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

void removeSessionFile()
{
    QFile::remove(sessionFilePath());
}

QList<SessionInfo> listSessions()
{
    QList<SessionInfo> sessions;
    const QString pinned = pinnedSessionPath();
    if (!pinned.isEmpty()) {
        SessionInfo info;
        if (parseSession(pinned, &info) && processAlive(info.pid))
            sessions.append(info);
        return sessions;
    }

    const QDir dir(sessionDirPath());
    const QStringList names = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &name : names) {
        const QString path = dir.filePath(name);
        SessionInfo info;
        if (!parseSession(path, &info))
            continue; // possibly mid-write by its owner; never delete what we cannot read
        if (!processAlive(info.pid)) {
            QFile::remove(path);
            continue;
        }
        sessions.append(info);
    }
    std::sort(sessions.begin(), sessions.end(), [](const SessionInfo &a, const SessionInfo &b) {
        return a.startedAt > b.startedAt;
    });
    return sessions;
}

bool resolveAttachedSession(SessionInfo *attached, QString *error)
{
    const QList<SessionInfo> sessions = listSessions();
    if (attached->pid == 0) {
        if (sessions.isEmpty()) {
            if (error) {
                *error = QStringLiteral(
                    "Drift MCP is off. Open Drift and enable Agent access in Settings.");
            }
            return false;
        }
        *attached = sessions.first();
        return true;
    }

    for (const SessionInfo &session : sessions) {
        if (session.pid == attached->pid) {
            *attached = session;
            return true;
        }
    }
    if (error) {
        const QString which = attached->projectName.isEmpty()
            ? QStringLiteral("pid %1").arg(attached->pid)
            : QStringLiteral("\"%1\", pid %2").arg(attached->projectName).arg(attached->pid);
        *error = processAlive(attached->pid)
            ? QStringLiteral("Agent access is off in the Drift instance this bridge is attached "
                             "to (%1). Enable it there again to continue.")
                  .arg(which)
            : QStringLiteral("The Drift instance this bridge was attached to (%1) has closed. "
                             "Restart the MCP client to attach to another instance.")
                  .arg(which);
    }
    return false;
}

} // namespace drift::mcp
