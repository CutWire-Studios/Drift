#include <QtTest>

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QCryptographicHash>
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
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>

#include "mcp/McpCatalog.h"
#include "mcp/McpDispatcher.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpSession.h"
#include "engine/ObjectDetector.h"
#include "models/AppController.h"
#include "models/AssetLibrary.h"

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
    void toolboxAnnotationsPresent();
    void toolboxUnknownIsError();
    void toolboxReturnsSchemas();
    void protocolInitializeAndToolsList();
    void protocolUnknownOp();
    void protocolNotificationHasNoReply();
    void sessionFileRoundTrip();
    void sessionFileMissing();
    void serverRequiresBearerToken();
    void serverInitializeWithToken();
    void serverNotificationReturns202();
    void applyUnknownOp();
    void applyBatchStopsAndUndoRevertsPrefix();
    void inspectIsCompact();
    void inspectIncludesProjectFields();
    void placeHonorsOverlapToggle();
    void workAreaRoundTrip();
    void exportOptionsAndSettings();
    void exportVideoRequiresPath();
    void projectSetupRoundTrip();
    void captureDoesNotInsertClip();
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
    QCOMPARE(boxes.size(), 17);
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
    const QJsonObject cat = drift::mcp::catalogPayload();
    const QJsonArray boxes = cat.value(QStringLiteral("toolboxes")).toArray();
    QVERIFY(cat.contains(QStringLiteral("endpoints")));
    QVERIFY(cat.contains(QStringLiteral("units")));
    QVERIFY(cat.contains(QStringLiteral("workflow")));
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
        const QJsonObject first = ops.at(0).toObject();
        QVERIFY(first.contains(QStringLiteral("name")));
        QVERIFY(first.contains(QStringLiteral("when")));
    }
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

    const QJsonValue listed = drift::mcp::handleJsonRpc(rpc(QStringLiteral("tools/list")), {}, {});
    const QJsonArray tools =
        listed.toObject().value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 5);
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
    const QJsonObject withClips = dispatcher.inspect({{QStringLiteral("clips"), true}});
    const QJsonArray tracks = withClips.value(QStringLiteral("tracks")).toArray();
    QVERIFY(!tracks.isEmpty());
    QVERIFY(tracks.at(0).toObject().contains(QStringLiteral("items")));
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
    const QJsonObject result = dispatcher.applyOne(QStringLiteral("list_effects"), {});
    QVERIFY(result.value(QStringLiteral("ok")).toBool());
    const QJsonArray effects = result.value(QStringLiteral("effects")).toArray();
    QVERIFY(!effects.isEmpty());
    bool sawParams = false;
    for (const QJsonValue &v : effects) {
        const QJsonArray params = v.toObject().value(QStringLiteral("params")).toArray();
        if (!params.isEmpty()) {
            sawParams = true;
            QVERIFY(params.at(0).toObject().contains(QStringLiteral("key")));
            break;
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

    // Out of range is clamped to what the mixer will actually honour, and says so.
    const QJsonObject loud = dispatcher.applyOne(
        QStringLiteral("set_volume"),
        {{QStringLiteral("clip"), clip}, {QStringLiteral("value"), 9.0}});
    QCOMPARE(loud.value(QStringLiteral("value")).toDouble(), 2.0);
    QCOMPARE(loud.value(QStringLiteral("clamped")).toBool(), true);
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
        inspect.value(QStringLiteral("sceneDetect")).toObject();
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

    const QJsonObject cat = drift::mcp::catalogPayload();
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
    QCOMPARE(entries.at(0).toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("Origin"));
    QCOMPARE(history.value(QStringLiteral("current")).toInt(), 1);
    const QString label = entries.last().toObject().value(QStringLiteral("label")).toString();
    QVERIFY2(label.contains(QStringLiteral("add_text")), qPrintable(label));
    const QString hash = entries.last().toObject().value(QStringLiteral("hash")).toString();
    QCOMPARE(hash.size(), 64);
    QCOMPARE(history.value(QStringLiteral("hash")).toString(), hash);
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

    bool hasFaceSwap = false;
    const QJsonObject listed = dispatcher.applyOne(QStringLiteral("list_effects"), {});
    for (const QJsonValue &v : listed.value(QStringLiteral("effects")).toArray()) {
        if (v.toObject().value(QStringLiteral("id")).toString() == QLatin1String("face_swap"))
            hasFaceSwap = true;
    }
    if (!hasFaceSwap)
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
    QVERIFY(entries.at(0).toObject().value(QStringLiteral("hash")).toString()
            != entries.at(1).toObject().value(QStringLiteral("hash")).toString());
    QCOMPARE(dispatcher.inspect({}).value(QStringLiteral("undo")).toObject()
                 .value(QStringLiteral("hash")).toString(),
             hist.value(QStringLiteral("hash")).toString());
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
    const QString mid = entries.at(1).toObject().value(QStringLiteral("hash")).toString();
    const QString prefix = mid.left(12);

    const QJsonObject jumped = dispatcher.applyOne(QStringLiteral("undo_to"),
                                                   {{QStringLiteral("hash"), prefix}});
    QVERIFY2(jumped.value(QStringLiteral("ok")).toBool(),
             qPrintable(QJsonDocument(jumped).toJson(QJsonDocument::Compact)));
    QCOMPARE(jumped.value(QStringLiteral("index")).toInt(), 1);
    QCOMPARE(jumped.value(QStringLiteral("hash")).toString(), mid);
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
    const QString dropped = before.value(QStringLiteral("entries")).toArray().at(2).toObject()
                                .value(QStringLiteral("hash")).toString();

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
        hashes.append(v.toObject().value(QStringLiteral("hash")).toString());
    QVERIFY(!hashes.contains(dropped));
    const QJsonObject missing = dispatcher.applyOne(QStringLiteral("undo_to"),
                                                    {{QStringLiteral("hash"), dropped}});
    QCOMPARE(missing.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(missing.value(QStringLiteral("error")).toString(), QStringLiteral("not_found"));
}

QTEST_MAIN(McpTest)
#include "tst_mcp.moc"
