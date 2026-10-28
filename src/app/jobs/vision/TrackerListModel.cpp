#include "app/jobs/vision/TrackerListModel.h"

#include "app/jobs/vision/TrackingController.h"

#include <QSet>

namespace {

constexpr int FirstRole = Qt::UserRole + 1;

const QList<QByteArray> kKeys = {"id",        "name",     "kind",    "solve",       "minConfidence",
                                 "pointCount", "referenceSeconds", "targets", "sampleRange"};

} // namespace

TrackerListModel::TrackerListModel(TrackingController *owner)
    : QAbstractListModel(owner)
    , m_owner(owner)
{
}

int TrackerListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> TrackerListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < kKeys.size(); ++i)
        names.insert(FirstRole + i, kKeys.at(i));
    return names;
}

QVariant TrackerListModel::data(const QModelIndex &index, int role) const
{
    const int k = role - FirstRole;
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size() || k < 0 || k >= kKeys.size())
        return {};
    return m_rows.at(index.row()).value(QString::fromLatin1(kKeys.at(k)));
}

QVariantMap TrackerListModel::rowAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QVariantMap{};
}

int TrackerListModel::rowOf(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}

void TrackerListModel::setSelectedId(const QString &id)
{
    if (id == m_selectedId)
        return;
    m_selectedId = id;
    emit selectedIdChanged();
}

void TrackerListModel::refresh(int trackIndex, int clipIndex)
{
    m_track = trackIndex;
    m_clip = clipIndex;

    QList<QVariantMap> next;
    if (trackIndex >= 0 && clipIndex >= 0) {
        for (const QVariant &v : m_owner->trackers(trackIndex, clipIndex))
            next.append(v.toMap());
    }

    bool sameIds = next.size() == m_rows.size();
    for (int i = 0; sameIds && i < next.size(); ++i)
        sameIds = next.at(i).value(QStringLiteral("id")) == m_rows.at(i).value(QStringLiteral("id"));

    const int before = int(m_rows.size());
    if (!sameIds) {
        beginResetModel();
        m_rows = next;
        endResetModel();
    } else {
        for (int i = 0; i < next.size(); ++i) {
            if (next.at(i) == m_rows.at(i))
                continue;
            m_rows[i] = next.at(i);
            emit dataChanged(index(i), index(i));
        }
    }
    if (before != m_rows.size())
        emit countChanged();

    if (rowOf(m_selectedId) < 0)
        setSelectedId(m_rows.isEmpty() ? QString() : m_rows.first().value(QStringLiteral("id")).toString());
}

QVariantList TrackerListModel::candidates(const QString &trackerId) const
{
    const int row = rowOf(trackerId);
    if (row < 0)
        return {};
    QSet<QString> driven;
    for (const QVariant &t : m_rows.at(row).value(QStringLiteral("targets")).toList())
        driven.insert(t.toMap().value(QStringLiteral("clipId")).toString());
    QVariantList out;
    for (const QVariant &c : m_owner->targetCandidates(m_track, m_clip)) {
        if (!driven.contains(c.toMap().value(QStringLiteral("clipId")).toString()))
            out.append(c);
    }
    return out;
}

double TrackerListModel::patternPercent(const QString &trackerId, double timelineSeconds) const
{
    const int row = rowOf(trackerId);
    if (row < 0 || m_rows.at(row).value(QStringLiteral("kind")).toString() != QLatin1String("point"))
        return 0.0;
    const TrackingController::FrameView frame = m_owner->frameView(m_track, m_clip, trackerId, timelineSeconds);
    return frame.points.isEmpty() ? 0.0 : frame.points.first().patternW * 100.0;
}
