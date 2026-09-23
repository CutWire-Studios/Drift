#pragma once

#include "core/Project.h"

#include <QUndoCommand>

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

private:
    void restore(const Project &snapshot);

    Project *m_project = nullptr;
    Project m_before;
    Project m_after;
    mutable QString m_beforeHash;
    mutable QString m_afterHash;
};

} // namespace drift
