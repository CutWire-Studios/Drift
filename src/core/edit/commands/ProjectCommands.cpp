#include "core/edit/commands/ProjectCommands.h"

#include <QJsonArray>

namespace drift {

QString HistoryState::hash() const
{
    const QMutexLocker lock(&m_mutex);
    if (m_hash.isEmpty())
        m_hash = m_project.contentHash();
    return m_hash;
}

QStringList HistoryState::mediaPaths(const std::function<QStringList(const Project &)> &compute) const
{
    const QMutexLocker lock(&m_mutex);
    if (!m_media)
        m_media = compute(m_project);
    return *m_media;
}

ProjectSnapshotCommand::ProjectSnapshotCommand(Project *project, Project before, Project after,
                                               const QString &text)
    : ProjectSnapshotCommand(project, std::make_shared<const HistoryState>(std::move(before)),
                             std::make_shared<const HistoryState>(std::move(after)), text)
{
}

ProjectSnapshotCommand::ProjectSnapshotCommand(Project *project, HistoryStatePtr before,
                                               HistoryStatePtr after, const QString &text)
    : QUndoCommand(text)
    , m_project(project)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

void ProjectSnapshotCommand::undo()
{
    restore(m_before->project());
}

void ProjectSnapshotCommand::redo()
{
    if (m_skipRedo) {
        m_skipRedo = false;
        return;
    }
    restore(m_after->project());
}

void ProjectSnapshotCommand::restore(const Project &snapshot)
{
    if (!m_project)
        return;
    // Transcripts are not part of the undoable state (see Project::transcript).
    const QHash<QString, TranscriptPtr> transcripts = m_project->transcripts();
    *m_project = snapshot;
    m_project->setTranscripts(transcripts);
}

namespace {

const ProjectSnapshotCommand *snapshotAt(const QUndoCommand *command, bool first)
{
    while (command) {
        if (const auto *snapshot = dynamic_cast<const ProjectSnapshotCommand *>(command))
            return snapshot;
        const int children = command->childCount();
        if (children == 0)
            return nullptr;
        command = command->child(first ? 0 : children - 1);
    }
    return nullptr;
}

} // namespace

HistoryCapture captureHistory(const QUndoStack &stack)
{
    HistoryCapture capture;
    for (int i = 0; i < stack.count(); ++i) {
        const auto *first = snapshotAt(stack.command(i), true);
        const auto *last = snapshotAt(stack.command(i), false);
        if (!first || !last)
            continue;
        capture.steps.append({first->beforeState(), last->afterState(), stack.text(i)});
    }
    capture.index = qMin(stack.index(), static_cast<int>(capture.steps.size()));
    return capture;
}

HistoryDocument historyToJson(const HistoryCapture &capture)
{
    HistoryDocument document;
    if (capture.steps.isEmpty())
        return document;

    QJsonArray states;
    QHash<QString, int> stateIndex;
    const auto stateFor = [&](const HistoryStatePtr &state) {
        const QString hash = state->hash();
        const auto it = stateIndex.constFind(hash);
        if (it != stateIndex.constEnd())
            return it.value();
        const int index = states.size();
        states.append(state->project().toJson(false));
        stateIndex.insert(hash, index);
        document.states.append(state);
        return index;
    };

    QJsonArray steps;
    for (const HistoryCapture::Step &step : capture.steps) {
        steps.append(QJsonObject{
            {QStringLiteral("before"), stateFor(step.before)},
            {QStringLiteral("after"), stateFor(step.after)},
            {QStringLiteral("text"), step.text},
        });
    }
    document.json = QJsonObject{
        {QStringLiteral("index"), capture.index},
        {QStringLiteral("states"), states},
        {QStringLiteral("steps"), steps},
    };
    return document;
}

std::optional<StoredHistory> historyFromJson(const QJsonObject &object)
{
    const QJsonArray stored = object.value(QStringLiteral("states")).toArray();
    const QJsonArray steps = object.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty())
        return std::nullopt;

    StoredHistory history;
    history.states.reserve(stored.size());
    for (const QJsonValue &value : stored) {
        if (!value.isObject())
            return std::nullopt;
        QString error;
        Project project = Project::fromJson(value.toObject(), &error);
        if (!error.isEmpty())
            return std::nullopt;
        history.states.append(std::move(project));
    }

    for (const QJsonValue &value : steps) {
        const QJsonObject step = value.toObject();
        const int before = step.value(QStringLiteral("before")).toInt(-1);
        const int after = step.value(QStringLiteral("after")).toInt(-1);
        if (before < 0 || before >= history.states.size() || after < 0
            || after >= history.states.size())
            return std::nullopt;
        history.steps.append({before, after, step.value(QStringLiteral("text")).toString()});
    }
    history.index = qBound(0, object.value(QStringLiteral("index")).toInt(history.steps.size()),
                           static_cast<int>(history.steps.size()));
    return history;
}

} // namespace drift
