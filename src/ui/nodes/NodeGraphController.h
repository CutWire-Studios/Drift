#pragma once

#include "ui/nodes/EdgeListModel.h"
#include "ui/nodes/NodeListModel.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// What a graph editor needs from the graph behind it. The base owns the two list models and the
// selection; a subclass (Forge, Audio Forge) owns the document, pushes it into nodes()/edges()
// with setNodes()/setEdges() after every change, and implements the edits.
//
// Wires always run from an output port to an input port, so connect calls take the output end
// first whichever way the user dragged.
class NodeGraphController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Subclassed by the graph workspaces")

    Q_PROPERTY(NodeListModel *nodes READ nodes CONSTANT)
    Q_PROPERTY(EdgeListModel *edges READ edges CONSTANT)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
    struct CatalogSocket
    {
        QString id;
        QString type;
    };

    struct CatalogEntry
    {
        QString type;
        QString label;
        QString category;
        QString description;
        QString accent;
        QList<CatalogSocket> inputs;
        QList<CatalogSocket> outputs;
    };

    explicit NodeGraphController(QObject *parent = nullptr);

    NodeListModel *nodes() const { return m_nodes; }
    EdgeListModel *edges() const { return m_edges; }
    QStringList selection() const { return m_selection; }

    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;

    Q_INVOKABLE void select(const QString &id, bool additive);
    Q_INVOKABLE void setSelection(const QStringList &ids);
    Q_INVOKABLE void selectInRect(const QRectF &rect, bool additive);
    Q_INVOKABLE QStringList nodesInRect(const QRectF &rect) const;
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void reportNodeHeight(const QString &id, qreal height);

    // dx/dy are relative to the nodes' current positions; endMove() closes the undo step.
    Q_INVOKABLE virtual void moveNodes(const QStringList &ids, qreal dx, qreal dy) = 0;
    Q_INVOKABLE virtual void endMove() = 0;
    Q_INVOKABLE virtual bool canConnect(const QString &fromNode, const QString &fromPort,
                                        const QString &toNode, const QString &toPort) const = 0;
    Q_INVOKABLE virtual bool connectPorts(const QString &fromNode, const QString &fromPort,
                                          const QString &toNode, const QString &toPort) = 0;
    Q_INVOKABLE virtual void disconnectInput(const QString &node, const QString &port) = 0;
    Q_INVOKABLE virtual void deleteSelection() = 0;
    Q_INVOKABLE virtual void duplicateSelection() = 0;
    Q_INVOKABLE virtual void copySelection() = 0;
    Q_INVOKABLE virtual void paste(const QPointF &at) = 0;
    Q_INVOKABLE virtual void undo() = 0;
    Q_INVOKABLE virtual void redo() = 0;
    Q_INVOKABLE virtual QString addNode(const QString &type, const QPointF &at) = 0;
    Q_INVOKABLE virtual QString addNodeConnected(const QString &type, const QPointF &at,
                                                 const QString &fromNode, const QString &fromPort,
                                                 bool fromIsOutput) = 0;
    // With coalesce set the edit merges into the previous undo step for the same input.
    Q_INVOKABLE virtual void setInputValue(const QString &node, const QString &input,
                                           const QVariant &value, bool coalesce) = 0;
    // Entries are {id, label, enabled}.
    Q_INVOKABLE virtual QVariantList nodeContextActions(const QString &id) const = 0;
    Q_INVOKABLE virtual void triggerNodeAction(const QString &id, const QString &action) = 0;

    // Entries are {type, label, category, description, accent}. portType is the socket type of a
    // dragged wire ("" for a plain add) and fromOutput says which end it was dragged from.
    Q_INVOKABLE QVariantList quickAddEntries(const QString &query, const QString &portType,
                                             bool fromOutput) const;

signals:
    void selectionChanged();
    void historyChanged();

protected:
    virtual QList<CatalogEntry> catalog() const = 0;
    virtual bool socketsCompatible(const QString &outputType, const QString &inputType) const;
    // The first socket of an entry a wire of portType can attach to, or an empty id.
    QString firstCompatibleSocket(const CatalogEntry &entry, const QString &portType, bool fromOutput) const;
    qreal nodeHeight(const QString &id) const { return m_heights.value(id, 80.0); }

private:
    bool entryHasSocket(const CatalogEntry &entry, const QString &portType, bool fromOutput) const;
    void pruneSelection();

    NodeListModel *m_nodes;
    EdgeListModel *m_edges;
    QStringList m_selection;
    QHash<QString, qreal> m_heights;
};
