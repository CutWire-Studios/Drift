#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace drift::commandbar {

// One MCP op as the command bar sees it. `description` is the tools/list text, including its
// "When: " prefix, because the retriever indexes it as is and the prompt compaction strips it.
struct CommandOp
{
    QString name;
    QString toolbox;
    QString description;
    QJsonObject schema;
};

class CommandCatalog
{
public:
    CommandCatalog() = default;
    explicit CommandCatalog(QList<CommandOp> ops);

    // Every op of the running app's McpCatalog.
    static CommandCatalog live();
    // [{name, toolbox, description, schema}], the format of cactus-research/data/ops.json.
    static CommandCatalog fromJson(const QJsonArray &ops);

    const QList<CommandOp> &ops() const { return m_ops; }
    const CommandOp *find(const QString &name) const;
    // Skips unknown names; keeps the order given.
    QList<CommandOp> pick(const QStringList &names) const;

private:
    QList<CommandOp> m_ops;
    QHash<QString, int> m_index;
};

} // namespace drift::commandbar
