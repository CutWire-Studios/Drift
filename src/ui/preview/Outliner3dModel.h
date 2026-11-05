#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

// The 3D Outliner's rows: the cameras, models and 3D layers under the playhead, in that order.
// PreviewController fills it; the model only holds the rows and tells views what changed.
class Outliner3dModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Group { Cameras, Models, Layers };
    enum Role { GroupRole = Qt::UserRole + 1, NameRole, TrackRole, ClipRole, SelectedRole, HiddenRole };

    struct Entry
    {
        int group = Layers;
        QString name;
        int track = -1;
        int clip = -1;
        bool selected = false;
        bool hidden = false;

        bool operator==(const Entry &) const = default;
    };

    explicit Outliner3dModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_entries.size()); }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Rows that moved or appeared reset the model; the same rows with new flags only change in place.
    void setEntries(const QList<Entry> &entries);

private:
    QList<Entry> m_entries;
};
