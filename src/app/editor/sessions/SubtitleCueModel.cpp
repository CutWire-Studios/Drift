#include "app/editor/sessions/SubtitleCueModel.h"

namespace {

bool sameCue(const drift::SubtitleCue &a, const drift::SubtitleCue &b)
{
    return a.startUs == b.startUs && a.endUs == b.endUs && a.text == b.text && a.source == b.source;
}

} // namespace

SubtitleCueModel::SubtitleCueModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SubtitleCueModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_cues.size();
}

QVariant SubtitleCueModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_cues.size())
        return {};
    const drift::SubtitleCue &cue = m_cues.at(index.row());
    switch (role) {
    case StartRole:
        return drift::usToSeconds(cue.startUs);
    case EndRole:
        return drift::usToSeconds(cue.endUs);
    case TextRole:
        return cue.text;
    case SourceRole:
        return cue.source;
    default:
        return {};
    }
}

QHash<int, QByteArray> SubtitleCueModel::roleNames() const
{
    return {
        {StartRole, "start"},
        {EndRole, "end"},
        {TextRole, "text"},
        {SourceRole, "source"},
    };
}

void SubtitleCueModel::setCues(const QList<drift::SubtitleCue> &cues)
{
    if (cues.size() != m_cues.size()) {
        beginResetModel();
        m_cues = cues;
        endResetModel();
        return;
    }
    for (int i = 0; i < cues.size(); ++i) {
        if (sameCue(cues.at(i), m_cues.at(i)))
            continue;
        m_cues[i] = cues.at(i);
        emit dataChanged(index(i), index(i));
    }
}

#include "moc_SubtitleCueModel.cpp"
