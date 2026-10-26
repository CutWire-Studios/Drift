#pragma once

#include <QList>
#include <QString>

namespace drift::mcp {

// One running instance's MCP endpoint, as recorded in the session registry.
struct SessionInfo
{
    quint16 port = 0;
    QString token;
    qint64 pid = 0;
    qint64 startedAt = 0; // ms since epoch; newer instances sort first
    bool headless = false;
    bool isolated = false; // started by launch_instance; the only kind close_instance may end
    QString project;     // project file path, empty for an unsaved timeline
    QString projectName;
    // A project load is under way, or a startup project is queued and has not begun.
    bool loading = false;
    // The most recent load that finished: hasLoaded is false until one has.
    bool hasLoaded = false;
    bool loadOk = false;
    QString loadMessage;
    QString file;        // the registry file this record came from (read side only)
};

// Every instance with its HTTP server listening keeps one file in the registry directory,
// named by pid, so several editors can run side by side and `drift --mcp-stdio` can reach
// any of them. DRIFT_MCP_SESSION_DIR moves the directory; DRIFT_MCP_SESSION_PATH pins both
// sides to a single file instead, which is how tests keep instances apart.
QString sessionDirPath();
// This process's own record.
QString sessionFilePath();
bool writeSessionFile(const SessionInfo &info);
void removeSessionFile();

// Live instances, newest first. Records left behind by processes that died without
// removing them are skipped, and deleted from the registry directory.
QList<SessionInfo> listSessions();

// The instance a stdio bridge talks to. With `attached->pid` still 0 it attaches only
// when exactly one instance is running — with several, guessing could put an agent's
// edits into the wrong project, so it fails with the list instead. After that it follows
// only that pid, refreshing its port and token, and fails rather than drifting to another
// editor once that instance is gone.
bool resolveAttachedSession(SessionInfo *attached, QString *error);

// "1234 \"My Project\" (headless)" — how errors and logs name an instance.
QString describeSession(const SessionInfo &session);

} // namespace drift::mcp
