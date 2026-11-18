#include "ui/nodes/NodeGraphController.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

NodeItem node(const QString &id, qreal x = 0, qreal y = 0)
{
    NodeItem n;
    n.nodeId = id;
    n.type = QStringLiteral("t");
    n.title = id;
    n.x = x;
    n.y = y;
    n.width = 200;
    return n;
}

QVariantMap input(const QString &id, const QVariant &value)
{
    return {{"id", id}, {"type", "float"}, {"value", value}};
}

class TestController : public NodeGraphController
{
public:
    bool canUndo() const override { return false; }
    bool canRedo() const override { return false; }
    void moveNodes(const QStringList &, qreal, qreal) override {}
    void endMove() override {}
    bool canConnect(const QString &, const QString &, const QString &, const QString &) const override { return false; }
    bool connectPorts(const QString &, const QString &, const QString &, const QString &) override { return false; }
    void disconnectInput(const QString &, const QString &) override {}
    void deleteSelection() override {}
    void duplicateSelection() override {}
    void copySelection() override {}
    void paste(const QPointF &) override {}
    void undo() override {}
    void redo() override {}
    QString addNode(const QString &, const QPointF &) override { return {}; }
    QString addNodeConnected(const QString &, const QPointF &, const QString &, const QString &, bool) override
    {
        return {};
    }
    void setInputValue(const QString &, const QString &, const QVariant &, bool) override {}
    QVariantList nodeContextActions(const QString &) const override { return {}; }
    void triggerNodeAction(const QString &, const QString &) override {}

    QList<CatalogEntry> entries;

protected:
    QList<CatalogEntry> catalog() const override { return entries; }
};

QStringList labels(const QVariantList &list)
{
    QStringList out;
    for (const QVariant &v : list)
        out << v.toMap().value("label").toString();
    return out;
}

} // namespace

class TstNodeGraphModels : public QObject
{
    Q_OBJECT

private slots:
    void moveEmitsDataChangedForThatRowOnly()
    {
        NodeListModel model;
        model.setNodes({node("a"), node("b"), node("c")});
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);

        model.setNodes({node("a"), node("b", 40, 10), node("c")});

        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 1);
        QCOMPARE(changed.at(0).at(1).toModelIndex().row(), 1);
        const QList<int> roles = changed.at(0).at(2).value<QList<int>>();
        QCOMPARE(roles, (QList<int>{NodeListModel::XRole, NodeListModel::YRole}));
        QCOMPARE(reset.size(), 0);
        QCOMPARE(inserted.size(), 0);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(model.get(1).value("x").toReal(), 40.0);
    }

    void unchangedNodesEmitNothing()
    {
        NodeListModel model;
        model.setNodes({node("a"), node("b")});
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        model.setNodes({node("a"), node("b")});
        QCOMPARE(changed.size(), 0);
    }

    void inputValueChangeIsOneRoleOnOneRow()
    {
        NodeListModel model;
        NodeItem a = node("a");
        a.inputs = {input("amount", 0.5)};
        model.setNodes({a, node("b")});
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        a.inputs = {input("amount", 0.75)};
        model.setNodes({a, node("b")});

        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 0);
        QCOMPARE(changed.at(0).at(2).value<QList<int>>(), (QList<int>{NodeListModel::InputsRole}));
        QCOMPARE(reset.size(), 0);
    }

    void addAndRemoveInsertAndRemoveRows()
    {
        NodeListModel model;
        model.setNodes({node("a"), node("b")});
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        model.setNodes({node("a"), node("c"), node("b")});
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(inserted.at(0).at(1).toInt(), 1);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(model.rowOfNode("c"), 1);
        QCOMPARE(model.rowOfNode("b"), 2);

        model.setNodes({node("b"), node("c")});
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.at(0).at(1).toInt(), 0);
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.rowOfNode("a"), -1);
        QCOMPARE(model.rowOfNode("b"), 0);
        QCOMPARE(model.rowOfNode("c"), 1);
        QCOMPARE(reset.size(), 0);
    }

    void edgeModelDiff()
    {
        EdgeListModel model;
        const EdgeItem e1{"e1", "a", "out", "b", "in", "float"};
        const EdgeItem e2{"e2", "b", "out", "c", "in", "color"};
        model.setEdges({e1, e2});
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
        QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

        EdgeItem moved = e2;
        moved.toPort = "other";
        model.setEdges({e1, moved});
        QCOMPARE(changed.size(), 1);
        QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 1);
        QCOMPARE(changed.at(0).at(2).value<QList<int>>(), (QList<int>{EdgeListModel::ToPortRole}));

        const EdgeItem e3{"e3", "a", "out", "c", "in", "float"};
        model.setEdges({e1, moved, e3});
        QCOMPARE(inserted.size(), 1);
        model.setEdges({moved, e3});
        QCOMPARE(removed.size(), 1);
        QCOMPARE(model.rowOfEdge("e1"), -1);
        QCOMPARE(model.rowOfEdge("e3"), 1);
        QCOMPARE(reset.size(), 0);
    }

    void selectionFollowsModelAndSurvivesUpdates()
    {
        TestController c;
        c.nodes()->setNodes({node("a"), node("b"), node("c")});
        QSignalSpy changed(c.nodes(), &QAbstractItemModel::dataChanged);
        c.select("b", false);
        QCOMPARE(c.selection(), QStringList{"b"});
        QCOMPARE(changed.size(), 1);
        QVERIFY(c.nodes()->get(1).value("selected").toBool());

        c.nodes()->setNodes({node("a"), node("b", 5, 5), node("c")});
        QVERIFY(c.nodes()->get(1).value("selected").toBool());

        c.select("c", true);
        QCOMPARE(c.selection(), (QStringList{"b", "c"}));
        c.select("b", true);
        QCOMPARE(c.selection(), QStringList{"c"});

        c.nodes()->setNodes({node("a"), node("b")});
        QVERIFY(c.selection().isEmpty());
    }

    void rectSelection()
    {
        TestController c;
        c.nodes()->setNodes({node("a", 0, 0), node("b", 300, 0), node("c", 0, 300)});
        c.reportNodeHeight("c", 50);
        c.selectInRect(QRectF(-10, -10, 250, 100), false);
        QCOMPARE(c.selection(), QStringList{"a"});
        c.selectInRect(QRectF(290, -10, 50, 50), true);
        QCOMPARE(c.selection(), (QStringList{"a", "b"}));
        c.selectInRect(QRectF(0, 320, 20, 20), false);
        QCOMPARE(c.selection(), QStringList{"c"});
        c.selectAll();
        QCOMPARE(c.selection().size(), 3);
    }

    void quickAddRanking()
    {
        using E = NodeGraphController::CatalogEntry;
        TestController c;
        c.entries = {
            E{"blur", "Blur", "Filter", "Softens the image", "blue", {{"in", "color"}}, {{"out", "color"}}},
            E{"noise", "Noise", "Generate", "Makes a blurry grain", "pink", {}, {{"out", "float"}}},
            E{"gaussian", "Gaussian Blur", "Filter", "", "blue", {{"in", "color"}}, {{"out", "color"}}},
            E{"tint", "Tint", "Color", "Multiplies by a blur colour", "amber", {{"in", "color"}}, {{"out", "color"}}},
            E{"wobble", "Wobble", "Blur Tools", "", "red", {{"in", "float"}}, {{"out", "float"}}},
        };

        QCOMPARE(labels(c.quickAddEntries("blur", "", true)),
                 (QStringList{"Blur", "Gaussian Blur", "Wobble", "Noise", "Tint"}));

        QCOMPARE(labels(c.quickAddEntries("blur", "float", true)),
                 (QStringList{"Blur", "Gaussian Blur", "Wobble", "Noise", "Tint"}));
        QCOMPARE(labels(c.quickAddEntries("blur", "color", true)),
                 (QStringList{"Blur", "Gaussian Blur", "Wobble", "Tint", "Noise"}));

        QCOMPARE(labels(c.quickAddEntries("", "", false)),
                 (QStringList{"Blur", "Gaussian Blur", "Noise", "Tint", "Wobble"}));
        QCOMPARE(labels(c.quickAddEntries("", "float", true)), (QStringList{"Wobble"}));
        QCOMPARE(labels(c.quickAddEntries("", "float", false)), (QStringList{"Noise", "Wobble"}));
        QVERIFY(c.quickAddEntries("zzz", "", true).isEmpty());

        const QVariantMap first = c.quickAddEntries("noi", "", true).first().toMap();
        QCOMPARE(first.value("type").toString(), QStringLiteral("noise"));
        QCOMPARE(first.value("accent").toString(), QStringLiteral("pink"));
    }
};

QTEST_APPLESS_MAIN(TstNodeGraphModels)
#include "tst_nodegraphmodels.moc"
