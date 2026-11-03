#include "app/editor/sessions/SubtitleCueFilterModel.h"

#include "app/editor/sessions/SubtitleCueModel.h"

SubtitleCueFilterModel::SubtitleCueFilterModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setFilterRole(SubtitleCueModel::TextRole);
    connect(this, &QAbstractItemModel::rowsInserted, this, &SubtitleCueFilterModel::updateCount);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &SubtitleCueFilterModel::updateCount);
    connect(this, &QAbstractItemModel::modelReset, this, &SubtitleCueFilterModel::updateCount);
    connect(this, &QAbstractItemModel::layoutChanged, this, &SubtitleCueFilterModel::updateCount);
}

void SubtitleCueFilterModel::setSourceModel(QAbstractItemModel *model)
{
    QSortFilterProxyModel::setSourceModel(model);
    if (!model)
        return;
    // A cue's overlap flag reads the next cue, so an edit also changes the row before it.
    connect(model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
                const int row = topLeft.row() - 1;
                if (row < 0 || topLeft.row() != bottomRight.row())
                    return;
                const QModelIndex mapped = mapFromSource(sourceModel()->index(row, 0));
                if (mapped.isValid())
                    emit dataChanged(mapped, mapped, {OverlapsNextRole});
            });
}

QVariant SubtitleCueFilterModel::data(const QModelIndex &index, int role) const
{
    if (role == CueIndexRole)
        return index.isValid() ? mapToSource(index).row() : -1;
    if (role == OverlapsNextRole) {
        if (!index.isValid())
            return false;
        const QModelIndex src = mapToSource(index);
        const QModelIndex next = sourceModel()->index(src.row() + 1, 0);
        if (!next.isValid())
            return false;
        const double end = sourceModel()->data(src, SubtitleCueModel::EndRole).toDouble();
        return end > sourceModel()->data(next, SubtitleCueModel::StartRole).toDouble() + 1e-6;
    }
    return QSortFilterProxyModel::data(index, role);
}

QHash<int, QByteArray> SubtitleCueFilterModel::roleNames() const
{
    QHash<int, QByteArray> roles = QSortFilterProxyModel::roleNames();
    roles.insert(CueIndexRole, "cueIndex");
    roles.insert(OverlapsNextRole, "overlapsNext");
    return roles;
}

void SubtitleCueFilterModel::setFilterText(const QString &text)
{
    if (m_filterText == text)
        return;
    m_filterText = text;
    setFilterFixedString(text.trimmed());
    emit filterTextChanged();
}

int SubtitleCueFilterModel::rowForCue(int cueIndex) const
{
    if (!sourceModel() || cueIndex < 0)
        return -1;
    return mapFromSource(sourceModel()->index(cueIndex, 0)).row();
}

int SubtitleCueFilterModel::cueAt(int row) const
{
    if (row < 0 || row >= rowCount())
        return -1;
    return mapToSource(index(row, 0)).row();
}

void SubtitleCueFilterModel::updateCount()
{
    const int count = rowCount();
    if (count == m_count)
        return;
    m_count = count;
    emit countChanged();
}

#include "moc_SubtitleCueFilterModel.cpp"
