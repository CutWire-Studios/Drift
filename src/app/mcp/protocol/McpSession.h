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
    QString project;     // project file path, empty for an unsaved timeline
    QString projectName;
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

// The instance a stdio bridge talks to. With `attached->pid` still 0 it picks the newest
// live instance; after that it follows only that pid, refreshing its port and token,
// and fails rather than drifting to another editor once that instance is gone — an
// agent that inspected one project must never have its next edit land in another.
bool resolveAttachedSession(SessionInfo *attached, QString *error);

} // namespace drift::mcp
