#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QNetworkAccessManager;
class QProcess;
class QTimer;

namespace drift::llm {

struct Completion
{
    bool ok = false;
    QString content;
    QString error;
    int promptTokens = 0;
    int predictedTokens = 0;
    double promptMs = 0;
    double predictedMs = 0;
    double totalMs = 0; // wall clock, request to reply
};

// One llama.cpp `llama-server` child process, started on first use and stopped after an idle
// spell. The binary comes from an installed addon of kind "llama-runtime" (or $DRIFT_LLAMA_RUNTIME);
// the model path is the caller's. Requests go to the raw /completion endpoint, so the caller
// supplies the fully rendered prompt.
class LlamaServer : public QObject
{
    Q_OBJECT

public:
    explicit LlamaServer(QObject *parent = nullptr);
    ~LlamaServer() override;

    // First search root that holds the server binary, or empty.
    static QString runtimeRoot();
    static bool available();
    static int defaultThreads();

    // Begins loading the model (no-op while starting or running). Emits ready() or failed().
    // threads <= 0 picks defaultThreads().
    void start(const QString &modelPath, int threads = 0);
    void stop();
    bool isRunning() const { return m_state == State::Running; }
    bool isStarting() const { return m_state == State::Starting; }

    // POST /completion with temperature 0 and prompt caching. Starts the server with the last
    // start() arguments if it is not up; the callback runs once, in this object's thread.
    void complete(const QString &prompt, int nPredict, std::function<void(const Completion &)> callback);

    void setIdleTimeoutSeconds(int seconds);
    int idleTimeoutSeconds() const { return m_idleSeconds; }
    int port() const { return m_port; }
    QString stderrTail() const { return m_log; }

signals:
    void ready();
    void failed(const QString &reason);
    void stopped();

private:
    enum class State { Stopped, Starting, Running };
    struct Pending
    {
        QString prompt;
        int nPredict = 0;
        std::function<void(const Completion &)> callback;
    };

    void pollHealth();
    void fail(const QString &reason);
    void send(const Pending &request);
    void touchIdle();
    void idleExpired();
    void killProcess();

    State m_state = State::Stopped;
    QProcess *m_process = nullptr;
    QNetworkAccessManager *m_net = nullptr;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_idleTimer = nullptr;
    QString m_model;
    int m_threads = 0;
    int m_port = 0;
    int m_idleSeconds = 300;
    int m_inFlight = 0;
    qint64 m_startedAt = 0;
    bool m_healthInFlight = false;
    bool m_stopping = false;
    QString m_log;
    QList<Pending> m_pending;
};

} // namespace drift::llm
