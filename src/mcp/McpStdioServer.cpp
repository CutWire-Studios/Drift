#include "mcp/McpStdioServer.h"

#include "mcp/McpServer.h"
#include "mcp/McpStdio.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QThread>

#include <cstdio>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace drift::mcp {

McpStdioServer::McpStdioServer(McpServer *server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

McpStdioServer::~McpStdioServer()
{
    // A reader parked in a blocking read on stdin cannot be woken, and deleting a running
    // QThread aborts. The process is on its way out either way, so let this one go and
    // let the OS reclaim its stack.
    if (m_thread && !m_thread->isRunning())
        delete m_thread;
}

void McpStdioServer::start()
{
    if (m_thread)
        return;

#ifdef Q_OS_WIN
    // Text mode would rewrite \n as \r\n on the way out and split framing on the way in.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    m_thread = QThread::create([this] { readLoop(); });
    connect(m_thread, &QThread::finished, this, &McpStdioServer::finished);
    m_thread->start();
}

bool McpStdioServer::isReading() const
{
    return m_thread && m_thread->isRunning();
}

void McpStdioServer::readLoop()
{
    for (;;) {
        const QByteArray message = readStdioMessage(stdin);
        if (message.isEmpty())
            return;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(message, &parseError);
        if (!doc.isObject() && !doc.isArray()) {
            const QJsonObject error{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), QJsonValue::Null},
                {QStringLiteral("error"),
                 QJsonObject{{QStringLiteral("code"), -32700},
                             {QStringLiteral("message"), parseError.errorString()}}},
            };
            writeStdioMessage(stdout, QJsonDocument(error).toJson(QJsonDocument::Compact));
            continue;
        }
        const QJsonValue body = doc.isArray() ? QJsonValue(doc.array()) : QJsonValue(doc.object());

        // The dispatcher edits the project, so it has to run where the models live. Same
        // hop McpServer::start() gives the HTTP thread.
        QJsonValue result;
        QMetaObject::invokeMethod(
            m_server, [this, body, &result] { result = m_server->handleRpc(QString(), body); },
            Qt::BlockingQueuedConnection);

        // Notifications get no reply — the HTTP transport answers those with 202.
        if (result.isUndefined() || result.isNull())
            continue;

        writeStdioMessage(stdout,
                          result.isArray()
                              ? QJsonDocument(result.toArray()).toJson(QJsonDocument::Compact)
                              : QJsonDocument(result.toObject()).toJson(QJsonDocument::Compact));
    }
}

} // namespace drift::mcp
