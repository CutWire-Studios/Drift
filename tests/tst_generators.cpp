#include <QtTest>

#include "core/project/Project.h"
#include "core/project/sources/GeneratorKinds.h"
#include "core/text/TextScriptPresets.h"
#include "engine/compositor/ScriptedText.h"
#include "engine/procedural/TextScript.h"
#include "engine/render2d/generators/ParticleSim.h"

#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/generators/SkiaGeneratorPainter.h"
#include "engine/render2d/skia/SkiaRuntime.h"
#endif

using namespace drift;

namespace {

ProceduralSource particleSource(const QString &presetId, int seed = 7)
{
    const GeneratorPreset *preset = generatorPreset(presetId);
    const GeneratorKind *kind = generatorKind(preset ? preset->kindId : presetId);
    ProceduralSource s;
    s.packageId = kind->id;
    s.parameters = generatorDefaultParameters(*kind, preset ? preset->params : QMap<QString, QVariant>{});
    s.seed = seed;
    return s;
}

bool samePositions(const particles::Frame &a, const particles::Frame &b)
{
    if (a.particles.size() != b.particles.size())
        return false;
    for (size_t i = 0; i < a.particles.size(); ++i) {
        const particles::Particle &p = a.particles[i];
        const particles::Particle &q = b.particles[i];
        if (p.x != q.x || p.y != q.y || p.age != q.age || p.id != q.id || p.rot != q.rot)
            return false;
    }
    return true;
}

} // namespace

class GeneratorsTest : public QObject
{
    Q_OBJECT

private slots:
    void kindsAndPresetsAreConsistent();
    void projectRoundTrip();
    void olderProjectsLoadWithoutScripts();
    void particleSimIsDeterministic();
    void particleCheckpointsAreKept();
    void particleEmitterKeyframesMoveTheSpawn();
    void particleBurstAndPrewarm();
    void textScriptResults();
    void textScriptErrorsAndLimits();
    void textScriptPresetsRun();
    void scriptedTextFindsSourceTime();
#ifdef DRIFT_WITH_SKIA
    void paintersDrawEveryKind();
    void paintersAreDeterministic();
    void solidAndTimeDrivenFlags();
#endif
};

void GeneratorsTest::kindsAndPresetsAreConsistent()
{
    QVERIFY(generatorKinds().size() >= 8);
    for (const GeneratorKind &kind : generatorKinds()) {
        QVERIFY2(kind.id.startsWith(QLatin1String("gen.")), qPrintable(kind.id));
        QVERIFY(!kind.parameters.isEmpty() || kind.id == QLatin1String("gen.solid"));
        QSet<QString> keys;
        for (const EffectParamSpec &spec : kind.parameters) {
            QVERIFY2(!keys.contains(spec.key), qPrintable(kind.id + " duplicate " + spec.key));
            keys.insert(spec.key);
            if (spec.isEnum())
                QVERIFY(spec.options.size() >= 2);
        }
        const QMap<QString, QVariant> defaults = generatorDefaultParameters(kind);
        for (const EffectParamSpec &spec : kind.parameters) {
            if (spec.isVec2())
                QVERIFY(defaults.contains(spec.key + QStringLiteral(".x")));
            else
                QVERIFY2(defaults.contains(spec.key), qPrintable(spec.key));
        }
    }
    QCOMPARE(generatorPresets().size(), 8);
    for (const GeneratorPreset &preset : generatorPresets()) {
        const GeneratorKind *kind = generatorKind(preset.kindId);
        QVERIFY(kind);
        for (auto it = preset.params.constBegin(); it != preset.params.constEnd(); ++it) {
            bool known = false;
            for (const EffectParamSpec &spec : kind->parameters)
                known = known || spec.key == it.key() || it.key().startsWith(spec.key + QLatin1Char('.'));
            QVERIFY2(known, qPrintable(preset.id + " sets unknown " + it.key()));
        }
    }
}

void GeneratorsTest::projectRoundTrip()
{
    QCOMPARE(Project::kCurrentVersion, 35);
    Project project;
    project.tracks().clear();
    Track track;
    track.type = TrackType::Shape;
    Clip gen;
    gen.id = QStringLiteral("g1");
    gen.type = ClipType::Generator;
    gen.timelineDuration = secondsToUs(4);
    gen.procedural = particleSource(QStringLiteral("particles.confetti"), 42);
    KeyframeTrack<double> ex;
    ex.setKeyframe(0, 0.2);
    ex.setKeyframe(secondsToUs(2), 0.8);
    gen.procedural.paramKeyframes.insert(QStringLiteral("emitter.x"), ex);
    track.clips.append(gen);

    Track textTrack;
    textTrack.type = TrackType::Text;
    Clip text;
    text.id = QStringLiteral("t1");
    text.type = ClipType::Text;
    text.timelineDuration = secondsToUs(2);
    text.textScript = QStringLiteral("return timecode(ctx.timeline, ctx.fps)\n");
    textTrack.clips.append(text);
    project.tracks().append(track);
    project.tracks().append(textTrack);

    const QJsonObject json = project.toJson();
    QCOMPARE(json.value(QStringLiteral("version")).toInt(), 35);
    QString error;
    const Project loaded = Project::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const Clip &g = loaded.tracks().at(0).clips.at(0);
    QCOMPARE(g.type, ClipType::Generator);
    QCOMPARE(g.procedural.packageId, QStringLiteral("gen.particles"));
    QCOMPARE(g.procedural.seed, 42);
    QCOMPARE(g.procedural.parameters, gen.procedural.parameters);
    QVERIFY(g.procedural.paramKeyframes.contains(QStringLiteral("emitter.x")));
    QCOMPARE(g.procedural.paramKeyframes.value(QStringLiteral("emitter.x")).evaluateAt(secondsToUs(1)), 0.5);
    QCOMPARE(loaded.tracks().at(1).clips.at(0).textScript, text.textScript);
    QCOMPARE(clipTypeFromString(clipTypeToString(ClipType::Generator)), ClipType::Generator);
}

void GeneratorsTest::olderProjectsLoadWithoutScripts()
{
    Project project;
    project.tracks().clear();
    Track textTrack;
    textTrack.type = TrackType::Text;
    Clip text;
    text.id = QStringLiteral("t1");
    text.type = ClipType::Text;
    text.textContent = QStringLiteral("hello");
    textTrack.clips.append(text);
    project.tracks().append(textTrack);
    QJsonObject json = project.toJson();
    json.insert(QStringLiteral("version"), 33);
    QString error;
    const Project loaded = Project::fromJson(json, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(loaded.tracks().at(0).clips.at(0).textScript, QString());
    QCOMPARE(loaded.tracks().at(0).clips.at(0).textContent, QStringLiteral("hello"));
}

void GeneratorsTest::particleSimIsDeterministic()
{
    particles::clearCache();
    const ProceduralSource source = particleSource(QStringLiteral("particles.fireworks"));
    const double aspect = 16.0 / 9.0;
    const TimeUs t = secondsToUs(3.37);

    const particles::Frame direct = particles::simulateUncached(source, aspect, t);
    QVERIFY(!direct.particles.empty());

    // Cached, reached by scrubbing forward in jumps, backwards, then straight there.
    particles::simulate(QStringLiteral("c"), source, aspect, secondsToUs(1.0));
    particles::simulate(QStringLiteral("c"), source, aspect, secondsToUs(5.0));
    particles::simulate(QStringLiteral("c"), source, aspect, secondsToUs(0.5));
    const particles::Frame scrubbed = particles::simulate(QStringLiteral("c"), source, aspect, t);
    QVERIFY(samePositions(direct, scrubbed));

    // Frame by frame at 30 fps, the way playback and export step.
    particles::clearCache();
    particles::Frame stepped;
    for (TimeUs us = 0; us <= t; us += 33333)
        stepped = particles::simulate(QStringLiteral("p"), source, aspect, us);
    const particles::Frame again = particles::simulateUncached(source, aspect, 33333 * (t / 33333));
    QVERIFY(samePositions(stepped, again));

    ProceduralSource other = source;
    other.seed = 8;
    QVERIFY(!samePositions(direct, particles::simulateUncached(other, aspect, t)));
}

void GeneratorsTest::particleCheckpointsAreKept()
{
    particles::clearCache();
    const ProceduralSource source = particleSource(QStringLiteral("particles.sparks"));
    particles::simulate(QStringLiteral("cp"), source, 16.0 / 9.0, secondsToUs(6.0));
    QCOMPARE(particles::cachedCheckpointCount(QStringLiteral("cp")), 3);
    // Editing the clip drops the checkpoints of the old parameters.
    ProceduralSource edited = source;
    edited.parameters.insert(QStringLiteral("gravity"), 50.0);
    particles::simulate(QStringLiteral("cp"), edited, 16.0 / 9.0, secondsToUs(1.0));
    QCOMPARE(particles::cachedCheckpointCount(QStringLiteral("cp")), 0);
}

void GeneratorsTest::particleEmitterKeyframesMoveTheSpawn()
{
    ProceduralSource source = particleSource(QStringLiteral("particles.sparks"));
    source.parameters.insert(QStringLiteral("gravity"), 0.0);
    source.parameters.insert(QStringLiteral("speed"), 0.0);
    source.parameters.insert(QStringLiteral("drag"), 0.0);
    source.parameters.insert(QStringLiteral("life"), 30.0);
    KeyframeTrack<double> ex;
    ex.setKeyframe(0, 0.1);
    ex.setKeyframe(secondsToUs(4), 0.9);
    source.paramKeyframes.insert(QStringLiteral("emitter.x"), ex);
    const double aspect = 2.0;
    const particles::Frame f = particles::simulateUncached(source, aspect, secondsToUs(4.0));
    QVERIFY(f.particles.size() > 100);
    float lo = 1e9f, hi = -1e9f;
    for (const particles::Particle &p : f.particles) {
        lo = std::min(lo, p.x);
        hi = std::max(hi, p.x);
    }
    // Early particles spawned near x = 0.1 * 2000, late ones near 0.9 * 2000.
    QVERIFY(lo < 400.0f);
    QVERIFY(hi > 1500.0f);
}

void GeneratorsTest::particleBurstAndPrewarm()
{
    ProceduralSource burst = particleSource(QStringLiteral("particles.confetti"));
    const particles::Frame early = particles::simulateUncached(burst, 1.0, secondsToUs(0.1));
    QCOMPARE(int(early.particles.size()), 160);

    ProceduralSource snow = particleSource(QStringLiteral("particles.snow"));
    const particles::Frame start = particles::simulateUncached(snow, 1.0, 0);
    QVERIFY2(start.particles.size() > 500, "pre-warmed snow is already falling at the clip start");
    snow.parameters.insert(QStringLiteral("prewarm"), 0.0);
    QVERIFY(particles::simulateUncached(snow, 1.0, 0).particles.empty());
}

void GeneratorsTest::textScriptResults()
{
    TextScriptContext ctx;
    ctx.timelineTime = 3725.5;
    ctx.clipTime = 2.0;
    ctx.duration = 4.0;
    ctx.fps = 25;
    ctx.projectName = "demo";
    QCOMPARE(QString::fromStdString(evaluateTextScript("return timecode(ctx.timeline, ctx.fps)", ctx).text),
             QStringLiteral("01:02:05:12"));
    QCOMPARE(QString::fromStdString(evaluateTextScript("return ctx.frame", ctx).text), QStringLiteral("50"));
    QCOMPARE(QString::fromStdString(evaluateTextScript("return ctx.project .. '!'", ctx).text), QStringLiteral("demo!"));
    QCOMPARE(QString::fromStdString(evaluateTextScript("return {'a', 'b'}", ctx).text), QStringLiteral("a\nb"));
    QCOMPARE(QString::fromStdString(evaluateTextScript("return commas(lerp(0, 12500, ease('linear', ctx.progress)))", ctx).text),
             QStringLiteral("6,250"));
    QCOMPARE(QString::fromStdString(evaluateTextScript("return tostring(ctx.source)", ctx).text), QStringLiteral("nil"));
    ctx.hasSource = true;
    ctx.sourceTime = 10.0;
    QCOMPARE(QString::fromStdString(evaluateTextScript("return timecode(ctx.source or ctx.timeline, 25)", ctx).text),
             QStringLiteral("00:00:10:00"));
    // Same inputs, same text.
    const TextScriptResult a = evaluateTextScript("return string.format('%.3f', ctx.clipTime * 1.5)", ctx);
    const TextScriptResult b = evaluateTextScript("return string.format('%.3f', ctx.clipTime * 1.5)", ctx);
    QCOMPARE(a.text, b.text);
}

void GeneratorsTest::textScriptErrorsAndLimits()
{
    TextScriptContext ctx;
    TextScriptResult r = evaluateTextScript("return nope.x", ctx);
    QVERIFY(!r.error.empty());
    QVERIFY(r.text.empty());
    QVERIFY2(QString::fromStdString(r.error).contains(QStringLiteral("script:1")), r.error.c_str());

    r = evaluateTextScript("return (", ctx);
    QVERIFY(!r.error.empty());

    // The sandbox has no clock, files or randomness.
    QCOMPARE(QString::fromStdString(evaluateTextScript("return tostring(os) .. tostring(io) .. tostring(math.random)", ctx).text),
             QStringLiteral("nilnilnil"));

    QElapsedTimer timer;
    timer.start();
    r = evaluateTextScript("while true do end", ctx);
    QVERIFY(!r.error.empty());
    QVERIFY(timer.elapsed() < 2000);

    r = evaluateTextScript("return function() end", ctx);
    QVERIFY(!r.error.empty());
}

void GeneratorsTest::textScriptPresetsRun()
{
    TextScriptContext ctx;
    ctx.timelineTime = 12.0;
    ctx.clipTime = 1.5;
    ctx.duration = 3.0;
    QVERIFY(textScriptPresets().size() >= 7);
    for (const TextScriptPreset &preset : textScriptPresets()) {
        const TextScriptResult r = evaluateTextScript(preset.script.toStdString(), ctx);
        QVERIFY2(r.error.empty(), qPrintable(preset.id + ": " + QString::fromStdString(r.error)));
        QVERIFY2(!r.text.empty(), qPrintable(preset.id));
    }
    QCOMPARE(QString::fromStdString(evaluateTextScript(textScriptPreset(QStringLiteral("loader"))->script.toStdString(), ctx).text),
             QStringLiteral("#####-----  50%"));
    QCOMPARE(QString::fromStdString(evaluateTextScript(textScriptPreset(QStringLiteral("stopwatch"))->script.toStdString(), ctx).text),
             QStringLiteral("00:01.50"));
}

void GeneratorsTest::scriptedTextFindsSourceTime()
{
    Project project;
    project.tracks().clear();
    project.setFps(25);
    Track video;
    video.type = TrackType::Video;
    Clip v;
    v.id = QStringLiteral("v");
    v.type = ClipType::Video;
    v.timelineStart = secondsToUs(2);
    v.timelineDuration = secondsToUs(10);
    v.srcIn = secondsToUs(100);
    v.srcOut = secondsToUs(110);
    video.clips.append(v);
    Track textTrack;
    textTrack.type = TrackType::Text;
    Clip text;
    text.id = QStringLiteral("t");
    text.type = ClipType::Text;
    text.timelineStart = 0;
    text.timelineDuration = secondsToUs(12);
    text.textScript = QStringLiteral("return timecode(ctx.source or -1, ctx.fps)");
    textTrack.clips.append(text);
    project.tracks().append(video);
    project.tracks().append(textTrack);
    const ScriptedText r = scriptedTextAt(project, project.tracks().at(1).clips.at(0), secondsToUs(5));
    QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
    QCOMPARE(r.text, QStringLiteral("00:01:43:00"));
}

#ifdef DRIFT_WITH_SKIA
namespace {

skia::GeneratorPaintRequest requestFor(const QString &id, TimeUs t, QSize size = QSize(320, 180))
{
    skia::GeneratorPaintRequest r;
    r.source = particleSource(id);
    r.resolved = r.source;
    r.clipId = QStringLiteral("clip-") + id;
    r.layoutRect = QRectF(0, 0, size.width(), size.height());
    r.renderScale = size.height() / 1080.0;
    r.clipTimeUs = t;
    r.textStyle.fontFamily = QStringLiteral("Inter");
    return r;
}

int inkOf(const QImage &image)
{
    int ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            ink += qAlpha(line[x]) > 8;
    }
    return ink;
}

} // namespace

void GeneratorsTest::paintersDrawEveryKind()
{
    for (const GeneratorKind &kind : generatorKinds()) {
        const skia::GeneratorPaintRequest r = requestFor(kind.id, secondsToUs(1.2));
        const skia::GeneratorPainterResult result = skia::makeGeneratorPainter(r);
        QVERIFY2(result.painter, qPrintable(kind.id));
        QCOMPARE(result.painter->size(), QSize(320, 180));
        const QImage image = skia::SkiaRuntime::rasterize(*result.painter);
        QCOMPARE(image.size(), QSize(320, 180));
        QVERIFY2(inkOf(image) > (kind.id == QLatin1String("gen.particles") ? 30 : 1000), qPrintable(kind.id));
    }
    for (const GeneratorPreset &preset : generatorPresets()) {
        const skia::GeneratorPaintRequest r = requestFor(preset.id, secondsToUs(1.2));
        const QImage image = skia::SkiaRuntime::rasterize(*skia::makeGeneratorPainter(r).painter);
        QVERIFY2(inkOf(image) > 20, qPrintable(preset.id));
    }
}

void GeneratorsTest::paintersAreDeterministic()
{
    for (const QString &id : {QStringLiteral("gen.noise"), QStringLiteral("gen.countdown"), QStringLiteral("particles.fireworks"),
                              QStringLiteral("gen.gradient"), QStringLiteral("gen.bars")}) {
        const TimeUs t = secondsToUs(2.35);
        const QImage a = skia::SkiaRuntime::rasterize(*skia::makeGeneratorPainter(requestFor(id, t)).painter);
        // Seek away and back, as a scrub does.
        skia::makeGeneratorPainter(requestFor(id, secondsToUs(0.4)));
        const QImage b = skia::SkiaRuntime::rasterize(*skia::makeGeneratorPainter(requestFor(id, t)).painter);
        QVERIFY2(a == b, qPrintable(id));
    }
    const QImage n0 = skia::SkiaRuntime::rasterize(*skia::makeGeneratorPainter(requestFor(QStringLiteral("gen.noise"), 0)).painter);
    const QImage n1 = skia::SkiaRuntime::rasterize(*skia::makeGeneratorPainter(requestFor(QStringLiteral("gen.noise"), secondsToUs(1))).painter);
    QVERIFY2(n0 != n1, "the noise moves with time");
}

void GeneratorsTest::solidAndTimeDrivenFlags()
{
    skia::GeneratorPaintRequest solid = requestFor(QStringLiteral("gen.solid"), 0);
    solid.source.parameters.insert(QStringLiteral("color"), QStringLiteral("#ff0000ff"));
    solid.resolved = solid.source;
    const skia::GeneratorPainterResult result = skia::makeGeneratorPainter(solid);
    QVERIFY(result.painter->cacheKey() != 0);
    const QImage image = skia::SkiaRuntime::rasterize(*result.painter).convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(image.pixel(10, 10), qRgba(255, 0, 0, 255));
    QVERIFY(!skia::generatorIsTimeDriven(solid));

    skia::GeneratorPaintRequest keyed = solid;
    KeyframeTrack<double> r;
    r.setKeyframe(0, 0.0);
    r.setKeyframe(secondsToUs(1), 1.0);
    keyed.source.paramKeyframes.insert(QStringLiteral("color.r"), r);
    QVERIFY(skia::generatorIsTimeDriven(keyed));
    QCOMPARE(skia::makeGeneratorPainter(keyed).painter->cacheKey(), quint64(0));

    QVERIFY(skia::generatorIsTimeDriven(requestFor(QStringLiteral("gen.noise"), 0)));
    QVERIFY(skia::generatorIsTimeDriven(requestFor(QStringLiteral("particles.snow"), 0)));
    QVERIFY(!skia::generatorIsTimeDriven(requestFor(QStringLiteral("gen.bars"), 0)));
}
#endif

QTEST_GUILESS_MAIN(GeneratorsTest)
#include "tst_generators.moc"
