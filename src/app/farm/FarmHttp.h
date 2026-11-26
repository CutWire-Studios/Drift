#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTcpServer>
#include <QUrlQuery>

#include <functional>
#include <memory>

class QFile;
class QTcpSocket;

namespace drift::farm {

// A small HTTP/1.1 server for the farm's own protocol: JSON bodies, files streamed both ways, one
// request per connection. The MCP transport is loopback-only and JSON-only, which is why this is
// not that.
class FarmHttpServer : public QObject
{
    Q_OBJECT

public:
    struct Request
    {
        QByteArray method;
        QString path;
        QUrlQuery query;
        QByteArray body;      // small bodies
        QString bodyFile;     // large bodies the sink asked to have written to disk
    };

    // Completes one request; may be kept and answered later, from any slot on the server's thread.
    class Reply
    {
    public:
        Reply() = default;
        void json(int status, const QJsonObject &object) const;
        void error(int status, const QString &message) const;
        void file(const QString &path) const;

    private:
        friend class FarmHttpServer;
        QPointer<QTcpSocket> m_socket;
    };

    using Handler = std::function<void(const Request &, Reply)>;
    // Asked when a PUT's headers arrive: the file its body should stream into, or empty to buffer.
    using BodySink = std::function<QString(const QString &path, const QUrlQuery &query)>;

    explicit FarmHttpServer(QObject *parent = nullptr);
    ~FarmHttpServer() override;

    void setToken(const QString &token) { m_token = token; }
    void setHandler(Handler handler) { m_handler = std::move(handler); }
    void setBodySink(BodySink sink) { m_sink = std::move(sink); }

    bool listen(quint16 port);
    void close();
    quint16 port() const { return m_server.serverPort(); }
    QString error() const { return m_server.errorString(); }

private:
    struct Connection
    {
        QByteArray buffer;
        bool headerDone = false;
        Request request;
        qint64 contentLength = 0;
        qint64 received = 0;
        std::shared_ptr<QFile> bodyFile;
    };

    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void dispatch(QTcpSocket *socket);

    QTcpServer m_server;
    QHash<QTcpSocket *, Connection> m_connections;
    QString m_token;
    Handler m_handler;
    BodySink m_sink;
};

} // namespace drift::farm
