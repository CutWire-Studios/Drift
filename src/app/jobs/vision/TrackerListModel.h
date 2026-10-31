#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class TrackingController;

// The trackers of the Tracking panel's host clip, one row each. refresh() diffs against the current
// rows, so a row's delegate only goes away with its tracker and a slider drag survives edits to other
// trackers. selectedId is kept pointing at an existing row.
class TrackerListModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(QString selectedId READ selectedId WRITE setSelectedId NOTIFY selectedIdChanged)

public:
    explicit TrackerListModel(TrackingController *owner);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString selectedId() const { return m_selectedId; }
    void setSelectedId(const QString &id);

    // Loads the trackers of the clip at (track, clip); (-1, -1) empties the list. If selectedId no
    // longer names a row, the first row (else none) is selected.
    Q_INVOKABLE void refresh(int trackIndex, int clipIndex);
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    // Masks and clips the tracker with this id could still drive: the host's target candidates minus
    // the clips it already drives.
    Q_INVOKABLE QVariantList candidates(const QString &trackerId) const;
    // The pattern box width of the tracker's first point at the timeline time, in percent; 0 for
    // planar trackers or when nothing is tracked there.
    Q_INVOKABLE double patternPercent(const QString &trackerId, double timelineSeconds) const;

signals:
    void countChanged();
    void selectedIdChanged();

private:
    int rowOf(const QString &id) const;

    TrackingController *m_owner = nullptr;
    QList<QVariantMap> m_rows;
    QString m_selectedId;
    int m_track = -1;
    int m_clip = -1;
};
