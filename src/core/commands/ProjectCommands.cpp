#include "ProjectCommands.h"

namespace drift {

ProjectSnapshotCommand::ProjectSnapshotCommand(Project *project, Project before, Project after,
                                               const QString &text)
    : QUndoCommand(text)
    , m_project(project)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

QString ProjectSnapshotCommand::beforeHash() const
{
    if (m_beforeHash.isEmpty())
        m_beforeHash = m_before.contentHash();
    return m_beforeHash;
}

QString ProjectSnapshotCommand::afterHash() const
{
    if (m_afterHash.isEmpty())
        m_afterHash = m_after.contentHash();
    return m_afterHash;
}

void ProjectSnapshotCommand::undo()
{
    if (m_project)
        *m_project = m_before;
}

void ProjectSnapshotCommand::redo()
{
    if (m_project)
        *m_project = m_after;
}

} // namespace drift
