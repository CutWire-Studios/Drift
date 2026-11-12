#include "app/mcp/transport/McpHttp.h"

#include <QElapsedTimer>
#include "app/mcp/tools/McpCatalog.h"

#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>

namespace drift::mcp {
namespace {

QByteArray headerValue(const QByteArray &header, const QByteArray &name)
{
    const QByteArray lower = header.toLower();
    const QByteArray key = name.toLower() + ':';
    int from = 0;
    while (true) {
        const int at = lower.indexOf(key, from);
        if (at < 0)
            return {};
        if (at == 0 || header.at(at - 1) == '\n') {
            int start = at + key.size();
            while (start < header.size() && (header.at(start) == ' ' || header.at(start) == '\t'))
                ++start;
            int end = header.indexOf('\r', start);
            if (end < 0)
                end = header.indexOf('\n', start);
            if (end < 0)
                end = header.size();
            return header.mid(start, end - start);
        }
        from = at + 1;
    }
}

QByteArray requestLine(const QByteArray &header)
{
    const int nl = header.indexOf('\n');
    QByteArray line = nl < 0 ? header : header.left(nl);
    if (line.endsWith('\r'))
        line.chop(1);
    return line;
}

QByteArray requestPath(const QByteArray &header)
{
    const QByteArray line = requestLine(header);
    const int sp1 = line.indexOf(' ');
    if (sp1 < 0)
        return {};
    const int sp2 = line.indexOf(' ', sp1 + 1);
    QByteArray path = sp2 < 0 ? line.mid(sp1 + 1) : line.mid(sp1 + 1, sp2 - sp1 - 1);
    const int q = path.indexOf('?');
    if (q >= 0)
        path = path.left(q);
    return path;
}

QByteArray requestMethod(const QByteArray &header)
{
    const QByteArray line = requestLine(header);
    const int sp = line.indexOf(' ');
    return sp < 0 ? line : line.left(sp);
}

bool tokensEqual(const QByteArray &got, const QByteArray &want)
{
    if (got.size() != want.size())
        return false;
    unsigned char diff = 0;
    for (int i = 0; i < got.size(); ++i)
        diff |= static_cast<unsigned char>(got.at(i)) ^ static_cast<unsigned char>(want.at(i));
    return diff == 0;
}

QByteArray corsHeaders()
{
    return "Access-Control-Allow-Origin: *\r\n"
           "Access-Control-Allow-Headers: Authorization, Content-Type, MCP-Protocol-Version\r\n"
           "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
}

void writeResponse(QTcpSocket *socket, int status, const char *reason, const QByteArray &contentType,
                   const QByteArray &body, bool keepAlive)
{
    QByteArray out;
    out += "HTTP/1.1 ";
    out += QByteArray::number(status);
    out += ' ';
    out += reason;
    out += "\r\n";
    out += corsHeaders();
    if (!contentType.isEmpty()) {
        out += "Content-Type: ";
        out += contentType;
        out += "\r\n";
    }
    out += "Content-Length: ";
    out += QByteArray::number(body.size());
    out += "\r\nConnection: ";
    out += keepAlive ? "keep-alive" : "close";
    out += "\r\n\r\n";
    out += body;
    socket->write(out);
    if (!keepAlive)
        socket->disconnectFromHost();
}

QString toolboxFromPath(const QByteArray &path)
{
    if (path == "/mcp")
        return {};
    const QByteArray prefix = "/mcp/";
    if (!path.startsWith(prefix))
        return {};
    return QString::fromUtf8(path.mid(prefix.size())).trimmed().toLower();
}

} // namespace

McpHttp::McpHttp(QObject *parent)
    : QObject(parent)
{
}

bool McpHttp::listen(quint16 preferredPort)
{
    if (m_server)
        close();

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &McpHttp::onNewConnection);

    const QHostAddress loopback(QHostAddress::LocalHost);
    quint16 port = preferredPort;
    if (!m_server->listen(loopback, port)) {
        if (!m_server->listen(loopback, 0)) {
            emit failed(m_server->errorString());
            return false;
        }
    }
    m_port = m_server->serverPort();
    emit listening(m_port);
    return true;
}

void McpHttp::close()
{
    // Replies already written (the call that asked to stop, say) still reach their clients:
    // deleting a socket aborts it, dropping whatever it hadn't sent yet.
    for (QTcpSocket *socket : findChildren<QTcpSocket *>()) {
        socket->flush();
        // Each wait returns after one chunk; a large reply (a capture) takes several.
        QElapsedTimer sending;
        sending.start();
        while (socket->bytesToWrite() > 0 && sending.elapsed() < 1000)
            socket->waitForBytesWritten(int(1000 - sending.elapsed()));
        socket->disconnectFromHost();
    }
    m_buffers.clear();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_port = 0;
}

void McpHttp::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void McpHttp::onReadyRead(QTcpSocket *socket)
{
    m_buffers[socket] += socket->readAll();
    QByteArray &buf = m_buffers[socket];

    while (true) {
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray header = buf.left(headerEnd);
        const int contentLength = headerValue(header, "Content-Length").toInt();
        const int bodyStart = headerEnd + 4;
        if (buf.size() < bodyStart + contentLength)
            return;
        const QByteArray body = buf.mid(bodyStart, contentLength);
        buf.remove(0, bodyStart + contentLength);
        handleRequest(socket, header, body);
        if (buf.isEmpty())
            return;
    }
}

bool McpHttp::authorized(const QByteArray &header) const
{
    const QByteArray raw = headerValue(header, "Authorization");
    QByteArray got = raw;
    if (got.startsWith("Bearer ") || got.startsWith("bearer "))
        got = got.mid(7).trimmed();
    QMutexLocker lock(&m_tokenMutex);
    return tokensEqual(got, m_token.toUtf8());
}

void McpHttp::handleRequest(QTcpSocket *socket, const QByteArray &header, const QByteArray &body)
{
    const QByteArray method = requestMethod(header);
    const QByteArray path = requestPath(header);
    const bool keepAlive = headerValue(header, "Connection").toLower() != "close";

    if (method == "OPTIONS") {
        writeResponse(socket, 204, "No Content", {}, {}, keepAlive);
        return;
    }

    if (method == "GET" && path == "/") {
        const QByteArray html = homepageHtml().toUtf8();
        writeResponse(socket, 200, "OK", "text/html; charset=utf-8", html, keepAlive);
        return;
    }

    if (method == "GET" && path == "/health") {
        writeResponse(socket, 200, "OK", "application/json", "{\"ok\":true}\n", keepAlive);
        return;
    }

    if (path != "/mcp" && !path.startsWith("/mcp/")) {
        writeResponse(socket, 404, "Not Found", "text/plain", "not found\n", false);
        return;
    }

    if (method == "GET") {
        writeResponse(socket, 405, "Method Not Allowed", "text/plain", "POST JSON-RPC to this path\n",
                      keepAlive);
        return;
    }

    if (method != "POST") {
        writeResponse(socket, 405, "Method Not Allowed", "text/plain", "method not allowed\n", false);
        return;
    }

    if (!authorized(header)) {
        writeResponse(socket, 401, "Unauthorized", "application/json",
                      "{\"error\":\"unauthorized\"}\n", false);
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    QJsonValue rpcBody = QJsonValue::Null;
    if (doc.isObject())
        rpcBody = doc.object();
    else if (doc.isArray())
        rpcBody = doc.array();
    else {
        writeResponse(socket, 400, "Bad Request", "application/json",
                      "{\"error\":\"invalid json\"}\n", keepAlive);
        return;
    }

    const QString toolbox = toolboxFromPath(path);
    if (!path.startsWith("/mcp/") && path != "/mcp") {
        writeResponse(socket, 404, "Not Found", "text/plain", "not found\n", false);
        return;
    }
    if (path.startsWith("/mcp/") && !toolboxNames().contains(toolbox)) {
        writeResponse(socket, 404, "Not Found", "application/json",
                      "{\"error\":\"unknown_toolbox\"}\n", keepAlive);
        return;
    }

    if (!m_handler) {
        writeResponse(socket, 500, "Internal Server Error", "text/plain", "no handler\n", false);
        return;
    }

    const QJsonValue result = m_handler(path == "/mcp" ? QString() : toolbox, rpcBody);
    if (result.isUndefined() || result.isNull()) {
        writeResponse(socket, 202, "Accepted", {}, {}, keepAlive);
        return;
    }

    const QByteArray payload = result.isArray()
                                   ? QJsonDocument(result.toArray()).toJson(QJsonDocument::Compact)
                                   : QJsonDocument(result.toObject()).toJson(QJsonDocument::Compact);
    writeResponse(socket, 200, "OK", "application/json", payload + '\n', keepAlive);
}

} // namespace drift::mcp

#include "moc_McpHttp.cpp"
