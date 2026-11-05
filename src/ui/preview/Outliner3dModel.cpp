#include "ui/preview/Outliner3dModel.h"

QVariant Outliner3dModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case GroupRole:
        return e.group;
    case NameRole:
        return e.name;
    case TrackRole:
        return e.track;
    case ClipRole:
        return e.clip;
    case SelectedRole:
        return e.selected;
    case HiddenRole:
        return e.hidden;
    default:
        return {};
    }
}

QHash<int, QByteArray> Outliner3dModel::roleNames() const
{
    return {{GroupRole, "group"}, {NameRole, "name"},         {TrackRole, "trackIndex"},
            {ClipRole, "clipIndex"},   {SelectedRole, "selected"}, {HiddenRole, "hidden"}};
}

void Outliner3dModel::setEntries(const QList<Entry> &entries)
{
    if (entries == m_entries)
        return;
    bool sameRows = entries.size() == m_entries.size();
    for (int i = 0; sameRows && i < entries.size(); ++i)
        sameRows = entries.at(i).group == m_entries.at(i).group && entries.at(i).track == m_entries.at(i).track
                   && entries.at(i).clip == m_entries.at(i).clip;
    if (sameRows) {
        m_entries = entries;
        emit dataChanged(index(0), index(int(entries.size()) - 1));
        return;
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
}
