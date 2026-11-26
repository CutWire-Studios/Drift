#include "core/project/Project.h"
#include "core/project/folder/ProjectFolder.h"
#include "core/project/ProjectDiff.h"
#include "core/project/folder/ProjectData.h"
#include "core/project/folder/ProjectLint.h"
#include "core/project/folder/ProjectSchema.h"
#include "engine/project/ProjectBundle.h"
#include "engine/projdir/ProjectFolderLint.h"
#include "engine/projdir/ProjectLsp.h"
#include "engine/projdir/ProjectScripts.h"
#include "engine/projdir/ProjectTemplate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace drift;
using namespace drift::projdir;

class TestProjectFolder : public QObject
{
    Q_OBJECT

private slots:
    void canonicalJsonIsStable();
    void roundTripsBuiltProject();
    void roundTripsBundleFixture();
    void splitsHeavyClipPayloadsIntoSidecars();
    void relativizesMediaInsideFolder();
    void readsHandEditedFiles();
    void reportsParseErrorsWithPosition();
    void writesOnlyChangedFiles();
    void externalEditWinsAndOursGoesToConflicts();
    void filesNameTheirSchema();
    void schemaDescribesEveryKeyTheWriterEmits();
    void schemasAreDraft2020Documents();
    void lintFindsSemanticProblems();
    void lintIsCleanOnWrittenProject();
    void lspPublishesDiagnosticsAndHovers();
    void generatedByRoundTripsThroughTheProject();
    void scriptsGenerateOwnedItems();
    void scriptsReplaceOnlyTheirOwnOutput();
    void scriptErrorKeepsLastGoodOutput();
    void scriptsAreSandboxedAndReadOnly();
    void timeHelpers();
    void detachMakesItemsPlain();
    void templateRendersOneProjectPerRow();
    void templateParamsAreCheckedAndTyped();
    void templateReadsCsvAndPatterns();

private:
    static Project sample(const QString &root);
};

namespace {

KeyframeTrack<double> ramp(int n)
{
    KeyframeTrack<double> track;
    for (int i = 0; i < n; ++i)
        track.setKeyframe(secondsToUs(i * 0.03), i * 3.25);
    return track;
}

QJsonObject normalized(QJsonObject doc)
{
    // splitProject sorts clips by start; the model writes them in order already.
    return doc;
}

} // namespace

Project TestProjectFolder::sample(const QString &root)
{
    Project project;
    project.setName(QStringLiteral("Sample Project"));
    project.setId(QStringLiteral("11111111-2222-3333-4444-555555555555"));
    project.tracks().clear();

    MediaAsset a;
    a.name = QStringLiteral("Intro Clip");
    a.path = root + QStringLiteral("/media/intro.mp4");
    a.kind = MediaKind::Video;
    a.durationUs = secondsToUs(10);
    const QString idA = project.addAsset(a);
    MediaAsset b;
    b.name = QStringLiteral("Outside");
    b.path = QStringLiteral("/somewhere/else/outside.mp4");
    const QString idB = project.addAsset(b);

    Track video;
    video.id = QStringLiteral("t-video");
    video.name = QStringLiteral("Main Video");
    video.type = TrackType::Video;
    for (int i = 0; i < 3; ++i) {
        Clip clip;
        clip.id = QStringLiteral("clip-%1").arg(i);
        clip.assetId = i == 2 ? idB : idA;
        clip.path = i == 2 ? b.path : a.path;
        clip.timelineStart = secondsToUs(i * 2);
        clip.timelineDuration = secondsToUs(2);
        clip.srcOut = secondsToUs(2);
        clip.opacity = ramp(i == 1 ? 40 : 1);
        if (i == 1) {
            Effect effect;
            effect.catalogId = QStringLiteral("adjust.contrast");
            effect.paramKeyframes.insert(QStringLiteral("contrast"), ramp(60));
            clip.effects.append(effect);
        }
        video.clips.append(clip);
    }
    Track text;
    text.id = QStringLiteral("t-text");
    text.name = QStringLiteral("Titles & more");
    text.type = TrackType::Text;
    project.tracks() = {video, text};

    Clip inner;
    inner.id = QStringLiteral("inner");
    inner.type = ClipType::Shape;
    inner.timelineDuration = secondsToUs(4);
    project.addSequence(TrackList{Track{.type = TrackType::Shape, .clips = {inner}}});
    Bookmark mark;
    mark.timeUs = secondsToUs(3);
    mark.label = QStringLiteral("hit");
    project.bookmarks().append(mark);
    return project;
}

void TestProjectFolder::canonicalJsonIsStable()
{
    const QJsonObject object{{QStringLiteral("b"), 1.5},
                             {QStringLiteral("a"), QJsonArray{1, 2, 3}},
                             {QStringLiteral("id"), QStringLiteral("x\"y")},
                             {QStringLiteral("n"), QJsonObject{{QStringLiteral("k"), true}}},
                             {QStringLiteral("empty"), QJsonObject()}};
    const QByteArray bytes = canonicalJson(object);
    QCOMPARE(bytes,
             QByteArray("{\n  \"id\": \"x\\\"y\",\n  \"a\": [1, 2, 3],\n  \"b\": 1.5,\n  \"empty\": {},\n"
                        "  \"n\": {\n    \"k\": true\n  }\n}\n"));
    QCOMPARE(canonicalJson(QJsonDocument::fromJson(bytes).object()), bytes);
}

void TestProjectFolder::roundTripsBuiltProject()
{
    QTemporaryDir dir;
    const QString root = dir.path();
    const QJsonObject doc = sample(root).toJson(true);
    const FileMap files = splitProject(doc, root);
    QVERIFY(files.contains(QStringLiteral("project.json")));
    QVERIFY(files.contains(QStringLiteral("bin.json")));
    QVERIFY(files.contains(QStringLiteral("markers.json")));
    int trackFiles = 0;
    for (const QString &key : files.keys())
        trackFiles += key.startsWith(QLatin1String("timeline/tracks/"));
    QCOMPARE(trackFiles, 2);
    QVERIFY(files.contains(QStringLiteral("timeline/tracks/01-video-main-video.json")));

    const JoinResult joined = joinProject(files, root);
    QVERIFY(joined.ok);
    QCOMPARE(joined.doc, normalized(doc));
    // And through the loader: the folder is a lossless carrier for the project.
    QString error;
    const Project loaded = Project::fromJson(joined.doc, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(diffProjects(Project::fromJson(doc), loaded).isEmpty());
    QCOMPARE(loaded.toJson(true), Project::fromJson(doc).toJson(true));
    // The bytes are a pure function of the content.
    QCOMPARE(splitProject(joined.doc, root), files);
}

void TestProjectFolder::roundTripsBundleFixture()
{
    QString error;
    const auto info = bundle::readManifest(QStringLiteral(DRIFT_TEST_DATA_DIR "/text-reel.drift"), &error);
    QVERIFY2(info.has_value(), qPrintable(error));
    QJsonObject doc = info->document;
    for (const char *key : {"undoHistory", "playheadUs", "snapEnabled", "rippleEnabled", "allowClipOverlap",
                            "mediaViewMode", "mediaGridMode", "loopWorkArea", "undoHistorySettings"})
        doc.remove(QLatin1String(key));
    const FileMap files = splitProject(doc);
    const JoinResult joined = joinProject(files);
    QVERIFY(joined.ok);
    // Clips are written in start order; the fixture's own order is the baseline.
    QJsonObject expected = doc;
    QJsonArray tracks;
    for (const QJsonValue &t : doc.value(QStringLiteral("tracks")).toArray()) {
        QJsonObject track = t.toObject();
        QList<QJsonObject> clips;
        for (const QJsonValue &c : track.value(QStringLiteral("clips")).toArray())
            clips.append(c.toObject());
        std::stable_sort(clips.begin(), clips.end(), [](const QJsonObject &x, const QJsonObject &y) {
            return x.value(QStringLiteral("timelineStartUs")).toDouble()
                < y.value(QStringLiteral("timelineStartUs")).toDouble();
        });
        QJsonArray out;
        for (const QJsonObject &c : clips)
            out.append(c);
        track.insert(QStringLiteral("clips"), out);
        tracks.append(track);
    }
    expected.insert(QStringLiteral("tracks"), tracks);
    QCOMPARE(joined.doc, expected);
    QVERIFY(!doc.value(QStringLiteral("tracks")).toArray().isEmpty());
}

void TestProjectFolder::splitsHeavyClipPayloadsIntoSidecars()
{
    QTemporaryDir dir;
    const FileMap files = splitProject(sample(dir.path()).toJson(true), dir.path());
    QVERIFY(files.contains(QStringLiteral("sidecars/clip-1/keyframes.json")));
    QVERIFY(files.contains(QStringLiteral("sidecars/clip-1/effects.json")));
    QVERIFY(!files.contains(QStringLiteral("sidecars/clip-0/keyframes.json")));
    const QByteArray track = files.value(QStringLiteral("timeline/tracks/01-video-main-video.json"));
    QVERIFY(track.contains("\"sidecars\""));
    QVERIFY(!track.contains("adjust.contrast"));
}

void TestProjectFolder::relativizesMediaInsideFolder()
{
    QTemporaryDir dir;
    const FileMap files = splitProject(sample(dir.path()).toJson(true), dir.path());
    QString assetFile;
    for (const QString &key : files.keys()) {
        if (key.startsWith(QLatin1String("assets/")) && key.contains(QLatin1String("intro")))
            assetFile = key;
    }
    QVERIFY(!assetFile.isEmpty());
    QVERIFY(files.value(assetFile).contains("\"path\": \"media/intro.mp4\""));
    QVERIFY(files.value(assetFile).contains("\"name\": \"Intro Clip\""));
    for (const QString &key : files.keys()) {
        if (key.startsWith(QLatin1String("assets/")) && key.contains(QLatin1String("outside")))
            QVERIFY(files.value(key).contains("/somewhere/else/outside.mp4"));
    }
}

void TestProjectFolder::readsHandEditedFiles()
{
    QTemporaryDir dir;
    FileMap files = splitProject(sample(dir.path()).toJson(true), dir.path());
    // A hand-added track file, an asset not listed in bin.json and a sidecar edit.
    files.insert(QStringLiteral("timeline/tracks/03-audio-music.json"),
                 QByteArray("{\"id\": \"t-music\", \"type\": \"audio\", \"name\": \"Music\", \"clips\": []}\n"));
    files.insert(QStringLiteral("assets/zz-extra.json"),
                 QByteArray("{\"id\": \"zz\", \"name\": \"Extra\", \"kind\": \"audio\", \"path\": \"a.wav\"}\n"));
    const JoinResult joined = joinProject(files, dir.path());
    QVERIFY(joined.ok);
    const QJsonArray tracks = joined.doc.value(QStringLiteral("tracks")).toArray();
    QCOMPARE(tracks.size(), 3);
    QCOMPARE(tracks.at(2).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("t-music"));
    const QJsonArray assets = joined.doc.value(QStringLiteral("assets")).toArray();
    QCOMPARE(assets.last().toObject().value(QStringLiteral("id")).toString(), QStringLiteral("zz"));
    QCOMPARE(assets.last().toObject().value(QStringLiteral("path")).toString(), dir.path() + QStringLiteral("/a.wav"));
}

void TestProjectFolder::reportsParseErrorsWithPosition()
{
    QTemporaryDir dir;
    FileMap files = splitProject(sample(dir.path()).toJson(true), dir.path());
    files.insert(QStringLiteral("project.json"), QByteArray("{\n  \"fps\": 30,\n  \"width\": }\n"));
    const JoinResult joined = joinProject(files, dir.path());
    QVERIFY(!joined.ok);
    QCOMPARE(joined.problems.size(), 1);
    QCOMPARE(joined.problems.first().file, QStringLiteral("project.json"));
    QCOMPARE(joined.problems.first().line, 3);
    QVERIFY(joined.problems.first().column > 1);
}

void TestProjectFolder::writesOnlyChangedFiles()
{
    QTemporaryDir dir;
    const QString folder = dir.path() + QStringLiteral("/p.driftproj");
    FileMap files = splitProject(sample(dir.path()).toJson(true), folder);
    const WriteReport first = writeProjectFolder(folder, files);
    QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
    QCOMPARE(first.written.size(), files.size());
    QVERIFY(isProjectFolder(folder));
    QCOMPARE(readProjectFolder(folder), files);
    QCOMPARE(writeProjectFolder(folder, files).changed(), QStringList());

    files.remove(QStringLiteral("sidecars/clip-1/effects.json"));
    files.insert(QStringLiteral("bin.json"), QByteArray("{\"assetOrder\": [], \"folders\": []}\n"));
    const WriteReport second = writeProjectFolder(folder, files);
    QCOMPARE(second.changed().size(), 2);
    QVERIFY(!QFileInfo::exists(folder + QStringLiteral("/sidecars/clip-1/effects.json")));
}

void TestProjectFolder::externalEditWinsAndOursGoesToConflicts()
{
    QTemporaryDir dir;
    const QString folder = dir.path() + QStringLiteral("/p.driftproj");
    FileMap files = splitProject(sample(dir.path()).toJson(true), folder);
    writeProjectFolder(folder, files);
    const FileMap known = files;

    // Someone edits markers.json on disk while our in-memory state also changed it.
    QFile external(folder + QStringLiteral("/markers.json"));
    QVERIFY(external.open(QIODevice::WriteOnly));
    external.write("{\"markers\": []}\n");
    external.close();
    files.insert(QStringLiteral("markers.json"), QByteArray("{\"markers\": [{\"label\": \"ours\", \"timeUs\": 1}]}\n"));
    files.insert(QStringLiteral("bin.json"), QByteArray("{\"assetOrder\": [], \"folders\": []}\n"));

    const QString conflicts = folder + QStringLiteral("/.drift/conflicts/t1");
    const WriteReport report = writeProjectFolder(folder, files, &known, conflicts);
    QCOMPARE(report.conflicts, QStringList{QStringLiteral("markers.json")});
    QCOMPARE(report.written, QStringList{QStringLiteral("bin.json")});
    QFile onDisk(folder + QStringLiteral("/markers.json"));
    QVERIFY(onDisk.open(QIODevice::ReadOnly));
    QCOMPARE(onDisk.readAll(), QByteArray("{\"markers\": []}\n"));
    QVERIFY(QFileInfo::exists(conflicts + QStringLiteral("/markers.json")));
}

void TestProjectFolder::filesNameTheirSchema()
{
    QTemporaryDir dir;
    const FileMap files = splitProject(sample(dir.path()).toJson(true), dir.path());
    QVERIFY(files.value(QStringLiteral("project.json")).contains("\"$schema\": \"./.drift/schema/project.schema.json\"")
            || files.value(QStringLiteral("project.json")).contains("\"$schema\": \".drift/schema/project.schema.json\""));
    QVERIFY(files.value(QStringLiteral("timeline/tracks/01-video-main-video.json"))
                .contains("\"$schema\": \"../../.drift/schema/track.schema.json\""));
    QVERIFY(files.value(QStringLiteral("sidecars/clip-1/effects.json"))
                .contains("\"$schema\": \"../../.drift/schema/sidecar.effects.schema.json\""));
    // And reading them back drops the key again.
    QVERIFY(!joinProject(files, dir.path()).doc.contains(QStringLiteral("$schema")));
}

void TestProjectFolder::schemaDescribesEveryKeyTheWriterEmits()
{
    QTemporaryDir dir;
    Project project = sample(dir.path());
    // Optional keys only some clips write.
    Clip &clip = project.tracks()[0].clips[0];
    clip.type = ClipType::Text;
    clip.textContent = QStringLiteral("hi");
    clip.layer3d = true;
    clip.enabled = false;
    clip.positionZ = ramp(2);
    clip.label = QStringLiteral("L");
    clip.sourceFrame = QRectF(0.1, 0.1, 0.5, 0.5);
    project.setTeleprompterScript(QStringLiteral("x"));
    project.setWorkAreaInUs(0);
    project.setWorkAreaOutUs(1000000);
    const FileMap files = splitProject(project.toJson(true), dir.path());
    QStringList missing;
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString kindName = kindForPath(it.key());
        QVERIFY2(!kindName.isEmpty(), qPrintable(it.key()));
        const KindDesc *kind = kindDescription(kindName);
        QVERIFY2(kind, qPrintable(kindName));
        QJsonValue value;
        Problem problem;
        QVERIFY(parseJsonFile(it.key(), it.value(), &value, &problem));
        const QJsonObject object = value.toObject();
        for (const QString &key : object.keys()) {
            if (kind->closed && !kind->field(key) && key != QLatin1String("$schema"))
                missing.append(it.key() + QLatin1Char(':') + key);
        }
        if (kindName == QLatin1String("track")) {
            const KindDesc *clipKind = kindDescription(QStringLiteral("clip"));
            for (const QJsonValue &c : object.value(QStringLiteral("clips")).toArray()) {
                for (const QString &key : c.toObject().keys()) {
                    if (!clipKind->field(key))
                        missing.append(it.key() + QStringLiteral(":clip.") + key);
                }
            }
        }
    }
    QVERIFY2(missing.isEmpty(), qPrintable(missing.join(QLatin1Char(' '))));
}

void TestProjectFolder::schemasAreDraft2020Documents()
{
    const FileMap schemas = schemaFiles();
    QVERIFY(schemas.contains(QStringLiteral(".drift/schema/track.schema.json")));
    for (auto it = schemas.constBegin(); it != schemas.constEnd(); ++it) {
        QJsonValue value;
        Problem problem;
        QVERIFY2(parseJsonFile(it.key(), it.value(), &value, &problem), qPrintable(it.key()));
        QCOMPARE(value.toObject().value(QStringLiteral("$schema")).toString(),
                 QStringLiteral("https://json-schema.org/draft/2020-12/schema"));
    }
    const QJsonObject track = schemaFor(QStringLiteral("track"));
    const QJsonObject clip = track.value(QStringLiteral("$defs")).toObject().value(QStringLiteral("clip")).toObject();
    const QJsonArray types = clip.value(QStringLiteral("properties")).toObject()
                                 .value(QStringLiteral("type")).toObject().value(QStringLiteral("enum")).toArray();
    QVERIFY(types.contains(QStringLiteral("text")));
    QVERIFY(types.contains(QStringLiteral("generator")));
}

void TestProjectFolder::lintIsCleanOnWrittenProject()
{
    QTemporaryDir dir;
    LintContext context;
    context.checkMedia = false;
    context.effectParams = [](const QString &id) -> std::optional<QStringList> {
        if (id == QLatin1String("adjust.contrast"))
            return QStringList{QStringLiteral("contrast")};
        return std::nullopt;
    };
    const FileMap files = splitProject(sample(dir.path()).toJson(true), dir.path());
    const QList<Problem> problems = lintProject(files, dir.path(), context);
    for (const Problem &p : problems)
        QVERIFY2(p.severity == QLatin1String("info"), qPrintable(formatProblems({p})));
}

void TestProjectFolder::lintFindsSemanticProblems()
{
    QTemporaryDir dir;
    LintContext context;
    context.checkMedia = false;
    context.effectParams = [](const QString &id) -> std::optional<QStringList> {
        if (id == QLatin1String("adjust.contrast"))
            return QStringList{QStringLiteral("contrast")};
        return std::nullopt;
    };
    FileMap files = splitProject(sample(dir.path()).toJson(true), dir.path());
    files.insert(QStringLiteral("timeline/tracks/03-video-bad.json"), QByteArray(R"({
  "id": "t-bad",
  "type": "video",
  "clips": [
    {"id": "a", "type": "video", "assetId": "nope", "timelineStartUs": 0, "timelineDurationUs": 2000000,
     "opacity": {"keyframes": [{"timeUs": 9000000, "value": 1}]},
     "effects": [{"catalogId": "made.up", "parameters": {}}, {"catalogId": "adjust.contrast", "parameters": {"bogus": 1}}]},
    {"id": "b", "type": "video", "timelineStartUs": 1000000, "timelineDurationUs": 2000000, "durationn": 1},
    {"id": "clip-0", "type": "video", "timelineStartUs": 5000000, "timelineDurationUs": 1000000}
  ]
}
)"));
    files.insert(QStringLiteral("sidecars/ghost/effects.json"), QByteArray("{\"effects\": []}\n"));
    const QList<Problem> problems = lintProject(files, dir.path(), context);
    const QString text = formatProblems(problems);
    const auto has = [&](const QString &needle) { return text.contains(needle); };
    QVERIFY2(has(QStringLiteral("unknown asset \"nope\"")), qPrintable(text));
    QVERIFY2(has(QStringLiteral("overlaps clip \"a\"")), qPrintable(text));
    QVERIFY2(has(QStringLiteral("outside the clip")), qPrintable(text));
    QVERIFY2(has(QStringLiteral("unknown effect \"made.up\"")), qPrintable(text));
    QVERIFY2(has(QStringLiteral("no parameter \"bogus\"")), qPrintable(text));
    QVERIFY2(has(QStringLiteral("unknown key \"durationn\"")), qPrintable(text));
    QVERIFY2(has(QStringLiteral("Duplicate clip id \"clip-0\"")), qPrintable(text));
    QVERIFY2(has(QStringLiteral("sidecars/ghost/effects.json:1:1: warning: Sidecar belongs to no clip")), qPrintable(text));
    // Positions point at the line holding the problem.
    for (const Problem &p : problems) {
        if (p.message.contains(QLatin1String("unknown asset"))) {
            QCOMPARE(p.file, QStringLiteral("timeline/tracks/03-video-bad.json"));
            QCOMPARE(p.line, 5);
        }
    }
}

void TestProjectFolder::lspPublishesDiagnosticsAndHovers()
{
    QTemporaryDir dir;
    const FileMap files = splitProject(sample(dir.path()).toJson(true), dir.path());
    QString error;
    const WriteReport report = writeProjectFolder(dir.path(), files);
    QVERIFY(report.error.isEmpty());

    LintContext context;
    context.checkMedia = false;
    ProjectLsp lsp(context);
    const QString rootUri = QUrl::fromLocalFile(dir.path()).toString();
    const auto init = lsp.handle({{QStringLiteral("id"), 1}, {QStringLiteral("method"), QStringLiteral("initialize")},
                                  {QStringLiteral("params"), QJsonObject{{QStringLiteral("rootUri"), rootUri}}}});
    QCOMPARE(init.size(), 1);
    QVERIFY(init.first().value(QStringLiteral("result")).toObject().value(QStringLiteral("capabilities")).toObject()
                .value(QStringLiteral("hoverProvider")).toBool());

    const QString uri = QUrl::fromLocalFile(dir.path() + QStringLiteral("/project.json")).toString();
    const QByteArray bad = "{\n  \"fps\": \"thirty\",\n  \"version\": 34\n}\n";
    const auto out = lsp.handle({{QStringLiteral("method"), QStringLiteral("textDocument/didOpen")},
                                 {QStringLiteral("params"),
                                  QJsonObject{{QStringLiteral("textDocument"),
                                               QJsonObject{{QStringLiteral("uri"), uri},
                                                           {QStringLiteral("text"), QString::fromUtf8(bad)}}}}}});
    bool found = false;
    for (const QJsonObject &message : out) {
        const QJsonObject params = message.value(QStringLiteral("params")).toObject();
        if (params.value(QStringLiteral("uri")).toString() != uri)
            continue;
        const QJsonArray diagnostics = params.value(QStringLiteral("diagnostics")).toArray();
        QVERIFY(!diagnostics.isEmpty());
        const QJsonObject first = diagnostics.first().toObject();
        QCOMPARE(first.value(QStringLiteral("range")).toObject().value(QStringLiteral("start")).toObject()
                     .value(QStringLiteral("line")).toInt(), 1);
        QVERIFY(first.value(QStringLiteral("message")).toString().contains(QLatin1String("fps")));
        found = true;
    }
    QVERIFY(found);

    const auto hover = lsp.handle({{QStringLiteral("id"), 2}, {QStringLiteral("method"), QStringLiteral("textDocument/hover")},
                                   {QStringLiteral("params"),
                                    QJsonObject{{QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), uri}}},
                                                {QStringLiteral("position"), QJsonObject{{QStringLiteral("line"), 1}, {QStringLiteral("character"), 5}}}}}});
    QCOMPARE(hover.size(), 1);
    QVERIFY(hover.first().value(QStringLiteral("result")).toObject().value(QStringLiteral("contents")).toObject()
                .value(QStringLiteral("value")).toString().contains(QLatin1String("Frames per second")));

    ProjectLsp::KeyAt at;
    const QByteArray trackText = "{\n  \"clips\": [\n    {\n      \"timelineStartUs\": 0\n    }\n  ]\n}\n";
    QVERIFY(ProjectLsp::keyAt(trackText, 3, 10, &at));
    QCOMPARE(at.key, QStringLiteral("timelineStartUs"));
    QVERIFY(ProjectLsp::hoverText(QStringLiteral("timeline/tracks/01-video-a.json"), at).contains(QLatin1String("microseconds")));
}

namespace {

QJsonObject baseDoc()
{
    Project project;
    project.setName(QStringLiteral("Base"));
    project.tracks().clear();
    Track video;
    video.id = QStringLiteral("t-main");
    video.type = TrackType::Video;
    Clip mine;
    mine.id = QStringLiteral("mine");
    mine.timelineDuration = secondsToUs(2);
    video.clips.append(mine);
    project.tracks().append(video);
    return project.toJson(true);
}

QJsonObject clipById(const QJsonObject &doc, const QString &id, QString *trackId = nullptr)
{
    for (const QJsonValue &t : doc.value(QStringLiteral("tracks")).toArray()) {
        for (const QJsonValue &c : t.toObject().value(QStringLiteral("clips")).toArray()) {
            if (c.toObject().value(QStringLiteral("id")).toString() == id) {
                if (trackId)
                    *trackId = t.toObject().value(QStringLiteral("id")).toString();
                return c.toObject();
            }
        }
    }
    return {};
}

int clipCount(const QJsonObject &doc)
{
    int n = 0;
    for (const QJsonValue &t : doc.value(QStringLiteral("tracks")).toArray())
        n += int(t.toObject().value(QStringLiteral("clips")).toArray().size());
    return n;
}

ScriptInput titlesInput()
{
    ScriptInput input;
    input.scripts.insert(QStringLiteral("scripts/titles.lua"), QByteArray(R"(
local rows = data "titles.csv"
for i, row in ipairs(rows) do
  title { id = "title-" .. i, text = row.text, start = sec((i - 1) * 2), duration = sec(1.5) }
end
local t = track { id = "t-main" }
t:clip { id = "gen-on-main", start = tc "00:00:10:15", duration = frames(30) }
asset { id = "gen-asset", name = "Gen", kind = "image", path = "media/x.png" }
)"));
    input.data.insert(QStringLiteral("data/titles.csv"), QByteArray("text,n\nHello,1\n\"Wor,ld\",2\nLast,3\n"));
    input.rootDir = QStringLiteral("/proj");
    return input;
}

} // namespace

void TestProjectFolder::generatedByRoundTripsThroughTheProject()
{
    Project project;
    project.tracks().clear();
    Track track;
    track.id = QStringLiteral("t");
    track.generatedBy = QStringLiteral("scripts/a.lua");
    Clip clip;
    clip.id = QStringLiteral("c");
    clip.generatedBy = QStringLiteral("scripts/a.lua");
    track.clips.append(clip);
    project.tracks().append(track);
    MediaAsset asset;
    asset.generatedBy = QStringLiteral("scripts/a.lua");
    project.addAsset(asset);
    QString error;
    const Project loaded = Project::fromJson(project.toJson(true), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(loaded.tracks().first().generatedBy, QStringLiteral("scripts/a.lua"));
    QCOMPARE(loaded.tracks().first().clips.first().generatedBy, QStringLiteral("scripts/a.lua"));
    QCOMPARE(loaded.assets().begin()->generatedBy, QStringLiteral("scripts/a.lua"));
    QCOMPARE(project.toJson().value(QStringLiteral("version")).toInt(), 35);
    // And through the folder files.
    const FileMap files = splitProject(project.toJson(true));
    QCOMPARE(joinProject(files).doc, project.toJson(true));
}

void TestProjectFolder::scriptsGenerateOwnedItems()
{
    const ScriptOutcome first = runScripts(baseDoc(), titlesInput());
    QVERIFY2(first.problems.isEmpty(), qPrintable(formatProblems(first.problems)));
    QCOMPARE(clipCount(first.doc), 5);
    QString trackId;
    const QJsonObject second = clipById(first.doc, QStringLiteral("title-2"), &trackId);
    QCOMPARE(second.value(QStringLiteral("textContent")).toString(), QStringLiteral("Wor,ld"));
    QCOMPARE(second.value(QStringLiteral("timelineStartUs")).toDouble(), 2000000.0);
    QCOMPARE(second.value(QStringLiteral("generatedBy")).toString(), QStringLiteral("scripts/titles.lua"));
    QCOMPARE(second.value(QStringLiteral("type")).toString(), QStringLiteral("text"));
    QCOMPARE(trackId, QStringLiteral("titles-titles"));

    // A clip added to a track the user owns is tagged, the track is not.
    QString mainId;
    const QJsonObject onMain = clipById(first.doc, QStringLiteral("gen-on-main"), &mainId);
    QCOMPARE(mainId, QStringLiteral("t-main"));
    QCOMPARE(onMain.value(QStringLiteral("generatedBy")).toString(), QStringLiteral("scripts/titles.lua"));
    QCOMPARE(onMain.value(QStringLiteral("timelineStartUs")).toDouble(), 10500000.0);
    QCOMPARE(onMain.value(QStringLiteral("timelineDurationUs")).toDouble(), 1000000.0);
    QVERIFY(clipById(first.doc, QStringLiteral("mine")).value(QStringLiteral("generatedBy")).toString().isEmpty());
    for (const QJsonValue &t : first.doc.value(QStringLiteral("tracks")).toArray()) {
        if (t.toObject().value(QStringLiteral("id")).toString() == QLatin1String("t-main"))
            QVERIFY(!t.toObject().contains(QStringLiteral("generatedBy")));
    }
    QJsonObject asset;
    for (const QJsonValue &a : first.doc.value(QStringLiteral("assets")).toArray())
        if (a.toObject().value(QStringLiteral("id")).toString() == QLatin1String("gen-asset"))
            asset = a.toObject();
    QCOMPARE(asset.value(QStringLiteral("path")).toString(), QStringLiteral("/proj/media/x.png"));

    // The result is a valid project, and running again changes nothing (deterministic).
    QString error;
    Project::fromJson(first.doc, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(runScripts(first.doc, titlesInput()).doc, first.doc);
}

void TestProjectFolder::scriptsReplaceOnlyTheirOwnOutput()
{
    ScriptInput input = titlesInput();
    const QJsonObject once = runScripts(baseDoc(), input).doc;
    input.data.insert(QStringLiteral("data/titles.csv"), QByteArray("text\nOnly\n"));
    const ScriptOutcome again = runScripts(once, input);
    QVERIFY(again.problems.isEmpty());
    QCOMPARE(clipCount(again.doc), 3); // mine, title-1, gen-on-main
    QVERIFY(clipById(again.doc, QStringLiteral("title-2")).isEmpty());
    QCOMPARE(clipById(again.doc, QStringLiteral("title-1")).value(QStringLiteral("textContent")).toString(),
             QStringLiteral("Only"));
    QVERIFY(!clipById(again.doc, QStringLiteral("mine")).isEmpty());
}

void TestProjectFolder::scriptErrorKeepsLastGoodOutput()
{
    ScriptInput input = titlesInput();
    const QJsonObject good = runScripts(baseDoc(), input).doc;
    input.scripts.insert(QStringLiteral("scripts/titles.lua"), QByteArray("local x = 1\nerror('boom')\n"));
    const ScriptOutcome failed = runScripts(good, input);
    QCOMPARE(failed.problems.size(), 1);
    QCOMPARE(failed.problems.first().file, QStringLiteral("scripts/titles.lua"));
    QCOMPARE(failed.problems.first().line, 2);
    QVERIFY(failed.problems.first().message.contains(QLatin1String("boom")));
    QCOMPARE(failed.doc, good);
}

void TestProjectFolder::scriptsAreSandboxedAndReadOnly()
{
    ScriptInput input;
    const auto run = [&](const char *source) {
        input.scripts.clear();
        input.scripts.insert(QStringLiteral("scripts/x.lua"), QByteArray(source));
        return runScripts(baseDoc(), input);
    };
    QVERIFY(run("io.open('/etc/passwd')").problems.size() == 1);
    QVERIFY(run("os.execute('true')").problems.size() == 1);
    QVERIFY(run("project.fps = 1").problems.first().message.contains(QLatin1String("read-only")));
    QVERIFY(run("data 'missing.csv'").problems.first().message.contains(QLatin1String("no such data file")));
    QVERIFY(run("while true do end").problems.first().message.contains(QLatin1String("time limit")));
    const ScriptOutcome clash = run("track{id='t-main'}:clip{id='mine'}");
    QVERIFY(clash.problems.first().message.contains(QLatin1String("already used")));
    QCOMPARE(run("local n = 0 for _, t in ipairs(project.tracks) do n = n + #t.clips end "
                 "track{id='t-main'}:clip{id='count-' .. n}").doc.value(QStringLiteral("tracks")).toArray().size(), 1);
}

void TestProjectFolder::timeHelpers()
{
    ScriptInput input;
    input.scripts.insert(QStringLiteral("scripts/t.lua"), QByteArray(R"(
local t = track { id = "tt" }
t:clip { id = "a", start = tc "00:00:01:12", duration = beats(120, 2) }
t:clip { id = "b", start = tc "1:00", duration = sec(0.5) }
t:clip { id = "c", start = frames(3), duration = 1 }
)"));
    const QJsonObject doc = runScripts(baseDoc(), input).doc;
    QCOMPARE(clipById(doc, QStringLiteral("a")).value(QStringLiteral("timelineStartUs")).toDouble(), 1400000.0); // 30 fps
    QCOMPARE(clipById(doc, QStringLiteral("a")).value(QStringLiteral("timelineDurationUs")).toDouble(), 1000000.0);
    QCOMPARE(clipById(doc, QStringLiteral("b")).value(QStringLiteral("timelineStartUs")).toDouble(), 1000000.0);
    QCOMPARE(clipById(doc, QStringLiteral("c")).value(QStringLiteral("timelineStartUs")).toDouble(), 100000.0);
}

void TestProjectFolder::detachMakesItemsPlain()
{
    const QJsonObject generated = runScripts(baseDoc(), titlesInput()).doc;
    const QJsonObject one = detachGenerated(generated, QStringLiteral("scripts/titles.lua"));
    QVERIFY(clipById(one, QStringLiteral("title-1")).value(QStringLiteral("generatedBy")).toString().isEmpty());
    for (const QJsonValue &t : one.value(QStringLiteral("tracks")).toArray())
        QVERIFY(!t.toObject().contains(QStringLiteral("generatedBy")));
    // A detached clip is plain data: when its script runs again the id clash is reported and the document is kept.
    const ScriptOutcome again = runScripts(one, titlesInput());
    QVERIFY(!again.problems.isEmpty());
    QVERIFY(again.problems.first().message.contains(QLatin1String("already used")));
    QCOMPARE(clipCount(again.doc), clipCount(one));
}

namespace {

FolderContents sampleTemplate(const QString &root)
{
    FolderContents folder;
    folder.rootDir = root;
    Project project;
    project.setName(QStringLiteral("Template"));
    project.tracks().clear();
    folder.project = splitProject(project.toJson(true), root);
    folder.scripts.insert(QStringLiteral("scripts/main.lua"), QByteArray(R"(
title { id = "headline", text = param "headline", duration = sec(param "seconds") }
local c = track { id = "t-bg", type = "shape" }:clip { id = "bg", type = "shape", name = param "accent" .. "/" .. param "style" }
asset { id = "logo", kind = "image", path = param "logo" }
)"));
    folder.templateJson = QByteArray(R"({
  "params": [
    {"name": "headline", "type": "text", "default": "Hello"},
    {"name": "seconds", "type": "number", "default": 2},
    {"name": "accent", "type": "color", "default": "#FF3366"},
    {"name": "style", "type": "choice", "options": ["bold", "light"], "default": "bold"},
    {"name": "logo", "type": "media", "default": "media/logo.png"}
  ],
  "data": "data/rows.csv",
  "output": "{row}-{headline}.mp4"
})");
    folder.data.insert(QStringLiteral("data/rows.csv"),
                       QByteArray("headline,seconds,style\nFirst,3,light\nSecond: a/b,4,bold\nThird,x,bold\n"));
    return folder;
}

} // namespace

void TestProjectFolder::templateRendersOneProjectPerRow()
{
    const FolderContents folder = sampleTemplate(QStringLiteral("/t"));
    TemplateSpec spec;
    QList<Problem> problems;
    QVERIFY(readTemplate(folder.templateJson, &spec, &problems));
    QCOMPARE(spec.params.size(), 5);
    const QJsonArray rows = templateRows(folder, spec);
    QCOMPARE(rows.size(), 3);

    const Expansion first = expandTemplate(folder, spec, rows.at(0).toObject());
    QVERIFY2(first.ok, qPrintable(formatProblems(first.problems)));
    const QJsonObject headline = clipById(first.doc, QStringLiteral("headline"));
    QCOMPARE(headline.value(QStringLiteral("textContent")).toString(), QStringLiteral("First"));
    QCOMPARE(headline.value(QStringLiteral("timelineDurationUs")).toDouble(), 3000000.0);
    QCOMPARE(clipById(first.doc, QStringLiteral("bg")).value(QStringLiteral("name")).toString(),
             QStringLiteral("#ff3366/light")); // the colour is normalised, the row's style won
    QString error;
    Project::fromJson(first.doc, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));

    const Expansion second = expandTemplate(folder, spec, rows.at(1).toObject());
    QCOMPARE(clipById(second.doc, QStringLiteral("headline")).value(QStringLiteral("textContent")).toString(),
             QStringLiteral("Second: a/b"));
    QCOMPARE(expandOutputPattern(spec.outputPattern, 2, second.params), QStringLiteral("2-Second_ a_b.mp4"));
    // Same inputs, same project.
    QCOMPARE(expandTemplate(folder, spec, rows.at(0).toObject()).doc, first.doc);
    // The media param became an absolute path.
    QCOMPARE(first.params.value(QStringLiteral("logo")).toString(), QStringLiteral("/t/media/logo.png"));
}

void TestProjectFolder::templateParamsAreCheckedAndTyped()
{
    const FolderContents folder = sampleTemplate(QStringLiteral("/t"));
    TemplateSpec spec;
    QList<Problem> problems;
    QVERIFY(readTemplate(folder.templateJson, &spec, &problems));
    const QJsonArray rows = templateRows(folder, spec);
    const Expansion bad = expandTemplate(folder, spec, rows.at(2).toObject()); // seconds = "x"
    QVERIFY(!bad.ok);
    QVERIFY(formatProblems(bad.problems).contains(QLatin1String("should be a number")));
    const Expansion choice = expandTemplate(folder, spec, {}, {{QStringLiteral("style"), QStringLiteral("neon")}});
    QVERIFY(!choice.ok);
    QVERIFY(formatProblems(choice.problems).contains(QLatin1String("choose one of bold, light")));
    // Overrides beat the row.
    const Expansion over = expandTemplate(folder, spec, rows.at(0).toObject(), {{QStringLiteral("headline"), QStringLiteral("Mine")}});
    QCOMPARE(over.params.value(QStringLiteral("headline")).toString(), QStringLiteral("Mine"));

    TemplateSpec broken;
    QList<Problem> brokenProblems;
    QVERIFY(!readTemplate(QByteArray("{\"params\": [{\"name\": \"a\", \"type\": \"vector\"}, {\"name\": \"c\", \"type\": \"choice\"}]}"),
                          &broken, &brokenProblems));
    QCOMPARE(brokenProblems.size(), 2);
}

void TestProjectFolder::templateReadsCsvAndPatterns()
{
    QString error;
    const QJsonArray rows = parseDataRows(QStringLiteral("x.csv"),
                                          QByteArray("a,b\r\n1,\"x \"\"q\"\"\"\r\n007,\r\n"), &error);
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).toObject().value(QStringLiteral("a")).toDouble(), 1.0);
    QCOMPARE(rows.at(0).toObject().value(QStringLiteral("b")).toString(), QStringLiteral("x \"q\""));
    QCOMPARE(rows.at(1).toObject().value(QStringLiteral("a")).toString(), QStringLiteral("007"));
    QCOMPARE(parseDataRows(QStringLiteral("x.json"), QByteArray("[{\"a\": 1}]")).size(), 1);
    parseDataRows(QStringLiteral("x.json"), QByteArray("{"), &error);
    QVERIFY(!error.isEmpty());
    QCOMPARE(expandOutputPattern(QStringLiteral("/o/{row}/{n}.mp4"), 7, {{QStringLiteral("n"), 4.0}}),
             QStringLiteral("/o/7/4.mp4"));
}

QTEST_APPLESS_MAIN(TestProjectFolder)
#include "tst_projectfolder.moc"
