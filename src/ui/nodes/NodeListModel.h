#pragma once

#include "ui/nodes/ListDiff.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QtQml/qqmlregistration.h>
#include <QVariantList>
#include <QVariantMap>

// One node as the canvas sees it. Each entry of inputs is a map {id, label, type, connected,
// widget, value, min, max, step, options, exposed}; each entry of outputs is {id, label, type}.
// accent names a key of the nodeDelegate accent tokens.
struct NodeItem
{
    QString nodeId;
    QString type;
    QString title;
    QString category;
    QString accent;
    qreal x = 0;
    qreal y = 0;
    qreal width = 0;
    QVariantList inputs;
    QVariantList outputs;
    bool selected = false;
    QString error;
    QString thumbnail;
    bool deletable = true;
};

class NodeListModel : public QAbstractListModel, public ListDiff<NodeListModel, NodeItem>
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NodeGraphController")

public:
    enum Role {
        NodeIdRole = Qt::UserRole + 1,
        TypeRole,
        TitleRole,
        CategoryRole,
        AccentRole,
        XRole,
        YRole,
        WidthRole,
        InputsRole,
        OutputsRole,
        SelectedRole,
        ErrorRole,
        ThumbnailRole,
        DeletableRole,
    };
    Q_ENUM(Role)

    explicit NodeListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setNodes(const QList<NodeItem> &nodes);
    const QList<NodeItem> &nodes() const { return m_nodes; }
    void setSelected(const QStringList &ids);

    Q_INVOKABLE int rowOfNode(const QString &id) const;
    Q_INVOKABLE QVariantMap get(int row) const;

private:
    friend class ListDiff<NodeListModel, NodeItem>;
    static QString idOf(const NodeItem &n) { return n.nodeId; }
    QList<int> changedRoles(const NodeItem &a, const NodeItem &b) const;

    QList<NodeItem> m_nodes;
    QHash<QString, int> m_rows;
    QSet<QString> m_selected;
};
