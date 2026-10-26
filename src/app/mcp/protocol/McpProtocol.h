#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>

namespace drift::mcp {

using ToolHandler = std::function<QJsonObject(const QString &name, const QJsonObject &args)>;

// Handle a JSON-RPC request or batch. `toolbox` empty = homepage tools; otherwise that toolbox's ops.
// Notifications (no id) return a null QJsonValue.
QJsonValue handleJsonRpc(const QJsonValue &body, const QString &toolbox, const ToolHandler &handler);

QJsonArray toolsForEndpoint(const QString &toolbox);

QJsonObject initializeResult();
QJsonObject jsonRpcOk(const QJsonValue &id, const QJsonValue &result);
QJsonObject jsonRpcError(const QJsonValue &id, int code, const QString &message);

} // namespace drift::mcp
