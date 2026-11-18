#include "ui/nodes/NodeGraphController.h"

#include <algorithm>

NodeGraphController::NodeGraphController(QObject *parent)
    : QObject(parent)
    , m_nodes(new NodeListModel(this))
    , m_edges(new EdgeListModel(this))
{
    connect(m_nodes, &QAbstractItemModel::rowsRemoved, this, &NodeGraphController::pruneSelection);
}

void NodeGraphController::pruneSelection()
{
    QStringList kept;
    for (const QString &id : std::as_const(m_selection)) {
        const auto &list = m_nodes->nodes();
        if (std::any_of(list.begin(), list.end(), [&](const NodeItem &n) { return n.nodeId == id; }))
            kept << id;
    }
    if (kept.size() != m_selection.size())
        setSelection(kept);
}

void NodeGraphController::setSelection(const QStringList &ids)
{
    QStringList next;
    for (const QString &id : ids)
        if (!next.contains(id))
            next << id;
    if (next == m_selection)
        return;
    m_selection = next;
    m_nodes->setSelected(m_selection);
    emit selectionChanged();
}

void NodeGraphController::select(const QString &id, bool additive)
{
    if (!additive) {
        setSelection({id});
        return;
    }
    QStringList next = m_selection;
    if (!next.removeOne(id))
        next << id;
    setSelection(next);
}

QStringList NodeGraphController::nodesInRect(const QRectF &rect) const
{
    QStringList hits;
    for (const NodeItem &n : m_nodes->nodes()) {
        const QRectF box(n.x, n.y, n.width, nodeHeight(n.nodeId));
        if (box.intersects(rect))
            hits << n.nodeId;
    }
    return hits;
}

void NodeGraphController::selectInRect(const QRectF &rect, bool additive)
{
    QStringList next = additive ? m_selection : QStringList();
    for (const QString &id : nodesInRect(rect))
        if (!next.contains(id))
            next << id;
    setSelection(next);
}

void NodeGraphController::selectAll()
{
    QStringList all;
    for (const NodeItem &n : m_nodes->nodes())
        all << n.nodeId;
    setSelection(all);
}

void NodeGraphController::clearSelection()
{
    setSelection({});
}

void NodeGraphController::reportNodeHeight(const QString &id, qreal height)
{
    m_heights.insert(id, height);
}

bool NodeGraphController::socketsCompatible(const QString &outputType, const QString &inputType) const
{
    return outputType == inputType;
}

bool NodeGraphController::entryHasSocket(const CatalogEntry &entry, const QString &portType, bool fromOutput) const
{
    return !firstCompatibleSocket(entry, portType, fromOutput).isEmpty();
}

QString NodeGraphController::firstCompatibleSocket(const CatalogEntry &entry, const QString &portType,
                                                   bool fromOutput) const
{
    if (portType.isEmpty())
        return {};
    for (const CatalogSocket &s : fromOutput ? entry.inputs : entry.outputs) {
        const bool ok = fromOutput ? socketsCompatible(portType, s.type) : socketsCompatible(s.type, portType);
        if (ok)
            return s.id;
    }
    return {};
}

QVariantList NodeGraphController::quickAddEntries(const QString &query, const QString &portType,
                                                  bool fromOutput) const
{
    struct Hit
    {
        const CatalogEntry *entry;
        double score;
        int order;
    };

    const QString q = query.trimmed().toLower();
    const QList<CatalogEntry> all = catalog();
    QList<Hit> hits;
    for (int i = 0; i < all.size(); ++i) {
        const CatalogEntry &e = all.at(i);
        double score = 0;
        if (!q.isEmpty()) {
            if (e.label.toLower().startsWith(q))
                score = 4;
            else if (e.label.toLower().contains(q))
                score = 3;
            else if (e.category.toLower().contains(q))
                score = 2;
            else if (e.description.toLower().contains(q))
                score = 1;
            else
                continue;
        }
        const bool socketMatch = entryHasSocket(e, portType, fromOutput);
        if (q.isEmpty() && !portType.isEmpty() && !socketMatch)
            continue;
        if (socketMatch)
            score += 0.5;
        hits.append({&e, score, i});
    }

    QList<Hit> ordered;
    if (q.isEmpty()) {
        QStringList categories;
        for (const Hit &h : std::as_const(hits))
            if (!categories.contains(h.entry->category))
                categories << h.entry->category;
        for (const QString &c : std::as_const(categories))
            for (const Hit &h : std::as_const(hits))
                if (h.entry->category == c)
                    ordered.append(h);
    } else {
        ordered = hits;
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const Hit &a, const Hit &b) { return a.score > b.score; });
    }

    QVariantList out;
    for (const Hit &h : std::as_const(ordered)) {
        out.append(QVariantMap{
            {QStringLiteral("type"), h.entry->type},
            {QStringLiteral("label"), h.entry->label},
            {QStringLiteral("category"), h.entry->category},
            {QStringLiteral("description"), h.entry->description},
            {QStringLiteral("accent"), h.entry->accent},
        });
    }
    return out;
}
