#pragma once

#include "app/mcp/protocol/McpSession.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace drift::mcp {

// The routing half of `drift --mcp-stdio`: one JSON-RPC message in, the reply out.
//
// Several Drift instances can be running, so the bridge holds which one it is attached
// to and adds four tools of its own to the homepage — instances, use_instance,
// launch_instance and close_instance. Those, initialize, ping and tools/list are answered
// here, so an agent
// sees the full tool list and can pick or start an instance before any editor is
// reachable; every other call is forwarded to the attached instance over HTTP.
class StdioBridge
{
public:
    // pinnedPid: --mcp-instance, or 0 to attach on first use (see resolveAttachedSession).
    explicit StdioBridge(qint64 pinnedPid = 0);

    // Empty when there is nothing to send back (a notification).
    QByteArray handle(const QByteArray &message);

    const SessionInfo &attached() const { return m_attached; }

    static bool isBridgeTool(const QString &name);
    static QJsonArray bridgeTools();

private:
    QJsonValue handleOne(const QJsonObject &request);
    QJsonObject callBridgeTool(const QString &name, const QJsonObject &args);
    QJsonValue forward(const QJsonObject &request);

    QJsonObject listInstances();
    QJsonObject useInstance(const QJsonObject &args);
    QJsonObject launchInstance(const QJsonObject &args);
    QJsonObject closeInstance(const QJsonObject &args);

    SessionInfo m_attached;
};

} // namespace drift::mcp
