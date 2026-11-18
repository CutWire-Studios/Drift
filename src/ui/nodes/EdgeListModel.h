#pragma once

#include "ui/nodes/ListDiff.h"

#include <QAbstractListModel>
#include <QHash>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

struct EdgeItem
{
    QString edgeId;
    QString fromNode;
    QString fromPort;
    QString toNode;
    QString toPort;
    QString type;
};

class EdgeListModel : public QAbstractListModel, public ListDiff<EdgeListModel, EdgeItem>
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NodeGraphController")

public:
    enum Role {
        EdgeIdRole = Qt::UserRole + 1,
        FromNodeRole,
        FromPortRole,
        ToNodeRole,
        ToPortRole,
        TypeRole,
    };
    Q_ENUM(Role)

    explicit EdgeListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEdges(const QList<EdgeItem> &edges);
    const QList<EdgeItem> &edges() const { return m_edges; }

    Q_INVOKABLE int rowOfEdge(const QString &id) const;
    Q_INVOKABLE QVariantMap get(int row) const;

private:
    friend class ListDiff<EdgeListModel, EdgeItem>;
    static QString idOf(const EdgeItem &e) { return e.edgeId; }
    QList<int> changedRoles(const EdgeItem &a, const EdgeItem &b) const;

    QList<EdgeItem> m_edges;
};
