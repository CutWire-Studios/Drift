#pragma once

#include "core/BinFolder.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace drift {
class Project;
}

// Media bin folder tree model, backed by the project's folder table. Mirrors AssetLibrary's
// shape: flat list, no structural reset on navigation — QML filters rows by parentId the same
// way MediaAssetsTab filters assets by folderId.
class BinFolderListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        ParentIdRole,
    };
    Q_ENUM(Role)

    explicit BinFolderListModel(QObject *parent = nullptr);

    void setProject(drift::Project *project);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }

    Q_INVOKABLE QVariantMap folderAt(int index) const;
    // Empty map when `id` doesn't name a folder (including the root "" id) — QML's breadcrumb
    // walk stops there.
    Q_INVOKABLE QVariantMap folderById(const QString &id) const;
    Q_INVOKABLE int indexOfId(const QString &id) const;
    Q_INVOKABLE QString idAt(int index) const;

    // Plain mutators — undo-agnostic, same contract as AssetLibrary::removeAssetAt: callers own
    // the undo snapshot.
    QString createFolder(const QString &name, const QString &parentId);
    bool renameFolder(const QString &id, const QString &name);
    // Reparents the folder itself; its own children (assets and subfolders) keep pointing at
    // it, so they move along for free. Refuses a no-op (already there), moving into itself, or
    // moving into one of its own descendants — the last would otherwise disconnect that whole
    // branch from the root by making it its own ancestor.
    bool moveFolder(const QString &id, const QString &newParentId);
    // Removes the folder row. Any other folder whose parentId == id is reparented to this
    // folder's own parentId (moved up one level, not deleted). Does not touch assets — the
    // caller reparents those separately via AssetLibrary::reparentAssetsInFolder.
    bool deleteFolder(const QString &id);
    // The folder's parentId, read before deleteFolder mutates the tree.
    QString parentIdOf(const QString &id) const;

    // Re-reads the project after undo/redo has swapped it wholesale.
    void syncToProject();

signals:
    void countChanged();

private:
    void snapshotFolders();
    void emitFolderRowChanged(int index, const QList<int> &roles);
    const drift::BinFolder *folderAtIndex(int index) const;
    drift::BinFolder *folderAtIndex(int index);
    QList<QString> currentParentIds() const;
    QList<QString> currentNames() const;
    // True if candidateId is ancestorId itself or nested anywhere under it — walking up
    // candidateId's own parentId chain. Guards against a corrupt (cyclic) chain the same way
    // BinBreadcrumb.qml's trail walk does, since this reads the same project data.
    bool isFolderOrDescendant(const QString &candidateId, const QString &ancestorId) const;

    drift::Project *m_project = nullptr;
    QList<QString> m_syncedOrder;
    QList<QString> m_syncedParentIds;
    QList<QString> m_syncedNames;
};
