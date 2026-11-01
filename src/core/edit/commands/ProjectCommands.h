#pragma once

#include "core/project/Project.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QStringList>
#include <QUndoCommand>
#include <QUndoStack>

#include <functional>
#include <memory>
#include <optional>

namespace drift {

// One immutable project state in the undo history. Shared between the command that owns it and
// any save running on a worker, so the derived values (content hash, media paths) are computed
// once, under a lock, by whichever thread asks first.
class HistoryState
{
public:
    explicit HistoryState(Project project) : m_project(std::move(project)) {}

    const Project &project() const { return m_project; }
    // Serialising a snapshot and running SHA-256 over it is the cost of identifying a state, so it
    // is paid on first ask rather than on every edit.
    QString hash() const;
    QStringList mediaPaths(const std::function<QStringList(const Project &)> &compute) const;

private:
    Project m_project;
    mutable QMutex m_mutex;
    mutable QString m_hash;
    mutable std::optional<QStringList> m_media;
};

using HistoryStatePtr = std::shared_ptr<const HistoryState>;

// Restores a full project snapshot on undo/redo.
class ProjectSnapshotCommand : public QUndoCommand
{
public:
    ProjectSnapshotCommand(Project *project, Project before, Project after, const QString &text);
    ProjectSnapshotCommand(Project *project, HistoryStatePtr before, HistoryStatePtr after,
                           const QString &text);

    void undo() override;
    void redo() override;

    // Computed on first ask, not in the constructor. Hashing a snapshot means serialising the
    // whole project to JSON and running SHA-256 over it -- twice per edit, once for each side --
    // and the only thing that ever reads the result is history introspection over MCP. Every
    // edit in the app was paying for an answer almost nothing asks for.
    QString beforeHash() const { return m_before->hash(); }
    QString afterHash() const { return m_after->hash(); }
    const Project &before() const { return m_before->project(); }
    const Project &after() const { return m_after->project(); }
    const HistoryStatePtr &beforeState() const { return m_before; }
    const HistoryStatePtr &afterState() const { return m_after; }

    // Skips the redo() QUndoStack::push runs, for a command whose project is already in its
    // `after` state (history read back from a saved project).
    void adoptApplied() { m_skipRedo = true; }

private:
    void restore(const Project &snapshot);

    Project *m_project;
    HistoryStatePtr m_before;
    HistoryStatePtr m_after;
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

// What a save needs from the undo stack, taken on the GUI thread and safe to read anywhere: the
// states are immutable and shared, never the live commands.
struct HistoryCapture
{
    struct Step
    {
        HistoryStatePtr before;
        HistoryStatePtr after;
        QString text;
    };
    QList<Step> steps;
    int index = 0;
};

HistoryCapture captureHistory(const QUndoStack &stack);

struct HistoryDocument
{
    QJsonObject json; // empty when there is no history
    QList<HistoryStatePtr> states; // one per distinct content
};

// Identical states are stored once, as plain JSON. Any thread.
HistoryDocument historyToJson(const HistoryCapture &capture);
std::optional<StoredHistory> historyFromJson(const QJsonObject &object);

} // namespace drift
