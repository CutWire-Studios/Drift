#include "core/edit/commands/ProjectCommands.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>

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
    restore(m_before);
}

void ProjectSnapshotCommand::redo()
{
    if (m_skipRedo) {
        m_skipRedo = false;
        return;
    }
    restore(m_after);
}

static ProjectSnapshotCommand::Packed pack(const Project &project)
{
    const QByteArray json = project.toCompactJson();
    return {QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex()),
            qCompress(json, 6)};
}

const ProjectSnapshotCommand::Packed &ProjectSnapshotCommand::packedBefore() const
{
    if (!m_packedBefore)
        m_packedBefore = pack(m_before);
    return *m_packedBefore;
}

const ProjectSnapshotCommand::Packed &ProjectSnapshotCommand::packedAfter() const
{
    if (!m_packedAfter)
        m_packedAfter = pack(m_after);
    return *m_packedAfter;
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

QHash<QString, const Project *> historyStates(const QUndoStack &stack)
{
    QHash<QString, const Project *> states;
    for (int i = 0; i < stack.count(); ++i) {
        if (const auto *first = snapshotAt(stack.command(i), true))
            states.insert(first->packedBefore().hash, &first->before());
        if (const auto *last = snapshotAt(stack.command(i), false))
            states.insert(last->packedAfter().hash, &last->after());
    }
    return states;
}

QJsonObject historyToJson(const QUndoStack &stack)
{
    QJsonArray blobs;
    QHash<QString, int> blobIndex;
    const auto blobFor = [&](const ProjectSnapshotCommand::Packed &packed) {
        const auto it = blobIndex.constFind(packed.hash);
        if (it != blobIndex.constEnd())
            return it.value();
        const int index = blobs.size();
        blobs.append(QString::fromLatin1(packed.compressed.toBase64()));
        blobIndex.insert(packed.hash, index);
        return index;
    };

    QJsonArray steps;
    for (int i = 0; i < stack.count(); ++i) {
        const auto *first = snapshotAt(stack.command(i), true);
        const auto *last = snapshotAt(stack.command(i), false);
        if (!first || !last)
            continue;
        steps.append(QJsonObject{
            {QStringLiteral("before"), blobFor(first->packedBefore())},
            {QStringLiteral("after"), blobFor(last->packedAfter())},
            {QStringLiteral("text"), stack.text(i)},
        });
    }
    if (steps.isEmpty())
        return {};
    return QJsonObject{
        {QStringLiteral("index"), qMin(stack.index(), static_cast<int>(steps.size()))},
        {QStringLiteral("blobs"), blobs},
        {QStringLiteral("steps"), steps},
    };
}

std::optional<StoredHistory> historyFromJson(const QJsonObject &object)
{
    const QJsonArray blobs = object.value(QStringLiteral("blobs")).toArray();
    const QJsonArray steps = object.value(QStringLiteral("steps")).toArray();
    if (steps.isEmpty())
        return std::nullopt;

    StoredHistory history;
    history.states.reserve(blobs.size());
    for (const QJsonValue &blob : blobs) {
        const QByteArray json = qUncompress(QByteArray::fromBase64(blob.toString().toLatin1()));
        const QJsonDocument document = QJsonDocument::fromJson(json);
        if (!document.isObject())
            return std::nullopt;
        QString error;
        Project project = Project::fromJson(document.object(), &error);
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
