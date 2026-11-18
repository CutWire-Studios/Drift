#include "app/forge/audio/RackGraphMapping.h"
#include "app/forge/doc/Util.h"

#include <QFile>
#include <QJsonDocument>
#include <QtTest>

using namespace drift::forge;
namespace rg = drift::forge::rackgraph;

namespace {

const QString kRoot = QStringLiteral(DRIFT_TEST_DATA_DIR "/forge");

ForgeDoc load(const QString &name)
{
    QFile f(kRoot + "/" + name + "/forge.json");
    Q_ASSERT(f.open(QIODevice::ReadOnly));
    QString error;
    const ForgeDoc doc = ForgeDoc::fromJsonText(f.readAll(), &error);
    Q_ASSERT(error.isEmpty());
    return doc;
}

ForgeDoc must(const OpResult &r)
{
    Q_ASSERT(r.ok());
    return r.doc;
}

const NodeItem *find(const QList<NodeItem> &items, const QString &id)
{
    for (const NodeItem &n : items)
        if (n.nodeId == id)
            return &n;
    return nullptr;
}

int countAudioEdges(const QList<EdgeItem> &edges)
{
    int n = 0;
    for (const EdgeItem &e : edges)
        n += e.type == QLatin1String("audio");
    return n;
}

}

class TstAudioForgeMapping : public QObject
{
    Q_OBJECT

private slots:
    void fixtureShape_data()
    {
        QTest::addColumn<QString>("name");
        for (const char *n : {"audio_echo", "audio_dreamy_space", "audio_radio_voice"})
            QTest::newRow(n) << n;
    }

    void fixtureShape()
    {
        QFETCH(QString, name);
        const ForgeDoc doc = load(name);
        Rack rack = rackOf(doc);
        int pedals = 0, splits = 0;
        walkRack(static_cast<const std::vector<RackItem> &>(rack.chain), [&](const RackItem &i, int) {
            (i.isSplit() ? splits : pedals)++;
        });
        const QList<NodeItem> nodes = rg::nodeItems(doc);
        QCOMPARE(nodes.size(), 2 + pedals + 2 * splits + int(rack.modulators.size()));
        const QList<EdgeItem> edges = rg::edgeItems(doc);
        int lanes = 0;
        walkRack(static_cast<const std::vector<RackItem> &>(rack.chain), [&](const RackItem &i, int) {
            lanes += int(i.lanes.size());
        });
        QCOMPARE(countAudioEdges(edges), pedals + splits + 1 + lanes);
        QCOMPARE(edges.size() - countAudioEdges(edges), int(rack.routes.size()));
        for (const NodeItem &n : nodes)
            QVERIFY2(n.x == n.x && n.y == n.y, "positions are numbers");
        walkRack(static_cast<const std::vector<RackItem> &>(rack.chain), [&](const RackItem &i, int) {
            if (i.isSplit()) {
                QVERIFY(find(nodes, i.id));
                const NodeItem *merge = find(nodes, rg::mergeId(i.id));
                QVERIFY(merge);
                QCOMPARE(merge->inputs.size(), int(i.lanes.size()));
                QCOMPARE(find(nodes, i.id)->outputs.size(), int(i.lanes.size()));
                QVERIFY(!merge->deletable);
            }
        });
        for (const EdgeItem &e : edges) {
            QVERIFY(find(nodes, e.fromNode));
            QVERIFY(find(nodes, e.toNode));
        }
    }

    void echoIsThreeNodes()
    {
        const ForgeDoc doc = load("audio_echo");
        QCOMPARE(rg::nodeItems(doc).size(), 3);
        QCOMPARE(rg::edgeItems(doc).size(), 2);
    }

    void routeEdgesTargetKnobPorts()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const OpResult f = addPedal(doc, "filter");
        const OpResult lfo = addModulator(f.doc, "lfo");
        doc = must(addRoute(lfo.doc, lfo.created[0], f.created[0], "cutoff"));
        const QList<EdgeItem> edges = rg::edgeItems(doc);
        const EdgeItem &route = edges.last();
        QCOMPARE(route.type, QStringLiteral("mod"));
        QCOMPARE(route.fromNode, lfo.created[0]);
        QCOMPARE(route.toNode, f.created[0]);
        QCOMPARE(route.toPort, QStringLiteral("cutoff"));
        const QList<NodeItem> items = rg::nodeItems(doc);
        QCOMPARE(items.size(), 4);
        const NodeItem *pedal = find(items, f.created[0]);
        bool connected = false;
        for (const QVariant &v : pedal->inputs)
            if (v.toMap().value("id").toString() == QLatin1String("cutoff"))
                connected = v.toMap().value("connected").toBool();
        QVERIFY(connected);
        QVERIFY(!rg::canConnect(doc, lfo.created[0], "out", f.created[0], "cutoff"));
    }

    void connectMovesItems()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const QString a = addPedal(doc, "filter").created[0];
        doc = addPedal(doc, "filter").doc;
        const OpResult first = addPedal(doc, "filter");
        doc = first.doc;
        const OpResult second = addPedal(doc, "gain");
        doc = second.doc;
        const OpResult third = addPedal(doc, "delay");
        doc = third.doc;
        Rack rack = rackOf(doc);
        const QString last = rack.chain.back().id;
        const QString head = rack.chain.front().id;
        Q_UNUSED(a);
        QVERIFY(rg::canConnect(doc, rg::kInputId, "out", last, "in"));
        const OpResult moved = rg::connect(doc, rg::kInputId, "out", last, "in");
        QVERIFY(moved.ok());
        QCOMPARE(rackOf(moved.doc).chain.front().id, last);
        QCOMPARE(rackOf(moved.doc).chain.size(), rack.chain.size());
        QVERIFY(!rg::canConnect(moved.doc, rg::kInputId, "out", last, "in"));

        const OpResult tail = rg::connect(doc, head, "out", rg::kOutputId, "in");
        QVERIFY(!rg::canConnect(doc, rg::kOutputId, "in", head, "out"));
        QVERIFY(tail.ok() ? rackOf(tail.doc).chain.back().id == head : false);
    }

    void rejectsInvalidWiring()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const OpResult s1 = addSplit(doc, "parallel");
        const QString outer = s1.created[0];
        const QString lane = rackOf(s1.doc).chain.back().lanes[0].id;
        const OpResult s2 = addSplit(s1.doc, "parallel", 2, {lane, -1});
        const QString mid = s2.created[0];
        const QString lane2 = [&] {
            Rack r = rackOf(s2.doc);
            return splitAt(r, mid)->lanes[0].id;
        }();
        const OpResult s3 = addSplit(s2.doc, "parallel", 2, {lane2, -1});
        QVERIFY(!s3.ok());
        const OpResult other = addSplit(s2.doc, "bands");
        const QString foreign = other.created[0];
        doc = other.doc;

        QVERIFY(!rg::canConnect(doc, foreign, "lane1", rg::mergeId(outer), "lane1"));
        QVERIFY(!rg::canConnect(doc, outer, "lane1", outer, "in"));
        const OpResult pedal = addPedal(doc, "gain");
        doc = pedal.doc;
        QVERIFY(!rg::canConnect(doc, pedal.created[0], "out", pedal.created[0], "in"));
        QVERIFY(!rg::canConnect(doc, pedal.created[0], "out", pedal.created[0], "gain"));
        QVERIFY(!rg::canConnect(doc, rg::kOutputId, "in", rg::kInputId, "out"));
        QVERIFY(rg::canConnect(doc, mid, "lane1", pedal.created[0], "in"));
        const QString m = rg::mergeId(mid);
        QVERIFY(!rg::canConnect(doc, rg::mergeId(foreign), "out", m, "lane1"));
    }

    void splitCannotNestThreeDeepViaWire()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const OpResult a = addSplit(doc, "parallel");
        Rack r = rackOf(a.doc);
        const OpResult b = addSplit(a.doc, "parallel", 2, {r.chain.back().lanes[0].id, -1});
        r = rackOf(b.doc);
        const QString innerLane = splitAt(r, b.created[0])->lanes[0].id;
        const OpResult c = addSplit(b.doc, "parallel");
        QVERIFY(c.ok());
        const QString third = c.created[0];
        const OpResult wired = rg::connect(c.doc, b.created[0], "lane1", third, "in");
        QVERIFY(!wired.ok());
        Q_UNUSED(innerLane);
    }

    void autoLayoutRunsLeftToRight()
    {
        const ForgeDoc doc = load("audio_dreamy_space");
        QVERIFY(rg::storedLayout(doc).isEmpty());
        const QMap<QString, QPointF> layout = rg::layoutOf(doc);
        QVERIFY(layout.value(rg::kInputId).x() < layout.value(rg::kOutputId).x());
        const Rack rack = rackOf(doc);
        qreal prev = layout.value(rg::kInputId).x();
        for (const RackItem &i : rack.chain) {
            QVERIFY(layout.value(i.id).x() > prev);
            prev = layout.value(i.id).x();
        }
        QCOMPARE(layout.size(), rg::nodeItems(doc).size());
    }

    void lanesStackVertically()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        doc = addSplit(doc, "parallel", 3).doc;
        Rack rack = rackOf(doc);
        doc = addPedal(doc, "gain", {rack.chain[0].lanes[0].id, -1}).doc;
        const OpResult p2 = addPedal(doc, "gain", {rack.chain[0].lanes[1].id, -1});
        const QMap<QString, QPointF> layout = rg::layoutOf(p2.doc);
        const Rack after = rackOf(p2.doc);
        const QString a = after.chain[0].lanes[0].chain[0].id;
        const QString b = after.chain[0].lanes[1].chain[0].id;
        QCOMPARE(layout[a].x(), layout[b].x());
        QVERIFY(layout[b].y() > layout[a].y());
    }

    void layoutRoundTrips()
    {
        ForgeDoc doc = load("audio_echo");
        const QString id = rackOf(doc).chain[0].id;
        doc = rg::moveNodes(doc, {id}, 25, -10);
        const QPointF moved = rg::layoutOf(doc).value(id);
        QString error;
        const ForgeDoc again = ForgeDoc::fromJsonText(QJsonDocument(doc.toJson()).toJson(), &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(rg::storedLayout(again).value(id), moved);
        QCOMPARE(rg::nodeItems(again).size(), 3);
        QCOMPARE(rg::nodeItems(again)[1].x, moved.x());
    }

    void addAndDeleteNodes()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const OpResult split = rg::addNode(doc, "split:parallel", QPointF(100, 50));
        QVERIFY(split.ok());
        QCOMPARE(rg::nodeItems(split.doc).size(), 4);
        const OpResult lfo = rg::addNode(split.doc, "lfo", QPointF(0, 300));
        QCOMPARE(rg::nodeItems(lfo.doc).size(), 5);
        const ForgeDoc gone = rg::deleteNodes(lfo.doc, {split.created[0], lfo.created[0], rg::kInputId});
        QCOMPARE(rg::nodeItems(gone).size(), 2);
        QVERIFY(rg::storedLayout(gone).value(split.created[0]).isNull());
    }

    void connectedAddSplicesIntoLane()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const OpResult split = rg::addNode(doc, "split:parallel", QPointF(0, 0));
        const OpResult added = rg::addNodeConnected(split.doc, "gain", QPointF(300, 0), split.created[0], "lane2", true);
        QVERIFY(added.ok());
        const Rack rack = rackOf(added.doc);
        QCOMPARE(rack.chain.size(), size_t(1));
        QCOMPARE(rack.chain[0].lanes[1].chain.size(), size_t(1));
    }

    void inputValuesReachTheRack()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const OpResult f = rg::addNode(doc, "filter", QPointF());
        doc = rg::setInputValue(f.doc, f.created[0], "cutoff", 5000.0);
        QCOMPARE(getValue(doc, ValuePath::knobOf(f.created[0], "cutoff"))->number, 5000.0);
        doc = rg::setInputValue(doc, f.created[0], "bypass", true);
        QVERIFY(getValue(doc, ValuePath::bypassOf(f.created[0]))->truthy());
    }

    void validationLandsOnNodes()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const OpResult f = rg::addNode(doc, "filter", QPointF());
        doc = must(exposeValue(f.doc, ValuePath::knobOf(f.created[0], "cutoff")));
        doc.params.clear();
        const QMap<QString, QString> errors = rg::nodeErrors(doc);
        QVERIFY(errors.contains(f.created[0]));
    }
};

QTEST_GUILESS_MAIN(TstAudioForgeMapping)
#include "tst_audioforgemapping.moc"
