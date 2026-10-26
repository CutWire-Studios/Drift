#include "app/mcp/transport/McpBridge.h"
#include "app/mcp/protocol/McpJson.h"
#include "app/mcp/protocol/McpProtocol.h"
#include "app/mcp/tools/McpCatalog.h"
#include "app/platform/RunningInstances.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTcpSocket>
#include <QThread>

#include <cstdio>

namespace drift::mcp {
namespace {

const QString kInstances = QStringLiteral("instances");
const QString kUseInstance = QStringLiteral("use_instance");
const QString kLaunchInstance = QStringLiteral("launch_instance");
const QString kCloseInstance = QStringLiteral("close_instance");

int httpStatus(const QByteArray &response)
{
    const int nl = response.indexOf('\n');
    const QByteArray line = nl < 0 ? response : response.left(nl);
    const auto parts = line.split(' ');
    return parts.size() >= 2 ? parts.at(1).toInt() : 0;
}

QByteArray postJson(quint16 port, const QString &token, const QByteArray &body, QString *error,
                    int *statusOut)
{
    QTcpSocket socket;
    socket.connectToHost(QStringLiteral("127.0.0.1"), port);
    if (!socket.waitForConnected(2000)) {
        if (error)
            *error = QStringLiteral(
                "Could not connect to Drift. Is the editor open with Agent access enabled?");
        return {};
    }

    QByteArray req;
    req += "POST /mcp HTTP/1.1\r\n";
    req += "Host: 127.0.0.1\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Authorization: Bearer ";
    req += token.toUtf8();
    req += "\r\nContent-Length: ";
    req += QByteArray::number(body.size());
    req += "\r\nConnection: close\r\n\r\n";
    req += body;
    socket.write(req);
    socket.waitForBytesWritten(2000);

    QByteArray response;
    while (socket.waitForReadyRead(15000) || socket.bytesAvailable() > 0)
        response += socket.readAll();

    const int sep = response.indexOf("\r\n\r\n");
    if (sep < 0) {
        if (error)
            *error = QStringLiteral("Empty response from Drift MCP.");
        return {};
    }
    if (statusOut)
        *statusOut = httpStatus(response);
    return response.mid(sep + 4);
}

// Never the token: the agent addresses instances by id and the bridge holds the secrets.
QJsonObject sessionRow(const SessionInfo &session, qint64 attachedPid)
{
    QJsonObject row{
        {QStringLiteral("id"), session.pid},
        {QStringLiteral("name"), session.projectName},
        {QStringLiteral("headless"), session.headless},
        {QStringLiteral("isolated"), session.isolated},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:%1/mcp").arg(session.port)},
        {QStringLiteral("started"),
         QDateTime::fromMSecsSinceEpoch(session.startedAt).toString(Qt::ISODate)},
        {QStringLiteral("attached"), session.pid == attachedPid},
    };
    if (!session.project.isEmpty())
        row.insert(QStringLiteral("project"), session.project);
    if (session.loading)
        row.insert(QStringLiteral("loading"), true);
    return row;
}

QString instructions()
{
    return initializeResult().value(QStringLiteral("instructions")).toString()
        + QStringLiteral(
              " Several Drift instances can be open: instances() lists them, "
              "use_instance({id}) picks the one every other call goes to, and "
              "launch_instance({project?}) opens a new editor to work in without touching "
              "the user's session; close_instance({id}) quits one it launched when done. "
              "With one instance running it is used automatically.");
}

} // namespace

StdioBridge::StdioBridge(qint64 pinnedPid)
{
    m_attached.pid = pinnedPid;
}

bool StdioBridge::isBridgeTool(const QString &name)
{
    return name == kInstances || name == kUseInstance || name == kLaunchInstance
        || name == kCloseInstance;
}

QJsonArray StdioBridge::bridgeTools()
{
    QJsonArray tools;
    tools.append(toolDef(
        kInstances,
        QStringLiteral("When: Several Drift windows may be open, or a call said to pick one. Lists the "
                       "running instances with Agent access on: id, name (project), project path, "
                       "headless, started, and which one this session is attached to."),
        objectSchema({}), toolAnnotations(true, false, true)));
    tools.append(toolDef(
        kUseInstance,
        QStringLiteral("When: Point this session at one instance. Every later call goes to it until "
                       "the next use_instance. Check the name first: edits land in that project."),
        objectSchema({{QStringLiteral("id"), numberProp(QStringLiteral("Instance id from instances()"))}},
                     {QStringLiteral("id")}),
        toolAnnotations(false, false, true)));
    tools.append(toolDef(
        kLaunchInstance,
        QStringLiteral("When: Work without disturbing the user's open editor. Starts a new Drift with "
                       "Agent access on, waits until it is up and project has finished opening, and "
                       "attaches to it. The new instance never restores the user's last session or "
                       "touches their recovery file. headless:true runs it with no window; it then "
                       "keeps running until close_instance({id})."),
        objectSchema({{QStringLiteral("project"),
                       stringProp(QStringLiteral("Project file to open (absolute path); empty timeline when omitted"))},
                      {QStringLiteral("headless"),
                       propWithDefault(boolProp(QStringLiteral("No window")), false)},
                      {QStringLiteral("wait"),
                       propWithDefault(numberProp(QStringLiteral("Seconds to wait for it to come up"), 1, 300),
                                       60)}}),
        toolAnnotations(false, false, false)));
    tools.append(toolDef(
        kCloseInstance,
        QStringLiteral("When: Done with an instance launch_instance started. Quits it cleanly and waits "
                       "for it to exit. Only launched instances (isolated:true in instances()) can be "
                       "closed; never the user's own editor. Refuses unsaved_changes unless discard:true "
                       "— save_project first to keep the work."),
        objectSchema({{QStringLiteral("id"), numberProp(QStringLiteral("Instance id from instances()"))},
                      {QStringLiteral("discard"),
                       propWithDefault(boolProp(QStringLiteral("Close even with unsaved changes, losing them")),
                                       false)}},
                     {QStringLiteral("id")}),
        toolAnnotations(false, true, false)));
    return tools;
}

QByteArray StdioBridge::handle(const QByteArray &message)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(message, &parseError);
    // Undefined, not the default null: a batch of notifications only must send nothing.
    QJsonValue reply = QJsonValue::Undefined;
    if (doc.isObject()) {
        reply = handleOne(doc.object());
    } else if (doc.isArray()) {
        QJsonArray out;
        for (const QJsonValue &item : doc.array()) {
            if (!item.isObject())
                continue;
            const QJsonValue one = handleOne(item.toObject());
            if (!one.isUndefined())
                out.append(one);
        }
        if (!out.isEmpty())
            reply = out;
    } else {
        reply = jsonRpcError(QJsonValue::Null, -32700, QStringLiteral("Parse error"));
    }

    if (reply.isUndefined())
        return {};
    return reply.isArray() ? QJsonDocument(reply.toArray()).toJson(QJsonDocument::Compact)
                           : QJsonDocument(reply.toObject()).toJson(QJsonDocument::Compact);
}

QJsonValue StdioBridge::handleOne(const QJsonObject &request)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const bool notification = !request.contains(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();

    // None of these depend on which instance is attached, and answering them here is what
    // lets an agent see launch_instance while no editor is running at all.
    if (method == QLatin1String("initialize")) {
        QJsonObject result = initializeResult();
        result.insert(QStringLiteral("instructions"), instructions());
        return jsonRpcOk(id, result);
    }
    if (method == QLatin1String("notifications/initialized") || method == QLatin1String("initialized"))
        return notification ? QJsonValue(QJsonValue::Undefined) : jsonRpcOk(id, QJsonObject{});
    if (method == QLatin1String("ping"))
        return jsonRpcOk(id, QJsonObject{});
    if (method == QLatin1String("tools/list")) {
        QJsonArray tools = homepageTools();
        for (const QJsonValue &tool : bridgeTools())
            tools.append(tool);
        return jsonRpcOk(id, QJsonObject{{QStringLiteral("tools"), tools}});
    }

    const bool toolCall = method == QLatin1String("tools/call");
    if (toolCall) {
        const QJsonObject params = request.value(QStringLiteral("params")).toObject();
        const QString name = params.value(QStringLiteral("name")).toString();
        if (isBridgeTool(name))
            return jsonRpcOk(id, textResult(callBridgeTool(
                                     name, params.value(QStringLiteral("arguments")).toObject())));
    }

    QString error;
    if (!resolveAttachedSession(&m_attached, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        if (notification)
            return QJsonValue(QJsonValue::Undefined);
        // A tool result rather than a protocol error, so the agent reads the reason (and
        // the instance list) instead of the client reporting a broken server.
        if (toolCall)
            return jsonRpcOk(id, textResult(err("no_instance", error)));
        return jsonRpcError(id, -32000, error);
    }
    return forward(request);
}

QJsonValue StdioBridge::forward(const QJsonObject &request)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    const bool notification = !request.contains(QStringLiteral("id"));
    const QByteArray body = QJsonDocument(request).toJson(QJsonDocument::Compact);

    QString postError;
    int status = 0;
    const QByteArray reply = postJson(m_attached.port, m_attached.token, body, &postError, &status);
    QString error;
    if (!postError.isEmpty() && reply.isEmpty())
        error = postError;
    else if (status > 0 && (status < 200 || status >= 300))
        error = QStringLiteral("Drift MCP HTTP %1").arg(status);
    if (!error.isEmpty()) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return notification ? QJsonValue(QJsonValue::Undefined)
                            : QJsonValue(jsonRpcError(id, -32000, error));
    }
    // JSON-RPC notifications (no id) must not get a response. The HTTP server answers
    // those with 202 and an empty body — forwarding that would break stdio clients after
    // `notifications/initialized`.
    if (status == 202 || reply.trimmed().isEmpty())
        return QJsonValue(QJsonValue::Undefined);
    const QJsonDocument doc = QJsonDocument::fromJson(reply);
    if (doc.isArray())
        return doc.array();
    if (doc.isObject())
        return doc.object();
    return notification ? QJsonValue(QJsonValue::Undefined)
                        : QJsonValue(jsonRpcError(id, -32000, QStringLiteral("Invalid reply from Drift MCP.")));
}

QJsonObject StdioBridge::callBridgeTool(const QString &name, const QJsonObject &args)
{
    if (name == kInstances)
        return listInstances();
    if (name == kUseInstance)
        return useInstance(args);
    if (name == kCloseInstance)
        return closeInstance(args);
    return launchInstance(args);
}

QJsonObject StdioBridge::listInstances()
{
    QJsonArray rows;
    for (const SessionInfo &session : listSessions())
        rows.append(sessionRow(session, m_attached.pid));
    QJsonObject result{{QStringLiteral("instances"), rows}};
    if (m_attached.pid != 0)
        result.insert(QStringLiteral("attached"), m_attached.pid);
    return ok(result);
}

QJsonObject StdioBridge::useInstance(const QJsonObject &args)
{
    const QJsonValue idValue = args.value(QStringLiteral("id"));
    const qint64 pid = idValue.isString() ? idValue.toString().toLongLong() : idValue.toInteger();
    if (pid <= 0)
        return err("bad_args", QStringLiteral("id is required: an instance id from instances()"));

    const QList<SessionInfo> sessions = listSessions();
    for (const SessionInfo &session : sessions) {
        if (session.pid == pid) {
            m_attached = session;
            return ok({{QStringLiteral("instance"), sessionRow(session, pid)}});
        }
    }
    QStringList known;
    for (const SessionInfo &session : sessions)
        known.append(describeSession(session));
    return err("not_found",
               known.isEmpty()
                   ? QStringLiteral("No instance %1; none are running with Agent access on.").arg(pid)
                   : QStringLiteral("No instance %1. Running: %2.").arg(pid).arg(known.join(QStringLiteral(", "))));
}

QJsonObject StdioBridge::launchInstance(const QJsonObject &args)
{
    // Isolated: the new instance neither restores the user's last session nor touches the
    // recovery file, lastSessionPath or recents that the user's own editor relies on.
    QStringList arguments{QStringLiteral("--isolated-session")};
    const bool headless = args.value(QStringLiteral("headless")).toBool(false);
    if (headless) {
        // 4731 is only a preference: once it is taken the server binds a free port.
        arguments << QStringLiteral("--headless") << QStringLiteral("--mcp-port")
                  << QStringLiteral("4731");
    } else {
        arguments << QStringLiteral("--agent-access");
    }

    const QString project = args.value(QStringLiteral("project")).toString();
    if (!project.isEmpty()) {
        const QFileInfo info(project);
        if (!info.isFile())
            return err("not_found", QStringLiteral("%1 does not exist").arg(project));
        arguments << info.absoluteFilePath();
    }

    // stdin and stdout are this bridge's MCP stream: a child inheriting them could read
    // requests meant for the bridge, and a headless banner would corrupt the replies.
    QProcess process;
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments(arguments);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    qint64 pid = 0;
    if (!process.startDetached(&pid) || pid <= 0)
        return err("launch_failed", QStringLiteral("Could not start %1").arg(process.program()));

    const double waitSeconds = args.value(QStringLiteral("wait")).toDouble(60);
    const qint64 deadline = qint64(qBound(1.0, waitSeconds, 300.0) * 1000);
    QElapsedTimer timer;
    timer.start();
    bool registered = false;
    while (timer.elapsed() < deadline) {
        for (const SessionInfo &session : listSessions()) {
            if (session.pid != pid)
                continue;
            registered = true;
            // Registering is not being ready: the startup project loads asynchronously, and
            // a call made before it lands would read or edit the empty timeline instead.
            if (session.loading || (!project.isEmpty() && !session.hasLoaded))
                break;
            m_attached = session;
            const QJsonObject row = sessionRow(session, pid);
            if (!project.isEmpty() && !session.loadOk) {
                QJsonObject failed = err("load_failed",
                                         QStringLiteral("Drift started (pid %1, attached) but could "
                                                        "not open %2: %3")
                                             .arg(pid)
                                             .arg(project, session.loadMessage));
                failed.insert(QStringLiteral("instance"), row);
                return failed;
            }
            return ok({{QStringLiteral("instance"), row}});
        }
        if (!drift::instances::processAlive(pid)) {
            return err("launch_failed",
                       QStringLiteral("Drift (pid %1) exited before enabling Agent access").arg(pid));
        }
        QThread::msleep(200);
    }
    return err("launch_timeout",
               registered && !project.isEmpty()
                   ? QStringLiteral("Drift started (pid %1) but was still opening %2 after %3 s; "
                                    "use_instance({id:%1}) once instances() no longer shows it "
                                    "loading.")
                         .arg(pid)
                         .arg(project)
                         .arg(deadline / 1000)
                   : QStringLiteral("Drift started (pid %1) but did not enable Agent access within "
                                    "%2 s; instances() shows it once it does.")
                         .arg(pid)
                         .arg(deadline / 1000));
}

QJsonObject StdioBridge::closeInstance(const QJsonObject &args)
{
    const QJsonValue idValue = args.value(QStringLiteral("id"));
    const qint64 pid = idValue.isString() ? idValue.toString().toLongLong() : idValue.toInteger();
    if (pid <= 0)
        return err("bad_args", QStringLiteral("id is required: an instance id from instances()"));

    SessionInfo target;
    for (const SessionInfo &session : listSessions()) {
        if (session.pid == pid)
            target = session;
    }
    if (target.pid == 0)
        return err("not_found", QStringLiteral("No instance %1 with Agent access on").arg(pid));
    // Checked here as well as by the instance, so the refusal names what to do instead.
    if (!target.isolated) {
        return err("not_isolated",
                   QStringLiteral("%1 is not an instance launch_instance started; only the user can "
                                  "close it, from its own window.")
                       .arg(describeSession(target)));
    }

    // The instance quits itself so its exit path runs: its registry record and instance marker
    // go with it, which a signal would skip on the GUI and cannot do at all on Windows.
    const QJsonObject call{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 1},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("quit_instance")},
                     {QStringLiteral("arguments"),
                      QJsonObject{{QStringLiteral("discard"),
                                   args.value(QStringLiteral("discard")).toBool()}}}}},
    };
    QString postError;
    int status = 0;
    const QByteArray reply = postJson(target.port, target.token,
                                      QJsonDocument(call).toJson(QJsonDocument::Compact),
                                      &postError, &status);
    if (reply.isEmpty() || status < 200 || status >= 300) {
        // The instance may have quit before its reply was out; the process is the real answer.
        QElapsedTimer grace;
        grace.start();
        while (drift::instances::processAlive(pid) && grace.elapsed() < 2000)
            QThread::msleep(100);
        if (!drift::instances::processAlive(pid)) {
            if (m_attached.pid == pid)
                m_attached = SessionInfo{};
            return ok({{QStringLiteral("closed"), pid}});
        }
        return err("close_failed", postError.isEmpty()
                                       ? QStringLiteral("Drift MCP HTTP %1").arg(status)
                                       : postError);
    }
    const QJsonArray content = QJsonDocument::fromJson(reply)
                                   .object()
                                   .value(QStringLiteral("result"))
                                   .toObject()
                                   .value(QStringLiteral("content"))
                                   .toArray();
    const QJsonObject answer =
        QJsonDocument::fromJson(
            content.at(0).toObject().value(QStringLiteral("text")).toString().toUtf8())
            .object();
    if (!answer.value(QStringLiteral("ok")).toBool())
        return answer; // not_isolated, busy, unsaved_changes: the instance says why

    QElapsedTimer timer;
    timer.start();
    while (drift::instances::processAlive(pid) && timer.elapsed() < 20000)
        QThread::msleep(100);
    if (drift::instances::processAlive(pid)) {
        return err("close_timeout",
                   QStringLiteral("%1 accepted quit_instance but is still running after 20 s")
                       .arg(describeSession(target)));
    }
    // Back to the attach rule rather than pointing at a pid that no longer exists.
    if (m_attached.pid == pid)
        m_attached = SessionInfo{};
    return ok({{QStringLiteral("closed"), pid}});
}

} // namespace drift::mcp
