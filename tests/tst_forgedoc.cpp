#include "app/forge/compiler/Validate.h"
#include "app/forge/doc/ForgeDoc.h"
#include "app/forge/doc/Naming.h"
#include "app/forge/doc/Util.h"
#include "app/forge/edit/GraphOps.h"
#include "app/forge/edit/ParamOps.h"
#include "app/forge/glsl/Literals.h"
#include "app/forge/nodes/Registry.h"

#include <QDir>
#include <QJsonDocument>
#include <QtTest>

using namespace drift::forge;

namespace {

QStringList fixtureDirs()
{
    QDir root(QStringLiteral(DRIFT_TEST_DATA_DIR "/forge"));
    return root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

QJsonObject fixture(const QString &name, const QString &rel = QStringLiteral("forge.json"))
{
    return readJson(QStringLiteral(DRIFT_TEST_DATA_DIR "/forge/%1/%2").arg(name, rel));
}

ForgeNode makeNode(const QString &id, const QString &type)
{
    ForgeNode n = createNode(type, 0, 0);
    n.id = id;
    return n;
}

ForgeDoc timeChain()
{
    ForgeDoc d = emptyDoc(Kind::Effect);
    d.nodes << makeNode("a", "time") << makeNode("b", "time");
    return d;
}

}

class TstForgeDoc : public QObject
{
    Q_OBJECT
private slots:
    void roundTripFixtures()
    {
        const QStringList dirs = fixtureDirs();
        QVERIFY(dirs.size() >= 150);
        for (const QString &name : dirs) {
            const QJsonObject original = fixture(name);
            QString error;
            const ForgeDoc doc = ForgeDoc::fromJson(original, &error);
            QVERIFY2(error.isEmpty(), qPrintable(name + ": " + error));
            QVERIFY2(doc.toJson() == original, qPrintable(name + " does not round-trip"));
        }
    }

    void unknownKeysSurvive()
    {
        QJsonObject o = fixture(QStringLiteral("node_number"));
        o["webOnly"] = 7;
        QJsonObject meta = o["meta"].toObject();
        meta["badge"] = "x";
        o["meta"] = meta;
        QJsonArray nodes = o["nodes"].toArray();
        QJsonObject n0 = nodes[0].toObject();
        n0["collapsed"] = true;
        nodes[0] = n0;
        o["nodes"] = nodes;
        QCOMPARE(ForgeDoc::fromJson(o).toJson(), o);
    }

    void schema2ClipBecomesTrack()
    {
        QJsonObject o = fixture(QStringLiteral("node_number"));
        o["forge"] = 2;
        o["params"] = QJsonArray{QJsonObject{{"identifier", "src"}, {"displayName", "Src"}, {"type", "clip"},
                                             {"min", 0}, {"max", 1}, {"default", ""}}};
        const ForgeDoc d = ForgeDoc::fromJson(o);
        QCOMPARE(d.params.at(0).type, ParamType::Track);
        QCOMPARE(d.toJson().value("forge").toInt(), 3);
    }

    void rejectsBadDocuments()
    {
        QString error;
        ForgeDoc::fromJson(QJsonObject{{"kind", "effect"}}, &error);
        QCOMPARE(error, QStringLiteral("This isn't a Drift Forge document."));
        QJsonObject o = fixture(QStringLiteral("node_number"));
        o["forge"] = 4;
        ForgeDoc::fromJson(o, &error);
        QVERIFY(error.contains("newer"));
        o = fixture(QStringLiteral("node_number"));
        o["kind"] = "bogus";
        ForgeDoc::fromJson(o, &error);
        QCOMPARE(error, QStringLiteral("Unknown document kind."));
        o = fixture(QStringLiteral("node_number"));
        o.remove("nodes");
        ForgeDoc::fromJson(o, &error);
        QCOMPARE(error, QStringLiteral("The document is incomplete."));
        QJsonObject legacy = fixture(QStringLiteral("audio_echo"));
        legacy["audio"] = QJsonObject{{"processor", "echo"}};
        ForgeDoc::fromJson(legacy, &error);
        QVERIFY(error.contains("Schema-1"));
    }

    void validateFixtures()
    {
        int checked = 0;
        QMap<QString, int> skipped;
        int skippedDocs = 0;
        for (const QString &name : fixtureDirs()) {
            const ForgeDoc doc = ForgeDoc::fromJson(fixture(name));
            if (doc.kind == Kind::Audio)
                continue;
            if (!fixture(name, QStringLiteral("preview/preview.json")).value("ok").toBool())
                continue;
            bool known = true;
            for (const ForgeNode &n : doc.nodes) {
                if (!nodeDef(n.type)) {
                    skipped[n.type]++;
                    known = false;
                }
            }
            if (!known) {
                ++skippedDocs;
                continue;
            }
            const auto errors = validate(doc);
            QVERIFY2(errors.isEmpty(), qPrintable(name + ": " + (errors.isEmpty() ? QString() : errors.first().message)));
            ++checked;
        }
        qInfo() << "validated" << checked << "fixtures; skipped" << skippedDocs << "with unregistered node types:" << skipped;
        QCOMPARE(skippedDocs, 0);
        QVERIFY(checked > 0);
    }

    void validateMessages()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        QCOMPARE(validate(d).first().message, QStringLiteral("Add an Output node."));
        QVERIFY(validate(d, {false}).isEmpty());
        d.nodes << makeNode("v", "from") << makeNode("o", "effect_output") << makeNode("o2", "effect_output");
        const auto errors = validate(d);
        QCOMPARE(errors.size(), 2);
        QCOMPARE(errors[0].message, QStringLiteral("From clip can't be used in an effect."));
        QCOMPARE(errors[1].message, QStringLiteral("Only one Output node is allowed."));
        QCOMPARE(nodeErrors(errors).value("o2"), QStringLiteral("Only one Output node is allowed."));
    }

    void validateLoopAndBindings()
    {
        ForgeDoc d = timeChain();
        d.edges << ForgeEdge{"e1", "a", "value", "b", "speed", {}} << ForgeEdge{"e2", "b", "value", "a", "speed", {}};
        d.nodes << makeNode("o", "effect_output");
        bool loop = false;
        for (const auto &e : validate(d))
            loop |= e.message.startsWith("The graph loops");
        QVERIFY(loop);

        ForgeDoc e = emptyDoc(Kind::Effect);
        e.nodes << makeNode("n", "number") << makeNode("o", "effect_output");
        e.nodes[0].inputs["value"] = makeParamRef("ghost");
        QCOMPARE(validate(e).first().message, QStringLiteral("Uses a slider \"ghost\" that no longer exists."));
    }

    void addNodeUnknownAndKnown()
    {
        const ForgeDoc d = emptyDoc(Kind::Effect);
        const OpResult bad = addNode(d, "nope", 0, 0);
        QCOMPARE(bad.error, QStringLiteral("Unknown block type \"nope\"."));
        const OpResult r = addNode(d, "number", 10, 20);
        QVERIFY(r.ok());
        QCOMPARE(r.doc.nodes.size(), 1);
        QCOMPARE(r.doc.node(r.created.at(0))->x, 10.0);
        QCOMPARE(r.doc.node(r.created.at(0))->inputs.value("value").toDouble(), 0.5);
        QCOMPARE(d.nodes.size(), 0);
    }

    void connectReplacesExistingWire()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        d.nodes << makeNode("a", "time") << makeNode("b", "time") << makeNode("c", "time");
        OpResult r = drift::forge::connect(d, "a", "value", "c", "speed");
        QVERIFY(r.ok());
        r = drift::forge::connect(r.doc, "b", "value", "c", "speed");
        QVERIFY(r.ok());
        QCOMPARE(r.doc.edges.size(), 1);
        QCOMPARE(r.doc.edges[0].from, QStringLiteral("b"));
    }

    void connectRejectsLoopsAndBadSockets()
    {
        const ForgeDoc d = timeChain();
        OpResult r = drift::forge::connect(d, "a", "value", "b", "speed");
        QVERIFY(r.ok());
        r = drift::forge::connect(r.doc, "b", "value", "a", "speed");
        QCOMPARE(r.error, QStringLiteral("That connection would make a loop."));
        QCOMPARE(drift::forge::connect(d, "a", "value", "a", "speed").error, QStringLiteral("That connection would make a loop."));
        QCOMPARE(drift::forge::connect(d, "a", "nope", "b", "speed").error, QStringLiteral("\"time\" has no output \"nope\"."));
        QCOMPARE(drift::forge::connect(d, "a", "value", "b", "nope").error, QStringLiteral("\"time\" has no input \"nope\"."));
        QCOMPARE(drift::forge::connect(d, "zz", "value", "b", "speed").error, QStringLiteral("One of those blocks doesn't exist."));
    }

    void removeNodesSkipsOutput()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        d.nodes << makeNode("v", "video") << makeNode("o", "effect_output");
        d = drift::forge::connect(d, "v", "image", "o", "image").doc;
        const ForgeDoc r = removeNodes(d, {"v", "o"});
        QCOMPARE(r.nodes.size(), 1);
        QCOMPARE(r.nodes[0].id, QStringLiteral("o"));
        QVERIFY(r.edges.isEmpty());
    }

    void duplicateRelinksInternalEdges()
    {
        ForgeDoc d = timeChain();
        d.nodes << makeNode("o", "effect_output");
        d = drift::forge::connect(d, "a", "value", "b", "speed").doc;
        d = drift::forge::connect(d, "b", "value", "o", "image").doc;
        const OpResult r = duplicateNodes(d, {"a", "b", "o"});
        QCOMPARE(r.created.size(), 2);
        QCOMPARE(r.doc.nodes.size(), 5);
        QCOMPARE(r.doc.edges.size(), 3);
        const ForgeEdge &copy = r.doc.edges.last();
        QCOMPARE(copy.from, r.created[0]);
        QCOMPARE(copy.to, r.created[1]);
        QCOMPARE(r.doc.node(r.created[0])->x, 40.0);
        QCOMPARE(r.doc.node(r.created[0])->y, 40.0);
    }

    void exposeAndUnexpose()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        d.nodes << makeNode("n", "number");
        const OpResult r = expose(d, "n", "value");
        QVERIFY(r.ok());
        QCOMPARE(r.created, QStringList{"value"});
        QCOMPARE(r.doc.params.size(), 1);
        QCOMPARE(r.doc.params[0].type, ParamType::Float);
        QCOMPARE(r.doc.params[0].defaultValue.toDouble(), 0.5);
        QCOMPARE(r.doc.nodes[0].inputs.value("value"), QJsonValue(QJsonObject{{"param", "value"}}));
        QCOMPARE(paramUsers(r.doc, "value"), 1);

        const ForgeDoc back = unexpose(r.doc, "n", "value");
        QVERIFY(back.params.isEmpty());
        QCOMPARE(back.nodes[0].inputs.value("value").toDouble(), 0.5);
    }

    void exposeColorAndNames()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        d.nodes << makeNode("c", "color_value");
        const OpResult r = expose(d, "c", "color");
        QVERIFY(r.ok());
        QCOMPARE(r.doc.params[0].type, ParamType::Color);
        QCOMPARE(r.doc.params[0].defaultValue.toString(), QStringLiteral("#ffc208"));
        QCOMPARE(r.doc.params[0].identifier, QStringLiteral("color"));
        const OpResult second = expose(addNode(r.doc, "number", 0, 0).doc, r.doc.nodes.size() ? r.doc.nodes[0].id : "", "color");
        QVERIFY(second.ok());
        QCOMPARE(second.doc.params.size(), 1);
    }

    void removeParamReleasesBindings()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        d.nodes << makeNode("n", "number");
        d = expose(d, "n", "value").doc;
        d = updateParam(d, "value", QJsonObject{{"default", 0.9}}).doc;
        const ForgeDoc r = removeParam(d, "value");
        QVERIFY(r.params.isEmpty());
        QCOMPARE(r.nodes[0].inputs.value("value").toDouble(), 0.9);
    }

    void renameUpdatesBindingsAndRejectsBadNames()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        d.nodes << makeNode("n", "number");
        d = expose(d, "n", "value").doc;
        d = addParam(d, [] {
            ParamDef p;
            p.identifier = "other";
            p.displayName = "Other";
            p.defaultValue = 0;
            return p;
        }());

        OpResult r = updateParam(d, "value", QJsonObject{{"identifier", "level"}});
        QVERIFY(r.ok());
        QCOMPARE(r.doc.nodes[0].inputs.value("value"), QJsonValue(QJsonObject{{"param", "level"}}));
        QCOMPARE(r.doc.params[0].identifier, QStringLiteral("level"));

        QVERIFY(updateParam(d, "value", QJsonObject{{"identifier", "u_speed"}}).error.contains("reserved by Drift"));
        QCOMPARE(updateParam(d, "value", QJsonObject{{"identifier", "float"}}).error,
                 QStringLiteral("That word is reserved in shader code."));
        QCOMPARE(updateParam(d, "value", QJsonObject{{"identifier", QString(49, 'a')}}).error,
                 QStringLiteral("Keep it under 48 characters."));
        QCOMPARE(updateParam(d, "value", QJsonObject{{"identifier", "other"}}).error,
                 QStringLiteral("Another slider already uses that name."));
        QCOMPARE(updateParam(d, "value", QJsonObject{{"identifier", "9lives"}}).error,
                 QStringLiteral("Use letters, digits and _ only, starting with a letter."));
        QVERIFY(updateParam(d, "value", QJsonObject{{"identifier", "n_x"}}).error.contains("generated code"));
        QVERIFY(paramNameProblem("fine_name").isNull());
    }

    void validateReportsDuplicateAndBadParams()
    {
        ForgeDoc d = emptyDoc(Kind::Effect);
        d.nodes << makeNode("o", "effect_output");
        ParamDef p;
        p.identifier = "dup";
        p.displayName = "Dup";
        p.min = 1;
        p.max = 1;
        p.defaultValue = 0;
        d.params << p << p;
        const auto errors = validate(d);
        QStringList msgs;
        for (const auto &e : errors)
            msgs << e.message;
        QVERIFY(msgs.contains("Two sliders are named \"dup\"."));
        QVERIFY(msgs.contains("Slider \"Dup\": the lowest value must be below the highest."));
    }

    void glslLiterals()
    {
        QCOMPARE(glslFloat(1), QStringLiteral("1.0"));
        QCOMPARE(glslFloat(0.5), QStringLiteral("0.5"));
        QCOMPARE(glslFloat(-0.0000001), QStringLiteral("0.0"));
        QCOMPARE(glslFloat(0.1234567), QStringLiteral("0.123457"));
        QCOMPARE(glslLiteral(0.5, SocketType::Color), QStringLiteral("vec4(vec3(0.5), 1.0)"));
        QCOMPARE(glslLiteral(QJsonArray{0.5, 1}, SocketType::Vec2), QStringLiteral("vec2(0.5, 1.0)"));
        QCOMPARE(glslLiteral(QJsonArray{1, 0, 0, 1}, SocketType::Color), QStringLiteral("vec4(1.0, 0.0, 0.0, 1.0)"));
        QCOMPARE(coerce("x", SocketType::Color, SocketType::Float), QStringLiteral("luma(x)"));
        QCOMPARE(coerce("x", SocketType::Vec2, SocketType::Color), QStringLiteral("vec4(x, 0.0, 1.0)"));
    }
};

QTEST_APPLESS_MAIN(TstForgeDoc)
#include "tst_forgedoc.moc"
