#include "app/mcp/protocol/McpSession.h"
#include "app/platform/RunningInstances.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

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
    info->loading = o.value(QStringLiteral("loading")).toBool();
    const QJsonObject lastLoad = o.value(QStringLiteral("lastLoad")).toObject();
    info->hasLoaded = !lastLoad.isEmpty();
    info->loadOk = lastLoad.value(QStringLiteral("ok")).toBool();
    info->loadMessage = lastLoad.value(QStringLiteral("message")).toString();
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

    QJsonObject body{
        {QStringLiteral("port"), static_cast<int>(info.port)},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:%1/mcp").arg(info.port)},
        {QStringLiteral("token"), info.token},
        {QStringLiteral("pid"), QCoreApplication::applicationPid()},
        {QStringLiteral("startedAt"), info.startedAt},
        {QStringLiteral("headless"), info.headless},
        {QStringLiteral("project"), info.project},
        {QStringLiteral("projectName"), info.projectName},
        {QStringLiteral("loading"), info.loading},
    };
    if (info.hasLoaded) {
        body.insert(QStringLiteral("lastLoad"),
                    QJsonObject{{QStringLiteral("ok"), info.loadOk},
                                {QStringLiteral("message"), info.loadMessage}});
    }

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
        if (parseSession(pinned, &info) && drift::instances::processAlive(info.pid))
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
        if (!drift::instances::processAlive(info.pid)) {
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

QString describeSession(const SessionInfo &session)
{
    QString text = QString::number(session.pid);
    if (!session.projectName.isEmpty())
        text += QStringLiteral(" \"%1\"").arg(session.projectName);
    if (session.headless)
        text += QStringLiteral(" (headless)");
    return text;
}

bool resolveAttachedSession(SessionInfo *attached, QString *error)
{
    const QList<SessionInfo> sessions = listSessions();
    if (attached->pid == 0) {
        if (sessions.size() == 1) {
            *attached = sessions.first();
            return true;
        }
        if (!error)
            return false;
        if (sessions.isEmpty()) {
            *error = QStringLiteral(
                "Drift MCP is off. Open Drift and enable Agent access in Settings, or call "
                "launch_instance to start a new instance.");
        } else {
            QStringList list;
            for (const SessionInfo &session : sessions)
                list.append(describeSession(session));
            *error = QStringLiteral("%1 Drift instances are running; pick one with "
                                    "use_instance({id}) before editing: %2.")
                         .arg(sessions.size())
                         .arg(list.join(QStringLiteral(", ")));
        }
        return false;
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
        *error = drift::instances::processAlive(attached->pid)
            ? QStringLiteral("Agent access is off in the Drift instance this bridge is attached "
                             "to (%1). Enable it there again, or switch with use_instance({id}).")
                  .arg(which)
            : QStringLiteral("The Drift instance this bridge was attached to (%1) has closed. "
                             "instances() lists the others; use_instance({id}) attaches to one.")
                  .arg(which);
    }
    return false;
}

} // namespace drift::mcp
