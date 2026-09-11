#include <QtTest>

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include "mcp/McpCatalog.h"
#include "mcp/McpDispatcher.h"
#include "mcp/McpJson.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpSession.h"
#include "mcp/McpStdio.h"
#include "engine/ObjectDetector.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"
#include "models/MarketClient.h"

#include <cmath>
#include <cstdio>

class McpTest : public QObject
{
    Q_OBJECT

private slots:
    void catalogListsToolboxes();
    void sceneToolboxExposesSchemas();
    void sceneOpsRequireAnalysisFirst();
    void sceneWorkflowEndToEnd();
    void aiCapabilitiesReportsMissingModels();
    void catalogOpsIncludeWhen();
    void toolboxDescriptionsIncludeWhen();
    void segmentationOutputDefaultsToAdjustment();
    void toolboxAnnotationsPresent();
    void toolboxUnknownIsError();
    void toolboxReturnsSchemas();
    void protocolInitializeAndToolsList();
    void protocolUnknownOp();
    void protocolNotificationHasNoReply();
    void sessionFileRoundTrip();
    void sessionFileMissing();
    void stdioFramingIsNewlineDelimited();
    void stdioWriteEscapesEmbeddedNewlines();
    void stdioReadsNewlineDelimited();
    void stdioReadsCrlfBomAndBlankLines();
    void stdioReadsLegacyContentLength();
    void stdioReadsLargeMessage();
    void stdioReadsEofAndTrailingMessage();
    void serverRequiresBearerToken();
    void serverInitializeWithToken();
    void serverNotificationReturns202();
    void applyUnknownOp();
    void catalogDispatcherParity();
    void textResultRoundsNumbers();
    void validateRejectsWrongType();
    void validateEnumAndRange();
    void unknownOpSuggests();
    void listEffectsCompactAndById();
    void listEmojiHasIds();
    void listHistoryLimitAndShort();
    void applyFailureNotDuplicated();
    void searchFindsOps();
    void captureBeyondEndFlags();
    void applyBatchStopsAndUndoRevertsPrefix();
    void inspectIsCompact();
    void inspectDetailRowIsCompact();
    void inspectClipAndTrackFilters();
    void inspectJobsCollapsed();
    void inspectIncludesProjectFields();
    void placeHonorsOverlapToggle();
    void workAreaRoundTrip();
    void exportOptionsAndSettings();
    void exportVideoRequiresPath();
    void projectSetupRoundTrip();
    void captureDoesNotInsertClip();
    void framesUniformReturnsN();
    void framesChangesDedupesFourShotClip();
    void framesAtRendersExactTimes();
    void framesClipModeReportsSourceAndTimeline();
    void framesReturnPathWritesJpeg();
    void activityPeaksLandOnCuts();
    void activityClipModeSkipsAudioWhenAsked();
    void waveformImageReturnsPngAndSummary();
    void applyRejectsWaveformImage();
    void toolboxEndpointAllowsFramesAndActivity();
    void listScenesExposesThumb();
    void marketOpsGateOnConsent();
    void marketSearchAndDownloadImportsAsset();
    void sceneOpsAcceptClipRef();
    void addEffectReportsHost();
    void framesFlagsBeyondEnd();
    void inspectRevisionUnchanged();
    void listEffectsIncludesParams();
    void audioToolboxReturnsSchemas();
    void waveformReturnsPeaksOnFirstCall();
    void waveformReportsSilenceAsZero();
    void detectBeatsRejectsShortRange();
    void detectBeatsFindsClickTempoAndPublishes();
    void splitOnBeatsCutsAndUndoesAsOneStep();
    void snapClipsToBeatsRespectsMaxDistance();
    void setVolumeRoundTrips();
    void audioReadOpsAreNotUndoable();
    void armedBeatGridMakesMoveClipSnap();
    void undoExemptOpsMatchCatalogLimitations();
    void selectionBasedOpsExcludePlayheadOps();
    void inspectReportsSelectionAndUndo();
    void listHistoryLabelsApplyBatch();
    void undoToRestoresSnapshot();
    void setRippleIsNotUndoable();
    void closeGapClosesOneHole();
    void saveProjectWithoutPathUsesCurrent();
    void detectSilenceFindsInjectedGap();
    void setEffectStringParamSetsFileParam();
    void addShapeReturnsMintedId();
    void motionToolboxIsCatalogued();
    void addLottieReportsDocument();
    void setLottieSlotValidatesTypes();
    void inspectLottieAddsNothing();
    void addSvgShowsInCapture();
    void lottieBatchUndoesAsOneStep();
    void setTextStyleAcceptsAnimation();
    void textKeyframesThroughSetKeyframe();
    void historyEntriesHaveHashes();
    void undoToByHash();
    void snapshotFileHashMatchesHistory();
    void linearHistoryDropsRedo();
};

static QJsonObject rpc(const QString &method, const QJsonObject &params = {}, int id = 1)
{
    QJsonObject o{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), id},
        {QStringLiteral("method"), method},
    };
    if (!params.isEmpty())
        o.insert(QStringLiteral("params"), params);
    return o;
}

static QByteArray httpPost(quint16 port, const QByteArray &auth, const QByteArray &body,
                           int *statusOut = nullptr)
{
    QTcpSocket socket;
    QEventLoop loop;
    QObject::connect(&socket, &QTcpSocket::connected, &loop, &QEventLoop::quit);
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    socket.connectToHost(QStringLiteral("127.0.0.1"), port);
    if (socket.state() != QAbstractSocket::ConnectedState)
        loop.exec();
    if (socket.state() != QAbstractSocket::ConnectedState)
        return {};

    QByteArray req = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n";
    if (!auth.isEmpty()) {
        req += "Authorization: ";
        req += auth;
        req += "\r\n";
    }
    req += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
    req += body;
    socket.write(req);

    QByteArray response;
    QObject::connect(&socket, &QTcpSocket::readyRead, &loop, [&]() {
        response += socket.readAll();
        if (response.contains("\r\n\r\n")) {
            const int sep = response.indexOf("\r\n\r\n");
            const QByteArray header = response.left(sep);
            const int length = [&] {
                const QByteArray lower = header.toLower();
                const int at = lower.indexOf("content-length:");
                if (at < 0)
                    return 0;
                return header.mid(at + 15).trimmed().toInt();
            }();
            if (response.size() >= sep + 4 + length)
                loop.quit();
        }
    });
    QObject::connect(&socket, &QTcpSocket::disconnected, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    response += socket.readAll();

    const int sep = response.indexOf("\r\n\r\n");
    if (sep < 0)
        return {};
    if (statusOut) {
        const QByteArray line = response.left(response.indexOf('\n'));
        const auto parts = line.split(' ');
        *statusOut = parts.size() >= 2 ? parts.at(1).toInt() : 0;
    }
    return response.mid(sep + 4);
}

void McpTest::catalogListsToolboxes()
{
    const QJsonObject cat = drift::mcp::catalogPayload();
    QVERIFY(cat.value(QStringLiteral("ok")).toBool());
    const QJsonArray boxes = cat.value(QStringLiteral("toolboxes")).toArray();
    QCOMPARE(boxes.size(), 19);
    QStringList names;
    for (const QJsonValue &v : boxes)
        names.append(v.toObject().value(QStringLiteral("name")).toString());
    QVERIFY(names.contains(QStringLiteral("media")));
    QVERIFY(names.contains(QStringLiteral("timeline")));
    QVERIFY(names.contains(QStringLiteral("canvas")));
    QVERIFY(names.contains(QStringLiteral("project")));
    QVERIFY(names.contains(QStringLiteral("audio")));
    QVERIFY(names.contains(QStringLiteral("scene")));
    QVERIFY(names.contains(QStringLiteral("multicam")));
}

void McpTest::catalogOpsIncludeWhen()
{
    const QJsonObject compact = drift::mcp::catalogPayload();
    QVERIFY(!compact.contains(QStringLiteral("endpoints")));
    QVERIFY(!compact.contains(QStringLiteral("guide")));
    QVERIFY(!compact.contains(QStringLiteral("hint")));
    QVERIFY(QJsonDocument(compact).toJson(QJsonDocument::Compact).size() < 17000);
    QVERIFY(QJsonDocument(drift::mcp::catalogPayload({{QStringLiteral("brief"), true}}))
                .toJson(QJsonDocument::Compact).size() < 8000);

    const QJsonObject cat = drift::mcp::catalogPayload(
        {{QStringLiteral("guide"), true}, {QStringLiteral("endpoints"), true}});
    const QJsonArray boxes = cat.value(QStringLiteral("toolboxes")).toArray();
    QVERIFY(cat.contains(QStringLiteral("endpoints")));
    QVERIFY(cat.contains(QStringLiteral("guide")));
    const QString guide = cat.value(QStringLiteral("guide")).toString();
    QVERIFY(guide.contains(QStringLiteral("YOUR own tools")));
    QString limitations;
    for (const QJsonValue &v : cat.value(QStringLiteral("limitations")).toArray())
        limitations += v.toString();
    QVERIFY(limitations.contains(QStringLiteral("filesystem tools")));
    for (const QJsonValue &v : boxes) {
        const QJsonArray ops = v.toObject().value(QStringLiteral("ops")).toArray();
        QVERIFY(!ops.isEmpty());
        for (const QJsonValue &op : ops) {
            const QString line = op.toString();
            const int sep = line.indexOf(QStringLiteral(" — "));
            QVERIFY2(sep > 0, qPrintable(line));
            const QString when = line.mid(sep + 3);
            QVERIFY2(when.split(QLatin1Char(' ')).size() >= 3, qPrintable(line));
            QVERIFY2(when.size() <= 72, qPrintable(line));
        }
    }
}

// The cutout now always lands as a mask layer, so "adjustment" is the default. The two older
// spellings stay in the enum: repointing them silently would change what every existing agent
// call does, and dropping them would make previously valid calls fail.
void McpTest::segmentationOutputDefaultsToAdjustment()
{
    const QJsonObject payload = drift::mcp::toolboxPayload(QStringLiteral("segmentation"));
    const QJsonArray tools = payload.value(QStringLiteral("tools")).toArray();

    int checked = 0;
    for (const QJsonValue &v : tools) {
        const QJsonObject tool = v.toObject();
        const QString name = tool.value(QStringLiteral("name")).toString();
        if (name != QLatin1String("segment_clip") && name != QLatin1String("run_segmentation"))
            continue;

        const QJsonObject output = tool.value(QStringLiteral("inputSchema"))
                                       .toObject()
                                       .value(QStringLiteral("properties"))
                                       .toObject()
                                       .value(QStringLiteral("output"))
                                       .toObject();
        QVERIFY2(!output.isEmpty(), qPrintable(name));
        QCOMPARE(output.value(QStringLiteral("default")).toString(), QStringLiteral("adjustment"));

        QStringList values;
        for (const QJsonValue &e : output.value(QStringLiteral("enum")).toArray())
            values.append(e.toString());
        QVERIFY2(values.contains(QStringLiteral("adjustment")), qPrintable(name));
        QVERIFY2(values.contains(QStringLiteral("clips")), qPrintable(name));
        QVERIFY2(values.contains(QStringLiteral("mask")), qPrintable(name));

        // The description must not still promise the old two-track behaviour.
        QVERIFY2(!output.value(QStringLiteral("description"))
                      .toString()
                      .contains(QStringLiteral("splits the subject onto its own clip")),
                 qPrintable(name));
        ++checked;
    }
    QCOMPARE(checked, 2);
}

void McpTest::toolboxDescriptionsIncludeWhen()
{
    const QJsonObject payload = drift::mcp::toolboxPayload(QStringLiteral("timeline"));
    const QJsonArray tools = payload.value(QStringLiteral("tools")).toArray();
    bool sawAddTrack = false;
    for (const QJsonValue &v : tools) {
        const QJsonObject tool = v.toObject();
        if (tool.value(QStringLiteral("name")).toString() == QLatin1String("add_track")) {
            sawAddTrack = true;
            QVERIFY(tool.value(QStringLiteral("description")).toString().startsWith(QStringLiteral("When:")));
            const QJsonObject type =
                tool.value(QStringLiteral("inputSchema")).toObject()
                    .value(QStringLiteral("properties")).toObject()
                    .value(QStringLiteral("type")).toObject();
            QVERIFY(type.contains(QStringLiteral("enum")));
        }
    }
    QVERIFY(sawAddTrack);
}

void McpTest::toolboxAnnotationsPresent()
{
    const QJsonObject payload = drift::mcp::toolboxPayload(QStringLiteral("media"));
    const QJsonArray tools = payload.value(QStringLiteral("tools")).toArray();
    bool sawList = false;
    bool sawRename = false;
    for (const QJsonValue &v : tools) {
        const QJsonObject tool = v.toObject();
        const QString name = tool.value(QStringLiteral("name")).toString();
        if (name == QLatin1String("list_assets")) {
            sawList = true;
            const QJsonObject ann = tool.value(QStringLiteral("annotations")).toObject();
            QVERIFY(ann.value(QStringLiteral("readOnlyHint")).toBool());
            QVERIFY(ann.value(QStringLiteral("idempotentHint")).toBool());
        }
        if (name == QLatin1String("rename_asset"))
            sawRename = true;
    }
    QVERIFY(sawList);
    QVERIFY(sawRename);
    QVERIFY(drift::mcp::toolboxNames().contains(QStringLiteral("project")));
}

void McpTest::toolboxUnknownIsError()
{
    const QJsonObject payload = drift::mcp::toolboxPayload(QStringLiteral("nope"));
    QCOMPARE(payload.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(payload.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_toolbox"));
}

void McpTest::toolboxReturnsSchemas()
{
    const QJsonObject payload = drift::mcp::toolboxPayload(QStringLiteral("timeline"));
    QVERIFY(payload.value(QStringLiteral("ok")).toBool());
    const QJsonArray tools = payload.value(QStringLiteral("tools")).toArray();
    QVERIFY(tools.size() >= 8);
    bool sawPlace = false;
    for (const QJsonValue &v : tools) {
        if (v.toObject().value(QStringLiteral("name")).toString() == QLatin1String("place_clip")) {
            sawPlace = true;
            QVERIFY(v.toObject().contains(QStringLiteral("inputSchema")));
        }
    }
    QVERIFY(sawPlace);
}

void McpTest::protocolInitializeAndToolsList()
{
    const QJsonValue init = drift::mcp::handleJsonRpc(rpc(QStringLiteral("initialize")), {}, {});
    QVERIFY(init.isObject());
    QCOMPARE(init.toObject().value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("serverInfo")).toObject()
                 .value(QStringLiteral("name")).toString(),
             QStringLiteral("drift"));

    const QString instructions = init.toObject().value(QStringLiteral("result")).toObject()
                                     .value(QStringLiteral("instructions")).toString();
    QVERIFY(instructions.contains(QStringLiteral("frames(")));
    QVERIFY(instructions.size() < 1600);

    const QJsonValue listed = drift::mcp::handleJsonRpc(rpc(QStringLiteral("tools/list")), {}, {});
    const QJsonArray tools =
        listed.toObject().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 8);
    QStringList names;
    for (const QJsonValue &v : tools)
        names.append(v.toObject().value(QStringLiteral("name")).toString());
    for (const char *expected : {"catalog", "toolbox", "apply", "inspect", "capture", "frames", "activity", "search"})
        QVERIFY2(names.contains(QLatin1String(expected)), expected);
}

void McpTest::protocolNotificationHasNoReply()
{
    const QJsonObject note{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("notifications/initialized")},
    };
    const QJsonValue reply = drift::mcp::handleJsonRpc(note, {}, {});
    QVERIFY(reply.isUndefined());

    const QJsonValue batch = drift::mcp::handleJsonRpc(QJsonArray{note}, {}, {});
    QVERIFY(batch.isUndefined());
}

void McpTest::protocolUnknownOp()
{
    bool called = false;
    const QJsonValue reply = drift::mcp::handleJsonRpc(
        rpc(QStringLiteral("tools/call"),
            {{QStringLiteral("name"), QStringLiteral("not_a_tool")},
             {QStringLiteral("arguments"), QJsonObject{}}}),
        {},
        [&](const QString &, const QJsonObject &) {
            called = true;
            return QJsonObject{};
        });
    QVERIFY(!called);
    const QJsonArray content = reply.toObject()
                                   .value(QStringLiteral("result"))
                                   .toObject()
                                   .value(QStringLiteral("content"))
                                   .toArray();
    QVERIFY(!content.isEmpty());
    const auto payload = QJsonDocument::fromJson(
                             content.at(0).toObject().value(QStringLiteral("text")).toString().toUtf8())
                             .object();
    QCOMPARE(payload.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
}

void McpTest::sessionFileRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("session.json"));
    qputenv("DRIFT_MCP_SESSION_PATH", path.toUtf8());
    QVERIFY(drift::mcp::writeSessionFile(4731, QStringLiteral("abc123")));
    quint16 port = 0;
    QString token;
    QString error;
    QVERIFY(drift::mcp::readSessionFile(&port, &token, &error));
    QCOMPARE(port, quint16(4731));
    QCOMPARE(token, QStringLiteral("abc123"));
    drift::mcp::removeSessionFile();
    QVERIFY(!QFile::exists(path));
    qunsetenv("DRIFT_MCP_SESSION_PATH");
}

void McpTest::sessionFileMissing()
{
    qputenv("DRIFT_MCP_SESSION_PATH", "/tmp/drift-mcp-does-not-exist-test.json");
    QString error;
    QVERIFY(!drift::mcp::readSessionFile(nullptr, nullptr, &error));
    QVERIFY(error.contains(QStringLiteral("Agent access")));
    qunsetenv("DRIFT_MCP_SESSION_PATH");
}

void McpTest::serverRequiresBearerToken()
{
    QTemporaryDir dir;
    qputenv("DRIFT_MCP_SESSION_PATH", dir.filePath(QStringLiteral("s.json")).toUtf8());
    AssetLibrary library;
    AppController state(&library);
    state.setMcpEnabled(true);
    QVERIFY2(state.mcpRunning(), qPrintable(state.mcpError()));
    int status = 0;
    httpPost(quint16(state.mcpPort()), {},
             QJsonDocument(rpc(QStringLiteral("ping"))).toJson(QJsonDocument::Compact), &status);
    QCOMPARE(status, 401);
    state.setMcpEnabled(false);
    QVERIFY(!state.mcpRunning());
    qunsetenv("DRIFT_MCP_SESSION_PATH");
}

void McpTest::serverInitializeWithToken()
{
    QTemporaryDir dir;
    qputenv("DRIFT_MCP_SESSION_PATH", dir.filePath(QStringLiteral("s.json")).toUtf8());
    AssetLibrary library;
    AppController state(&library);
    state.setMcpEnabled(true);
    QVERIFY(state.mcpRunning());
    int status = 0;
    const QByteArray auth = "Bearer " + state.mcpToken().toUtf8();
    const QByteArray body = httpPost(
        quint16(state.mcpPort()), auth,
        QJsonDocument(rpc(QStringLiteral("initialize"))).toJson(QJsonDocument::Compact), &status);
    QCOMPARE(status, 200);
    const auto doc = QJsonDocument::fromJson(body);
    QCOMPARE(doc.object().value(QStringLiteral("result")).toObject()
                 .value(QStringLiteral("serverInfo")).toObject()
                 .value(QStringLiteral("name")).toString(),
             QStringLiteral("drift"));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("s.json"))));
    state.setMcpEnabled(false);
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("s.json"))));
    qunsetenv("DRIFT_MCP_SESSION_PATH");
}

void McpTest::serverNotificationReturns202()
{
    QTemporaryDir dir;
    qputenv("DRIFT_MCP_SESSION_PATH", dir.filePath(QStringLiteral("s.json")).toUtf8());
    AssetLibrary library;
    AppController state(&library);
    state.setMcpEnabled(true);
    QVERIFY2(state.mcpRunning(), qPrintable(state.mcpError()));
    int status = 0;
    const QByteArray auth = "Bearer " + state.mcpToken().toUtf8();
    const QJsonObject note{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("notifications/initialized")},
    };
    httpPost(quint16(state.mcpPort()), auth, QJsonDocument(note).toJson(QJsonDocument::Compact),
             &status);
    QCOMPARE(status, 202);
    state.setMcpEnabled(false);
    qunsetenv("DRIFT_MCP_SESSION_PATH");
}

void McpTest::applyUnknownOp()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject result =
        dispatcher.applyOne(QStringLiteral("not_real"), {});
    QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
}

void McpTest::applyBatchStopsAndUndoRevertsPrefix()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    QVERIFY(track >= 0);
    const QString id = state.mcpCompactClip(track, clip).value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());

    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject batch = dispatcher.apply(QJsonObject{
        {QStringLiteral("ops"),
         QJsonArray{
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("set_duration")},
                         {QStringLiteral("args"),
                          QJsonObject{{QStringLiteral("clip"), id},
                                      {QStringLiteral("duration"), 2.0}}}},
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("not_real")},
                         {QStringLiteral("args"), QJsonObject{}}},
         }},
    });
    QCOMPARE(batch.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(batch.value(QStringLiteral("error")).toString(), QStringLiteral("apply_failed"));
    QCOMPARE(batch.value(QStringLiteral("stopped")).toInt(), 1);
    QCOMPARE(state.mcpCompactClip(track, clip).value(QStringLiteral("duration")).toDouble(), 2.0);

    QVERIFY(state.undoAvailable());
    state.undo();
    QVERIFY(state.mcpCompactClip(track, clip).value(QStringLiteral("duration")).toDouble() > 2.0);
}

void McpTest::inspectIsCompact()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject summary = dispatcher.inspect({});
    QVERIFY(summary.value(QStringLiteral("ok")).toBool());
    QVERIFY(summary.contains(QStringLiteral("tracks")));
    QVERIFY(summary.contains(QStringLiteral("overlap")));
    QCOMPARE(summary.value(QStringLiteral("overlap")).toBool(), false);
    QVERIFY(!summary.contains(QStringLiteral("work_in")));
    QVERIFY(!summary.toVariantMap().contains(QStringLiteral("effects")));
    QCOMPARE(summary.value(QStringLiteral("path")).toString(), QString());
    QCOMPARE(summary.value(QStringLiteral("dirty")).toBool(), true);
    QVERIFY(summary.contains(QStringLiteral("background")));
    QVERIFY(summary.contains(QStringLiteral("export")));
    QVERIFY(!summary.contains(QStringLiteral("package")));
    const QJsonObject withClips = dispatcher.inspect({{QStringLiteral("clips"), true}});
    const QJsonArray tracks = withClips.value(QStringLiteral("tracks")).toArray();
    QVERIFY(!tracks.isEmpty());
    QVERIFY(tracks.at(0).toObject().contains(QStringLiteral("items")));
}

namespace {

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonObject firstItemOfKind(const QJsonObject &inspect, const QString &kind)
{
    const QJsonArray tracks = inspect.value(QStringLiteral("tracks")).toArray();
    for (const QJsonValue &t : tracks) {
        const QJsonArray items = t.toObject().value(QStringLiteral("items")).toArray();
        for (const QJsonValue &item : items) {
            if (item.toObject().value(QStringLiteral("kind")).toString() == kind)
                return item.toObject();
        }
    }
    return {};
}

int serialisedSize(const QJsonObject &o)
{
    return QJsonDocument(o).toJson(QJsonDocument::Compact).size();
}

} // namespace

void McpTest::inspectDetailRowIsCompact()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("Hello")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());
    const QJsonArray shapes = dispatcher.applyOne(QStringLiteral("list_shapes"), {})
                                  .value(QStringLiteral("shapes"))
                                  .toArray();
    if (!shapes.isEmpty()) {
        QVERIFY(dispatcher.applyOne(QStringLiteral("add_shape"),
                                    {{QStringLiteral("shape"),
                                      shapes.at(0).toObject().value(QStringLiteral("id")).toString()},
                                     {QStringLiteral("at"), 10.0}})
                    .value(QStringLiteral("ok"))
                    .toBool());
    }
    drift::Track videoTrack{.type = drift::TrackType::Video};
    drift::Clip video;
    video.id = QStringLiteral("plain-video");
    video.type = drift::ClipType::Video;
    video.name = QStringLiteral("clip.mp4");
    video.path = QStringLiteral("/nonexistent/clip.mp4");
    video.timelineStart = drift::secondsToUs(20.0);
    video.timelineDuration = drift::secondsToUs(5.0);
    video.srcOut = drift::secondsToUs(5.0);
    videoTrack.clips.append(video);
    state.project()->tracks().append(videoTrack);

    const QJsonObject inspect = dispatcher.inspect(
        {{QStringLiteral("clips"), true}, {QStringLiteral("detail"), true}});
    QVERIFY(inspect.value(QStringLiteral("ok")).toBool());

    const QJsonObject text = firstItemOfKind(inspect, QStringLiteral("text"));
    QVERIFY(!text.isEmpty());
    QVERIFY(text.contains(QStringLiteral("textStyle")));
    QVERIFY(text.contains(QStringLiteral("textContent")));
    QVERIFY(!text.contains(QStringLiteral("shapeStyle")));
    QVERIFY(!text.contains(QStringLiteral("mask")));
    QVERIFY(!text.contains(QStringLiteral("stabilizeMode")));
    QVERIFY(!text.contains(QStringLiteral("stabilized")));
    QVERIFY(!text.contains(QStringLiteral("filmstripPath")));
    QVERIFY(!text.contains(QStringLiteral("thumbnailPath")));
    QVERIFY(!text.contains(QStringLiteral("canFaceTrack")));
    QVERIFY(!text.contains(QStringLiteral("keyframes")));
    QVERIFY(!text.contains(QStringLiteral("animated")));
    QVERIFY(!text.contains(QStringLiteral("reverse")));
    QVERIFY(!text.contains(QStringLiteral("fadeShape")));
    const QJsonObject transform = text.value(QStringLiteral("transform")).toObject();
    for (const char *key : {"x", "y", "w", "h", "rotation", "opacity"})
        QVERIFY2(transform.contains(QLatin1String(key)), key);

    const QJsonObject plain = firstItemOfKind(inspect, QStringLiteral("video"));
    QVERIFY(!plain.isEmpty());
    QVERIFY(!plain.contains(QStringLiteral("textStyle")));
    QVERIFY(!plain.contains(QStringLiteral("textContent")));
    QVERIFY(plain.contains(QStringLiteral("transform")));
    QVERIFY2(serialisedSize(plain) < 1000, qPrintable(QString::number(serialisedSize(plain))));
    QVERIFY2(serialisedSize(text) < 1600, qPrintable(QString::number(serialisedSize(text))));

    if (!shapes.isEmpty()) {
        const QJsonObject shape = firstItemOfKind(inspect, QStringLiteral("shape"));
        QVERIFY(!shape.isEmpty());
        QVERIFY(shape.contains(QStringLiteral("shapeStyle")));
        QVERIFY(!shape.contains(QStringLiteral("textStyle")));
    }

    const QJsonObject verbose = dispatcher.inspect({{QStringLiteral("clips"), true},
                                                    {QStringLiteral("detail"), true},
                                                    {QStringLiteral("verbose"), true}});
    const QJsonObject full = firstItemOfKind(verbose, QStringLiteral("text"));
    QVERIFY(full.contains(QStringLiteral("keyframes")));
    QVERIFY(full.contains(QStringLiteral("shapeStyle")));
    QVERIFY(full.contains(QStringLiteral("stabilizeMode")));
    QVERIFY(full.contains(QStringLiteral("filmstripPath")));
    QVERIFY(!full.contains(QStringLiteral("transform")));
}

void McpTest::inspectClipAndTrackFilters()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject a = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("A")}, {QStringLiteral("at"), 0.0}});
    QVERIFY(a.value(QStringLiteral("ok")).toBool());
    const QJsonObject b = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("B")}, {QStringLiteral("at"), 10.0}});
    QVERIFY(b.value(QStringLiteral("ok")).toBool());
    const QString idB = b.value(QStringLiteral("id")).toString();
    QVERIFY(!idB.isEmpty());

    const QJsonObject one = dispatcher.inspect({{QStringLiteral("clip"), idB}});
    QVERIFY(one.value(QStringLiteral("ok")).toBool());
    const QJsonArray tracks = one.value(QStringLiteral("tracks")).toArray();
    QCOMPARE(tracks.size(), 1);
    const QJsonArray items = tracks.at(0).toObject().value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 1);
    const QJsonObject row = items.at(0).toObject();
    QCOMPARE(row.value(QStringLiteral("id")).toString(), idB);
    QVERIFY(row.contains(QStringLiteral("textStyle")));
    QVERIFY(row.contains(QStringLiteral("transform")));
    QCOMPARE(one.value(QStringLiteral("clips")).toInt(), 2);

    const QJsonObject missing = dispatcher.inspect({{QStringLiteral("clip"), QStringLiteral("no-such-clip")}});
    QCOMPARE(missing.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(missing.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QVERIFY(missing.value(QStringLiteral("detail")).toString().contains(QStringLiteral("no-such-clip")));

    const int trackIndex = tracks.at(0).toObject().value(QStringLiteral("i")).toInt();
    const QJsonObject track = dispatcher.inspect({{QStringLiteral("track"), trackIndex},
                                                  {QStringLiteral("clips"), true}});
    QVERIFY(track.value(QStringLiteral("ok")).toBool());
    QCOMPARE(track.value(QStringLiteral("tracks")).toArray().size(), 1);
    QCOMPARE(track.value(QStringLiteral("tracks")).toArray().at(0).toObject()
                 .value(QStringLiteral("items")).toArray().size(),
             2);

    const QJsonObject badTrack = dispatcher.inspect({{QStringLiteral("track"), 99}});
    QCOMPARE(badTrack.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(badTrack.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
}

void McpTest::inspectJobsCollapsed()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject inspect = dispatcher.inspect({{QStringLiteral("detail"), true}});
    QVERIFY(inspect.value(QStringLiteral("ok")).toBool());
    for (const char *key : {"package", "subtitleGen", "reverseRender", "sceneDetect", "jobs", "beats"})
        QVERIFY2(!inspect.contains(QLatin1String(key)), key);
    QVERIFY(inspect.contains(QStringLiteral("export")));
    QVERIFY(inspect.contains(QStringLiteral("bookmarks")));
    QCOMPARE(inspect.value(QStringLiteral("undo")).toObject().value(QStringLiteral("hash")).toString().size(), 12);
}

void McpTest::inspectIncludesProjectFields()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject inspect = dispatcher.inspect({});
    QVERIFY(inspect.contains(QStringLiteral("path")));
    QVERIFY(inspect.contains(QStringLiteral("dirty")));
    QVERIFY(inspect.contains(QStringLiteral("background")));
    const QJsonObject exportState = inspect.value(QStringLiteral("export")).toObject();
    QVERIFY(exportState.contains(QStringLiteral("active")));
    QVERIFY(exportState.contains(QStringLiteral("progress")));
}

void McpTest::placeHonorsOverlapToggle()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject first = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("A")}, {QStringLiteral("at"), 0.0}});
    QVERIFY(first.value(QStringLiteral("ok")).toBool());

    const QJsonObject gapped = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("B")}, {QStringLiteral("at"), 1.0}});
    QVERIFY(gapped.value(QStringLiteral("ok")).toBool());
    QVERIFY(gapped.value(QStringLiteral("start")).toDouble() > 4.0);

    QVERIFY(state.undoAvailable());
    state.undo();

    const QJsonObject overlapOn = dispatcher.applyOne(
        QStringLiteral("set_overlap"), {{QStringLiteral("enabled"), true}});
    QVERIFY(overlapOn.value(QStringLiteral("ok")).toBool());
    QCOMPARE(overlapOn.value(QStringLiteral("overlap")).toBool(), true);

    const QJsonObject overlapping = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("B")}, {QStringLiteral("at"), 1.0}});
    QVERIFY(overlapping.value(QStringLiteral("ok")).toBool());
    QCOMPARE(overlapping.value(QStringLiteral("start")).toDouble(), 1.0);
    QVERIFY(!overlapping.contains(QStringLiteral("reason")));
    QCOMPARE(dispatcher.inspect({}).value(QStringLiteral("overlap")).toBool(), true);
}

void McpTest::workAreaRoundTrip()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject bad = dispatcher.applyOne(
        QStringLiteral("set_work_area"),
        {{QStringLiteral("in"), 5.0}, {QStringLiteral("out"), 1.0}});
    QCOMPARE(bad.value(QStringLiteral("ok")).toBool(), false);

    const QJsonObject set = dispatcher.applyOne(
        QStringLiteral("set_work_area"),
        {{QStringLiteral("in"), 1.5}, {QStringLiteral("out"), 4.0}});
    QVERIFY(set.value(QStringLiteral("ok")).toBool());
    QCOMPARE(set.value(QStringLiteral("work_in")).toDouble(), 1.5);
    QCOMPARE(set.value(QStringLiteral("work_out")).toDouble(), 4.0);

    const QJsonObject inspect = dispatcher.inspect({});
    QCOMPARE(inspect.value(QStringLiteral("work_in")).toDouble(), 1.5);
    QCOMPARE(inspect.value(QStringLiteral("work_out")).toDouble(), 4.0);

    const QJsonObject cleared = dispatcher.applyOne(QStringLiteral("clear_work_area"), {});
    QVERIFY(cleared.value(QStringLiteral("ok")).toBool());
    QVERIFY(!dispatcher.inspect({}).contains(QStringLiteral("work_in")));
}

void McpTest::exportOptionsAndSettings()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("DriftMcpTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftMcpTest"));
    const auto restore = qScopeGuard([&] {
        QSettings().remove(QStringLiteral("export"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
    });
    QSettings().remove(QStringLiteral("export"));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject options = dispatcher.applyOne(QStringLiteral("list_export_options"), {});
    QVERIFY(options.value(QStringLiteral("ok")).toBool());
    QVERIFY(!options.value(QStringLiteral("video")).toArray().isEmpty());
    QVERIFY(!options.value(QStringLiteral("fps")).toArray().isEmpty());
    QVERIFY(options.contains(QStringLiteral("gif")));
}

void McpTest::exportVideoRequiresPath()
{
    QStandardPaths::setTestModeEnabled(true);
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("DriftMcpTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DriftMcpTest"));
    const auto restore = qScopeGuard([&] {
        QSettings().remove(QStringLiteral("export"));
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app);
        QStandardPaths::setTestModeEnabled(false);
    });
    QSettings().remove(QStringLiteral("export"));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject missing = dispatcher.applyOne(QStringLiteral("export_video"), {});
    QCOMPARE(missing.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(missing.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString outPath = dir.filePath(QStringLiteral("out.mp4"));
    const QJsonObject started = dispatcher.applyOne(
        QStringLiteral("export_video"), {{QStringLiteral("path"), outPath}});
    if (started.value(QStringLiteral("ok")).toBool()) {
        QVERIFY(started.value(QStringLiteral("started")).toBool());
        const QJsonObject status = dispatcher.applyOne(QStringLiteral("export_status"), {});
        QVERIFY(status.contains(QStringLiteral("busy")));
        QVERIFY(status.contains(QStringLiteral("progress")));
    }
}

void McpTest::projectSetupRoundTrip()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject setup = dispatcher.applyOne(
        QStringLiteral("set_project_setup"),
        {{QStringLiteral("width"), 1280}, {QStringLiteral("height"), 720}, {QStringLiteral("fps"), 30}});
    QVERIFY(setup.value(QStringLiteral("ok")).toBool());
    QCOMPARE(setup.value(QStringLiteral("w")).toInt(), 1280);
    QCOMPARE(setup.value(QStringLiteral("h")).toInt(), 720);
    QCOMPARE(setup.value(QStringLiteral("fps")).toInt(), 30);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("test.drift"));
    const QJsonObject saved = dispatcher.applyOne(
        QStringLiteral("save_project"), {{QStringLiteral("path"), path}});
    QVERIFY(saved.value(QStringLiteral("ok")).toBool());
    QVERIFY(QFile::exists(path));
}

void McpTest::captureDoesNotInsertClip()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    auto clipCount = [](AppController &s) {
        int n = 0;
        for (const QVariant &t : s.tracks())
            n += t.toMap().value(QStringLiteral("clips")).toList().size();
        return n;
    };
    const int before = clipCount(state);
    const QJsonObject result = state.mcpCaptureFrame(-1.0, false);
    QCOMPARE(clipCount(state), before);
    if (result.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    QVERIFY(result.value(QStringLiteral("content")).toArray().size() >= 1);
}

void McpTest::inspectRevisionUnchanged()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject first = dispatcher.inspect({});
    QVERIFY(first.value(QStringLiteral("ok")).toBool());
    QVERIFY(first.contains(QStringLiteral("revision")));
    const int revision = first.value(QStringLiteral("revision")).toInt();
    const QJsonObject unchanged = dispatcher.inspect({{QStringLiteral("since"), revision}});
    QVERIFY(unchanged.value(QStringLiteral("ok")).toBool());
    QCOMPARE(unchanged.value(QStringLiteral("unchanged")).toBool(), true);
    QCOMPARE(unchanged.value(QStringLiteral("revision")).toInt(), revision);
}

void McpTest::listEffectsIncludesParams()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject compact = dispatcher.applyOne(QStringLiteral("list_effects"), {});
    QVERIFY(compact.value(QStringLiteral("ok")).toBool());
    const QJsonObject cats = compact.value(QStringLiteral("cats")).toObject();
    QVERIFY(!cats.isEmpty());
    bool sawParams = false;
    for (auto it = cats.begin(); it != cats.end() && !sawParams; ++it) {
        for (const QJsonValue &row : it.value().toArray()) {
            const QString id = row.toObject().value(QStringLiteral("id")).toString();
            const QJsonObject one = dispatcher.applyOne(QStringLiteral("list_effects"), {{QStringLiteral("id"), id}});
            QVERIFY(one.value(QStringLiteral("ok")).toBool());
            const QJsonArray effects = one.value(QStringLiteral("effects")).toArray();
            QCOMPARE(effects.size(), 1);
            const QJsonArray params = effects.at(0).toObject().value(QStringLiteral("params")).toArray();
            if (!params.isEmpty()) {
                sawParams = true;
                QVERIFY(params.at(0).toObject().contains(QStringLiteral("key")));
                break;
            }
        }
    }
    QVERIFY(sawParams);
}

// --- audio -------------------------------------------------------------------------------
//
// The audio ops need real PCM, so these generate fixtures with the ffmpeg CLI the same way
// tst_editorstate does, and skip when it is not installed.

namespace {

QString ffmpegPath()
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

bool runFfmpeg(const QStringList &args)
{
    QProcess proc;
    proc.start(ffmpegPath(), QStringList{QStringLiteral("-y")} + args);
    return proc.waitForFinished(60000) && proc.exitCode() == 0;
}

// Broadband noise bursts every 0.5 s — 120 BPM, and broadband so every FFT bin jumps at once,
// which is what the spectral-flux detector keys on.
bool writeClickTrack(const QString &path, int seconds)
{
    return runFfmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("aevalsrc=0.9*random(0)*exp(-mod(t\\,0.5)*80):s=48000:d=%1")
                          .arg(seconds),
                      QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path});
}

// Two seconds of tone then two of digital silence, concatenated rather than gated so the tail
// is genuinely zero. The gain is there because ffmpeg's sine filter emits at -18 dBFS (0.125
// linear); these peaks are linear, so without it "loud" and "quiet" would be a factor of 8
// apart instead of the full scale the assertions read as.
bool writeHalfSilentTone(const QString &path)
{
    return runFfmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("sine=frequency=440:sample_rate=48000:duration=2"),
                      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("anullsrc=r=48000:cl=mono:d=2"),
                      QStringLiteral("-filter_complex"),
                      QStringLiteral("[0:a]volume=8[loud];[loud][1:a]concat=n=2:v=0:a=1[out]"),
                      QStringLiteral("-map"), QStringLiteral("[out]"), QStringLiteral("-c:a"),
                      QStringLiteral("pcm_s16le"), path});
}

// Imports `path` and drops it on the timeline at `at`, returning the new clip's UUID.
QString importAndPlace(drift::mcp::McpDispatcher &dispatcher, const QString &path, double at)
{
    const QJsonObject imported = dispatcher.applyOne(
        QStringLiteral("import_media"),
        {{QStringLiteral("paths"), QJsonArray{path}}});
    if (!imported.value(QStringLiteral("ok")).toBool())
        return {};

    const QJsonArray assets = imported.value(QStringLiteral("assets")).toArray();
    if (assets.isEmpty())
        return {};
    const QString assetId = assets.at(0).toObject().value(QStringLiteral("id")).toString();

    const QJsonObject placed = dispatcher.applyOne(
        QStringLiteral("place_clip"),
        {{QStringLiteral("asset"), assetId}, {QStringLiteral("at"), at}});
    if (!placed.value(QStringLiteral("ok")).toBool())
        return {};
    return placed.value(QStringLiteral("id")).toString();
}

} // namespace

void McpTest::audioToolboxReturnsSchemas()
{
    const QJsonObject payload = drift::mcp::toolboxPayload(QStringLiteral("audio"));
    QVERIFY(payload.value(QStringLiteral("ok")).toBool());
    const QJsonArray tools = payload.value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 14);

    QStringList names;
    for (const QJsonValue &v : tools) {
        const QJsonObject tool = v.toObject();
        names.append(tool.value(QStringLiteral("name")).toString());
        QVERIFY(tool.contains(QStringLiteral("inputSchema")));
    }
    QVERIFY(names.contains(QStringLiteral("get_waveform")));
    QVERIFY(names.contains(QStringLiteral("detect_beats")));
    QVERIFY(names.contains(QStringLiteral("split_on_beats")));
    QVERIFY(names.contains(QStringLiteral("snap_clips_to_beats")));
    QVERIFY(names.contains(QStringLiteral("set_volume")));
    QVERIFY(names.contains(QStringLiteral("detect_silence")));
    QVERIFY(names.contains(QStringLiteral("remove_silence")));
    QVERIFY(names.contains(QStringLiteral("analyze_loudness")));
    QVERIFY(names.contains(QStringLiteral("normalize_volume")));
    QVERIFY(names.contains(QStringLiteral("duck_under")));
    QVERIFY(names.contains(QStringLiteral("clear_beat_analysis")));

    // detect_beats is on /mcp/audio, not the homepage.
    QCOMPARE(drift::mcp::toolboxForOp(QStringLiteral("detect_beats")), QStringLiteral("audio"));
}

// The whole reason these ops call the engine directly instead of the QML getters: those come
// back empty the first time and repaint on a signal, which an agent never sees.
void McpTest::waveformReturnsPeaksOnFirstCall()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("tone.wav"));
    QVERIFY(writeHalfSilentTone(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QString clip = importAndPlace(dispatcher, source, 0.0);
    QVERIFY(!clip.isEmpty());

    const QJsonObject result = dispatcher.applyOne(
        QStringLiteral("get_waveform"),
        {{QStringLiteral("clip"), clip}, {QStringLiteral("buckets"), 64}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(result).toJson(QJsonDocument::Compact)));
    QCOMPARE(result.value(QStringLiteral("source")).toString(), QStringLiteral("clip"));

    const QJsonArray peaks = result.value(QStringLiteral("peaks")).toArray();
    QCOMPARE(peaks.size(), 64);
    for (const QJsonValue &v : peaks)
        QVERIFY(v.toDouble() >= 0.0 && v.toDouble() <= 1.0);
    QVERIFY2(result.value(QStringLiteral("max")).toDouble() > 0.2,
             qPrintable(QString::number(result.value(QStringLiteral("max")).toDouble())));
}

// reduceDensePeaks floors silence at 0.05 so a quiet lane still draws; these peaks must not,
// or "is this stretch empty" becomes unanswerable.
void McpTest::waveformReportsSilenceAsZero()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("half-tone.wav"));
    QVERIFY(writeHalfSilentTone(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    QVERIFY(!importAndPlace(dispatcher, source, 0.0).isEmpty());

    const QString assetId = state.mcpInspect(false)
                                .value(QStringLiteral("assets")).toArray().at(0).toObject()
                                .value(QStringLiteral("id")).toString();
    QVERIFY(!assetId.isEmpty());

    const QJsonObject loud = dispatcher.applyOne(
        QStringLiteral("get_waveform"),
        {{QStringLiteral("asset"), assetId}, {QStringLiteral("start"), 0.0},
         {QStringLiteral("duration"), 1.5}, {QStringLiteral("buckets"), 16}});
    QVERIFY2(loud.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(loud).toJson(QJsonDocument::Compact)));
    QVERIFY2(loud.value(QStringLiteral("max")).toDouble() > 0.2,
             qPrintable(QJsonDocument(loud).toJson(QJsonDocument::Compact)));

    const QJsonObject quiet = dispatcher.applyOne(
        QStringLiteral("get_waveform"),
        {{QStringLiteral("asset"), assetId}, {QStringLiteral("start"), 2.5},
         {QStringLiteral("duration"), 1.0}, {QStringLiteral("buckets"), 16}});
    QVERIFY(quiet.value(QStringLiteral("ok")).toBool());
    QCOMPARE(quiet.value(QStringLiteral("max")).toDouble(), 0.0);
}

void McpTest::detectBeatsRejectsShortRange()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject tooShort = dispatcher.applyOne(
        QStringLiteral("detect_beats"), {{QStringLiteral("duration"), 1.0}});
    QCOMPARE(tooShort.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(tooShort.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));

    const QJsonObject tooLong = dispatcher.applyOne(
        QStringLiteral("detect_beats"), {{QStringLiteral("duration"), 5000.0}});
    QCOMPARE(tooLong.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));

    const QJsonObject missing = dispatcher.applyOne(QStringLiteral("detect_beats"), {});
    QCOMPARE(missing.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
}

void McpTest::detectBeatsFindsClickTempoAndPublishes()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("clicks.wav"));
    QVERIFY(writeClickTrack(source, 12));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    QVERIFY(!importAndPlace(dispatcher, source, 0.0).isEmpty());

    const QJsonObject beats = dispatcher.applyOne(
        QStringLiteral("detect_beats"),
        {{QStringLiteral("start"), 0.0}, {QStringLiteral("duration"), 10.0}});
    QVERIFY2(beats.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(beats).toJson(QJsonDocument::Compact)));

    const double bpm = beats.value(QStringLiteral("bpm")).toDouble();
    QVERIFY2(std::abs(bpm - 120.0) < 3.0, qPrintable(QStringLiteral("bpm %1").arg(bpm)));
    QVERIFY(!beats.value(QStringLiteral("beats")).toArray().isEmpty());
    QVERIFY(!beats.value(QStringLiteral("onsets")).toArray().isEmpty());
    QCOMPARE(beats.value(QStringLiteral("cached")).toBool(), false);

    // The same range comes back from cache rather than re-mixing.
    const QJsonObject again = dispatcher.applyOne(
        QStringLiteral("detect_beats"),
        {{QStringLiteral("start"), 0.0}, {QStringLiteral("duration"), 10.0}});
    QCOMPARE(again.value(QStringLiteral("cached")).toBool(), true);

    // And the editor sees the same analysis the agent got.
    const QJsonObject detail = dispatcher.inspect({{QStringLiteral("detail"), true}});
    const QJsonObject state_ = detail.value(QStringLiteral("beats")).toObject();
    QCOMPARE(state_.value(QStringLiteral("analysed")).toBool(), true);
    QCOMPARE(state_.value(QStringLiteral("stale")).toBool(), false);
    QVERIFY(std::abs(state_.value(QStringLiteral("bpm")).toDouble() - bpm) < 0.5);

    // Arming the layers is what turns beats into snap targets.
    const QJsonObject layers = dispatcher.applyOne(
        QStringLiteral("set_beat_layers"), {{QStringLiteral("grid"), true}});
    QVERIFY(layers.value(QStringLiteral("ok")).toBool());
    QCOMPARE(layers.value(QStringLiteral("gridVisible")).toBool(), true);
    QVERIFY(layers.value(QStringLiteral("snapTargets")).toInt() > 0);
}

void McpTest::splitOnBeatsCutsAndUndoesAsOneStep()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("clicks.wav"));
    QVERIFY(writeClickTrack(source, 12));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QString clip = importAndPlace(dispatcher, source, 0.0);
    QVERIFY(!clip.isEmpty());

    const QJsonObject beats = dispatcher.applyOne(
        QStringLiteral("detect_beats"),
        {{QStringLiteral("start"), 0.0}, {QStringLiteral("duration"), 10.0}});
    QVERIFY(beats.value(QStringLiteral("ok")).toBool());
    if (beats.value(QStringLiteral("beats")).toArray().isEmpty())
        QSKIP("no tempo found in the generated click track");

    const int clipsBefore = dispatcher.inspect({}).value(QStringLiteral("clips")).toInt();

    const QJsonObject split = dispatcher.applyOne(
        QStringLiteral("split_on_beats"),
        {{QStringLiteral("clip"), clip}, {QStringLiteral("unit"), QStringLiteral("bar")}});
    QVERIFY2(split.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(split).toJson(QJsonDocument::Compact)));

    const QJsonArray produced = split.value(QStringLiteral("clips")).toArray();
    QVERIFY(produced.size() > 1);
    QCOMPARE(produced.at(0).toString(), clip); // the original id names the first piece
    for (const QJsonValue &v : produced)
        QVERIFY(state.mcpLocateClip(v.toString()).first >= 0);
    QCOMPARE(dispatcher.inspect({}).value(QStringLiteral("clips")).toInt(),
             clipsBefore + produced.size() - 1);

    // However many cuts it made, it is one step.
    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(dispatcher.inspect({}).value(QStringLiteral("clips")).toInt(), clipsBefore);
}

void McpTest::snapClipsToBeatsRespectsMaxDistance()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("clicks.wav"));
    QVERIFY(writeClickTrack(source, 12));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    QVERIFY(!importAndPlace(dispatcher, source, 0.0).isEmpty());

    const QJsonObject beats = dispatcher.applyOne(
        QStringLiteral("detect_beats"),
        {{QStringLiteral("start"), 0.0}, {QStringLiteral("duration"), 10.0}});
    QVERIFY(beats.value(QStringLiteral("ok")).toBool());
    if (beats.value(QStringLiteral("beats")).toArray().isEmpty())
        QSKIP("no tempo found in the generated click track");

    // A title on its own lane, deliberately nowhere near a beat.
    const QJsonObject text = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("A")}, {QStringLiteral("at"), 20.0}});
    QVERIFY(text.value(QStringLiteral("ok")).toBool());
    const QString title = text.value(QStringLiteral("id")).toString();

    const QJsonObject snapped = dispatcher.applyOne(
        QStringLiteral("snap_clips_to_beats"),
        {{QStringLiteral("clips"), QJsonArray{title}}, {QStringLiteral("max_distance"), 0.25}});
    QVERIFY(snapped.value(QStringLiteral("ok")).toBool());
    QCOMPARE(snapped.value(QStringLiteral("moved")).toArray().size(), 0);

    const QJsonArray skipped = snapped.value(QStringLiteral("skipped")).toArray();
    QCOMPARE(skipped.size(), 1);
    QCOMPARE(skipped.at(0).toObject().value(QStringLiteral("reason")).toString(),
             QStringLiteral("too_far"));

    // With a wide enough window it does move, and reports where it actually landed.
    const QJsonObject wide = dispatcher.applyOne(
        QStringLiteral("snap_clips_to_beats"),
        {{QStringLiteral("clips"), QJsonArray{title}}, {QStringLiteral("max_distance"), 60.0}});
    QVERIFY(wide.value(QStringLiteral("ok")).toBool());
    const QJsonArray moved = wide.value(QStringLiteral("moved")).toArray();
    QCOMPARE(moved.size(), 1);
    QVERIFY(moved.at(0).toObject().contains(QStringLiteral("to")));
}

void McpTest::setVolumeRoundTrips()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject text = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("A")}, {QStringLiteral("at"), 0.0}});
    QVERIFY(text.value(QStringLiteral("ok")).toBool());
    const QString clip = text.value(QStringLiteral("id")).toString();

    const QJsonObject set = dispatcher.applyOne(
        QStringLiteral("set_volume"),
        {{QStringLiteral("clip"), clip}, {QStringLiteral("value"), 0.5}});
    QVERIFY2(set.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(set).toJson(QJsonDocument::Compact)));
    QCOMPARE(set.value(QStringLiteral("value")).toDouble(), 0.5);
    QCOMPARE(set.value(QStringLiteral("volumeKeys")).toInt(), 1);

    const QJsonObject keys = dispatcher.applyOne(
        QStringLiteral("list_keyframes"),
        {{QStringLiteral("clip"), clip}, {QStringLiteral("prop"), QStringLiteral("volume")}});
    QVERIFY(keys.value(QStringLiteral("ok")).toBool());
    const QJsonArray points = keys.value(QStringLiteral("keys")).toArray();
    QCOMPARE(points.size(), 1);
    QCOMPARE(points.at(0).toObject().value(QStringLiteral("value")).toDouble(), 0.5);

    // Out of range is refused by the schema range before the mixer sees it.
    const QJsonObject loud = dispatcher.applyOne(
        QStringLiteral("set_volume"),
        {{QStringLiteral("clip"), clip}, {QStringLiteral("value"), 9.0}});
    QCOMPARE(loud.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QVERIFY(loud.value(QStringLiteral("detail")).toString().contains(QStringLiteral("0..2")));
}

void McpTest::audioReadOpsAreNotUndoable()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject result = dispatcher.apply(
        {{QStringLiteral("ops"),
          QJsonArray{QJsonObject{{QStringLiteral("tool"), QStringLiteral("audio_summary")}},
                     QJsonObject{{QStringLiteral("tool"), QStringLiteral("list_shapes")}},
                     QJsonObject{{QStringLiteral("tool"), QStringLiteral("set_beat_layers")},
                                 {QStringLiteral("args"),
                                  QJsonObject{{QStringLiteral("grid"), true}}}}}}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(result).toJson(QJsonDocument::Compact)));
    QVERIFY(!state.undoAvailable());
}

// The payoff of the whole toolbox: extraSnapTargets() already feeds drift::snapTime, so arming
// the grid makes every ordinary placement op quantise without asking for it. Nothing in the
// audio ops themselves would fail if this stopped working, so it needs its own test.
void McpTest::armedBeatGridMakesMoveClipSnap()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("clicks.wav"));
    QVERIFY(writeClickTrack(source, 12));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    QVERIFY(!importAndPlace(dispatcher, source, 0.0).isEmpty());

    const QJsonObject beats = dispatcher.applyOne(
        QStringLiteral("detect_beats"),
        {{QStringLiteral("start"), 0.0}, {QStringLiteral("duration"), 10.0}});
    QVERIFY(beats.value(QStringLiteral("ok")).toBool());
    const QJsonArray grid = beats.value(QStringLiteral("beats")).toArray();
    if (grid.isEmpty())
        QSKIP("no tempo found in the generated click track");

    // Two beats, seconds apart. They have to be different targets: snapTime does not exclude the
    // clip being moved, so aiming the armed move at where the unarmed one already parked the
    // clip would snap it to itself at distance 0 and prove nothing.
    //
    // Mid-range, so neither is near the targets snapTime always has (0, the playhead, and every
    // clip edge). The title goes on its own text track, so the music clip is not in its way.
    double unarmedBeat = 0.0;
    double beat = 0.0;
    for (const QJsonValue &v : grid) {
        const double t = v.toDouble();
        if (unarmedBeat <= 0.0 && t > 3.0)
            unarmedBeat = t;
        else if (unarmedBeat > 0.0 && t > unarmedBeat + 2.0) {
            beat = t;
            break;
        }
    }
    if (unarmedBeat <= 0.0 || beat <= 0.0)
        QSKIP("no usable pair of beats in the analysed range");

    const QJsonObject text = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("A")}, {QStringLiteral("at"), 30.0}});
    QVERIFY(text.value(QStringLiteral("ok")).toBool());
    const QString title = text.value(QStringLiteral("id")).toString();

    // Grid off: the clip lands exactly where it was told, 40 ms off the beat.
    const QJsonObject unarmed = dispatcher.applyOne(
        QStringLiteral("move_clip"),
        {{QStringLiteral("clip"), title}, {QStringLiteral("at"), unarmedBeat + 0.04}});
    QVERIFY(unarmed.value(QStringLiteral("ok")).toBool());
    QVERIFY2(std::abs(unarmed.value(QStringLiteral("placed")).toDouble() - (unarmedBeat + 0.04))
                 < 0.005,
             qPrintable(QJsonDocument(unarmed).toJson(QJsonDocument::Compact)));

    const QJsonObject layers = dispatcher.applyOne(QStringLiteral("set_beat_layers"),
                                                   {{QStringLiteral("grid"), true}});
    QVERIFY2(layers.value(QStringLiteral("snapTargets")).toInt() > 0,
             qPrintable(QJsonDocument(layers).toJson(QJsonDocument::Compact)));

    // Grid on: the same 40 ms miss is now pulled onto the beat.
    const QJsonObject armed = dispatcher.applyOne(
        QStringLiteral("move_clip"),
        {{QStringLiteral("clip"), title}, {QStringLiteral("at"), beat + 0.04}});
    QVERIFY(armed.value(QStringLiteral("ok")).toBool());
    QVERIFY2(std::abs(armed.value(QStringLiteral("placed")).toDouble() - beat) < 0.005,
             qPrintable(QStringLiteral("beat=%1 targets=%2 reply=%3")
                            .arg(beat)
                            .arg(layers.value(QStringLiteral("snapTargets")).toInt())
                            .arg(QString::fromUtf8(
                                QJsonDocument(armed).toJson(QJsonDocument::Compact)))));
}

// --- scene toolbox ----------------------------------------------------------

void McpTest::sceneToolboxExposesSchemas()
{
    const QJsonObject box = drift::mcp::toolboxPayload(QStringLiteral("scene"));
    QVERIFY(box.value(QStringLiteral("ok")).toBool());

    QStringList names;
    for (const QJsonValue &v : box.value(QStringLiteral("tools")).toArray())
        names.append(v.toObject().value(QStringLiteral("name")).toString());

    for (const char *expected : {"detect_scenes", "list_scenes", "describe_clip", "find_scenes",
                                 "split_on_scenes", "bookmark_scenes"}) {
        QVERIFY2(names.contains(QLatin1String(expected)), expected);
    }

    // ai_capabilities belongs to the ai toolbox, not scene.
    const QJsonObject ai = drift::mcp::toolboxPayload(QStringLiteral("ai"));
    QStringList aiNames;
    for (const QJsonValue &v : ai.value(QStringLiteral("tools")).toArray())
        aiNames.append(v.toObject().value(QStringLiteral("name")).toString());
    QVERIFY(aiNames.contains(QStringLiteral("ai_capabilities")));
}

void McpTest::sceneOpsRequireAnalysisFirst()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    // Reading before scanning must say so, rather than returning an empty success that an
    // agent would read as "this clip has no scenes".
    for (const char *tool : {"list_scenes", "describe_clip"}) {
        const QJsonObject result = dispatcher.applyOne(QLatin1String(tool), {});
        QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(result.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));
    }

    // find_scenes searches the cache rather than the live analysis, so an empty timeline is a
    // legitimate empty result, not an error.
    const QJsonObject found = dispatcher.applyOne(QStringLiteral("find_scenes"), {});
    QVERIFY(found.value(QStringLiteral("ok")).toBool());
    QCOMPARE(found.value(QStringLiteral("n")).toInt(), 0);

    // Scene detection on a text clip is a bad request, not a crash.
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    const QString id =
        state.mcpCompactClip(state.selectedTrack(), state.selectedClip())
            .value(QStringLiteral("id")).toString();
    const QJsonObject onText = dispatcher.applyOne(
        QStringLiteral("detect_scenes"), QJsonObject{{QStringLiteral("clip"), id}});
    QCOMPARE(onText.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(onText.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
}

void McpTest::sceneWorkflowEndToEnd()
{
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // Four visually distinct three-second shots, so the cuts are at 3, 6 and 9 seconds.
    QStringList parts;
    for (int i = 0; i < 4; ++i) {
        const QString part = dir.filePath(QStringLiteral("part%1.mp4").arg(i));
        QProcess proc;
        proc.start(ffmpeg, {QStringLiteral("-y"),
                            QStringLiteral("-f"), QStringLiteral("lavfi"),
                            QStringLiteral("-i"),
                            QStringLiteral("testsrc2=size=320x180:rate=30:duration=3"),
                            QStringLiteral("-filter:v"),
                            QStringLiteral("hue=h=%1").arg(i * 90),
                            QStringLiteral("-c:v"), QStringLiteral("libx264"),
                            QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), part});
        QVERIFY(proc.waitForFinished(120000) && proc.exitCode() == 0);
        parts.append(part);
    }

    const QString listFile = dir.filePath(QStringLiteral("list.txt"));
    {
        QFile f(listFile);
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (const QString &part : parts)
            f.write(QStringLiteral("file '%1'\n").arg(part).toUtf8());
    }
    const QString source = dir.filePath(QStringLiteral("cuts.mp4"));
    {
        QProcess proc;
        proc.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("concat"),
                            QStringLiteral("-safe"), QStringLiteral("0"),
                            QStringLiteral("-i"), listFile,
                            QStringLiteral("-c"), QStringLiteral("copy"), source});
        QVERIFY(proc.waitForFinished(120000) && proc.exitCode() == 0);
    }

    AssetLibrary library;
    AppController state(&library);

    drift::Project &project = *state.project();
    drift::Track track{.type = drift::TrackType::Video};
    drift::Clip clip;
    clip.id = QStringLiteral("scene-clip");
    clip.type = drift::ClipType::Video;
    clip.path = source;
    clip.timelineStart = 0;
    clip.timelineDuration = drift::secondsToUs(12.0);
    clip.srcIn = 0;
    clip.srcOut = drift::secondsToUs(12.0);
    track.clips.append(clip);
    project.tracks().clear();
    project.tracks().append(track);

    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject clipRef{{QStringLiteral("clip"), QStringLiteral("scene-clip")}};

    QSignalSpy finished(&state, &AppController::sceneDetectionFinished);
    const QJsonObject started = dispatcher.applyOne(QStringLiteral("detect_scenes"), clipRef);
    QVERIFY2(started.value(QStringLiteral("ok")).toBool(),
             qPrintable(started.value(QStringLiteral("detail")).toString()));

    // Either it started asynchronously, or a previous run left a usable cache entry.
    if (!started.value(QStringLiteral("cached")).toBool()) {
        QVERIFY(started.value(QStringLiteral("started")).toBool());
        QVERIFY2(finished.wait(180000), "scene detection did not finish");
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));
    }

    // The async job is advertised where the catalog says it is.
    const QJsonObject inspect = dispatcher.inspect(
        QJsonObject{{QStringLiteral("detail"), true}});
    const QJsonObject sceneState =
        inspect.value(QStringLiteral("jobs")).toObject().value(QStringLiteral("sceneDetect")).toObject();
    QCOMPARE(sceneState.value(QStringLiteral("active")).toBool(), false);
    QCOMPARE(sceneState.value(QStringLiteral("clip")).toString(), QStringLiteral("scene-clip"));

    const QJsonObject listed = dispatcher.applyOne(QStringLiteral("list_scenes"), {});
    QVERIFY(listed.value(QStringLiteral("ok")).toBool());
    const QJsonArray scenes = listed.value(QStringLiteral("scenes")).toArray();
    QCOMPARE(scenes.size(), 4);

    // Boundaries land on the real cuts, and the timeline mapping is filled in.
    const double expected[] = {0.0, 3.0, 6.0, 9.0};
    for (int i = 0; i < scenes.size(); ++i) {
        const QJsonObject scene = scenes.at(i).toObject();
        QVERIFY2(std::abs(scene.value(QStringLiteral("start")).toDouble() - expected[i]) < 0.1,
                 qPrintable(QStringLiteral("scene %1 starts at %2")
                                .arg(i)
                                .arg(scene.value(QStringLiteral("start")).toDouble())));
        // This clip is untrimmed and unretimed, so source and timeline times coincide.
        QVERIFY(std::abs(scene.value(QStringLiteral("timeline_start")).toDouble()
                         - scene.value(QStringLiteral("start")).toDouble())
                < 0.01);
    }

    // Asking for labels without the model must fail with a message naming what to install,
    // so an agent can relay it instead of retrying. The dispatcher resolves the clip before
    // the op sees with_objects, which is why this needs a real clip rather than an empty ref.
    if (!drift::ObjectDetector::modelPresent()) {
        QJsonObject labelled = clipRef;
        labelled.insert(QStringLiteral("with_objects"), true);
        const QJsonObject refused = dispatcher.applyOne(QStringLiteral("detect_scenes"), labelled);
        QCOMPARE(refused.value(QStringLiteral("ok")).toBool(), false);
        QVERIFY2(refused.value(QStringLiteral("detail")).toString().contains(
                     QStringLiteral("object-model")),
                 qPrintable(refused.value(QStringLiteral("detail")).toString()));
    }

    // describe_clip summarises the same analysis in one call.
    const QJsonObject described = dispatcher.applyOne(QStringLiteral("describe_clip"), {});
    QVERIFY(described.value(QStringLiteral("ok")).toBool());
    QCOMPARE(described.value(QStringLiteral("scenes")).toInt(), 4);
    QCOMPARE(described.value(QStringLiteral("cuts")).toInt(), 3);
    QCOMPARE(described.value(QStringLiteral("objects_scanned")).toBool(), false);

    // find_scenes reaches the same clip through the on-disk cache.
    const QJsonObject found = dispatcher.applyOne(QStringLiteral("find_scenes"), {});
    QVERIFY(found.value(QStringLiteral("ok")).toBool());
    QCOMPARE(found.value(QStringLiteral("n")).toInt(), 4);

    // bookmark_scenes marks the three interior boundaries, not the clip's own start.
    const QJsonObject marked = dispatcher.applyOne(QStringLiteral("bookmark_scenes"), clipRef);
    QVERIFY(marked.value(QStringLiteral("ok")).toBool());
    QCOMPARE(marked.value(QStringLiteral("added")).toInt(), 3);

    // split_on_scenes: the id passed in still names the first piece, and however many cuts it
    // made it collapses into a single undo step.
    const QJsonObject split = dispatcher.applyOne(QStringLiteral("split_on_scenes"), clipRef);
    QVERIFY2(split.value(QStringLiteral("ok")).toBool(),
             qPrintable(split.value(QStringLiteral("detail")).toString()));
    QCOMPARE(split.value(QStringLiteral("clips")).toArray().size(), 4);
    QCOMPARE(split.value(QStringLiteral("clips")).toArray().at(0).toString(),
             QStringLiteral("scene-clip"));
    QCOMPARE(project.tracks().at(0).clips.size(), 4);

    QVERIFY(state.undoAvailable());
    state.undo();
    QCOMPARE(project.tracks().at(0).clips.size(), 1);
}

void McpTest::aiCapabilitiesReportsMissingModels()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject result = dispatcher.applyOne(QStringLiteral("ai_capabilities"), {});
    QVERIFY(result.value(QStringLiteral("ok")).toBool());

    const QJsonArray models = result.value(QStringLiteral("models")).toArray();
    QVERIFY(models.size() >= 5);

    bool sawObjectModel = false;
    for (const QJsonValue &v : models) {
        const QJsonObject model = v.toObject();
        QVERIFY(model.contains(QStringLiteral("installed")));
        QVERIFY(!model.value(QStringLiteral("unlocks")).toString().isEmpty());
        if (model.value(QStringLiteral("kind")).toString() == QLatin1String("object-model")) {
            sawObjectModel = true;
            // Whether it is installed depends on the machine, so assert the report agrees
            // with reality rather than baking in either answer — claiming a capability that
            // then fails is the bug this op exists to prevent.
            QCOMPARE(model.value(QStringLiteral("installed")).toBool(),
                     drift::ObjectDetector::modelPresent());
        }
    }
    QVERIFY(sawObjectModel);

    // The runtime matters as much as the models: a model with nothing to execute it is not a
    // capability, so the report must name one either way.
    QVERIFY(!result.value(QStringLiteral("runtime")).toString().isEmpty());
}

void McpTest::undoExemptOpsMatchCatalogLimitations()
{
    const QJsonObject cat = drift::mcp::catalogPayload();
    QString blob;
    for (const QJsonValue &v : cat.value(QStringLiteral("limitations")).toArray())
        blob += v.toString() + QLatin1Char(' ');
    for (const QString &op : drift::mcp::undoExemptOps())
        QVERIFY2(blob.contains(op), qPrintable(op));
}

void McpTest::selectionBasedOpsExcludePlayheadOps()
{
    const QStringList ops = drift::mcp::selectionBasedOps();
    QVERIFY(ops.contains(QStringLiteral("separate_audio")));
    QVERIFY(ops.contains(QStringLiteral("copy_selection")));
    QVERIFY(!ops.contains(QStringLiteral("freeze_frame")));
    QVERIFY(!ops.contains(QStringLiteral("paste_at_playhead")));

    const QJsonObject cat = drift::mcp::catalogPayload({{QStringLiteral("guide"), true}});
    const QString guide = cat.value(QStringLiteral("guide")).toString();
    QVERIFY(guide.contains(QStringLiteral("freeze_frame and paste_at_playhead are playhead-based")));
}

void McpTest::inspectReportsSelectionAndUndo()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject empty = dispatcher.inspect({});
    QVERIFY(empty.value(QStringLiteral("ok")).toBool());
    QVERIFY(!empty.contains(QStringLiteral("selection")));
    const QJsonObject undo0 = empty.value(QStringLiteral("undo")).toObject();
    QCOMPARE(undo0.value(QStringLiteral("can")).toBool(), false);
    QCOMPARE(undo0.value(QStringLiteral("depth")).toInt(), 0);

    const QJsonObject added = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("Hello")}, {QStringLiteral("at"), 0.0}});
    QVERIFY(added.value(QStringLiteral("ok")).toBool());
    const QString id = added.value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());

    const QJsonObject withSel = dispatcher.inspect({});
    const QJsonObject sel = withSel.value(QStringLiteral("selection")).toObject();
    QCOMPARE(sel.value(QStringLiteral("clip")).toString(), id);
    QVERIFY(sel.contains(QStringLiteral("track")));
    QVERIFY(sel.contains(QStringLiteral("index")));
    const QJsonObject undo1 = withSel.value(QStringLiteral("undo")).toObject();
    QCOMPARE(undo1.value(QStringLiteral("can")).toBool(), true);
    QVERIFY(undo1.value(QStringLiteral("depth")).toInt() >= 1);

    QVERIFY(dispatcher.applyOne(QStringLiteral("clear_selection"), {}).value(QStringLiteral("ok")).toBool());
    QVERIFY(!dispatcher.inspect({}).contains(QStringLiteral("selection")));
}

void McpTest::listHistoryLabelsApplyBatch()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject batch = dispatcher.apply(
        {{QStringLiteral("ops"),
          QJsonArray{QJsonObject{{QStringLiteral("tool"), QStringLiteral("add_text")},
                                 {QStringLiteral("args"),
                                  QJsonObject{{QStringLiteral("text"), QStringLiteral("A")},
                                              {QStringLiteral("at"), 0.0}}}}}}});
    QVERIFY(batch.value(QStringLiteral("ok")).toBool());

    const QJsonObject history = dispatcher.applyOne(QStringLiteral("list_history"), {});
    QVERIFY(history.value(QStringLiteral("ok")).toBool());
    const QJsonArray entries = history.value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.last().toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("Origin"));
    QCOMPARE(history.value(QStringLiteral("current")).toInt(), 1);
    const QString label = entries.first().toObject().value(QStringLiteral("label")).toString();
    QVERIFY2(label.contains(QStringLiteral("add_text")), qPrintable(label));
    const QString shortHash = entries.first().toObject().value(QStringLiteral("short")).toString();
    QCOMPARE(shortHash.size(), 12);
    QCOMPARE(history.value(QStringLiteral("hash")).toString().size(), 64);
    QCOMPARE(history.value(QStringLiteral("short")).toString(), shortHash);
    QVERIFY(history.value(QStringLiteral("hash")).toString().startsWith(shortHash));
}

void McpTest::undoToRestoresSnapshot()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("A")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("B")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());

    const QJsonObject history = dispatcher.applyOne(QStringLiteral("list_history"), {});
    QCOMPARE(history.value(QStringLiteral("current")).toInt(), 2);
    QCOMPARE(dispatcher.inspect({}).value(QStringLiteral("clips")).toInt(), 2);

    const QJsonObject jumped = dispatcher.applyOne(QStringLiteral("undo_to"),
                                                   {{QStringLiteral("index"), 1}});
    QVERIFY(jumped.value(QStringLiteral("ok")).toBool());
    QCOMPARE(jumped.value(QStringLiteral("index")).toInt(), 1);
    QCOMPARE(dispatcher.inspect({}).value(QStringLiteral("clips")).toInt(), 1);
}

void McpTest::setRippleIsNotUndoable()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject result = dispatcher.applyOne(QStringLiteral("set_ripple"),
                                                   {{QStringLiteral("enabled"), true}});
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
    QCOMPARE(result.value(QStringLiteral("ripple")).toBool(), true);
    QCOMPARE(state.rippleEnabled(), true);
    QVERIFY(!state.undoAvailable());
}

void McpTest::closeGapClosesOneHole()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    QVERIFY(dispatcher.applyOne(QStringLiteral("set_overlap"), {{QStringLiteral("enabled"), true}})
                .value(QStringLiteral("ok"))
                .toBool());

    const QJsonObject a = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("A")}, {QStringLiteral("at"), 0.0}});
    QVERIFY(a.value(QStringLiteral("ok")).toBool());
    const QJsonObject b = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("B")}, {QStringLiteral("at"), 10.0}});
    QVERIFY(b.value(QStringLiteral("ok")).toBool());
    QCOMPARE(b.value(QStringLiteral("start")).toDouble(), 10.0);

    const int track = b.value(QStringLiteral("track")).toInt();
    const double gapAt = a.value(QStringLiteral("start")).toDouble()
                         + a.value(QStringLiteral("dur")).toDouble();
    const QJsonObject closed = dispatcher.applyOne(
        QStringLiteral("close_gap"),
        {{QStringLiteral("track"), track}, {QStringLiteral("at"), gapAt}});
    QVERIFY(closed.value(QStringLiteral("ok")).toBool());

    const QJsonObject inspect = dispatcher.inspect({{QStringLiteral("clips"), true}});
    const QJsonArray items = inspect.value(QStringLiteral("tracks")).toArray().at(track).toObject()
                                 .value(QStringLiteral("items")).toArray();
    QVERIFY(items.size() >= 2);
    const QString bid = b.value(QStringLiteral("id")).toString();
    bool found = false;
    for (const QJsonValue &v : items) {
        const QJsonObject clip = v.toObject();
        if (clip.value(QStringLiteral("id")).toString() != bid)
            continue;
        found = true;
        QCOMPARE(clip.value(QStringLiteral("start")).toDouble(), gapAt);
    }
    QVERIFY(found);
}

void McpTest::saveProjectWithoutPathUsesCurrent()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject neverSaved = dispatcher.applyOne(QStringLiteral("save_project"), {});
    QCOMPARE(neverSaved.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(neverSaved.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("named.drift"));
    QVERIFY(dispatcher.applyOne(QStringLiteral("save_project"), {{QStringLiteral("path"), path}})
                .value(QStringLiteral("ok"))
                .toBool());

    const QJsonObject again = dispatcher.applyOne(QStringLiteral("save_project"), {});
    QVERIFY2(again.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(again).toJson(QJsonDocument::Compact)));
    QCOMPARE(again.value(QStringLiteral("path")).toString(), path);

    // saveAs has to name the copy: falling back to the current path would rewrite the project it
    // was asked to leave alone.
    const QJsonObject noPath = dispatcher.applyOne(QStringLiteral("save_project"),
                                                  {{QStringLiteral("saveAs"), true}});
    QCOMPARE(noPath.value(QStringLiteral("ok")).toBool(), false);
    const QJsonObject samePath =
        dispatcher.applyOne(QStringLiteral("save_project"),
                            {{QStringLiteral("path"), path}, {QStringLiteral("saveAs"), true}});
    QCOMPARE(samePath.value(QStringLiteral("ok")).toBool(), false);

    const QByteArray originalBytes = readFile(path);
    QVERIFY(!originalBytes.isEmpty());
    const QString copyPath = dir.filePath(QStringLiteral("named-v2.drift"));
    QVERIFY(dispatcher
                .applyOne(QStringLiteral("save_project"),
                          {{QStringLiteral("path"), copyPath}, {QStringLiteral("saveAs"), true}})
                .value(QStringLiteral("ok"))
                .toBool());
    QCOMPARE(state.currentProjectPath(), copyPath);
    QCOMPARE(readFile(path), originalBytes);
}

void McpTest::detectSilenceFindsInjectedGap()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString source = dir.filePath(QStringLiteral("half-tone.wav"));
    QVERIFY(writeHalfSilentTone(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QString clip = importAndPlace(dispatcher, source, 0.0);
    QVERIFY(!clip.isEmpty());

    const QJsonObject result = dispatcher.applyOne(
        QStringLiteral("detect_silence"),
        {{QStringLiteral("clip"), clip},
         {QStringLiteral("threshold"), 0.02},
         {QStringLiteral("min_duration"), 0.35}});
    QVERIFY2(result.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(result).toJson(QJsonDocument::Compact)));
    QCOMPARE(result.value(QStringLiteral("source")).toString(), QStringLiteral("clip"));
    const QJsonArray ranges = result.value(QStringLiteral("ranges")).toArray();
    QVERIFY2(!ranges.isEmpty(), qPrintable(QJsonDocument(result).toJson(QJsonDocument::Compact)));
    const QJsonObject gap = ranges.last().toObject();
    QVERIFY(gap.value(QStringLiteral("start")).toDouble() > 1.5);
    QVERIFY(gap.value(QStringLiteral("end")).toDouble() > 3.0);
}

void McpTest::setEffectStringParamSetsFileParam()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject listed = dispatcher.applyOne(QStringLiteral("list_effects"),
                                                   {{QStringLiteral("id"), QStringLiteral("face_swap")}});
    if (!listed.value(QStringLiteral("ok")).toBool())
        QSKIP("face_swap effect is not in the test catalog");

    const QJsonObject added = dispatcher.applyOne(
        QStringLiteral("add_text"),
        {{QStringLiteral("text"), QStringLiteral("Face")}, {QStringLiteral("at"), 0.0}});
    QVERIFY(added.value(QStringLiteral("ok")).toBool());
    const QString clip = added.value(QStringLiteral("id")).toString();

    const QJsonObject fx = dispatcher.applyOne(
        QStringLiteral("add_effect"),
        {{QStringLiteral("clip"), clip}, {QStringLiteral("effect"), QStringLiteral("face_swap")}});
    QVERIFY2(fx.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(fx).toJson(QJsonDocument::Compact)));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString image = dir.filePath(QStringLiteral("face.png"));
    {
        QFile f(image);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray::fromHex("89504e470d0a1a0a"));
    }

    const QJsonObject set = dispatcher.applyOne(
        QStringLiteral("set_effect_string_param"),
        {{QStringLiteral("clip"), clip},
         {QStringLiteral("index"), 0},
         {QStringLiteral("key"), QStringLiteral("sourceImage")},
         {QStringLiteral("value"), image}});
    QVERIFY2(set.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(set).toJson(QJsonDocument::Compact)));
}

void McpTest::addShapeReturnsMintedId()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject shapes = dispatcher.applyOne(QStringLiteral("list_shapes"), {});
    QVERIFY(shapes.value(QStringLiteral("ok")).toBool());
    const QJsonArray list = shapes.value(QStringLiteral("shapes")).toArray();
    if (list.isEmpty())
        QSKIP("no builtin shapes");
    const QString shapeId = list.at(0).toObject().value(QStringLiteral("id")).toString();
    QVERIFY(!shapeId.isEmpty());

    const QJsonObject added = dispatcher.applyOne(
        QStringLiteral("add_shape"),
        {{QStringLiteral("shape"), shapeId}, {QStringLiteral("at"), 0.0}});
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(added).toJson(QJsonDocument::Compact)));
    QVERIFY(!added.value(QStringLiteral("id")).toString().isEmpty());
    QCOMPARE(added.value(QStringLiteral("n")).toInt(), 1);
}

namespace {

QString lottieFixture()
{
    QFile file(QStringLiteral(DRIFT_TEST_DATA_DIR "/vector/slide.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

int totalClips(AppController &s)
{
    int n = 0;
    for (const QVariant &t : s.tracks())
        n += t.toMap().value(QStringLiteral("clips")).toList().size();
    return n;
}

} // namespace

void McpTest::motionToolboxIsCatalogued()
{
    QVERIFY(drift::mcp::toolboxNames().contains(QStringLiteral("motion")));
    for (const char *op : {"add_lottie", "add_svg", "inspect_lottie", "set_lottie_source", "set_lottie_options",
                           "set_lottie_slot", "list_lottie_slots", "get_lottie_source"}) {
        QVERIFY2(drift::mcp::isKnownOp(QLatin1String(op)), op);
        QCOMPARE(drift::mcp::toolboxForOp(QLatin1String(op)), QStringLiteral("motion"));
    }
    QVERIFY(drift::mcp::isReadOnlyOp(QStringLiteral("inspect_lottie")));
    QVERIFY(drift::mcp::isReadOnlyOp(QStringLiteral("list_lottie_slots")));
    QVERIFY(drift::mcp::isReadOnlyOp(QStringLiteral("get_lottie_source")));
    QVERIFY(!drift::mcp::isReadOnlyOp(QStringLiteral("add_lottie")));
    const QJsonObject box = drift::mcp::toolboxPayload(QStringLiteral("motion"));
    QVERIFY(box.value(QStringLiteral("ok")).toBool());
}

void McpTest::addLottieReportsDocument()
{
    AssetLibrary library;
    AppController state(&library);
    if (!state.vectorSupportAvailable())
        QSKIP("built without Skia");
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject added = dispatcher.applyOne(
        QStringLiteral("add_lottie"),
        {{QStringLiteral("json"), lottieFixture()}, {QStringLiteral("at"), 1.0},
         {QStringLiteral("loop"), QStringLiteral("loop")},
         {QStringLiteral("slots"), QJsonObject{{QStringLiteral("accent"), QStringLiteral("#0000ff")}}}});
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(added).toJson(QJsonDocument::Compact)));
    const QString id = added.value(QStringLiteral("id")).toString();
    QVERIFY(!id.isEmpty());
    QCOMPARE(added.value(QStringLiteral("durationSec")).toDouble(), 2.0);
    QCOMPARE(added.value(QStringLiteral("width")).toInt(), 200);
    QCOMPARE(added.value(QStringLiteral("height")).toInt(), 100);
    QCOMPARE(added.value(QStringLiteral("fps")).toDouble(), 30.0);
    QCOMPARE(added.value(QStringLiteral("slots")).toArray().size(), 1);
    QCOMPARE(added.value(QStringLiteral("slots")).toArray().at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("accent"));
    QVERIFY(added.value(QStringLiteral("unsupported")).toArray().isEmpty());
    QVERIFY(!added.contains(QStringLiteral("slotErrors")));
    QVERIFY(!added.contains(QStringLiteral("ignored")));

    // The clip is on a graphic track, runs the animation's own length, and carries the override.
    const QPair<int, int> loc = state.mcpLocateClip(id);
    QVERIFY(loc.first >= 0);
    const QVariantMap clip = state.clipAt(loc.first, loc.second);
    QCOMPARE(clip.value(QStringLiteral("kind")).toString(), QStringLiteral("vector"));
    QCOMPARE(clip.value(QStringLiteral("duration")).toDouble(), 2.0);
    const QVariantMap vector = clip.value(QStringLiteral("vector")).toMap();
    QCOMPARE(vector.value(QStringLiteral("loop")).toString(), QStringLiteral("loop"));
    QVERIFY(vector.value(QStringLiteral("inline")).toBool());
    QCOMPARE(vector.value(QStringLiteral("slots")).toMap().value(QStringLiteral("accent")).toString(), QStringLiteral("#ff0000ff"));

    const QJsonObject listed = dispatcher.applyOne(QStringLiteral("list_lottie_slots"), {{QStringLiteral("clip"), id}});
    QVERIFY(listed.value(QStringLiteral("ok")).toBool());
    QCOMPARE(listed.value(QStringLiteral("n")).toInt(), 1);
    QCOMPARE(listed.value(QStringLiteral("slots")).toArray().at(0).toObject().value(QStringLiteral("value")).toString(), QStringLiteral("#ff0000ff"));

    const QJsonObject source = dispatcher.applyOne(QStringLiteral("get_lottie_source"), {{QStringLiteral("clip"), id}});
    QVERIFY(source.value(QStringLiteral("ok")).toBool());
    QVERIFY(source.value(QStringLiteral("source")).toString().contains(QStringLiteral("\"nm\": \"Slide\"")));
    QCOMPARE(source.value(QStringLiteral("hash")).toString().size(), 64);

    // Options patch, then an inspect through the clip reference.
    const QJsonObject opts = dispatcher.applyOne(QStringLiteral("set_lottie_options"),
                                                 {{QStringLiteral("clip"), id}, {QStringLiteral("fit"), QStringLiteral("cover")}, {QStringLiteral("offset"), 0.5}});
    QVERIFY2(opts.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(opts).toJson(QJsonDocument::Compact)));
    const QVariantMap after = state.clipAt(loc.first, loc.second).value(QStringLiteral("vector")).toMap();
    QCOMPARE(after.value(QStringLiteral("fit")).toString(), QStringLiteral("cover"));
    QCOMPARE(after.value(QStringLiteral("offset")).toDouble(), 0.5);
    const QJsonObject inspected = dispatcher.applyOne(QStringLiteral("inspect_lottie"), {{QStringLiteral("clip"), id}});
    QVERIFY(inspected.value(QStringLiteral("ok")).toBool());
    QCOMPARE(inspected.value(QStringLiteral("layers")).toArray().size(), 1);

    // Garbage is refused up front, not placed.
    const int before = totalClips(state);
    const QJsonObject bad = dispatcher.applyOne(QStringLiteral("add_lottie"), {{QStringLiteral("json"), QStringLiteral("{\"nope\":1}")}});
    QVERIFY(!bad.value(QStringLiteral("ok")).toBool());
    QCOMPARE(totalClips(state), before);
}

void McpTest::setLottieSlotValidatesTypes()
{
    AssetLibrary library;
    AppController state(&library);
    if (!state.vectorSupportAvailable())
        QSKIP("built without Skia");
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject added = dispatcher.applyOne(QStringLiteral("add_lottie"),
                                                  {{QStringLiteral("json"), lottieFixture()}, {QStringLiteral("at"), 0.0}});
    QVERIFY(added.value(QStringLiteral("ok")).toBool());
    const QString id = added.value(QStringLiteral("id")).toString();

    QJsonObject r = dispatcher.applyOne(QStringLiteral("set_lottie_slot"),
                                        {{QStringLiteral("clip"), id}, {QStringLiteral("name"), QStringLiteral("accent")}, {QStringLiteral("value"), 3.5}});
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
    QVERIFY(r.value(QStringLiteral("detail")).toString().contains(QStringLiteral("color")));

    r = dispatcher.applyOne(QStringLiteral("set_lottie_slot"),
                            {{QStringLiteral("clip"), id}, {QStringLiteral("name"), QStringLiteral("nosuch")}, {QStringLiteral("value"), QStringLiteral("#ff0000")}});
    QVERIFY(!r.value(QStringLiteral("ok")).toBool());
    QVERIFY(r.value(QStringLiteral("detail")).toString().contains(QStringLiteral("accent")));

    r = dispatcher.applyOne(QStringLiteral("set_lottie_slot"),
                            {{QStringLiteral("clip"), id}, {QStringLiteral("name"), QStringLiteral("accent")},
                             {QStringLiteral("value"), QJsonArray{0.0, 1.0, 0.0}}});
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(r).toJson(QJsonDocument::Compact)));
    const QPair<int, int> loc = state.mcpLocateClip(id);
    QCOMPARE(state.clipAt(loc.first, loc.second).value(QStringLiteral("vector")).toMap()
                 .value(QStringLiteral("slots")).toMap().value(QStringLiteral("accent")).toString(),
             QStringLiteral("#ff00ff00"));

    r = dispatcher.applyOne(QStringLiteral("set_lottie_slot"),
                            {{QStringLiteral("clip"), id}, {QStringLiteral("name"), QStringLiteral("accent")}, {QStringLiteral("value"), QJsonValue::Null}});
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    QVERIFY(r.value(QStringLiteral("cleared")).toBool());
    QVERIFY(state.clipAt(loc.first, loc.second).value(QStringLiteral("vector")).toMap()
                .value(QStringLiteral("slots")).toMap().isEmpty());
}

void McpTest::inspectLottieAddsNothing()
{
    AssetLibrary library;
    AppController state(&library);
    if (!state.vectorSupportAvailable())
        QSKIP("built without Skia");
    drift::mcp::McpDispatcher dispatcher(&state);
    const int before = totalClips(state);
    const bool undoBefore = state.undoAvailable();

    const QJsonObject report = dispatcher.applyOne(QStringLiteral("inspect_lottie"), {{QStringLiteral("json"), lottieFixture()}});
    QVERIFY2(report.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(report).toJson(QJsonDocument::Compact)));
    QCOMPARE(report.value(QStringLiteral("durationSec")).toDouble(), 2.0);
    QCOMPARE(report.value(QStringLiteral("markers")).toArray().size(), 2);
    QCOMPARE(totalClips(state), before);
    QCOMPARE(state.undoAvailable(), undoBefore);

    const QJsonObject svg = dispatcher.applyOne(
        QStringLiteral("inspect_lottie"),
        {{QStringLiteral("svg"), QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 5\"><rect width=\"5\" height=\"5\"><animate attributeName=\"x\" to=\"5\" dur=\"1s\"/></rect></svg>")}});
    QVERIFY(svg.value(QStringLiteral("ok")).toBool());
    QCOMPARE(svg.value(QStringLiteral("kind")).toString(), QStringLiteral("svg"));
    QCOMPARE(svg.value(QStringLiteral("unsupported")).toArray().size(), 1);

    const QJsonObject bad = dispatcher.applyOne(QStringLiteral("inspect_lottie"), {{QStringLiteral("json"), QStringLiteral("nonsense")}});
    QVERIFY(!bad.value(QStringLiteral("ok")).toBool());
}

void McpTest::addSvgShowsInCapture()
{
    AssetLibrary library;
    AppController state(&library);
    if (!state.vectorSupportAvailable())
        QSKIP("built without Skia");
    state.setProjectResolution(160, 90);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject added = dispatcher.applyOne(
        QStringLiteral("add_svg"),
        {{QStringLiteral("svg"), QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"160\" height=\"90\"><rect width=\"160\" height=\"90\" fill=\"#00ff00\"/></svg>")},
         {QStringLiteral("at"), 0.0}, {QStringLiteral("fit"), QStringLiteral("stretch")}});
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(added).toJson(QJsonDocument::Compact)));
    QCOMPARE(added.value(QStringLiteral("kind")).toString(), QStringLiteral("svg"));
    QCOMPARE(added.value(QStringLiteral("width")).toInt(), 160);

    const QJsonObject capture = state.mcpCaptureFrame(0.5, false);
    if (capture.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    QByteArray jpeg;
    for (const QJsonValue &part : capture.value(QStringLiteral("content")).toArray()) {
        if (part.toObject().value(QStringLiteral("type")).toString() == QLatin1String("image"))
            jpeg = QByteArray::fromBase64(part.toObject().value(QStringLiteral("data")).toString().toLatin1());
    }
    QImage frame;
    QVERIFY(frame.loadFromData(jpeg, "JPEG"));
    const QRgb centre = frame.pixel(frame.width() / 2, frame.height() / 2);
    QVERIFY2(qGreen(centre) > 180 && qRed(centre) < 80 && qBlue(centre) < 80,
             qPrintable(QString::number(centre, 16)));
}

void McpTest::lottieBatchUndoesAsOneStep()
{
    AssetLibrary library;
    AppController state(&library);
    if (!state.vectorSupportAvailable())
        QSKIP("built without Skia");
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject added = dispatcher.applyOne(QStringLiteral("add_lottie"),
                                                  {{QStringLiteral("json"), lottieFixture()}, {QStringLiteral("at"), 0.0}});
    QVERIFY(added.value(QStringLiteral("ok")).toBool());
    const QString id = added.value(QStringLiteral("id")).toString();
    const QPair<int, int> loc = state.mcpLocateClip(id);

    const QJsonObject batch = dispatcher.apply(QJsonObject{
        {QStringLiteral("ops"),
         QJsonArray{
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("set_lottie_slot")},
                         {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip"), id}, {QStringLiteral("name"), QStringLiteral("accent")},
                                                              {QStringLiteral("value"), QStringLiteral("#00ff00")}}}},
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("set_lottie_options")},
                         {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip"), id}, {QStringLiteral("loop"), QStringLiteral("pingpong")}}}},
         }},
    });
    QVERIFY2(batch.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(batch).toJson(QJsonDocument::Compact)));
    QVariantMap vector = state.clipAt(loc.first, loc.second).value(QStringLiteral("vector")).toMap();
    QCOMPARE(vector.value(QStringLiteral("loop")).toString(), QStringLiteral("pingpong"));
    QCOMPARE(vector.value(QStringLiteral("slots")).toMap().size(), 1);

    state.undo();
    vector = state.clipAt(loc.first, loc.second).value(QStringLiteral("vector")).toMap();
    QCOMPARE(vector.value(QStringLiteral("loop")).toString(), QStringLiteral("hold"));
    QVERIFY(vector.value(QStringLiteral("slots")).toMap().isEmpty());
}

// The style schema now spells out every key setTextStyle accepts, so an animation patch is
// applied rather than listed under ignored.
void McpTest::setTextStyleAcceptsAnimation()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    const QString id = state.mcpCompactClip(track, clip).value(QStringLiteral("id")).toString();
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject r = dispatcher.applyOne(
        QStringLiteral("set_text"),
        {{QStringLiteral("clip"), id},
         {QStringLiteral("style"), QJsonObject{
             {QStringLiteral("animIn"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("slideUp")}, {QStringLiteral("duration"), 0.8},
                                                     {QStringLiteral("unit"), QStringLiteral("word")}}},
             {QStringLiteral("outlineEnabled"), true},
             {QStringLiteral("outlineWidth"), 3.0},
             {QStringLiteral("accent"), QJsonObject{{QStringLiteral("rule"), QStringLiteral("everyNth")}, {QStringLiteral("n"), 3}}}}}});
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(r).toJson(QJsonDocument::Compact)));
    QVERIFY2(!r.contains(QStringLiteral("ignored")), qPrintable(QJsonDocument(r).toJson(QJsonDocument::Compact)));
    const QVariantMap style = state.clipAt(track, clip).value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("animIn")).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("slideUp"));
    QCOMPARE(style.value(QStringLiteral("animIn")).toMap().value(QStringLiteral("unit")).toString(), QStringLiteral("word"));
    QCOMPARE(style.value(QStringLiteral("outlineWidth")).toDouble(), 3.0);
    QCOMPARE(style.value(QStringLiteral("accent")).toMap().value(QStringLiteral("rule")).toString(), QStringLiteral("everyNth"));
}

void McpTest::textKeyframesThroughSetKeyframe()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Grow"), 0.0);
    const int track = state.selectedTrack();
    const int clip = state.selectedClip();
    const QString id = state.mcpCompactClip(track, clip).value(QStringLiteral("id")).toString();
    drift::mcp::McpDispatcher dispatcher(&state);

    QJsonObject r = dispatcher.applyOne(QStringLiteral("set_keyframe"),
                                        {{QStringLiteral("clip"), id}, {QStringLiteral("prop"), QStringLiteral("text.pixelSize")},
                                         {QStringLiteral("at"), 0.0}, {QStringLiteral("value"), 30.0}});
    QVERIFY2(r.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(r).toJson(QJsonDocument::Compact)));
    r = dispatcher.applyOne(QStringLiteral("set_keyframe"),
                            {{QStringLiteral("clip"), id}, {QStringLiteral("prop"), QStringLiteral("text.pixelSize")},
                             {QStringLiteral("at"), 2.0}, {QStringLiteral("value"), 90.0}});
    QVERIFY(r.value(QStringLiteral("ok")).toBool());
    // camelCase survives normalisation: the key lands on the style, not on "text.pixelsize".
    const QJsonObject listed = dispatcher.applyOne(QStringLiteral("list_keyframes"),
                                                   {{QStringLiteral("clip"), id}, {QStringLiteral("prop"), QStringLiteral("text.pixelSize")}});
    QVERIFY(listed.value(QStringLiteral("ok")).toBool());
    QCOMPARE(listed.value(QStringLiteral("keys")).toArray().size(), 2);
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("text.pixelSize"), 1.0, 0.0), 60.0);
    QVERIFY(state.clipAnimatedProperties(track, clip).contains(QStringLiteral("text.pixelSize")));

    const QVariantMap style = state.clipAt(track, clip).value(QStringLiteral("textStyle")).toMap();
    QCOMPARE(style.value(QStringLiteral("keyframes")).toMap().value(QStringLiteral("pixelSize")).toMap()
                 .value(QStringLiteral("points")).toList().size(), 2);
    // The static scalar mirrors the last key written, so a detail row still reads sensibly.
    QCOMPARE(style.value(QStringLiteral("pixelSize")).toInt(), 90);

    // An unknown text key mints no track (set_keyframe answers ok for unknown props, as it always has).
    dispatcher.applyOne(QStringLiteral("set_keyframe"),
                        {{QStringLiteral("clip"), id}, {QStringLiteral("prop"), QStringLiteral("text.nope")},
                         {QStringLiteral("at"), 0.0}, {QStringLiteral("value"), 1.0}});
    QVERIFY(!state.clipAnimatedProperties(track, clip).contains(QStringLiteral("text.nope")));
    QCOMPARE(state.clipAt(track, clip).value(QStringLiteral("textStyle")).toMap()
                 .value(QStringLiteral("keyframes")).toMap().size(), 1);

    // Colour fan-out.
    state.setClipColorKeyframe(track, clip, QStringLiteral("text.color"), 1.0, QColor(0, 128, 255));
    QCOMPARE(state.propertyValueAt(track, clip, QStringLiteral("text.color.b"), 1.0, 0.0), 1.0);
    QVERIFY(state.clipAnimatedProperties(track, clip).contains(QStringLiteral("text.color.r")));
}

void McpTest::historyEntriesHaveHashes()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject empty = dispatcher.applyOne(QStringLiteral("list_history"), {});
    QCOMPARE(empty.value(QStringLiteral("entries")).toArray().size(), 1);
    QCOMPARE(empty.value(QStringLiteral("current")).toInt(), 0);
    QCOMPARE(empty.value(QStringLiteral("hash")).toString().size(), 64);

    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("A")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());
    const QJsonObject hist = dispatcher.applyOne(QStringLiteral("list_history"), {});
    const QJsonArray entries = hist.value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.size(), 2);
    QVERIFY(entries.at(0).toObject().value(QStringLiteral("short")).toString()
            != entries.at(1).toObject().value(QStringLiteral("short")).toString());
    const QString undoHash = dispatcher.inspect({}).value(QStringLiteral("undo")).toObject()
                                 .value(QStringLiteral("hash")).toString();
    QCOMPARE(undoHash.size(), 12);
    QVERIFY(hist.value(QStringLiteral("hash")).toString().startsWith(undoHash));
}

void McpTest::undoToByHash()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("A")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("B")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());

    const QJsonObject history = dispatcher.applyOne(QStringLiteral("list_history"), {});
    const QJsonArray entries = history.value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(1).toObject().value(QStringLiteral("index")).toInt(), 1);
    const QString prefix = entries.at(1).toObject().value(QStringLiteral("short")).toString();
    QCOMPARE(prefix.size(), 12);

    const QJsonObject jumped = dispatcher.applyOne(QStringLiteral("undo_to"),
                                                   {{QStringLiteral("hash"), prefix}});
    QVERIFY2(jumped.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(jumped).toJson(QJsonDocument::Compact)));
    QCOMPARE(jumped.value(QStringLiteral("index")).toInt(), 1);
    QVERIFY(jumped.value(QStringLiteral("hash")).toString().startsWith(prefix));
    QCOMPARE(dispatcher.inspect({}).value(QStringLiteral("clips")).toInt(), 1);
}

void McpTest::snapshotFileHashMatchesHistory()
{
    QStandardPaths::setTestModeEnabled(true);
    const auto restore = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("Snap")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());

    const QJsonObject history = dispatcher.applyOne(QStringLiteral("list_history"), {});
    const QString hash = history.value(QStringLiteral("hash")).toString();
    QVERIFY(hash.size() == 64);

    const QJsonObject snap = dispatcher.applyOne(QStringLiteral("take_snapshot"),
                                                 {{QStringLiteral("label"), QStringLiteral("keep")}});
    QVERIFY2(snap.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(snap).toJson(QJsonDocument::Compact)));
    QCOMPARE(snap.value(QStringLiteral("hash")).toString(), hash);
    const QString path = snap.value(QStringLiteral("path")).toString();
    QVERIFY(QFile::exists(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray json = file.readAll();
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex()),
             hash);

    const QJsonObject listed = dispatcher.applyOne(QStringLiteral("list_snapshots"), {});
    QVERIFY(listed.value(QStringLiteral("ok")).toBool());
    QVERIFY(listed.value(QStringLiteral("n")).toInt() >= 1);
}

void McpTest::linearHistoryDropsRedo()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("A")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("B")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());

    const QJsonObject before = dispatcher.applyOne(QStringLiteral("list_history"), {});
    QCOMPARE(before.value(QStringLiteral("entries")).toArray().at(0).toObject()
                 .value(QStringLiteral("index")).toInt(), 2);
    const QString dropped = before.value(QStringLiteral("entries")).toArray().at(0).toObject()
                                .value(QStringLiteral("short")).toString();

    QVERIFY(dispatcher.applyOne(QStringLiteral("undo_to"), {{QStringLiteral("index"), 1}})
                .value(QStringLiteral("ok"))
                .toBool());
    QVERIFY(dispatcher.applyOne(QStringLiteral("add_text"),
                                {{QStringLiteral("text"), QStringLiteral("C")},
                                 {QStringLiteral("at"), 0.0}})
                .value(QStringLiteral("ok"))
                .toBool());

    const QJsonObject after = dispatcher.applyOne(QStringLiteral("list_history"), {});
    QCOMPARE(after.value(QStringLiteral("current")).toInt(), 2);
    QCOMPARE(after.value(QStringLiteral("entries")).toArray().size(), 3);
    QStringList hashes;
    for (const QJsonValue &v : after.value(QStringLiteral("entries")).toArray())
        hashes.append(v.toObject().value(QStringLiteral("short")).toString());
    QVERIFY(!hashes.contains(dropped));
    const QJsonObject missing = dispatcher.applyOne(QStringLiteral("undo_to"),
                                                    {{QStringLiteral("hash"), dropped}});
    QCOMPARE(missing.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(missing.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));
}

namespace {

// The framing helpers take a FILE*, so a pipe is not needed: a temporary file seeded
// with the bytes a client would have written reads back identically.
std::FILE *stdioFixture(const QByteArray &input)
{
    std::FILE *f = std::tmpfile();
    if (!f)
        return nullptr;
    std::fwrite(input.constData(), 1, static_cast<size_t>(input.size()), f);
    std::rewind(f);
    return f;
}

QByteArray readAllFrom(std::FILE *f)
{
    std::rewind(f);
    QByteArray out;
    char buf[4096];
    size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        out.append(buf, static_cast<int>(n));
    return out;
}

} // namespace

// #135: Drift used to answer with LSP-style "Content-Length: N\r\n\r\n{...}". The MCP
// stdio transport is newline-delimited JSON, so the official SDK's reader choked on the
// header line and then stalled forever on a body with no trailing newline.
void McpTest::stdioFramingIsNewlineDelimited()
{
    std::FILE *out = std::tmpfile();
    QVERIFY(out);
    const auto close = qScopeGuard([out] { std::fclose(out); });

    drift::mcp::writeStdioMessage(
        out, QByteArray(R"({"jsonrpc":"2.0","id":1,"result":{"ok":true}})"));

    const QByteArray written = readAllFrom(out);
    QVERIFY(!written.contains("Content-Length"));
    QVERIFY(written.endsWith('\n'));
    QCOMPARE(written.count('\n'), 1);

    const QJsonObject parsed = QJsonDocument::fromJson(written.trimmed()).object();
    QCOMPARE(parsed.value(QStringLiteral("id")).toInt(), 1);
}

void McpTest::stdioWriteEscapesEmbeddedNewlines()
{
    std::FILE *out = std::tmpfile();
    QVERIFY(out);
    const auto close = qScopeGuard([out] { std::fclose(out); });

    const QJsonObject body{{QStringLiteral("text"), QStringLiteral("first\nsecond")}};
    drift::mcp::writeStdioMessage(out, QJsonDocument(body).toJson(QJsonDocument::Indented));

    const QByteArray written = readAllFrom(out);
    // Indented input, one line out: the message must not carry a raw newline.
    QCOMPARE(written.count('\n'), 1);
    QCOMPARE(QJsonDocument::fromJson(written).object().value(QStringLiteral("text")).toString(),
             QStringLiteral("first\nsecond"));
}

void McpTest::stdioReadsNewlineDelimited()
{
    const QByteArray first = R"({"jsonrpc":"2.0","id":1,"method":"initialize"})";
    const QByteArray second = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
    std::FILE *in = stdioFixture(first + "\n" + second + "\n");
    QVERIFY(in);
    const auto close = qScopeGuard([in] { std::fclose(in); });

    QCOMPARE(drift::mcp::readStdioMessage(in), first);
    QCOMPARE(drift::mcp::readStdioMessage(in), second);
    QVERIFY(drift::mcp::readStdioMessage(in).isEmpty());
}

// A UTF-8 BOM or a leading blank line used to knock the reader into header-block mode,
// where it waited for a blank line a newline-delimited client never sends.
void McpTest::stdioReadsCrlfBomAndBlankLines()
{
    const QByteArray message = R"({"jsonrpc":"2.0","id":7,"method":"tools/list"})";
    std::FILE *in = stdioFixture(QByteArray("\xEF\xBB\xBF") + "\r\n" + message + "\r\n");
    QVERIFY(in);
    const auto close = qScopeGuard([in] { std::fclose(in); });

    QCOMPARE(drift::mcp::readStdioMessage(in), message);
}

void McpTest::stdioReadsLegacyContentLength()
{
    const QByteArray message = R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})";
    std::FILE *in = stdioFixture("Content-Length: " + QByteArray::number(message.size())
                                 + "\r\nContent-Type: application/json\r\n\r\n" + message);
    QVERIFY(in);
    const auto close = qScopeGuard([in] { std::fclose(in); });

    QCOMPARE(drift::mcp::readStdioMessage(in), message);
}

// The old reader gave up at 64 KB, which an apply({ops:[...]}) batch can exceed.
void McpTest::stdioReadsLargeMessage()
{
    const QJsonObject body{{QStringLiteral("id"), 3},
                           {QStringLiteral("blob"), QString(200000, QLatin1Char('x'))}};
    const QByteArray message = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QVERIFY(message.size() > 64 * 1024);

    std::FILE *in = stdioFixture(message + "\n");
    QVERIFY(in);
    const auto close = qScopeGuard([in] { std::fclose(in); });

    QCOMPARE(drift::mcp::readStdioMessage(in), message);
}

void McpTest::stdioReadsEofAndTrailingMessage()
{
    const QByteArray message = R"({"jsonrpc":"2.0","id":4,"method":"tools/list"})";
    std::FILE *unterminated = stdioFixture(message);
    QVERIFY(unterminated);
    const auto closeOne = qScopeGuard([unterminated] { std::fclose(unterminated); });
    // A client that closes without a trailing newline still gets its last message through.
    QCOMPARE(drift::mcp::readStdioMessage(unterminated), message);
    QVERIFY(drift::mcp::readStdioMessage(unterminated).isEmpty());

    std::FILE *empty = stdioFixture({});
    QVERIFY(empty);
    const auto closeTwo = qScopeGuard([empty] { std::fclose(empty); });
    QVERIFY(drift::mcp::readStdioMessage(empty).isEmpty());
}

// --- discoverability, validation and token diet ------------------------------------------

namespace {

QJsonObject firstTextPayload(const QJsonObject &result)
{
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    return QJsonDocument::fromJson(content.at(0).toObject().value(QStringLiteral("text")).toString().toUtf8())
        .object();
}

QString addTextClip(drift::mcp::McpDispatcher &dispatcher, const QString &text = QStringLiteral("A"))
{
    return dispatcher.applyOne(QStringLiteral("add_text"),
                               {{QStringLiteral("text"), text}, {QStringLiteral("at"), 0.0}})
        .value(QStringLiteral("id"))
        .toString();
}

} // namespace

// Every op the catalog advertises has a handler — with empty args it may fail bad_args or
// not_found, but never unknown_op.
void McpTest::catalogDispatcherParity()
{
    QStandardPaths::setTestModeEnabled(true);
    const auto restore = qScopeGuard([] { QStandardPaths::setTestModeEnabled(false); });

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QStringList names = drift::mcp::opNames();
    QVERIFY(names.size() > 200);
    for (const QString &name : names) {
        const QJsonObject result = dispatcher.applyOne(name, {});
        QVERIFY2(result.value(QStringLiteral("error")).toString() != QLatin1String("unknown_op"), qPrintable(name));
        QVERIFY2(drift::mcp::isKnownOp(name), qPrintable(name));
        QVERIFY2(!drift::mcp::toolboxForOp(name).isEmpty(), qPrintable(name));
    }
    dispatcher.applyOne(QStringLiteral("pause"), {});
}

void McpTest::textResultRoundsNumbers()
{
    const QJsonObject payload{
        {QStringLiteral("ok"), true},
        {QStringLiteral("score"), 0.2154654667582434},
        {QStringLiteral("fps"), 29.97002997},
        {QStringLiteral("n"), 3},
        {QStringLiteral("list"), QJsonArray{1.23456, 2.0}},
        {QStringLiteral("nested"), QJsonObject{{QStringLiteral("pos"), 0.123456789}, {QStringLiteral("t"), 1.00049}}},
    };
    const QJsonObject result = drift::mcp::textResult(payload);
    const QString text = result.value(QStringLiteral("content")).toArray().at(0).toObject()
                             .value(QStringLiteral("text")).toString();
    QVERIFY(!text.contains(QStringLiteral("0.2154654667582434")));
    const QJsonObject parsed = QJsonDocument::fromJson(text.toUtf8()).object();
    QCOMPARE(parsed.value(QStringLiteral("score")).toDouble(), 0.215);
    QCOMPARE(parsed.value(QStringLiteral("fps")).toDouble(), 29.97003);
    QCOMPARE(parsed.value(QStringLiteral("n")).toInt(), 3);
    QCOMPARE(parsed.value(QStringLiteral("list")).toArray().at(0).toDouble(), 1.235);
    QCOMPARE(parsed.value(QStringLiteral("list")).toArray().at(1).toDouble(), 2.0);
    const QJsonObject nested = parsed.value(QStringLiteral("nested")).toObject();
    QCOMPARE(nested.value(QStringLiteral("pos")).toDouble(), 0.123457);
    QCOMPARE(nested.value(QStringLiteral("t")).toDouble(), 1.0);
    QCOMPARE(result.value(QStringLiteral("isError")).toBool(), false);
}

void McpTest::validateRejectsWrongType()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QString clip = addTextClip(dispatcher);
    QVERIFY(!clip.isEmpty());

    const QJsonObject wrong = dispatcher.applyOne(
        QStringLiteral("set_transform"), {{QStringLiteral("clip"), clip}, {QStringLiteral("x"), QStringLiteral("abc")}});
    QCOMPARE(wrong.value(QStringLiteral("error")).toString(), QStringLiteral("type_mismatch"));
    QCOMPARE(wrong.value(QStringLiteral("detail")).toString(),
             QStringLiteral("x: expected number, got string \"abc\""));

    // Numeric strings are what existing agents send, so they still pass.
    const QJsonObject lenient = dispatcher.applyOne(
        QStringLiteral("set_transform"), {{QStringLiteral("clip"), clip}, {QStringLiteral("x"), QStringLiteral("10")}});
    QVERIFY2(lenient.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(lenient).toJson(QJsonDocument::Compact)));
    QCOMPARE(lenient.value(QStringLiteral("x")).toDouble(), 10.0);

    const QJsonObject missing = dispatcher.applyOne(
        QStringLiteral("set_volume"), {{QStringLiteral("clip"), clip}, {QStringLiteral("volume"), 0.5}});
    QCOMPARE(missing.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    const QString detail = missing.value(QStringLiteral("detail")).toString();
    QVERIFY2(detail.startsWith(QStringLiteral("value required — ")), qPrintable(detail));
    QVERIFY2(detail.endsWith(QStringLiteral("(got: clip, volume)")), qPrintable(detail));

    const QJsonObject notAnArray = dispatcher.applyOne(
        QStringLiteral("import_media"), {{QStringLiteral("paths"), QStringLiteral("/tmp/x.mp4")}});
    QCOMPARE(notAnArray.value(QStringLiteral("error")).toString(), QStringLiteral("type_mismatch"));

    // Nothing passed at all is a bad request, not a missing clip.
    const QJsonObject noRef = dispatcher.applyOne(QStringLiteral("reset_transform"), {});
    QCOMPARE(noRef.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QVERIFY(noRef.value(QStringLiteral("detail")).toString().contains(QStringLiteral("track+index")));
    const QJsonObject badId = dispatcher.applyOne(QStringLiteral("reset_transform"),
                                                  {{QStringLiteral("clip"), QStringLiteral("nope")}});
    QCOMPARE(badId.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));
    QVERIFY(badId.value(QStringLiteral("detail")).toString().contains(QStringLiteral("clip nope not found")));
    const QJsonObject badIndex = dispatcher.applyOne(
        QStringLiteral("reset_transform"), {{QStringLiteral("track"), 7}, {QStringLiteral("index"), 3}});
    QCOMPARE(badIndex.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));
    QCOMPARE(badIndex.value(QStringLiteral("detail")).toString(), QStringLiteral("no clip at track 7 index 3"));
}

void McpTest::validateEnumAndRange()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QString clip = addTextClip(dispatcher);

    const QJsonObject badEnum = dispatcher.applyOne(QStringLiteral("list_scenes"),
                                                    {{QStringLiteral("sort"), QStringLiteral("weird")}});
    QCOMPARE(badEnum.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QCOMPARE(badEnum.value(QStringLiteral("detail")).toString(), QStringLiteral("sort must be one of time, score"));

    // Case does not matter for enums; this one then fails downstream for lack of a scan.
    const QJsonObject upper = dispatcher.applyOne(QStringLiteral("list_scenes"),
                                                  {{QStringLiteral("sort"), QStringLiteral("SCORE")}});
    QCOMPARE(upper.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));

    const QJsonObject tooLoud = dispatcher.applyOne(
        QStringLiteral("set_volume"), {{QStringLiteral("clip"), clip}, {QStringLiteral("value"), 3.0}});
    QCOMPARE(tooLoud.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QCOMPARE(tooLoud.value(QStringLiteral("detail")).toString(), QStringLiteral("value must be 0..2"));

    const QJsonObject huge = dispatcher.applyOne(
        QStringLiteral("get_waveform"), {{QStringLiteral("clip"), clip}, {QStringLiteral("buckets"), 99999999}});
    QCOMPARE(huge.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QVERIFY(huge.value(QStringLiteral("detail")).toString().contains(QStringLiteral("1..4096")));

    // Unknown keys are not errors; they are echoed back so a misspelling is visible.
    const QJsonObject extra = dispatcher.applyOne(
        QStringLiteral("set_transform"),
        {{QStringLiteral("clip"), clip}, {QStringLiteral("opacity"), 0.5}, {QStringLiteral("bogus"), 1}});
    QVERIFY2(extra.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(extra).toJson(QJsonDocument::Compact)));
    QCOMPARE(extra.value(QStringLiteral("ignored")).toArray(), QJsonArray{QStringLiteral("bogus")});

    // Nested schemas are checked too.
    const QJsonObject badMask = dispatcher.applyOne(
        QStringLiteral("set_mask"),
        {{QStringLiteral("clip"), clip},
         {QStringLiteral("mask"), QJsonObject{{QStringLiteral("shape"), QStringLiteral("ellipse")},
                                              {QStringLiteral("x"), 5}}}});
    QCOMPARE(badMask.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QCOMPARE(badMask.value(QStringLiteral("detail")).toString(), QStringLiteral("mask.x must be 0..1"));

    // split_clip names the clip range it was aiming at.
    const QJsonObject outside = dispatcher.applyOne(
        QStringLiteral("split_clip"), {{QStringLiteral("clip"), clip}, {QStringLiteral("at"), 500.0}});
    QCOMPARE(outside.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QVERIFY2(outside.value(QStringLiteral("detail")).toString().startsWith(QStringLiteral("at=500 is outside clip [0, ")),
             qPrintable(outside.value(QStringLiteral("detail")).toString()));
}

void McpTest::unknownOpSuggests()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject typo = dispatcher.applyOne(QStringLiteral("set_transformx"), {});
    QCOMPARE(typo.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
    QVERIFY2(typo.value(QStringLiteral("detail")).toString().contains(QStringLiteral("set_transform (canvas)")),
             qPrintable(typo.value(QStringLiteral("detail")).toString()));

    const QJsonObject homepage = dispatcher.applyOne(QStringLiteral("inspect"), {});
    QCOMPARE(homepage.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
    QVERIFY(homepage.value(QStringLiteral("detail")).toString().contains(QStringLiteral("homepage tool")));

    const QJsonObject nothing = dispatcher.applyOne(QStringLiteral("zzzzqqq"), {});
    QCOMPARE(nothing.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
    QVERIFY(nothing.value(QStringLiteral("detail")).toString().contains(QStringLiteral("search")));

    QCOMPARE(drift::mcp::nearestOps(QStringLiteral("split-clip")).value(0), QStringLiteral("split_clip"));

    // Same shape from the protocol layer, which rejects unknown names before the handler runs.
    const QJsonValue reply = drift::mcp::handleJsonRpc(
        rpc(QStringLiteral("tools/call"),
            {{QStringLiteral("name"), QStringLiteral("set_transformx")}, {QStringLiteral("arguments"), QJsonObject{}}}),
        {}, [](const QString &, const QJsonObject &) { return QJsonObject{}; });
    const QJsonObject payload = firstTextPayload(reply.toObject().value(QStringLiteral("result")).toObject());
    QCOMPARE(payload.value(QStringLiteral("error")).toString(), QStringLiteral("unknown_op"));
    QVERIFY(payload.value(QStringLiteral("detail")).toString().contains(QStringLiteral("set_transform")));
}

void McpTest::listEffectsCompactAndById()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject compact = dispatcher.applyOne(QStringLiteral("list_effects"), {});
    QVERIFY(compact.value(QStringLiteral("ok")).toBool());
    const QJsonObject cats = compact.value(QStringLiteral("cats")).toObject();
    QVERIFY(!cats.isEmpty());
    QVERIFY(compact.value(QStringLiteral("n")).toInt() > 0);
    const QByteArray text = QJsonDocument(compact).toJson(QJsonDocument::Compact);
    QVERIFY2(text.size() < 8000, QByteArray::number(text.size()).constData());
    QVERIFY(!text.contains("\"params\""));

    QStringList ids;
    for (auto it = cats.begin(); it != cats.end(); ++it) {
        for (const QJsonValue &row : it.value().toArray())
            ids.append(row.toObject().value(QStringLiteral("id")).toString());
    }
    QVERIFY(!ids.isEmpty());

    const QJsonObject byId = dispatcher.applyOne(QStringLiteral("list_effects"), {{QStringLiteral("id"), ids.first()}});
    QVERIFY(byId.value(QStringLiteral("ok")).toBool());
    QCOMPARE(byId.value(QStringLiteral("effects")).toArray().size(), 1);
    QCOMPARE(byId.value(QStringLiteral("effects")).toArray().at(0).toObject().value(QStringLiteral("id")).toString(),
             ids.first());

    const QJsonObject byCat = dispatcher.applyOne(QStringLiteral("list_effects"), {{QStringLiteral("cat"), cats.begin().key()}});
    QVERIFY(byCat.value(QStringLiteral("ok")).toBool());
    QCOMPARE(byCat.value(QStringLiteral("effects")).toArray().size(), cats.begin().value().toArray().size());

    const QJsonObject byQ = dispatcher.applyOne(QStringLiteral("list_effects"), {{QStringLiteral("q"), ids.first().left(4)}});
    QVERIFY(byQ.value(QStringLiteral("ok")).toBool());
    QVERIFY(byQ.value(QStringLiteral("n")).toInt() >= 1);

    // Aliases: '.' and '_' are interchangeable and case does not matter.
    QString dotted;
    for (const QString &id : ids) {
        if (id.contains(QLatin1Char('.'))) {
            dotted = id;
            break;
        }
    }
    if (!dotted.isEmpty()) {
        QString alias = dotted;
        alias.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QJsonObject resolved = dispatcher.applyOne(QStringLiteral("list_effects"),
                                                         {{QStringLiteral("id"), alias.toUpper()}});
        QVERIFY2(resolved.value(QStringLiteral("ok")).toBool(), qPrintable(alias));
        QCOMPARE(resolved.value(QStringLiteral("effects")).toArray().at(0).toObject().value(QStringLiteral("id")).toString(),
                 dotted);
    }

    const QString clip = addTextClip(dispatcher);
    const QString typo = ids.first() + QLatin1Char('x');
    const QJsonObject added = dispatcher.applyOne(
        QStringLiteral("add_effect"), {{QStringLiteral("clip"), clip}, {QStringLiteral("effect"), typo}});
    QCOMPARE(added.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));
    const QString detail = added.value(QStringLiteral("detail")).toString();
    QVERIFY2(detail.contains(QStringLiteral("did you mean")) && detail.contains(ids.first()), qPrintable(detail));
    QVERIFY2(detail.contains(QStringLiteral("list_effects({q:")), qPrintable(detail));

    const QJsonObject unknown = dispatcher.applyOne(QStringLiteral("list_effects"),
                                                    {{QStringLiteral("id"), QStringLiteral("zzzzz")}});
    QCOMPARE(unknown.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));

    // The same modes exist for the other two catalogs.
    QVERIFY(dispatcher.applyOne(QStringLiteral("list_transitions"), {}).contains(QStringLiteral("cats")));
    QVERIFY(dispatcher.applyOne(QStringLiteral("list_audio_effects"), {}).contains(QStringLiteral("cats")));
}

void McpTest::listEmojiHasIds()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject unfiltered = dispatcher.applyOne(QStringLiteral("list_emoji"), {});
    QCOMPARE(unfiltered.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));

    const QJsonObject smiles = dispatcher.applyOne(QStringLiteral("list_emoji"), {{QStringLiteral("q"), QStringLiteral("smil")}});
    QVERIFY(smiles.value(QStringLiteral("ok")).toBool());
    QVERIFY(smiles.contains(QStringLiteral("groups")));
    const QJsonArray rows = smiles.value(QStringLiteral("emoji")).toArray();
    QVERIFY(rows.size() <= 50);
    for (const QJsonValue &v : rows) {
        const QJsonObject row = v.toObject();
        QVERIFY(!row.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(!row.value(QStringLiteral("label")).toString().isEmpty());
        QVERIFY(!row.contains(QStringLiteral("emoji")));
    }

    const QJsonObject fonts = dispatcher.applyOne(QStringLiteral("list_fonts"), {});
    QVERIFY(fonts.value(QStringLiteral("ok")).toBool());
    const QJsonArray fontRows = fonts.value(QStringLiteral("fonts")).toArray();
    QVERIFY(fontRows.size() <= 100);
    for (const QJsonValue &v : fontRows)
        QVERIFY(!v.toObject().value(QStringLiteral("label")).toString().isEmpty());

    const QJsonObject shapes = dispatcher.applyOne(QStringLiteral("list_shapes"), {{QStringLiteral("q"), QStringLiteral("zzzz-none")}});
    QVERIFY(shapes.value(QStringLiteral("ok")).toBool());
    QCOMPARE(shapes.value(QStringLiteral("n")).toInt(), 0);
}

void McpTest::listHistoryLimitAndShort()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    for (const char *text : {"A", "B", "C"})
        QVERIFY(!addTextClip(dispatcher, QLatin1String(text)).isEmpty());

    const QJsonObject all = dispatcher.applyOne(QStringLiteral("list_history"), {});
    QVERIFY(all.value(QStringLiteral("ok")).toBool());
    const QJsonArray entries = all.value(QStringLiteral("entries")).toArray();
    QCOMPARE(entries.size(), 4);
    QCOMPARE(all.value(QStringLiteral("total")).toInt(), 4);
    QCOMPARE(entries.at(0).toObject().value(QStringLiteral("index")).toInt(), 3);
    QCOMPARE(entries.at(3).toObject().value(QStringLiteral("label")).toString(), QStringLiteral("Origin"));
    const QJsonObject newest = entries.at(0).toObject();
    QCOMPARE(newest.value(QStringLiteral("short")).toString().size(), 12);
    QVERIFY(!newest.contains(QStringLiteral("hash")));
    QVERIFY(!newest.contains(QStringLiteral("snapshot")));
    QCOMPARE(all.value(QStringLiteral("hash")).toString().size(), 64);
    QCOMPARE(all.value(QStringLiteral("short")).toString(), all.value(QStringLiteral("hash")).toString().left(12));

    const QJsonObject two = dispatcher.applyOne(QStringLiteral("list_history"), {{QStringLiteral("limit"), 2}});
    QCOMPARE(two.value(QStringLiteral("entries")).toArray().size(), 2);
    QCOMPARE(two.value(QStringLiteral("total")).toInt(), 4);
    QCOMPARE(two.value(QStringLiteral("entries")).toArray().at(1).toObject().value(QStringLiteral("index")).toInt(), 2);
}

void McpTest::applyFailureNotDuplicated()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject batch = dispatcher.apply(QJsonObject{
        {QStringLiteral("ops"),
         QJsonArray{
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("add_text")},
                         {QStringLiteral("args"), QJsonObject{{QStringLiteral("text"), QStringLiteral("A")}}}},
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("not_real")}},
             QJsonObject{{QStringLiteral("tool"), QStringLiteral("add_text")},
                         {QStringLiteral("args"), QJsonObject{{QStringLiteral("text"), QStringLiteral("B")}}}},
         }},
    });
    QCOMPARE(batch.value(QStringLiteral("error")).toString(), QStringLiteral("apply_failed"));
    QCOMPARE(batch.value(QStringLiteral("stopped")).toInt(), 1);
    QCOMPARE(batch.value(QStringLiteral("tool")).toString(), QStringLiteral("not_real"));
    QCOMPARE(batch.value(QStringLiteral("done")).toArray().size(), 1);
    QCOMPARE(batch.value(QStringLiteral("failed")).toObject().value(QStringLiteral("error")).toString(),
             QStringLiteral("unknown_op"));
    QCOMPARE(dispatcher.inspect({}).value(QStringLiteral("clips")).toInt(), 1);
}

void McpTest::searchFindsOps()
{
    const QJsonObject fade = drift::mcp::searchOps(QStringLiteral("fade"));
    QVERIFY(fade.value(QStringLiteral("ok")).toBool());
    const QJsonArray hits = fade.value(QStringLiteral("hits")).toArray();
    QVERIFY(!hits.isEmpty() && hits.size() <= 8);
    QStringList names;
    for (const QJsonValue &v : hits)
        names.append(v.toObject().value(QStringLiteral("name")).toString());
    QVERIFY2(names.contains(QStringLiteral("set_fade")), qPrintable(names.join(QLatin1Char(' '))));
    QVERIFY(hits.at(0).toObject().value(QStringLiteral("name")).toString().contains(QStringLiteral("fade")));
    QVERIFY(hits.at(0).toObject().contains(QStringLiteral("args")));
    QVERIFY(!hits.at(0).toObject().contains(QStringLiteral("inputSchema")));

    const QJsonObject exact = drift::mcp::searchOps(QStringLiteral("split_clip"), 8, true);
    const QJsonObject first = exact.value(QStringLiteral("hits")).toArray().at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("split_clip"));
    QCOMPARE(first.value(QStringLiteral("toolbox")).toString(), QStringLiteral("timeline"));

    const QJsonObject withSchema = drift::mcp::searchOps(QStringLiteral("snap_clips_to_beats"), 1, true);
    QCOMPARE(withSchema.value(QStringLiteral("hits")).toArray().size(), 1);
    if (withSchema.value(QStringLiteral("n")).toInt() <= 3)
        QVERIFY(withSchema.value(QStringLiteral("hits")).toArray().at(0).toObject().contains(QStringLiteral("inputSchema")));

    QCOMPARE(drift::mcp::searchOps(QStringLiteral("   ")).value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));

    // toolbox({ops}) reaches across toolboxes.
    const QJsonObject picked = drift::mcp::toolboxPayload({}, {QStringLiteral("set_fade"), QStringLiteral("detect_beats"), QStringLiteral("nope")});
    QVERIFY(picked.value(QStringLiteral("ok")).toBool());
    QCOMPARE(picked.value(QStringLiteral("tools")).toArray().size(), 2);
    QCOMPARE(picked.value(QStringLiteral("unknown")).toArray(), QJsonArray{QStringLiteral("nope")});
    QCOMPARE(drift::mcp::toolboxPayload({}).value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
}

void McpTest::captureBeyondEndFlags()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    const double dur = state.durationSeconds();
    QVERIFY(dur > 0.0);

    const QJsonObject result = state.mcpCaptureFrame(dur + 100.0, false);
    if (result.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    const QJsonObject meta = firstTextPayload(result);
    QCOMPARE(meta.value(QStringLiteral("beyond_end")).toBool(), true);
    QCOMPARE(meta.value(QStringLiteral("dur")).toDouble(), dur);

    const QJsonObject inside = state.mcpCaptureFrame(0.0, false);
    QVERIFY(!firstTextPayload(inside).contains(QStringLiteral("beyond_end")));
}

namespace {

// Four visually distinct shots of three seconds each: moving test pattern, colour bars, a
// Mandelbrot zoom, then flat blue. Scene changes at 3, 6 and 9 s.
bool writeFourShotClip(const QString &path)
{
    return runFfmpeg({QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("testsrc2=size=320x180:rate=30:duration=3"),
                      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("smptebars=size=320x180:rate=30:duration=3"),
                      QStringLiteral("-t"), QStringLiteral("3"), QStringLiteral("-f"),
                      QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("mandelbrot=size=320x180:rate=30"),
                      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("color=c=blue:size=320x180:rate=30:duration=3"),
                      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("sine=frequency=440:beep_factor=4:sample_rate=48000:duration=12"),
                      QStringLiteral("-filter_complex"),
                      QStringLiteral("[0:v][1:v][2:v][3:v]concat=n=4:v=1:a=0[v]"),
                      QStringLiteral("-map"), QStringLiteral("[v]"), QStringLiteral("-map"),
                      QStringLiteral("4:a"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
                      QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-c:a"),
                      QStringLiteral("aac"), QStringLiteral("-shortest"), path});
}

QJsonObject firstText(const QJsonObject &raw)
{
    const QJsonArray content = raw.value(QStringLiteral("content")).toArray();
    for (const QJsonValue &v : content) {
        const QJsonObject block = v.toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("text"))
            return QJsonDocument::fromJson(block.value(QStringLiteral("text")).toString().toUtf8()).object();
    }
    return raw;
}

QJsonObject imageBlock(const QJsonObject &raw)
{
    for (const QJsonValue &v : raw.value(QStringLiteral("content")).toArray()) {
        const QJsonObject block = v.toObject();
        if (block.value(QStringLiteral("type")).toString() == QLatin1String("image"))
            return block;
    }
    return {};
}

bool nearAny(const QJsonArray &frames, double target, double tolerance)
{
    for (const QJsonValue &v : frames) {
        if (qAbs(v.toObject().value(QStringLiteral("t")).toDouble() - target) <= tolerance)
            return true;
    }
    return false;
}

} // namespace

void McpTest::framesUniformReturnsN()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject raw = dispatcher.frames({{QStringLiteral("sample"), QStringLiteral("uniform")},
                                               {QStringLiteral("n"), 6},
                                               {QStringLiteral("start"), 0},
                                               {QStringLiteral("end"), 3}});
    if (raw.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    const QJsonObject meta = firstText(raw);
    const QJsonArray frames = meta.value(QStringLiteral("frames")).toArray();
    QCOMPARE(frames.size(), 6);
    QCOMPARE(meta.value(QStringLiteral("grid")).toString(), QStringLiteral("3x2"));
    QCOMPARE(imageBlock(raw).value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/jpeg"));
    QCOMPARE(frames.at(0).toObject().value(QStringLiteral("t")).toDouble(), 0.0);
    double last = -1.0;
    for (const QJsonValue &v : frames) {
        const double t = v.toObject().value(QStringLiteral("t")).toDouble();
        QVERIFY(t > last);
        last = t;
    }
    QVERIFY(meta.value(QStringLiteral("w")).toInt() <= 1456);
}

void McpTest::framesChangesDedupesFourShotClip()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    const QString source = dir.filePath(QStringLiteral("shots.mp4"));
    QVERIFY(writeFourShotClip(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    QVERIFY(!importAndPlace(dispatcher, source, 0.0).isEmpty());

    const QJsonObject raw = dispatcher.frames({{QStringLiteral("sample"), QStringLiteral("changes")},
                                               {QStringLiteral("n"), 12}});
    if (raw.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    const QJsonObject meta = firstText(raw);
    const QJsonArray frames = meta.value(QStringLiteral("frames")).toArray();
    QVERIFY2(frames.size() >= 4 && frames.size() <= 8,
             qPrintable(QJsonDocument(frames).toJson(QJsonDocument::Compact)));
    QVERIFY(meta.value(QStringLiteral("skipped")).toInt() >= 30);
    QVERIFY(nearAny(frames, 3.0, 0.5));
    QVERIFY(nearAny(frames, 6.0, 0.5));
    QVERIFY(nearAny(frames, 9.0, 0.5));
    QCOMPARE(frames.at(0).toObject().value(QStringLiteral("diff")).toInt(), 0);
}

void McpTest::framesAtRendersExactTimes()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    drift::mcp::McpDispatcher dispatcher(&state);
    QVERIFY(dispatcher.applyOne(QStringLiteral("set_duration"), {{QStringLiteral("track"), 0}, {QStringLiteral("index"), 0}, {QStringLiteral("duration"), 12}}).value(QStringLiteral("ok")).toBool());

    const QJsonObject raw = dispatcher.frames({{QStringLiteral("at"), QJsonArray{1, 5, 10.5}}});
    if (raw.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    const QJsonObject meta = firstText(raw);
    QCOMPARE(meta.value(QStringLiteral("sample")).toString(), QStringLiteral("at"));
    const QJsonArray frames = meta.value(QStringLiteral("frames")).toArray();
    QCOMPARE(frames.size(), 3);
    QCOMPARE(frames.at(2).toObject().value(QStringLiteral("t")).toDouble(), 10.5);
}

void McpTest::framesClipModeReportsSourceAndTimeline()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    const QString source = dir.filePath(QStringLiteral("shots.mp4"));
    QVERIFY(writeFourShotClip(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QString clip = importAndPlace(dispatcher, source, 2.0);
    QVERIFY(!clip.isEmpty());
    QVERIFY(dispatcher.applyOne(QStringLiteral("set_trim"),
                                {{QStringLiteral("clip"), clip}, {QStringLiteral("in"), 1.0},
                                 {QStringLiteral("out"), 11.0}})
                .value(QStringLiteral("ok")).toBool());

    const QJsonObject raw = dispatcher.frames({{QStringLiteral("clip"), clip},
                                               {QStringLiteral("sample"), QStringLiteral("uniform")},
                                               {QStringLiteral("n"), 4}});
    QVERIFY2(!raw.value(QStringLiteral("isError")).toBool(),
             qPrintable(QJsonDocument(firstText(raw)).toJson(QJsonDocument::Compact)));
    const QJsonObject meta = firstText(raw);
    QCOMPARE(meta.value(QStringLiteral("space")).toString(), QStringLiteral("source"));
    const QJsonObject first = meta.value(QStringLiteral("frames")).toArray().at(0).toObject();
    QCOMPARE(first.value(QStringLiteral("t")).toDouble(), 1.0);
    QCOMPARE(first.value(QStringLiteral("tl")).toDouble(), 2.0);
}

void McpTest::framesReturnPathWritesJpeg()
{
    QStandardPaths::setTestModeEnabled(true);
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject raw = dispatcher.frames({{QStringLiteral("sample"), QStringLiteral("uniform")},
                                               {QStringLiteral("n"), 2},
                                               {QStringLiteral("return"), QStringLiteral("path")}});
    if (raw.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    const QJsonObject meta = firstText(raw);
    const QString path = meta.value(QStringLiteral("path")).toString();
    QVERIFY(QFile::exists(path));
    const QImage sheet(path);
    QCOMPARE(sheet.width(), meta.value(QStringLiteral("w")).toInt());
    QCOMPARE(sheet.height(), meta.value(QStringLiteral("h")).toInt());
    QFile::remove(path);
}

void McpTest::activityPeaksLandOnCuts()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    const QString source = dir.filePath(QStringLiteral("shots.mp4"));
    QVERIFY(writeFourShotClip(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    QVERIFY(!importAndPlace(dispatcher, source, 0.0).isEmpty());

    const QJsonObject reply = dispatcher.activity({{QStringLiteral("samples"), 120},
                                                   {QStringLiteral("peaks"), 3}});
    if (!reply.value(QStringLiteral("ok")).toBool()
        && reply.value(QStringLiteral("error")).toString() == QLatin1String("capture_failed"))
        QSKIP("Compositor could not produce a frame in this environment");
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    QCOMPARE(reply.value(QStringLiteral("content")).toArray().size(), 120);
    QCOMPARE(reply.value(QStringLiteral("audio")).toArray().size(), 120);
    const double step = reply.value(QStringLiteral("step")).toDouble();
    const QJsonArray peaks = reply.value(QStringLiteral("peaks")).toArray();
    QCOMPARE(peaks.size(), 3);
    for (double cut : {3.0, 6.0, 9.0}) {
        bool found = false;
        for (const QJsonValue &v : peaks)
            found = found || qAbs(v.toObject().value(QStringLiteral("t")).toDouble() - cut) <= step + 0.001;
        QVERIFY2(found, qPrintable(QJsonDocument(peaks).toJson(QJsonDocument::Compact)));
    }
}

void McpTest::activityClipModeSkipsAudioWhenAsked()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    const QString source = dir.filePath(QStringLiteral("shots.mp4"));
    QVERIFY(writeFourShotClip(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QString clip = importAndPlace(dispatcher, source, 0.0);
    QVERIFY(!clip.isEmpty());

    const QJsonObject reply = dispatcher.activity({{QStringLiteral("clip"), clip},
                                                   {QStringLiteral("samples"), 24},
                                                   {QStringLiteral("audio"), false}});
    QVERIFY2(reply.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(reply).toJson(QJsonDocument::Compact)));
    QCOMPARE(reply.value(QStringLiteral("space")).toString(), QStringLiteral("source"));
    QVERIFY(!reply.contains(QStringLiteral("audio")));
    QCOMPARE(reply.value(QStringLiteral("content")).toArray().size(), 24);
}

void McpTest::waveformImageReturnsPngAndSummary()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    const QString source = dir.filePath(QStringLiteral("tone.wav"));
    QVERIFY(writeHalfSilentTone(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    QVERIFY(!importAndPlace(dispatcher, source, 0.0).isEmpty());

    const QJsonObject raw = dispatcher.applyOne(QStringLiteral("get_waveform"),
                                                {{QStringLiteral("start"), 0},
                                                 {QStringLiteral("duration"), 4},
                                                 {QStringLiteral("image"), true},
                                                 {QStringLiteral("spectrogram"), true}});
    QVERIFY2(raw.contains(QStringLiteral("content")),
             qPrintable(QJsonDocument(raw).toJson(QJsonDocument::Compact)));
    QCOMPARE(raw.value(QStringLiteral("content")).toArray().size(), 2);
    QCOMPARE(imageBlock(raw).value(QStringLiteral("mimeType")).toString(), QStringLiteral("image/png"));
    const QJsonObject meta = firstText(raw);
    QCOMPARE(meta.value(QStringLiteral("buckets")).toInt(), 50);
    const QJsonArray silence = meta.value(QStringLiteral("silence")).toArray();
    QCOMPARE(silence.size(), 1);
    const double silentStart = silence.at(0).toObject().value(QStringLiteral("start")).toDouble();
    QVERIFY2(silentStart >= 1.9 && silentStart <= 2.2, qPrintable(QString::number(silentStart)));
    QVERIFY(meta.value(QStringLiteral("image")).toObject().value(QStringLiteral("lanes")).toArray().contains(QStringLiteral("spectrogram")));
}

void McpTest::applyRejectsWaveformImage()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject reply = dispatcher.apply(
        {{QStringLiteral("ops"),
          QJsonArray{QJsonObject{{QStringLiteral("tool"), QStringLiteral("get_waveform")},
                                 {QStringLiteral("args"), QJsonObject{{QStringLiteral("start"), 0},
                                                                      {QStringLiteral("duration"), 1},
                                                                      {QStringLiteral("image"), true}}}}}}});
    QVERIFY(!reply.value(QStringLiteral("ok")).toBool());
    QCOMPARE(reply.value(QStringLiteral("failed")).toObject().value(QStringLiteral("error")).toString(),
             QStringLiteral("bad_args"));
}

void McpTest::toolboxEndpointAllowsFramesAndActivity()
{
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const auto handler = [&dispatcher](const QString &name, const QJsonObject &args) {
        if (name == QLatin1String("activity"))
            return drift::mcp::textResult(dispatcher.activity(args));
        return drift::mcp::textResult(dispatcher.applyOne(name, args));
    };
    const QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                              {QStringLiteral("id"), 1},
                              {QStringLiteral("method"), QStringLiteral("tools/call")},
                              {QStringLiteral("params"),
                               QJsonObject{{QStringLiteral("name"), QStringLiteral("activity")},
                                           {QStringLiteral("arguments"), QJsonObject{}}}}};
    const QJsonObject response =
        drift::mcp::handleJsonRpc(request, QStringLiteral("scene"), handler).toObject();
    const QJsonObject result = firstText(response.value(QStringLiteral("result")).toObject());
    QVERIFY(result.value(QStringLiteral("error")).toString() != QLatin1String("wrong_endpoint"));
}

void McpTest::listScenesExposesThumb()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    const QString source = dir.filePath(QStringLiteral("shots.mp4"));
    QVERIFY(writeFourShotClip(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QString clip = importAndPlace(dispatcher, source, 0.0);
    QVERIFY(!clip.isEmpty());

    const QJsonObject started = dispatcher.applyOne(QStringLiteral("detect_scenes"),
                                                    {{QStringLiteral("clip"), clip}});
    QVERIFY2(started.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(started).toJson(QJsonDocument::Compact)));
    QTRY_VERIFY_WITH_TIMEOUT(
        !state.mcpInspect(false, -1, true, false).value(QStringLiteral("jobs")).toObject()
             .value(QStringLiteral("sceneDetect")).toObject().value(QStringLiteral("active")).toBool()
            && dispatcher.applyOne(QStringLiteral("list_scenes"), {}).value(QStringLiteral("ok")).toBool(),
        30000);

    const QJsonObject scenes = dispatcher.applyOne(QStringLiteral("list_scenes"), {});
    const QJsonArray rows = scenes.value(QStringLiteral("scenes")).toArray();
    QVERIFY(rows.size() >= 3);
    for (const QJsonValue &v : rows) {
        const QJsonObject row = v.toObject();
        const double thumb = row.value(QStringLiteral("thumb")).toDouble();
        QVERIFY(thumb >= row.value(QStringLiteral("start")).toDouble());
        QVERIFY(thumb <= row.value(QStringLiteral("end")).toDouble());
        const double tl = row.value(QStringLiteral("timeline_thumb")).toDouble();
        QVERIFY(tl >= row.value(QStringLiteral("timeline_start")).toDouble() - 0.001);
        QVERIFY(tl <= row.value(QStringLiteral("timeline_end")).toDouble() + 0.001);
    }

    const QJsonObject raw = dispatcher.frames({{QStringLiteral("sample"), QStringLiteral("scenes")}});
    if (raw.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    const QJsonObject meta = firstText(raw);
    const QJsonArray frames = meta.value(QStringLiteral("frames")).toArray();
    QCOMPARE(frames.size(), rows.size());
    QVERIFY(!meta.contains(QStringLiteral("unscanned")));
    for (int i = 0; i < frames.size(); ++i)
        QCOMPARE(frames.at(i).toObject().value(QStringLiteral("scene")).toInt(), i);
}

void McpTest::sceneOpsAcceptClipRef()
{
    if (ffmpegPath().isEmpty())
        QSKIP("ffmpeg not available to generate a test clip");
    QTemporaryDir dir;
    const QString source = dir.filePath(QStringLiteral("shots.mp4"));
    QVERIFY(writeFourShotClip(source));

    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QString scanned = importAndPlace(dispatcher, source, 0.0);
    const QString other = importAndPlace(dispatcher, source, 12.0);
    QVERIFY(!scanned.isEmpty() && !other.isEmpty());
    QVERIFY(dispatcher.applyOne(QStringLiteral("set_trim"),
                                {{QStringLiteral("clip"), other}, {QStringLiteral("in"), 6.0},
                                 {QStringLiteral("out"), 12.0}})
                .value(QStringLiteral("ok")).toBool());

    QVERIFY(dispatcher.applyOne(QStringLiteral("detect_scenes"), {{QStringLiteral("clip"), scanned}})
                .value(QStringLiteral("ok")).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(
        dispatcher.applyOne(QStringLiteral("list_scenes"), {}).value(QStringLiteral("ok")).toBool(), 30000);

    const QJsonObject described = dispatcher.applyOne(QStringLiteral("describe_clip"), {{QStringLiteral("clip"), scanned}});
    QVERIFY(described.value(QStringLiteral("ok")).toBool());
    QCOMPARE(described.value(QStringLiteral("clip")).toString(), scanned);
    QVERIFY(!described.contains(QStringLiteral("ignored")));

    const QJsonObject unscanned = dispatcher.applyOne(QStringLiteral("describe_clip"), {{QStringLiteral("clip"), other}});
    QVERIFY(!unscanned.value(QStringLiteral("ok")).toBool());
    QCOMPARE(unscanned.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));
    QVERIFY(unscanned.value(QStringLiteral("detail")).toString().contains(other));

    const QJsonObject listed = dispatcher.applyOne(QStringLiteral("list_scenes"), {{QStringLiteral("clip"), other}});
    QVERIFY(!listed.value(QStringLiteral("ok")).toBool());

    QVERIFY(dispatcher.applyOne(QStringLiteral("detect_scenes"), {{QStringLiteral("clip"), other}})
                .value(QStringLiteral("ok")).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(
        dispatcher.applyOne(QStringLiteral("list_scenes"), {{QStringLiteral("clip"), other}}).value(QStringLiteral("ok")).toBool(), 30000);
    const QJsonObject first = dispatcher.applyOne(QStringLiteral("list_scenes"), {{QStringLiteral("clip"), scanned}});
    QVERIFY2(first.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(first).toJson(QJsonDocument::Compact)));
    QCOMPARE(first.value(QStringLiteral("clip")).toString(), scanned);
    QVERIFY(first.value(QStringLiteral("total")).toInt() >= 3);
}

void McpTest::addEffectReportsHost()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    drift::mcp::McpDispatcher dispatcher(&state);
    const QJsonObject before = state.mcpInspect(false, -1, false, false);

    const QJsonObject added = dispatcher.applyOne(
        QStringLiteral("add_effect"),
        {{QStringLiteral("track"), 0}, {QStringLiteral("index"), 0}, {QStringLiteral("effect"), QStringLiteral("stylize_vignette")}});
    QVERIFY2(added.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(added).toJson(QJsonDocument::Compact)));
    QCOMPARE(added.value(QStringLiteral("effect")).toString(), QStringLiteral("stylize.vignette"));
    QCOMPARE(added.value(QStringLiteral("index")).toInt(), 0);

    const QJsonObject after = state.mcpInspect(false, -1, false, false);
    const bool newLane = after.value(QStringLiteral("tracks")).toArray().size()
                         > before.value(QStringLiteral("tracks")).toArray().size();
    QCOMPARE(added.contains(QStringLiteral("host")), newLane);
    if (newLane) {
        const QJsonObject host = added.value(QStringLiteral("host")).toObject();
        QCOMPARE(host.value(QStringLiteral("kind")).toString(), QStringLiteral("adjustment"));
        QVERIFY(!host.value(QStringLiteral("clip")).toString().isEmpty());
        const QJsonObject row = state.mcpInspect({true, true, false, false, -1, -1,
                                                  host.value(QStringLiteral("clip")).toString()});
        QVERIFY(row.value(QStringLiteral("ok")).toBool());
    }
}

void McpTest::framesFlagsBeyondEnd()
{
    AssetLibrary library;
    AppController state(&library);
    state.addTextClip(QStringLiteral("Hello"), 0.0);
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject raw = dispatcher.frames({{QStringLiteral("at"), QJsonArray{1, 40}}});
    if (raw.value(QStringLiteral("isError")).toBool())
        QSKIP("Compositor could not produce a frame in this environment");
    const QJsonObject meta = firstText(raw);
    QCOMPARE(meta.value(QStringLiteral("beyond_end")).toInt(), 1);
    QCOMPARE(meta.value(QStringLiteral("dur")).toDouble(), 5.0);
    const QJsonArray frames = meta.value(QStringLiteral("frames")).toArray();
    QVERIFY(!frames.at(0).toObject().contains(QStringLiteral("beyond_end")));
    QVERIFY(frames.at(1).toObject().value(QStringLiteral("beyond_end")).toBool());
}

namespace {

// Stands in for market.cutwire.org: enough of /api/v1 for a search, a download job that is
// ready at once, and the file it points at.
class FakeMarket : public QObject
{
public:
    QTcpServer server;
    int downloadsPosted = 0;

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()); }

    bool start()
    {
        if (!server.listen(QHostAddress::LocalHost))
            return false;
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *sock = server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock] { handle(sock); });
                connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        return true;
    }

    static QByteArray toneWav()
    {
        const int rate = 8000;
        const int frames = rate;
        QByteArray pcm;
        pcm.reserve(frames * 2);
        for (int i = 0; i < frames; ++i) {
            const qint16 v = qint16(12000.0 * std::sin(2.0 * M_PI * 440.0 * i / rate));
            pcm.append(char(v & 0xff));
            pcm.append(char((v >> 8) & 0xff));
        }
        QByteArray wav;
        QDataStream out(&wav, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::LittleEndian);
        out.writeRawData("RIFF", 4);
        out << quint32(36 + pcm.size());
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2)
            << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << quint32(pcm.size());
        wav.append(pcm);
        return wav;
    }

    void handle(QTcpSocket *sock)
    {
        QByteArray buf = sock->property("buf").toByteArray() + sock->readAll();
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            sock->setProperty("buf", buf);
            return;
        }
        const QByteArray header = buf.left(headerEnd);
        int contentLength = 0;
        for (const QByteArray &line : header.split('\n')) {
            if (line.toLower().startsWith("content-length:"))
                contentLength = line.mid(15).trimmed().toInt();
        }
        if (buf.size() < headerEnd + 4 + contentLength) {
            sock->setProperty("buf", buf);
            return;
        }
        const QList<QByteArray> requestLine = header.split('\n').first().trimmed().split(' ');
        const QByteArray method = requestLine.value(0);
        const QString path = QUrl::fromEncoded(requestLine.value(1)).path();

        int status = 200;
        QByteArray type = "application/json";
        QByteArray body;
        const QString fileUrl = base() + QStringLiteral("/file/tone.wav");
        const QJsonObject item{{QStringLiteral("id"), QStringLiteral("tone-1")},
                               {QStringLiteral("type"), QStringLiteral("audio")},
                               {QStringLiteral("provider"), QStringLiteral("fake")},
                               {QStringLiteral("title"), QStringLiteral("Test tone")},
                               {QStringLiteral("duration_ms"), 1000},
                               {QStringLiteral("price_coins"), 0},
                               {QStringLiteral("creator"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift")}}},
                               {QStringLiteral("thumb_url"), base() + QStringLiteral("/thumb.jpg")},
                               {QStringLiteral("variants"), QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("wav")}, {QStringLiteral("label"), QStringLiteral("WAV")}}}}};
        const QJsonObject quota{{QStringLiteral("limit"), 5}, {QStringLiteral("remaining"), 4}, {QStringLiteral("window"), QStringLiteral("day")}};
        const QJsonObject readyJob{{QStringLiteral("id"), QStringLiteral("job-1")},
                                   {QStringLiteral("status"), QStringLiteral("ready")},
                                   {QStringLiteral("progress"), 1},
                                   {QStringLiteral("file"), QJsonObject{{QStringLiteral("url"), fileUrl},
                                                                        {QStringLiteral("filename"), QStringLiteral("tone.wav")},
                                                                        {QStringLiteral("mime"), QStringLiteral("audio/wav")}}}};
        if (path == QLatin1String("/api/v1/catalog")) {
            // A literal rather than nested QJsonObject{...} initialisers: seven levels of
            // braced QJsonValue conversions is what exhausted MSVC's heap (C1060) on the
            // Windows package job, with a single translation unit and no parallelism at all.
            QJsonObject catalog = QJsonDocument::fromJson(R"({
                "types": [{
                    "id": "audio", "label": "Audio", "delivery": "media",
                    "providers": [{
                        "id": "fake", "label": "Fake", "capabilities": ["search"],
                        "filters": [{"id": "mood", "type": "enum", "label": "Mood",
                                     "options": [{"id": "calm", "label": "Calm"}]}]
                    }]
                }]
            })").object();
            QJsonArray types = catalog.value(QStringLiteral("types")).toArray();
            QJsonObject audio = types.first().toObject();
            QJsonArray providers = audio.value(QStringLiteral("providers")).toArray();
            QJsonObject provider = providers.first().toObject();
            provider.insert(QStringLiteral("quota"), quota);
            providers[0] = provider;
            audio.insert(QStringLiteral("providers"), providers);
            types[0] = audio;
            catalog.insert(QStringLiteral("types"), types);
            body = QJsonDocument(catalog).toJson(QJsonDocument::Compact);
        } else if (path == QLatin1String("/api/v1/search")) {
            body = QJsonDocument(QJsonObject{{QStringLiteral("items"), QJsonArray{item}}, {QStringLiteral("quota"), quota}}).toJson(QJsonDocument::Compact);
        } else if (path == QLatin1String("/api/v1/downloads") && method == "POST") {
            ++downloadsPosted;
            status = 201;
            body = QJsonDocument(readyJob).toJson(QJsonDocument::Compact);
        } else if (path == QLatin1String("/api/v1/downloads/job-1")) {
            body = QJsonDocument(readyJob).toJson(QJsonDocument::Compact);
        } else if (path == QLatin1String("/file/tone.wav")) {
            type = "audio/wav";
            body = toneWav();
        } else {
            status = 404;
            body = QJsonDocument(QJsonObject{{QStringLiteral("code"), QStringLiteral("not_found")}, {QStringLiteral("detail"), QStringLiteral("no such route")}}).toJson(QJsonDocument::Compact);
        }
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : status == 201 ? " Created" : " Not Found")
                              + "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size())
                              + "\r\nConnection: close\r\n\r\n" + body;
        sock->setProperty("buf", QByteArray());
        sock->write(response);
        sock->flush();
        sock->disconnectFromHost();
    }
};

} // namespace

void McpTest::marketOpsGateOnConsent()
{
    QStandardPaths::setTestModeEnabled(true);
    QSettings().remove(QStringLiteral("market/consented"));
    AssetLibrary library;
    AppController state(&library);
    drift::mcp::McpDispatcher dispatcher(&state);

    QJsonObject reply = dispatcher.applyOne(QStringLiteral("market_search"), {{QStringLiteral("q"), QStringLiteral("x")}});
    QCOMPARE(reply.value(QStringLiteral("error")).toString(), QStringLiteral("market_unavailable"));
    reply = dispatcher.applyOne(QStringLiteral("market_status"), {});
    QVERIFY(reply.value(QStringLiteral("ok")).toBool());
    QVERIFY(!reply.value(QStringLiteral("configured")).toBool());

    qputenv("DRIFT_MARKET_API_URL", "http://127.0.0.1:9/api/v1");
    const auto unset = qScopeGuard([] { qunsetenv("DRIFT_MARKET_API_URL"); });
    MarketClient client;
    client.setAssetLibrary(&library);
    state.setMarketClient(&client);
    QVERIFY(!client.consented());

    reply = dispatcher.applyOne(QStringLiteral("market_status"), {});
    QVERIFY(reply.value(QStringLiteral("configured")).toBool());
    QVERIFY(!reply.value(QStringLiteral("consented")).toBool());
    QVERIFY(reply.value(QStringLiteral("hint")).toString().contains(QStringLiteral("accept")));
    for (const char *op : {"market_search", "market_resolve", "market_item", "market_download"}) {
        reply = dispatcher.applyOne(QLatin1String(op), {{QStringLiteral("id"), QStringLiteral("x")}, {QStringLiteral("url"), QStringLiteral("http://x")}, {QStringLiteral("q"), QStringLiteral("x")}});
        QCOMPARE(reply.value(QStringLiteral("error")).toString(), QStringLiteral("consent_required"));
    }
}

void McpTest::marketSearchAndDownloadImportsAsset()
{
    QStandardPaths::setTestModeEnabled(true);
    FakeMarket fake;
    QVERIFY(fake.start());
    qputenv("DRIFT_MARKET_API_URL", (fake.base() + QStringLiteral("/api/v1")).toUtf8());
    const auto unset = qScopeGuard([] { qunsetenv("DRIFT_MARKET_API_URL"); });

    AssetLibrary library;
    AppController state(&library);
    MarketClient client;
    client.setAssetLibrary(&library);
    client.acceptTerms();
    state.setMarketClient(&client);
    QSettings().remove(QStringLiteral("market/consented"));
    drift::mcp::McpDispatcher dispatcher(&state);

    const QJsonObject status = dispatcher.applyOne(QStringLiteral("market_status"), {});
    QVERIFY2(status.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(status).toJson(QJsonDocument::Compact)));
    QVERIFY(status.value(QStringLiteral("consented")).toBool());
    const QJsonArray types = status.value(QStringLiteral("types")).toArray();
    QCOMPARE(types.size(), 1);
    const QJsonObject provider = types.at(0).toObject().value(QStringLiteral("providers")).toArray().at(0).toObject();
    QCOMPARE(provider.value(QStringLiteral("id")).toString(), QStringLiteral("fake"));
    QCOMPARE(provider.value(QStringLiteral("filters")).toArray().at(0).toObject().value(QStringLiteral("options")).toArray().at(0).toString(), QStringLiteral("calm"));
    QCOMPARE(provider.value(QStringLiteral("quota")).toObject().value(QStringLiteral("remaining")).toInt(), 4);

    const QJsonObject bad = dispatcher.applyOne(QStringLiteral("market_search"), {{QStringLiteral("type"), QStringLiteral("video")}});
    QCOMPARE(bad.value(QStringLiteral("error")).toString(), QStringLiteral("bad_args"));
    QVERIFY(bad.value(QStringLiteral("detail")).toString().contains(QStringLiteral("audio")));

    const QJsonObject found = dispatcher.applyOne(QStringLiteral("market_search"), {{QStringLiteral("q"), QStringLiteral("tone")}});
    QVERIFY2(found.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(found).toJson(QJsonDocument::Compact)));
    const QJsonArray items = found.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 1);
    const QJsonObject row = items.at(0).toObject();
    QCOMPARE(row.value(QStringLiteral("id")).toString(), QStringLiteral("tone-1"));
    QCOMPARE(row.value(QStringLiteral("dur")).toDouble(), 1.0);
    QCOMPARE(row.value(QStringLiteral("by")).toString(), QStringLiteral("Drift"));
    QVERIFY(!row.contains(QStringLiteral("coins")));
    QCOMPARE(row.value(QStringLiteral("variants")).toInt(), 1);
    QVERIFY(!found.value(QStringLiteral("has_more")).toBool());

    const QJsonObject item = dispatcher.applyOne(QStringLiteral("market_item"), {{QStringLiteral("id"), QStringLiteral("tone-1")}});
    QCOMPARE(item.value(QStringLiteral("item")).toObject().value(QStringLiteral("variants")).toArray().at(0).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("wav"));

    const QJsonObject job = dispatcher.applyOne(QStringLiteral("market_download"), {{QStringLiteral("id"), QStringLiteral("tone-1")}, {QStringLiteral("wait"), 30}});
    QVERIFY2(job.value(QStringLiteral("ok")).toBool(), qPrintable(QJsonDocument(job).toJson(QJsonDocument::Compact)));
    QCOMPARE(job.value(QStringLiteral("status")).toString(), QStringLiteral("done"));
    const QString asset = job.value(QStringLiteral("asset")).toString();
    QVERIFY(!asset.isEmpty());
    QVERIFY(QFile::exists(job.value(QStringLiteral("path")).toString()));
    QCOMPARE(fake.downloadsPosted, 1);

    const QJsonObject assets = dispatcher.applyOne(QStringLiteral("list_assets"), {});
    bool inBin = false;
    for (const QJsonValue &v : assets.value(QStringLiteral("assets")).toArray())
        inBin = inBin || v.toObject().value(QStringLiteral("id")).toString() == asset;
    QVERIFY(inBin);

    const QJsonObject jobs = dispatcher.applyOne(QStringLiteral("market_downloads"), {});
    QCOMPARE(jobs.value(QStringLiteral("n")).toInt(), 1);
    QCOMPARE(jobs.value(QStringLiteral("active")).toInt(), 0);
    const QJsonObject cancel = dispatcher.applyOne(QStringLiteral("market_cancel_download"), {{QStringLiteral("id"), QStringLiteral("tone-1")}});
    QCOMPARE(cancel.value(QStringLiteral("error")).toString(), QStringLiteral("conflict"));
    QCOMPARE(dispatcher.applyOne(QStringLiteral("market_downloads"), {{QStringLiteral("clear"), true}}).value(QStringLiteral("n")).toInt(), 0);
    QFile::remove(job.value(QStringLiteral("path")).toString());
}

QTEST_MAIN(McpTest)

#include "tst_mcp.moc"
