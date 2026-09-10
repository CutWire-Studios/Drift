#include "mcp/McpServer.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpDispatcher.h"
#include "mcp/McpHttp.h"
#include "mcp/McpJson.h"
#include "mcp/McpProtocol.h"
#include "mcp/McpSession.h"

#include <QEventLoop>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QThread>
#include <QTimer>

namespace drift::mcp {

McpServer::McpServer(AppController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_dispatcher(std::make_unique<McpDispatcher>(controller))
{
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

bool McpServer::start()
{
    if (m_running)
        return true;

    m_error.clear();
    m_token = m_fixedToken.isEmpty() ? makeToken() : m_fixedToken;

    m_thread = new QThread(this);
    m_http = new McpHttp;
    m_http->setToken(m_token);
    m_http->setRpcHandler([this](const QString &toolbox, const QJsonValue &body) {
        QJsonValue result;
        QMetaObject::invokeMethod(
            this,
            [this, toolbox, body, &result]() { result = handleRpc(toolbox, body); },
            Qt::BlockingQueuedConnection);
        return result;
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
    m_wroteSessionFile = writeSessionFile(port, m_token);
    m_running = true;
    emit runningChanged();
    return true;
}

void McpServer::stop()
{
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

QJsonValue McpServer::handleRpc(const QString &toolbox, const QJsonValue &body)
{
    return handleJsonRpc(body, toolbox, [this](const QString &name, const QJsonObject &args) {
        return dispatchTool(name, args);
    });
}

QJsonObject McpServer::dispatchTool(const QString &name, const QJsonObject &args)
{
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
