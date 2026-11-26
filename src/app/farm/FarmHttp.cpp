#include "app/farm/FarmHttp.h"

#include <QFile>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUrl>

#include <limits>

namespace drift::farm {

namespace {

QByteArray statusLine(int status)
{
    switch (status) {
    case 200: return "HTTP/1.1 200 OK\r\n";
    case 204: return "HTTP/1.1 204 No Content\r\n";
    case 400: return "HTTP/1.1 400 Bad Request\r\n";
    case 401: return "HTTP/1.1 401 Unauthorized\r\n";
    case 404: return "HTTP/1.1 404 Not Found\r\n";
    case 409: return "HTTP/1.1 409 Conflict\r\n";
    default: return "HTTP/1.1 500 Internal Server Error\r\n";
    }
}

void writeHead(QTcpSocket *socket, int status, const QByteArray &type, qint64 length)
{
    socket->write(statusLine(status) + "Content-Type: " + type + "\r\nContent-Length: " + QByteArray::number(length)
                  + "\r\nConnection: close\r\n\r\n");
}

} // namespace

void FarmHttpServer::Reply::json(int status, const QJsonObject &object) const
{
    if (!m_socket)
        return;
    const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
    writeHead(m_socket, status, "application/json", body.size());
    m_socket->write(body);
    m_socket->disconnectFromHost();
}

void FarmHttpServer::Reply::error(int status, const QString &message) const
{
    json(status, QJsonObject{{QStringLiteral("error"), message}});
}

void FarmHttpServer::Reply::file(const QString &path) const
{
    if (!m_socket)
        return;
    auto file = std::make_shared<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        error(404, QStringLiteral("File is gone"));
        return;
    }
    QTcpSocket *socket = m_socket;
    writeHead(socket, 200, "application/octet-stream", file->size());
    const auto pump = [socket, file] {
        while (socket->bytesToWrite() < (4 << 20) && !file->atEnd())
            socket->write(file->read(256 << 10));
        if (file->atEnd() && socket->bytesToWrite() == 0)
            socket->disconnectFromHost();
    };
    QObject::connect(socket, &QTcpSocket::bytesWritten, socket, pump);
    pump();
}

FarmHttpServer::FarmHttpServer(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &FarmHttpServer::onNewConnection);
}

FarmHttpServer::~FarmHttpServer()
{
    close();
}

bool FarmHttpServer::listen(quint16 port)
{
    return m_server.listen(QHostAddress::Any, port);
}

void FarmHttpServer::close()
{
    m_server.close();
    const auto sockets = m_connections.keys();
    for (QTcpSocket *socket : sockets)
        socket->abort();
}

void FarmHttpServer::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        m_connections.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_connections.remove(socket);
            socket->deleteLater();
        });
    }
}

void FarmHttpServer::onReadyRead(QTcpSocket *socket)
{
    auto it = m_connections.find(socket);
    if (it == m_connections.end())
        return;
    Connection &conn = it.value();
    conn.buffer += socket->readAll();

    if (!conn.headerDone) {
        const int end = conn.buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            if (conn.buffer.size() > (64 << 10))
                socket->abort();
            return;
        }
        const QList<QByteArray> lines = conn.buffer.left(end).split('\n');
        conn.buffer.remove(0, end + 4);
        const QList<QByteArray> start = lines.value(0).trimmed().split(' ');
        if (start.size() < 2) {
            socket->abort();
            return;
        }
        conn.request.method = start.at(0);
        const QUrl target = QUrl::fromEncoded(start.at(1));
        conn.request.path = target.path();
        conn.request.query = QUrlQuery(target);
        QByteArray authorization;
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines.at(i).indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray name = lines.at(i).left(colon).trimmed().toLower();
            const QByteArray value = lines.at(i).mid(colon + 1).trimmed();
            if (name == "content-length")
                conn.contentLength = value.toLongLong();
            else if (name == "authorization")
                authorization = value;
        }
        conn.headerDone = true;

        Reply reply;
        reply.m_socket = socket;
        if (authorization != "Bearer " + m_token.toUtf8()) {
            reply.error(401, QStringLiteral("Bad or missing token"));
            return;
        }
        if (conn.request.method == "PUT" && m_sink) {
            const QString path = m_sink(conn.request.path, conn.request.query);
            if (!path.isEmpty()) {
                conn.bodyFile = std::make_shared<QFile>(path);
                if (!conn.bodyFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    reply.error(500, QStringLiteral("Could not open the destination"));
                    return;
                }
                conn.request.bodyFile = path;
            }
        }
    }

    if (!conn.buffer.isEmpty()) {
        if (conn.bodyFile)
            conn.bodyFile->write(conn.buffer);
        else
            conn.request.body += conn.buffer;
        conn.received += conn.buffer.size();
        conn.buffer.clear();
    }
    if (conn.received >= conn.contentLength) {
        if (conn.bodyFile)
            conn.bodyFile->close();
        dispatch(socket);
    }
}

void FarmHttpServer::dispatch(QTcpSocket *socket)
{
    const Request request = m_connections.value(socket).request;
    // A second readyRead on the same socket must not dispatch again.
    m_connections[socket].contentLength = std::numeric_limits<qint64>::max();
    Reply reply;
    reply.m_socket = socket;
    if (m_handler)
        m_handler(request, reply);
    else
        reply.error(404, QStringLiteral("No handler"));
}

} // namespace drift::farm
