#include "app/forge/ForgeGraphMapping.h"
#include "app/forge/doc/Util.h"
#include "app/forge/edit/GraphOps.h"
#include "app/forge/edit/ParamOps.h"
#include "app/forge/nodes/Registry.h"

#include <QUndoStack>
#include <QVector2D>
#include <QVector4D>
#include <QtTest>

using namespace drift::forge;

namespace {

ForgeDoc brightnessDoc(QString *node = nullptr)
{
    ForgeDoc d = emptyDoc(Kind::Effect);
    const OpResult out = addNode(d, QStringLiteral("effect_output"), 400, 0);
    const OpResult video = addNode(out.doc, QStringLiteral("video"), 0, 0);
    const OpResult bc = addNode(video.doc, QStringLiteral("brightness_contrast"), 200, 0);
    if (node)
        *node = bc.created.value(0);
    OpResult wired = connect(bc.doc, video.created[0], QStringLiteral("image"), bc.created[0], QStringLiteral("image"));
    wired = connect(wired.doc, bc.created[0], QStringLiteral("image"), out.created[0], QStringLiteral("image"));
    return wired.doc;
}

}

class TstForgeGraphMapping : public QObject
{
    Q_OBJECT

private slots:
    void nodeItemsFollowTheDocument()
    {
        QString bcId;
        ForgeDoc d = brightnessDoc(&bcId);
        const QList<NodeItem> items = nodeItems(d, {{bcId, QStringLiteral("boom")}});
        QCOMPARE(items.size(), 3);
        const NodeItem *bc = nullptr;
        const NodeItem *out = nullptr;
        for (const NodeItem &i : items) {
            if (i.nodeId == bcId)
                bc = &i;
            if (i.type == QLatin1String("effect_output"))
                out = &i;
        }
        QVERIFY(bc && out);
        QCOMPARE(bc->title, QStringLiteral("Brightness / Contrast"));
        QCOMPARE(bc->accent, QStringLiteral("pink"));
        QCOMPARE(bc->error, QStringLiteral("boom"));
        QVERIFY(bc->deletable);
        QVERIFY(!out->deletable);
        QCOMPARE(bc->inputs.size(), 3);
        QVERIFY(bc->inputs[0].toMap().value("connected").toBool());
        QVERIFY(!bc->inputs[1].toMap().value("connected").toBool());
        QCOMPARE(bc->inputs[1].toMap().value("min").toDouble(), -1.0);
        QCOMPARE(bc->inputs[2].toMap().value("value").toDouble(), 1.0);
        QVERIFY(bc->outputs[0].toMap().value("connected").toBool());

        const QList<EdgeItem> edges = edgeItems(d);
        QCOMPARE(edges.size(), 2);
        QCOMPARE(edges[0].type, QStringLiteral("color"));
    }

    void exposedInputCarriesSliderName()
    {
        QString bcId;
        ForgeDoc d = brightnessDoc(&bcId);
        const OpResult r = expose(d, bcId, QStringLiteral("brightness"));
        QVERIFY(r.ok());
        const QList<NodeItem> items = nodeItems(r.doc, {});
        for (const NodeItem &i : items) {
            if (i.nodeId != bcId)
                continue;
            QCOMPARE(i.inputs[1].toMap().value("exposed").toString(), r.created.value(0));
        }
    }

    void catalogHidesOutputsAndOtherKinds()
    {
        const auto effect = catalogEntries(Kind::Effect);
        const auto transition = catalogEntries(Kind::Transition);
        auto has = [](const auto &list, const QString &type) {
            for (const auto &e : list)
                if (e.type == type)
                    return true;
            return false;
        };
        QVERIFY(has(effect, QStringLiteral("video")));
        QVERIFY(!has(effect, QStringLiteral("from")));
        QVERIFY(has(transition, QStringLiteral("from")));
        QVERIFY(!has(effect, QStringLiteral("effect_output")));
    }

    void literalConversion()
    {
        QCOMPARE(literalUniform(0.5, SocketType::Float).toDouble(), 0.5);
        QCOMPARE(literalUniform(true, SocketType::Float).toDouble(), 1.0);
        QCOMPARE(literalUniform(QJsonArray{0.25, 0.75}, SocketType::Vec2).value<QVector2D>(), QVector2D(0.25f, 0.75f));
        QCOMPARE(literalUniform(0.5, SocketType::Vec2).value<QVector2D>(), QVector2D(0.5f, 0.5f));
        QCOMPARE(literalUniform(QJsonArray{1, 0.5, 0, 0.25}, SocketType::Color).value<QVector4D>(),
                 QVector4D(1, 0.5f, 0, 0.25f));
        QCOMPARE(literalUniform(QJsonArray{1, 0.5, 0}, SocketType::Color).value<QVector4D>(), QVector4D(1, 0.5f, 0, 1));
        QCOMPARE(literalUniform(0.5, SocketType::Color).value<QVector4D>(), QVector4D(0.5f, 0.5f, 0.5f, 1));
    }

    void previewParamsMergeSlidersAndLiterals()
    {
        QString bcId;
        ForgeDoc d = brightnessDoc(&bcId);
        d = setInput(d, bcId, QStringLiteral("contrast"), 1.75);
        const OpResult r = expose(d, bcId, QStringLiteral("brightness"));
        QVERIFY(r.ok());
        d = r.doc;
        const QString slider = r.created.value(0);

        CompileOptions opts;
        opts.mode = CompileOptions::Preview;
        const CompileResult result = compile(d, opts);
        QVERIFY2(result.ok, qPrintable(result.errors.isEmpty() ? QString() : result.errors[0].message));

        QMap<QString, QVariant> params = previewParams(d, result, {});
        QVERIFY(params.contains(slider));
        QCOMPARE(params.value(slider).toDouble(), d.param(slider)->defaultValue.toDouble());
        const QString literal = QStringLiteral("k_%1_contrast").arg(bcId);
        QVERIFY2(params.contains(literal), qPrintable(params.keys().join(',')));
        QCOMPARE(params.value(literal).toDouble(), 1.75);

        params = previewParams(d, result, {{slider, 0.4}});
        QCOMPARE(params.value(slider).toDouble(), 0.4);
    }

    void colorAndPointParams()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        ParamDef c;
        c.identifier = QStringLiteral("tint");
        c.type = ParamType::Color;
        c.defaultValue = QStringLiteral("#FF8800");
        ParamDef p;
        p.identifier = QStringLiteral("spot");
        p.type = ParamType::Point;
        p.defaultValue = QJsonArray{0.2, 0.8};
        ParamDef a;
        a.identifier = QStringLiteral("glow");
        a.type = ParamType::Color;
        a.alpha = true;
        a.defaultValue = QStringLiteral("#00ff00");
        d.params = {c, p, a};
        const QMap<QString, QVariant> out = previewParams(d, {}, {});
        QCOMPARE(out.value("tint").toString(), QStringLiteral("#ff8800"));
        QCOMPARE(out.value("glow").toString(), QStringLiteral("#00ff00ff"));
        QCOMPARE(out.value("spot.x").toDouble(), 0.2);
        QCOMPARE(out.value("spot.y").toDouble(), 0.8);
    }

    void undoMergesSameKeyOnly()
    {
        ForgeDoc state = emptyDoc(Kind::Effect, QStringLiteral("a"));
        QUndoStack stack;
        auto apply = [&](const ForgeDoc &d) { state = d; };
        auto push = [&](const QString &name, const QString &key) {
            ForgeDoc next = state;
            next.meta.displayName = name;
            stack.push(new DocSnapshotCommand(state, next, QStringLiteral("edit"), key, apply));
        };
        push(QStringLiteral("b"), QStringLiteral("k#1"));
        push(QStringLiteral("c"), QStringLiteral("k#1"));
        push(QStringLiteral("d"), QStringLiteral("k#2"));
        push(QStringLiteral("e"), QString());
        push(QStringLiteral("f"), QString());
        QCOMPARE(stack.count(), 4);
        QCOMPARE(state.meta.displayName, QStringLiteral("f"));
        stack.undo();
        stack.undo();
        QCOMPARE(state.meta.displayName, QStringLiteral("d"));
        stack.undo();
        QCOMPARE(state.meta.displayName, QStringLiteral("c"));
        stack.undo();
        QCOMPARE(state.meta.displayName, QStringLiteral("a"));
        stack.redo();
        QCOMPARE(state.meta.displayName, QStringLiteral("c"));
    }
};

QTEST_GUILESS_MAIN(TstForgeGraphMapping)
#include "tst_forgegraphmapping.moc"
