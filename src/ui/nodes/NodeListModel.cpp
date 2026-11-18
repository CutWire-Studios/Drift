#include "ui/nodes/NodeListModel.h"

NodeListModel::NodeListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int NodeListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_nodes.size());
}

QVariant NodeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_nodes.size())
        return {};
    const NodeItem &n = m_nodes.at(index.row());
    switch (role) {
    case NodeIdRole: return n.nodeId;
    case TypeRole: return n.type;
    case TitleRole: return n.title;
    case CategoryRole: return n.category;
    case AccentRole: return n.accent;
    case XRole: return n.x;
    case YRole: return n.y;
    case WidthRole: return n.width;
    case InputsRole: return n.inputs;
    case OutputsRole: return n.outputs;
    case SelectedRole: return n.selected;
    case ErrorRole: return n.error;
    case ThumbnailRole: return n.thumbnail;
    case DeletableRole: return n.deletable;
    default: return {};
    }
}

QHash<int, QByteArray> NodeListModel::roleNames() const
{
    return {
        {NodeIdRole, "nodeId"},
        {TypeRole, "type"},
        {TitleRole, "title"},
        {CategoryRole, "category"},
        {AccentRole, "accent"},
        {XRole, "x"},
        {YRole, "y"},
        {WidthRole, "width"},
        {InputsRole, "inputs"},
        {OutputsRole, "outputs"},
        {SelectedRole, "selected"},
        {ErrorRole, "error"},
        {ThumbnailRole, "thumbnail"},
        {DeletableRole, "deletable"},
    };
}

QList<int> NodeListModel::changedRoles(const NodeItem &a, const NodeItem &b) const
{
    QList<int> roles;
    if (a.type != b.type) roles << TypeRole;
    if (a.title != b.title) roles << TitleRole;
    if (a.category != b.category) roles << CategoryRole;
    if (a.accent != b.accent) roles << AccentRole;
    if (a.x != b.x) roles << XRole;
    if (a.y != b.y) roles << YRole;
    if (a.width != b.width) roles << WidthRole;
    if (a.inputs != b.inputs) roles << InputsRole;
    if (a.outputs != b.outputs) roles << OutputsRole;
    if (a.selected != b.selected) roles << SelectedRole;
    if (a.error != b.error) roles << ErrorRole;
    if (a.thumbnail != b.thumbnail) roles << ThumbnailRole;
    if (a.deletable != b.deletable) roles << DeletableRole;
    return roles;
}

void NodeListModel::setNodes(const QList<NodeItem> &nodes)
{
    QList<NodeItem> next = nodes;
    for (NodeItem &n : next)
        n.selected = m_selected.contains(n.nodeId);
    applyDiff(m_nodes, next);
    m_rows.clear();
    for (int i = 0; i < m_nodes.size(); ++i)
        m_rows.insert(m_nodes.at(i).nodeId, i);
}

void NodeListModel::setSelected(const QStringList &ids)
{
    const QSet<QString> next(ids.begin(), ids.end());
    if (next == m_selected)
        return;
    m_selected = next;
    for (int i = 0; i < m_nodes.size(); ++i) {
        const bool sel = m_selected.contains(m_nodes.at(i).nodeId);
        if (m_nodes.at(i).selected == sel)
            continue;
        m_nodes[i].selected = sel;
        emit dataChanged(index(i), index(i), {SelectedRole});
    }
}

int NodeListModel::rowOfNode(const QString &id) const
{
    return m_rows.value(id, -1);
}

QVariantMap NodeListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_nodes.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return out;
}
