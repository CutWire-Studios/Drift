#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <memory>

class AppController;
class QThread;

namespace drift::mcp {
class McpHttp;
class McpDispatcher;

class McpServer : public QObject
{
    Q_OBJECT

public:
    explicit McpServer(AppController *controller, QObject *parent = nullptr);
    ~McpServer() override;

    bool running() const { return m_running; }
    quint16 port() const { return m_port; }
    QString token() const { return m_token; }
    QString url() const;
    QString error() const { return m_error; }

    QString cursorSnippet() const;
    QString claudeCommand() const;

    // Headless overrides, applied by the next start(). The editor sets neither and keeps
    // the defaults: the token persisted in QSettings (generated on first start) on port 4731.
    void setToken(const QString &token) { m_fixedToken = token; }
    void setPort(quint16 port) { m_requestedPort = port; }
    // Recorded in the session registry so a bridge can tell a headless worker from an editor.
    void setHeadless(bool headless) { m_headless = headless; }

    // Replaces the persisted token, live if the server is running. Every assistant set up
    // with the old one has to be re-pointed; that is the point.
    void rotateToken();

public slots:
    bool start();
    void stop();
    QJsonValue handleRpc(const QString &toolbox, const QJsonValue &body);

signals:
    void runningChanged();
    void tokenChanged();
    void errorChanged();
    void toolCallStarted(int id, const QString &tool, const QString &summary);
    void toolCallFinished(int id, bool ok, const QString &error, qint64 ms);
    void toolCallResult(int id, const QString &tool, const QJsonObject &args, const QJsonObject &result);

private:
    QString makeToken() const;
    QString persistedToken() const;
    bool publishSession();
    int m_callSeq = 0;
    QJsonObject dispatchTool(const QString &name, const QJsonObject &args);

    AppController *m_controller = nullptr;
    std::unique_ptr<McpDispatcher> m_dispatcher;
    QThread *m_thread = nullptr;
    McpHttp *m_http = nullptr;
    QString m_token;
    QString m_fixedToken;
    QString m_error;
    quint16 m_port = 0;
    quint16 m_requestedPort = 4731;
    qint64 m_startedAt = 0;
    bool m_headless = false;
    // The last project load to finish, for the session record (launch_instance waits on it).
    bool m_hasLoaded = false;
    bool m_loadOk = false;
    QString m_loadMessage;
    // stop() must not delete a session file this server never wrote: a headless instance
    // serving only stdio would otherwise unregister a GUI editor running alongside it.
    bool m_wroteSessionFile = false;
    bool m_running = false;
};

} // namespace drift::mcp
