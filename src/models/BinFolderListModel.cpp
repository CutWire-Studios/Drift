#include "BinFolderListModel.h"

#include "core/Project.h"

#include <QSet>
#include <QUuid>

BinFolderListModel::BinFolderListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &BinFolderListModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &BinFolderListModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &BinFolderListModel::countChanged);

    connect(this, &QAbstractItemModel::rowsInserted, this, &BinFolderListModel::snapshotFolders);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &BinFolderListModel::snapshotFolders);
    connect(this, &QAbstractItemModel::modelReset, this, &BinFolderListModel::snapshotFolders);
}

void BinFolderListModel::setProject(drift::Project *project)
{
    beginResetModel();
    m_project = project;
    endResetModel();
}

int BinFolderListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_project)
        return 0;
    return m_project->binFolderOrder().size();
}

const drift::BinFolder *BinFolderListModel::folderAtIndex(int index) const
{
    if (!m_project || index < 0 || index >= m_project->binFolderOrder().size())
        return nullptr;
    return m_project->binFolder(m_project->binFolderIdAt(index));
}

drift::BinFolder *BinFolderListModel::folderAtIndex(int index)
{
    if (!m_project || index < 0 || index >= m_project->binFolderOrder().size())
        return nullptr;
    return m_project->binFolder(m_project->binFolderIdAt(index));
}

QVariant BinFolderListModel::data(const QModelIndex &index, int role) const
{
    const drift::BinFolder *folder = folderAtIndex(index.row());
    if (!index.isValid() || !folder)
        return {};

    switch (role) {
    case IdRole:
        return folder->id;
    case NameRole:
        return folder->name;
    case ParentIdRole:
        return folder->parentId;
    default:
        return {};
    }
}

QHash<int, QByteArray> BinFolderListModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {ParentIdRole, "parentId"},
    };
}

QVariantMap BinFolderListModel::folderAt(int index) const
{
    const drift::BinFolder *folder = folderAtIndex(index);
    if (!folder)
        return {};
    return {
        {QStringLiteral("id"), folder->id},
        {QStringLiteral("name"), folder->name},
        {QStringLiteral("parentId"), folder->parentId},
    };
}

QVariantMap BinFolderListModel::folderById(const QString &id) const
{
    if (!m_project || id.isEmpty())
        return {};
    const drift::BinFolder *folder = m_project->binFolder(id);
    if (!folder)
        return {};
    return {
        {QStringLiteral("id"), folder->id},
        {QStringLiteral("name"), folder->name},
        {QStringLiteral("parentId"), folder->parentId},
    };
}

int BinFolderListModel::indexOfId(const QString &id) const
{
    if (!m_project)
        return -1;
    return m_project->binFolderIndex(id);
}

QString BinFolderListModel::idAt(int index) const
{
    if (!m_project)
        return {};
    return m_project->binFolderIdAt(index);
}

QString BinFolderListModel::parentIdOf(const QString &id) const
{
    if (!m_project)
        return {};
    const drift::BinFolder *folder = m_project->binFolder(id);
    return folder ? folder->parentId : QString{};
}

void BinFolderListModel::emitFolderRowChanged(int index, const QList<int> &roles)
{
    if (index < 0)
        return;
    const QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
}

QList<QString> BinFolderListModel::currentParentIds() const
{
    if (!m_project)
        return {};

    QList<QString> parentIds;
    parentIds.reserve(m_project->binFolderOrder().size());
    for (const QString &id : m_project->binFolderOrder()) {
        const drift::BinFolder *folder = m_project->binFolder(id);
        parentIds.append(folder ? folder->parentId : QString{});
    }
    return parentIds;
}

QList<QString> BinFolderListModel::currentNames() const
{
    if (!m_project)
        return {};

    QList<QString> names;
    names.reserve(m_project->binFolderOrder().size());
    for (const QString &id : m_project->binFolderOrder()) {
        const drift::BinFolder *folder = m_project->binFolder(id);
        names.append(folder ? folder->name : QString{});
    }
    return names;
}

void BinFolderListModel::snapshotFolders()
{
    m_syncedOrder = m_project ? m_project->binFolderOrder() : QList<QString>{};
    m_syncedParentIds = currentParentIds();
    m_syncedNames = currentNames();
}

void BinFolderListModel::syncToProject()
{
    if (!m_project)
        return;

    // Undo/redo assigns the whole project behind this model's back. Resetting unconditionally
    // would rebuild every tile on every unrelated project edit, so only an actual order change is
    // worth the churn.
    if (m_syncedOrder != m_project->binFolderOrder()) {
        beginResetModel();
        endResetModel();
        return;
    }

    // An undone rename or reparent leaves the order untouched — same row, same id, different
    // field — so both parent ids and names have to be compared too.
    const QList<QString> parentIds = currentParentIds();
    const QList<QString> names = currentNames();
    if (parentIds == m_syncedParentIds && names == m_syncedNames)
        return;

    for (int i = 0; i < parentIds.size(); ++i) {
        const bool parentChanged = i >= m_syncedParentIds.size() || m_syncedParentIds.at(i) != parentIds.at(i);
        const bool nameChanged = i >= m_syncedNames.size() || m_syncedNames.at(i) != names.at(i);
        if (!parentChanged && !nameChanged)
            continue;
        QList<int> roles;
        if (parentChanged)
            roles.append(ParentIdRole);
        if (nameChanged)
            roles.append(NameRole);
        emitFolderRowChanged(i, roles);
    }
    m_syncedParentIds = parentIds;
    m_syncedNames = names;
}

QString BinFolderListModel::createFolder(const QString &name, const QString &parentId)
{
    if (!m_project)
        return {};

    drift::BinFolder folder;
    folder.name = name;
    folder.parentId = parentId;

    const int row = m_project->binFolderOrder().size();
    beginInsertRows({}, row, row);
    const QString id = m_project->addBinFolder(folder);
    endInsertRows();
    return id;
}

bool BinFolderListModel::renameFolder(const QString &id, const QString &name)
{
    const int index = indexOfId(id);
    drift::BinFolder *folder = folderAtIndex(index);
    if (!folder || folder->name == name)
        return false;

    folder->name = name;
    emitFolderRowChanged(index, {NameRole});
    snapshotFolders();
    return true;
}

bool BinFolderListModel::isFolderOrDescendant(const QString &candidateId, const QString &ancestorId) const
{
    QSet<QString> visited;
    QString id = candidateId;
    while (!id.isEmpty()) {
        if (id == ancestorId)
            return true;
        if (visited.contains(id))
            break;
        visited.insert(id);
        const drift::BinFolder *folder = m_project ? m_project->binFolder(id) : nullptr;
        id = folder ? folder->parentId : QString{};
    }
    return false;
}

bool BinFolderListModel::moveFolder(const QString &id, const QString &newParentId)
{
    if (!m_project)
        return false;

    const int index = indexOfId(id);
    drift::BinFolder *folder = folderAtIndex(index);
    if (!folder || folder->parentId == newParentId)
        return false;
    // A non-empty newParentId that names no real folder (stale id, or a caller passing junk —
    // this is QML-invokable) would otherwise be assigned as-is, silently detaching id and
    // everything under it from the root hierarchy.
    if (!newParentId.isEmpty() && !m_project->binFolder(newParentId))
        return false;
    // Refuses id == newParentId (folder into itself) and newParentId being one of id's own
    // descendants — either would make id its own ancestor once the pointer is applied.
    if (isFolderOrDescendant(newParentId, id))
        return false;

    folder->parentId = newParentId;
    emitFolderRowChanged(index, {ParentIdRole});
    snapshotFolders();
    return true;
}

bool BinFolderListModel::deleteFolder(const QString &id)
{
    if (!m_project)
        return false;

    const int index = indexOfId(id);
    if (index < 0)
        return false;

    const drift::BinFolder *deleted = m_project->binFolder(id);
    const QString parentId = deleted ? deleted->parentId : QString{};

    for (int i = 0; i < m_project->binFolderOrder().size(); ++i) {
        drift::BinFolder *child = folderAtIndex(i);
        if (child && child->parentId == id) {
            child->parentId = parentId;
            emitFolderRowChanged(i, {ParentIdRole});
        }
    }

    beginRemoveRows({}, index, index);
    m_project->binFolders().remove(id);
    m_project->binFolderOrder().removeAll(id);
    endRemoveRows();
    return true;
}
