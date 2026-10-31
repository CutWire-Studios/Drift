#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class AppController;
class MaskingController;

// The masks that apply to the Masking panel's target, one row each, in stack order. refresh() re-reads
// the project and diffs against the current rows, so a delegate only goes away when its mask does and
// an open combo box or slider drag survives edits to other rows.
class MaskStackModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(int count READ rowCount NOTIFY summaryChanged)
    Q_PROPERTY(bool hasEnabled READ hasEnabled NOTIFY summaryChanged)
    // Id of the selected object cutout, which "Refine selected mask" reuses; empty when none is selected.
    Q_PROPERTY(QString refineMaskId READ refineMaskId NOTIFY summaryChanged)
    // Id and name of the selected AI cutout (object or people), which brush strokes fix per frame.
    Q_PROPERTY(QString cutoutFixId READ cutoutFixId NOTIFY summaryChanged)
    Q_PROPERTY(QString cutoutFixName READ cutoutFixName NOTIFY summaryChanged)
    // Shape key times (timeline seconds) of the selected freeform mask.
    Q_PROPERTY(QVariantList shapeKeyTimes READ shapeKeyTimes NOTIFY summaryChanged)
    Q_PROPERTY(int shapeKeyCount READ shapeKeyCount NOTIFY summaryChanged)
    Q_PROPERTY(bool earlierShapeKey READ earlierShapeKey NOTIFY shapeKeyStateChanged)
    Q_PROPERTY(bool laterShapeKey READ laterShapeKey NOTIFY shapeKeyStateChanged)
    Q_PROPERTY(bool playheadOnShapeKey READ playheadOnShapeKey NOTIFY shapeKeyStateChanged)
    // Bumped by every refresh, so bindings that read other state per row can re-evaluate.
    Q_PROPERTY(int revision READ revision NOTIFY summaryChanged)

public:
    explicit MaskStackModel(AppController *app, MaskingController *owner);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool hasEnabled() const { return m_hasEnabled; }
    QString refineMaskId() const { return m_refineMaskId; }
    QString cutoutFixId() const { return m_cutoutFixId; }
    QString cutoutFixName() const { return m_cutoutFixName; }
    QVariantList shapeKeyTimes() const { return m_shapeKeyTimes; }
    int shapeKeyCount() const { return int(m_shapeKeyTimes.size()); }
    bool earlierShapeKey() const { return m_earlier; }
    bool laterShapeKey() const { return m_later; }
    bool playheadOnShapeKey() const { return m_onKey; }
    int revision() const { return m_revision; }

    // Re-reads maskTarget() and its mask stack.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap rowAt(int row) const;
    // Moves the playhead to the nearest shape key before / after it.
    Q_INVOKABLE void stepShapeKey(bool previous);
    // Adds a key holding the shape on screen at the playhead, or removes the key already there.
    Q_INVOKABLE void toggleShapeKey(const QString &maskId);

signals:
    void summaryChanged();
    void shapeKeyStateChanged();

private:
    struct Entry
    {
        QVariantMap map;
        QVariantList keyTimes;
    };

    void updateShapeKeyState();
    static QList<QByteArray> keys();

    AppController *m_app = nullptr;
    MaskingController *m_owner = nullptr;
    QList<Entry> m_rows;
    bool m_hasEnabled = false;
    QString m_refineMaskId;
    QString m_cutoutFixId;
    QString m_cutoutFixName;
    QVariantList m_shapeKeyTimes;
    bool m_earlier = false;
    bool m_later = false;
    bool m_onKey = false;
    int m_revision = 0;
};
