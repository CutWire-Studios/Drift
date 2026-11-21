#include "app/commandbar/CommandCatalog.h"

#include "app/mcp/tools/McpCatalog.h"

namespace drift::commandbar {

CommandCatalog::CommandCatalog(QList<CommandOp> ops)
    : m_ops(std::move(ops))
{
    for (int i = 0; i < m_ops.size(); ++i)
        m_index.insert(m_ops.at(i).name, i);
}

CommandCatalog CommandCatalog::live()
{
    const QStringList names = mcp::opNames();
    const QJsonArray tools = mcp::toolboxPayload(QString(), names).value(QStringLiteral("tools")).toArray();
    QList<CommandOp> ops;
    ops.reserve(tools.size());
    for (const QJsonValue &v : tools) {
        const QJsonObject tool = v.toObject();
        CommandOp op;
        op.name = tool.value(QStringLiteral("name")).toString();
        op.toolbox = mcp::toolboxForOp(op.name);
        op.description = tool.value(QStringLiteral("description")).toString();
        op.schema = tool.value(QStringLiteral("inputSchema")).toObject();
        ops.append(op);
    }
    return CommandCatalog(std::move(ops));
}

CommandCatalog CommandCatalog::fromJson(const QJsonArray &array)
{
    QList<CommandOp> ops;
    ops.reserve(array.size());
    for (const QJsonValue &v : array) {
        const QJsonObject o = v.toObject();
        ops.append({o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("toolbox")).toString(),
                    o.value(QStringLiteral("description")).toString(), o.value(QStringLiteral("schema")).toObject()});
    }
    return CommandCatalog(std::move(ops));
}

const CommandOp *CommandCatalog::find(const QString &name) const
{
    const int i = m_index.value(name, -1);
    return i < 0 ? nullptr : &m_ops.at(i);
}

QList<CommandOp> CommandCatalog::pick(const QStringList &names) const
{
    QList<CommandOp> out;
    for (const QString &n : names) {
        if (const CommandOp *op = find(n))
            out.append(*op);
    }
    return out;
}

} // namespace drift::commandbar
