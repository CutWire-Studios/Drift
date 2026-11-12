#include "app/mcp/transport/McpServer.h"
#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/mcp/McpController.h"
#include "app/mcp/tools/McpCatalog.h"
#include "app/mcp/tools/McpDispatcher.h"
#include "app/mcp/transport/McpHttp.h"
#include "app/mcp/protocol/McpJson.h"
#include "app/mcp/protocol/McpProtocol.h"

#include "app/mcp/protocol/McpSession.h"

#include <QDateTime>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSemaphore>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QSettings>
#include <QThread>
#include <QTimer>

namespace drift::mcp {

McpServer::McpServer(AppController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_dispatcher(std::make_unique<McpDispatcher>(controller))
{
    // The registry names each instance's project so an agent can pick the right editor;
    // that is only useful while it tracks what is actually open.
    if (ProjectFileController *project = controller ? controller->projectFile() : nullptr) {
        const auto republish = [this]() {
            if (m_wroteSessionFile)
                publishSession();
        };
        connect(project, &ProjectFileController::currentProjectPathChanged, this, republish);
        connect(project, &ProjectFileController::projectNameChanged, this, republish);
        connect(project, &ProjectFileController::projectLoadPendingChanged, this, republish);
        connect(project, &ProjectFileController::projectLoadFinished, this,
                [this, republish](bool ok, const QString &message) {
                    m_hasLoaded = true;
                    m_loadOk = ok;
                    m_loadMessage = message;
                    republish();
                });
    }
}

McpServer::~McpServer()
{
    stop();
}

QString McpServer::url() const
{
    if (!m_running || m_port == 0)
        return {};
    return QStringLiteral("http://127.0.0.1:%1/mcp").arg(m_port);
}

QString McpServer::cursorSnippet() const
{
    if (!m_running)
        return {};
    return QStringLiteral(
               "{\n"
               "  \"mcpServers\": {\n"
               "    \"drift\": {\n"
               "      \"url\": \"%1\",\n"
               "      \"headers\": {\n"
               "        \"Authorization\": \"Bearer %2\"\n"
               "      }\n"
               "    }\n"
               "  }\n"
               "}\n")
        .arg(url(), m_token);
}

QString McpServer::claudeCommand() const
{
    if (!m_running)
        return {};
    return QStringLiteral(
               "claude mcp add --transport http drift %1 --header \"Authorization: Bearer %2\"")
        .arg(url(), m_token);
}

QString McpServer::makeToken() const
{
    QString token;
    token.reserve(64);
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < 32; ++i)
        token += QString::number(rng->bounded(256), 16).rightJustified(2, QLatin1Char('0'));
    return token;
}

// A token that changed on every launch meant re-pasting the Cursor/Claude setup each
// session, so the first generated one is kept until rotateToken().
QString McpServer::persistedToken() const
{
    QSettings settings;
    const QString key = QStringLiteral("mcp/token");
    QString token = settings.value(key).toString();
    if (token.isEmpty()) {
        token = makeToken();
        settings.setValue(key, token);
    }
    return token;
}

void McpServer::rotateToken()
{
    const QString token = makeToken();
    QSettings().setValue(QStringLiteral("mcp/token"), token);
    if (!m_running)
        return;
    m_token = token;
    // Guarded on m_http's side, so this never waits for its thread (which may be waiting on this
    // one for a request).
    m_http->setToken(token);
    if (m_wroteSessionFile)
        publishSession();
    emit tokenChanged();
}

bool McpServer::start()
{
    if (m_running)
        return true;

    m_error.clear();
    m_token = m_fixedToken.isEmpty() ? persistedToken() : m_fixedToken;

    m_thread = new QThread(this);
    m_http = new McpHttp;
    m_http->setToken(m_token);
    m_stopping->storeRelease(0);
    // The request runs on this (the main) thread while the HTTP thread waits for it. Not a
    // blocking connection: stop() waits for the HTTP thread, so a request it is still waiting on
    // (one pipelined behind the call that asked to stop, say) would hold both forever. Once
    // stopping, the HTTP thread gives up on a request that hasn't started, and it never runs.
    m_http->setRpcHandler([this, stopping = m_stopping](const QString &toolbox, const QJsonValue &body) {
        struct Call
        {
            QAtomicInt state; // 0 queued, 1 running on the main thread, 2 abandoned
            QSemaphore done;
            QJsonValue result;
        };
        const auto stoppingError = [&body]() {
            // A single request's id, so the client can match it; a batch gets one error with none.
            const QJsonValue id = body.isObject() ? body.toObject().value(QStringLiteral("id")) : QJsonValue();
            return QJsonValue(QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), id.isUndefined() ? QJsonValue(QJsonValue::Null) : id},
                {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), -32000},
                                                      {QStringLiteral("message"), QStringLiteral("Drift's MCP server is stopping")}}}});
        };
        // Already stopping: don't queue anything (each would cost stop() a wait slice).
        if (stopping->loadAcquire())
            return stoppingError();
        auto call = std::make_shared<Call>();
        QMetaObject::invokeMethod(
            this,
            [this, call, toolbox, body]() {
                if (!call->state.testAndSetAcquire(0, 1))
                    return;
                call->result = handleRpc(toolbox, body);
                call->done.release();
            },
            Qt::QueuedConnection);
        while (!call->done.tryAcquire(1, 50)) {
            if (stopping->loadAcquire() && call->state.testAndSetAcquire(0, 2))
                return stoppingError();
        }
        return call->result;
    });
    m_http->moveToThread(m_thread);

    QObject::connect(
        m_http, &McpHttp::failed, this,
        [this](const QString &error) {
            m_error = error;
            emit errorChanged();
            stop();
        },
        Qt::QueuedConnection);

    m_thread->start();

    bool ok = false;
    quint16 port = 0;
    QEventLoop loop;
    QMetaObject::Connection listeningConn = QObject::connect(
        m_http, &McpHttp::listening, this, [&](quint16 p) {
            ok = true;
            port = p;
            loop.quit();
        });
    QMetaObject::Connection failedConn = QObject::connect(
        m_http, &McpHttp::failed, this, [&](const QString &error) {
            m_error = error;
            loop.quit();
        });
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    QMetaObject::invokeMethod(
        m_http, [this]() { m_http->listen(m_requestedPort); }, Qt::QueuedConnection);
    loop.exec();
    QObject::disconnect(listeningConn);
    QObject::disconnect(failedConn);

    if (!ok) {
        if (m_error.isEmpty())
            m_error = QStringLiteral("Could not bind 127.0.0.1");
        emit errorChanged();
        stop();
        return false;
    }
    m_port = port;
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    m_wroteSessionFile = publishSession();
    m_running = true;
    emit runningChanged();
    return true;
}

void McpServer::stop()
{
    m_stopping->storeRelease(1);
    if (m_wroteSessionFile) {
        removeSessionFile();
        m_wroteSessionFile = false;
    }
    if (m_http) {
        McpHttp *http = m_http;
        m_http = nullptr;
        QMetaObject::invokeMethod(
            http,
            [http]() {
                http->close();
                delete http;
            },
            Qt::BlockingQueuedConnection);
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(2000);
        delete m_thread;
        m_thread = nullptr;
    }
    const bool wasRunning = m_running;
    m_running = false;
    m_port = 0;
    m_token.clear();
    if (wasRunning)
        emit runningChanged();
}

bool McpServer::publishSession()
{
    SessionInfo info;
    info.port = m_port;
    info.token = m_token;
    info.startedAt = m_startedAt;
    info.headless = m_headless;
    if (ProjectFileController *project = m_controller ? m_controller->projectFile() : nullptr) {
        info.project = project->currentProjectPath();
        info.projectName = project->projectName();
        info.loading = project->projectLoadPending() || project->startupProjectQueued();
        info.isolated = project->isolatedSession();
    }
    info.hasLoaded = m_hasLoaded;
    info.loadOk = m_loadOk;
    info.loadMessage = m_loadMessage;
    return writeSessionFile(info);
}

QJsonValue McpServer::handleRpc(const QString &toolbox, const QJsonValue &body)
{
    return handleJsonRpc(body, toolbox, [this](const QString &name, const QJsonObject &args) {
        // Both transports reach here on this object's thread, so the log never needs a lock.
        const int id = ++m_callSeq;
        QString summary = QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
        if (summary.size() > 120)
            summary = summary.left(119) + QChar(0x2026);
        emit toolCallStarted(id, name, summary);
        QElapsedTimer timer;
        timer.start();
        const QJsonObject result = dispatchTool(name, args);
        const bool failed = result.value(QStringLiteral("isError")).toBool();
        QString error;
        if (failed) {
            const QJsonArray content = result.value(QStringLiteral("content")).toArray();
            if (!content.isEmpty())
                error = content.first().toObject().value(QStringLiteral("text")).toString().left(600);
        }
        emit toolCallFinished(id, !failed, error, timer.elapsed());
        emit toolCallResult(id, name, args, result);
        return result;
    });
}

QJsonObject McpServer::dispatchTool(const QString &name, const QJsonObject &argsIn)
{
    // Notifications raised while the agent's call runs are filtered by the agentNotifications
    // preference (AppController::setLastMessage).
    McpController *mcp = m_controller ? m_controller->mcp() : nullptr;
    if (mcp)
        mcp->beginAgentCall();
    const auto endCall = qScopeGuard([mcp] {
        if (mcp)
            mcp->endAgentCall();
    });
    // Homepage tools are checked against their own schemas and refuse unknown keys.
    QJsonObject args = argsIn;
    if (const QJsonObject invalid = checkHomepageArgs(name, args); !invalid.isEmpty())
        return textResult(invalid, true);
    if (name == QLatin1String("catalog"))
        return textResult(catalogPayload(args));
    if (name == QLatin1String("toolbox")) {
        QStringList only;
        for (const QJsonValue &v : args.value(QStringLiteral("ops")).toArray())
            only.append(v.toString());
        return textResult(toolboxPayload(args.value(QStringLiteral("name")).toString(), only));
    }
    if (name == QLatin1String("search")) {
        return textResult(searchOps(args.value(QStringLiteral("q")).toString(),
                                    args.value(QStringLiteral("limit")).toInt(8),
                                    args.value(QStringLiteral("schema")).toBool()));
    }
    if (name == QLatin1String("inspect"))
        return textResult(m_dispatcher->inspect(args));
    if (name == QLatin1String("apply"))
        return textResult(m_dispatcher->apply(args));
    if (name == QLatin1String("capture"))
        return m_dispatcher->capture(args);
    if (name == QLatin1String("frames"))
        return m_dispatcher->frames(args);
    if (name == QLatin1String("activity"))
        return textResult(m_dispatcher->activity(args));
    const QJsonObject result = m_dispatcher->applyOne(name, args);
    return isRawResult(result) ? result : textResult(result);
}

} // namespace drift::mcp

#include "moc_McpServer.cpp"
