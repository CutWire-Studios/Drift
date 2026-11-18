#include "app/forge/audio/AudioManifest.h"
#include "app/forge/audio/RackGraph.h"
#include "app/forge/audio/RackOps.h"
#include "app/forge/audio/RackValidate.h"
#include "app/forge/compiler/Manifest.h"
#include "app/forge/compiler/PackageFiles.h"
#include "app/forge/doc/Util.h"
#include "app/forge/edit/ParamOps.h"
#include "engine/audio/dsp/graph/AudioGraph.h"

#include <QJsonDocument>
#include <QtTest>

using namespace drift::forge;

namespace {

const QString kRoot = QStringLiteral(DRIFT_TEST_DATA_DIR "/forge");

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

ForgeDoc load(const QString &name)
{
    QString error;
    const ForgeDoc doc = ForgeDoc::fromJsonText(readFile(kRoot + "/" + name + "/forge.json"), &error);
    Q_ASSERT(error.isEmpty());
    return doc;
}

ForgeDoc must(const OpResult &r)
{
    if (!r.ok())
        qWarning("%s", qPrintable(r.error));
    Q_ASSERT(r.ok());
    return r.doc;
}

std::shared_ptr<const drift::audiofx::AudioGraphDesc> parse(const QJsonObject &manifest, QString *error)
{
    std::vector<std::string> ids;
    std::vector<float> defaults;
    for (const QJsonValue &v : manifest.value("parameters").toArray()) {
        const QJsonObject p = v.toObject();
        ids.push_back(p.value("identifier").toString().toStdString());
        defaults.push_back(float(p.value("defaultValue").toDouble()));
    }
    const std::string json = QJsonDocument(manifest.value("graph").toObject()).toJson(QJsonDocument::Compact).toStdString();
    std::string err;
    auto graph = drift::audiofx::parseAudioGraph(
        json, ids, defaults,
        [](const std::string &, std::string *) {
            auto ir = std::make_shared<drift::audiofx::IrData>();
            ir->samples = {1.0f, 0.5f};
            ir->channels = 1;
            ir->frames = 2;
            ir->sampleRate = 48000;
            return std::shared_ptr<const drift::audiofx::IrData>(ir);
        },
        &err);
    *error = QString::fromStdString(err);
    return graph;
}

// A filter swept by an LFO, then a blend into a convolution room and a switchable delay.
ForgeDoc boardDoc()
{
    ForgeDoc doc = emptyDoc(Kind::Audio, "Fixture rack");
    doc.meta.id = "forge_fixture_rack";
    const OpResult filter = addPedal(doc, "filter");
    const QString f = filter.created[0];
    doc = setValue(filter.doc, ValuePath::knobOf(f, "mode"), 2);
    const OpResult tone = exposeValue(doc, ValuePath::knobOf(f, "cutoff"));
    doc = tone.doc;
    const OpResult lfo = addModulator(doc, "lfo");
    doc = must(addRoute(lfo.doc, lfo.created[0], f, "cutoff", 0.3));
    const OpResult env = addModulator(doc, "envelope");
    doc = setModulatorSource(env.doc, env.created[0], f);
    const OpResult split = addSplit(doc, "parallel");
    const QString s = split.created[0];
    doc = must(setCrossfade(split.doc, s, true));
    doc = must(exposeValue(doc, ValuePath::blendOf(s)));
    const QString wet = rackOf(doc).chain.back().lanes[1].id;
    const OpResult room = addPedal(doc, "convolution", {wet, -1});
    doc = setIr(room.doc, room.created[0], "builtin:room");
    const OpResult delay = addPedal(doc, "delay", {wet, -1});
    doc = must(exposeValue(delay.doc, ValuePath::bypassOf(delay.created[0])));
    return doc;
}

}

class TstAudioForge : public QObject
{
    Q_OBJECT

    static void fixtures()
    {
        QTest::addColumn<QString>("name");
        for (const char *n : {"audio_echo", "audio_dreamy_space", "audio_radio_voice"})
            QTest::newRow(n) << n;
    }

private slots:
    void rackRoundTrips_data() { fixtures(); }
    void manifestMatchesWeb_data() { fixtures(); }
    void graphParses_data() { fixtures(); }
    void previewManifestHoistsLiterals_data() { fixtures(); }

    void rackRoundTrips()
    {
        QFETCH(QString, name);
        const ForgeDoc doc = load(name);
        const QJsonObject rack = doc.audio->value("rack").toObject();
        QCOMPARE(Rack::fromJson(rack).toJson(), rack);
    }

    void rackKeepsUnknownKeys()
    {
        const QJsonObject rack = QJsonDocument::fromJson(R"({"chain":[{"id":"p","type":"gain","knobs":{"gain":1},"note":"x"},
            {"id":"s","type":"split","mode":"parallel","lanes":[{"id":"l","gain":1,"chain":[],"tag":1},{"id":"m","gain":1,"chain":[]}],"zz":true}],
            "modulators":[{"id":"m1","type":"lfo","knobs":{},"color":"red"}],"routes":[],"future":{"a":1}})").object();
        QCOMPARE(Rack::fromJson(rack).toJson(), rack);
    }

    void manifestMatchesWeb()
    {
        QFETCH(QString, name);
        const ForgeDoc doc = load(name);
        const QJsonObject expected = QJsonDocument::fromJson(readFile(kRoot + "/" + name + "/export/audio-effect.json")).object();
        QCOMPARE(audioManifest(doc), expected);
        QVERIFY(validateRack(doc).isEmpty());
    }

    void graphParses()
    {
        QFETCH(QString, name);
        const QJsonObject m = audioManifest(load(name));
        if (m.value("processor").toString() != QLatin1String("graph"))
            QSKIP("classic processor, no graph");
        QString error;
        QVERIFY2(parse(m, &error), qPrintable(error));
    }

    void previewManifestHoistsLiterals()
    {
        QFETCH(QString, name);
        const ForgeDoc doc = load(name);
        QJsonObject hoisted;
        const QJsonObject m = audioPreviewManifest(doc, &hoisted);
        QCOMPARE(m.value("processor").toString(), QStringLiteral("graph"));
        QVERIFY(!hoisted.isEmpty());
        const QJsonArray params = m.value("parameters").toArray();
        QCOMPARE(params.size(), doc.params.size() + hoisted.size());
        for (const QString &key : hoisted.keys()) {
            QVERIFY(key.startsWith("__k_"));
            bool found = false;
            for (const QJsonValue &v : params)
                if (v.toObject().value("identifier").toString() == key) {
                    found = true;
                    QCOMPARE(v.toObject().value("defaultValue").toDouble(), hoisted.value(key).toDouble());
                    QVERIFY(v.toObject().value("hidden").toBool());
                }
            QVERIFY2(found, qPrintable(key));
        }
        QString error;
        QVERIFY2(parse(m, &error), qPrintable(error));
        QVERIFY(!QString::fromUtf8(QJsonDocument(m.value("graph").toObject()).toJson()).contains("\"mode\": 0"));
    }

    void boardBuildsAndExports()
    {
        const ForgeDoc doc = boardDoc();
        QVERIFY(validateRack(doc).isEmpty());
        const QJsonObject m = audioManifest(doc);
        QCOMPARE(m.value("processor").toString(), QStringLiteral("graph"));
        QCOMPARE(m.value("nextFeatures").toArray(),
                 (QJsonArray{"audio:convolution", "audio:graph", "audio:modulation"}));
        QCOMPARE(m.value("icon").toString(), QStringLiteral("audio-waveform"));
        QString error;
        const auto graph = parse(m, &error);
        QVERIFY2(graph, qPrintable(error));
        QCOMPARE(int(graph->modulators.size()), 2);

        QJsonObject hoisted;
        const QJsonObject preview = audioPreviewManifest(doc, &hoisted);
        QVERIFY2(parse(preview, &error), qPrintable(error));
        QVERIFY(hoisted.contains("__k_" + rackOf(doc).routes[0].id + "_depth"));
        QCOMPARE(hoisted.value("__k_" + rackOf(doc).routes[0].id + "_depth").toDouble(), 0.3);

        const PackageContents c = packageContents(doc, {});
        QCOMPARE(c.manifest, m);
        QStringList paths;
        for (const PackageFile &f : c.files)
            paths << f.path;
        QCOMPARE(paths, (QStringList{"ir/room.wav", "forge.json"}));
        QVERIFY(c.files[0].data.startsWith("RIFF"));
        QCOMPARE(minAppVersion(doc, {}), QStringLiteral("0.8.0"));
        QVERIFY(exportProblem(doc).isEmpty());
    }

    void recordingIrComesFromAssets()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio, "Hall");
        doc.meta.id = "hall";
        doc = must(addPedal(doc, "convolution"));
        const QString p = rackOf(doc).chain[0].id;
        ForgeAsset a;
        a.id = "ir1";
        a.name = "mine";
        a.mime = "audio/wav";
        a.data = "data:audio/wav;base64," + QString::fromLatin1(QByteArray("RIFFxxxx").toBase64());
        doc.assets.append(a);
        doc = setIr(doc, p, "ir1");
        QCOMPARE(rackGraphJson(doc).value("chain").toArray()[0].toObject().value("ir").toString(), QStringLiteral("ir/ir1.wav"));
        const PackageContents c = packageContents(doc, {});
        QCOMPARE(c.files[0].path, QStringLiteral("ir/ir1.wav"));
        QCOMPARE(c.files[0].data, QByteArray("RIFFxxxx"));
        doc.assets.clear();
        QVERIFY(!validateRack(doc).isEmpty());
    }

    void addPedalIntoLaneAtIndex()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        const OpResult split = addSplit(doc, "bands", 3);
        QVERIFY(split.ok());
        const Rack r = rackOf(split.doc);
        QCOMPARE(r.chain[0].lanes.size(), size_t(3));
        QCOMPARE(r.chain[0].crossovers->size(), 2);
        QCOMPARE(r.chain[0].crossovers->at(0).number, 200.0);
        QCOMPARE(r.chain[0].crossovers->at(1).number, 5000.0);
        const QString lane = r.chain[0].lanes[1].id;
        doc = must(addPedal(split.doc, "gain", {lane, -1}));
        const OpResult second = addPedal(doc, "pan", {lane, 0});
        doc = second.doc;
        const Rack r2 = rackOf(doc);
        QCOMPARE(r2.chain[0].lanes[1].chain.size(), size_t(2));
        QCOMPARE(r2.chain[0].lanes[1].chain[0].id, second.created[0]);
        QCOMPARE(addPedal(doc, "nope").error, QStringLiteral("There is no pedal called \"nope\"."));
        QCOMPARE(addPedal(doc, "gain", {"missing", -1}).error, QStringLiteral("That lane doesn't exist."));
    }

    void splitDepthAndMoves()
    {
        ForgeDoc doc = must(addSplit(ForgeDoc(emptyDoc(Kind::Audio)), "parallel"));
        QString lane = rackOf(doc).chain[0].lanes[0].id;
        doc = must(addSplit(doc, "parallel", 2, {lane, -1}));
        const Rack r = rackOf(doc);
        const QString inner = r.chain[0].lanes[0].chain[0].lanes[0].id;
        QCOMPARE(addSplit(doc, "parallel", 2, {inner, -1}).error, QStringLiteral("Splits nest at most two deep."));

        const OpResult pedal = addPedal(doc, "gain");
        doc = pedal.doc;
        doc = must(moveItem(doc, pedal.created[0], {inner, -1}));
        QCOMPARE(rackOf(doc).chain.size(), size_t(1));
        QCOMPARE(rackOf(doc).chain[0].lanes[0].chain[0].lanes[0].chain[0].id, pedal.created[0]);
        const QString outerSplit = rackOf(doc).chain[0].id;
        QCOMPARE(moveItem(doc, outerSplit, {lane, -1}).error, QStringLiteral("A split can't go inside itself."));

        const OpResult a = addPedal(doc, "gain");
        const OpResult b = addPedal(a.doc, "pan");
        const OpResult c = addPedal(b.doc, "gain");
        doc = must(moveItem(c.doc, a.created[0], {QString(), 3}));
        const Rack moved = rackOf(doc);
        QCOMPARE(moved.chain[1].id, b.created[0]);
        QCOMPARE(moved.chain[2].id, a.created[0]);
        QCOMPARE(moved.chain[3].id, c.created[0]);
    }

    void lanesAndCrossfade()
    {
        ForgeDoc doc = must(addSplit(ForgeDoc(emptyDoc(Kind::Audio)), "parallel"));
        const QString s = rackOf(doc).chain[0].id;
        doc = must(setCrossfade(doc, s, true));
        QVERIFY(rackOf(doc).chain[0].crossfade);
        QCOMPARE(rackOf(doc).chain[0].blend->number, 0.5);
        doc = must(addLane(doc, s));
        QVERIFY(!rackOf(doc).chain[0].crossfade);
        QCOMPARE(setCrossfade(doc, s, true).error, QStringLiteral("Blending needs a parallel split with two lanes."));
        doc = must(addLane(doc, s));
        QCOMPARE(addLane(doc, s).error, QStringLiteral("A split has at most 4 lanes."));
        const QString lane = rackOf(doc).chain[0].lanes[3].id;
        doc = must(removeLane(doc, s, lane));
        doc = must(removeLane(doc, s, rackOf(doc).chain[0].lanes[2].id));
        QCOMPARE(removeLane(doc, s, rackOf(doc).chain[0].lanes[1].id).error, QStringLiteral("A split needs at least two lanes."));
    }

    void routesAddAndRemove()
    {
        ForgeDoc doc = must(addPedal(ForgeDoc(emptyDoc(Kind::Audio)), "filter"));
        const QString f = rackOf(doc).chain[0].id;
        const OpResult m = addModulator(doc, "lfo");
        QCOMPARE(addRoute(m.doc, "nope", f, "cutoff").error, QStringLiteral("That modulator doesn't exist."));
        QCOMPARE(addRoute(m.doc, m.created[0], f, "nope").error, QStringLiteral("That knob doesn't exist."));
        QCOMPARE(addRoute(m.doc, m.created[0], f, "mode").error, QStringLiteral("Switches can't be modulated; pick a knob that turns."));
        const OpResult r = addRoute(m.doc, m.created[0], f, "cutoff", 5);
        QVERIFY(r.ok());
        QCOMPARE(rackOf(r.doc).routes[0].depth.number, 1.0);
        const OpResult again = addRoute(r.doc, m.created[0], f, "cutoff", -0.5);
        QCOMPARE(again.created[0], r.created[0]);
        QCOMPARE(rackOf(again.doc).routes.size(), 1);
        QCOMPARE(rackOf(again.doc).routes[0].depth.number, -0.5);
        QVERIFY(rackOf(removeRoute(again.doc, r.created[0])).routes.isEmpty());
        QVERIFY(rackOf(removeItem(again.doc, f)).routes.isEmpty());
        QVERIFY(rackOf(removeModulator(again.doc, m.created[0])).routes.isEmpty());
        QVERIFY(validateRack(again.doc).isEmpty());
    }

    void exposeAndUnexpose()
    {
        ForgeDoc doc = must(addPedal(ForgeDoc(emptyDoc(Kind::Audio)), "filter"));
        const QString f = rackOf(doc).chain[0].id;
        const OpResult e = exposeValue(doc, ValuePath::knobOf(f, "cutoff"));
        QVERIFY(e.ok());
        QCOMPARE(e.doc.params.size(), 1);
        QCOMPARE(e.doc.params[0].displayName, QStringLiteral("Cutoff"));
        QCOMPARE(e.doc.params[0].ui.value("unit").toString(), QStringLiteral("Hz"));
        QCOMPARE(e.doc.params[0].defaultValue.toDouble(), 1200.0);
        QVERIFY(getValue(e.doc, ValuePath::knobOf(f, "cutoff"))->bound());
        QCOMPARE(setValue(e.doc, ValuePath::knobOf(f, "cutoff"), 500).audio, e.doc.audio);

        const OpResult mode = exposeValue(e.doc, ValuePath::knobOf(f, "mode"));
        QCOMPARE(mode.doc.params[1].type, ParamType::Choice);
        QCOMPARE(mode.doc.params[1].options->size(), 3);
        const OpResult off = exposeValue(mode.doc, ValuePath::bypassOf(f));
        QCOMPARE(off.doc.params[2].type, ParamType::Bool);
        QCOMPARE(off.doc.params[2].displayName, QStringLiteral("Off"));

        const ForgeDoc back = unexposeValue(off.doc, ValuePath::knobOf(f, "cutoff"));
        QCOMPARE(back.params.size(), 2);
        QCOMPARE(getValue(back, ValuePath::knobOf(f, "cutoff"))->number, 1200.0);
        QCOMPARE(exposeValue(doc, ValuePath::knobOf(f, "nope")).error, QStringLiteral("That control doesn't exist."));
        QCOMPARE(setValue(doc, ValuePath::knobOf(f, "cutoff"), 1e9).audio->value("rack").toObject()
                     .value("chain").toArray()[0].toObject().value("knobs").toObject().value("cutoff").toDouble(), 20000.0);
    }

    void validateCatchesLimits()
    {
        ForgeDoc doc = emptyDoc(Kind::Audio);
        Rack rack;
        for (int i = 0; i < 65; ++i) {
            RackItem p;
            p.id = QStringLiteral("p%1").arg(i);
            p.type = "gain";
            p.knobs.insert("gain", KnobValue::num(1));
            rack.chain.push_back(p);
        }
        doc = withRack(doc, rack);
        QVERIFY(validateRack(doc).contains("A pedalboard holds at most 64 pedals."));
        QCOMPARE(addPedal(doc, "gain").error, QStringLiteral("A pedalboard holds at most 64 pedals."));

        const auto split = [](const QString &id, std::vector<RackItem> inner) {
            RackItem s;
            s.id = id;
            s.type = "split";
            s.mode = "parallel";
            for (int i = 0; i < 2; ++i) {
                SplitLane l;
                l.id = id + QString::number(i);
                if (i == 0)
                    l.chain = inner;
                s.lanes.push_back(l);
            }
            return s;
        };
        Rack deep;
        deep.chain.push_back(split("a", {split("b", {split("c", {})})}));
        const QStringList problems = validateRack(withRack(emptyDoc(Kind::Audio), deep));
        QVERIFY(problems.contains("Splits nest at most two deep."));

        Rack broken;
        RackItem p;
        p.id = "x";
        p.type = "gain";
        p.knobs.insert("gain", KnobValue::ref("gone"));
        p.knobs.insert("bogus", KnobValue::num(1));
        broken.chain.push_back(p);
        broken.chain.push_back(p);
        const QStringList errs = validateRack(withRack(emptyDoc(Kind::Audio), broken));
        QVERIFY(errs.contains("Two pedals share the id x."));
        QVERIFY(errs.contains("Gain uses a slider that no longer exists."));
    }
};

QTEST_GUILESS_MAIN(TstAudioForge)
#include "tst_audioforge.moc"
