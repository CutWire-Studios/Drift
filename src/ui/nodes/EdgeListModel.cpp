#include "ui/nodes/EdgeListModel.h"

EdgeListModel::EdgeListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int EdgeListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_edges.size());
}

QVariant EdgeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_edges.size())
        return {};
    const EdgeItem &e = m_edges.at(index.row());
    switch (role) {
    case EdgeIdRole: return e.edgeId;
    case FromNodeRole: return e.fromNode;
    case FromPortRole: return e.fromPort;
    case ToNodeRole: return e.toNode;
    case ToPortRole: return e.toPort;
    case TypeRole: return e.type;
    default: return {};
    }
}

QHash<int, QByteArray> EdgeListModel::roleNames() const
{
    return {
        {EdgeIdRole, "edgeId"},
        {FromNodeRole, "fromNode"},
        {FromPortRole, "fromPort"},
        {ToNodeRole, "toNode"},
        {ToPortRole, "toPort"},
        {TypeRole, "type"},
    };
}

QList<int> EdgeListModel::changedRoles(const EdgeItem &a, const EdgeItem &b) const
{
    QList<int> roles;
    if (a.fromNode != b.fromNode) roles << FromNodeRole;
    if (a.fromPort != b.fromPort) roles << FromPortRole;
    if (a.toNode != b.toNode) roles << ToNodeRole;
    if (a.toPort != b.toPort) roles << ToPortRole;
    if (a.type != b.type) roles << TypeRole;
    return roles;
}

void EdgeListModel::setEdges(const QList<EdgeItem> &edges)
{
    applyDiff(m_edges, edges);
}

int EdgeListModel::rowOfEdge(const QString &id) const
{
    for (int i = 0; i < m_edges.size(); ++i)
        if (m_edges.at(i).edgeId == id)
            return i;
    return -1;
}

QVariantMap EdgeListModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_edges.size())
        return out;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.begin(); it != names.end(); ++it)
        out.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return out;
}
