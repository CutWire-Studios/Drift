#pragma once

#include "core/project/Project.h"

#include <QHash>
#include <QJsonObject>
#include <QUndoCommand>
#include <QUndoStack>

#include <optional>

namespace drift {

// Restores a full project snapshot on undo/redo.
class ProjectSnapshotCommand : public QUndoCommand
{
public:
    ProjectSnapshotCommand(Project *project, Project before, Project after, const QString &text);

    void undo() override;
    void redo() override;

    // Computed on first ask, not in the constructor. Hashing a snapshot means serialising the
    // whole project to JSON and running SHA-256 over it -- twice per edit, once for each side --
    // and the only thing that ever reads the result is history introspection over MCP. Every
    // edit in the app was paying for an answer almost nothing asks for.
    QString beforeHash() const;
    QString afterHash() const;
    const Project &before() const { return m_before; }
    const Project &after() const { return m_after; }

    // Skips the redo() QUndoStack::push runs, for a command whose project is already in its
    // `after` state (history read back from a saved project).
    void adoptApplied() { m_skipRedo = true; }

    struct Packed
    {
        QString hash;
        QByteArray compressed;
    };
    // Cached: serialising and compressing a snapshot is the cost of persisting history, and an
    // autosave every few seconds should only pay it for steps it has not seen.
    const Packed &packedBefore() const;
    const Packed &packedAfter() const;

private:
    void restore(const Project &snapshot);

    Project *m_project = nullptr;
    Project m_before;
    Project m_after;
    mutable QString m_beforeHash;
    mutable QString m_afterHash;
    mutable std::optional<Packed> m_packedBefore;
    mutable std::optional<Packed> m_packedAfter;
    bool m_skipRedo = false;
};

// One undo step as persisted, indexing StoredHistory::states. Macros are flattened into a single
// step: first child's before, last child's after.
struct HistoryStep
{
    int before = 0;
    int after = 0;
    QString text;
};

struct StoredHistory
{
    QList<Project> states;
    QList<HistoryStep> steps;
    int index = 0;
};

// Every distinct project state an undo stack can restore, keyed by content hash.
QHash<QString, const Project *> historyStates(const QUndoStack &stack);

// Identical states are stored once, each compressed. Empty object when the stack is empty.
QJsonObject historyToJson(const QUndoStack &stack);
std::optional<StoredHistory> historyFromJson(const QJsonObject &object);

} // namespace drift
