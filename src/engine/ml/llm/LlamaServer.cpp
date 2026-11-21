#include "engine/ml/llm/LlamaServer.h"

#include "engine/addons/PackageSearchPaths.h"

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QtCore/qglobal.h>
#if QT_CONFIG(process)
#include <QProcess>
#include <QProcessEnvironment>
#endif
#include <QTcpServer>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <utility>

namespace drift::llm {

namespace {

constexpr int kStartTimeoutMs = 60000;
constexpr int kPollIntervalMs = 250;
constexpr int kLogLimit = 4096;

QString serverName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("llama-server.exe");
#else
    return QStringLiteral("llama-server");
#endif
}

int freeLoopbackPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const int port = probe.serverPort();
    probe.close();
    return port;
}

QString serverPathIn(const QString &root)
{
    const QString path = QDir(root).filePath(serverName());
    return QFileInfo(path).isFile() ? path : QString();
}

} // namespace

LlamaServer::LlamaServer(QObject *parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &LlamaServer::pollHealth);
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, &LlamaServer::idleExpired);
}

LlamaServer::~LlamaServer()
{
    killProcess();
}

QString LlamaServer::runtimeRoot()
{
    const QStringList roots = PackageSearchPaths::defaultSearchPaths(
        QStringLiteral("DRIFT_LLAMA_RUNTIME"), QStringLiteral("llama-runtime"),
        QStringLiteral("llama-runtime"));
    for (const QString &root : roots) {
        if (!serverPathIn(root).isEmpty())
            return root;
    }
    return {};
}

bool LlamaServer::available()
{
#if QT_CONFIG(process)
    return !runtimeRoot().isEmpty();
#else
    return false;
#endif
}

int LlamaServer::defaultThreads()
{
    return qMax(1, qMin(8, QThread::idealThreadCount() - 1));
}

void LlamaServer::setIdleTimeoutSeconds(int seconds)
{
    m_idleSeconds = qMax(0, seconds);
    touchIdle();
}

void LlamaServer::start(const QString &modelPath, int threads)
{
    if (m_state != State::Stopped)
        return;
    m_model = modelPath;
    m_threads = threads > 0 ? threads : defaultThreads();
    m_log.clear();

    const QString root = runtimeRoot();
    if (root.isEmpty()) {
        fail(tr("The llama.cpp runtime is not installed."));
        return;
    }
    if (!QFileInfo(modelPath).isFile()) {
        fail(tr("The command model file is missing."));
        return;
    }
#if !QT_CONFIG(process)
    fail(tr("Local language models are not supported on this platform."));
#else
    m_port = freeLoopbackPort();
    if (m_port == 0) {
        fail(tr("No free local port for the language model."));
        return;
    }

    m_state = State::Starting;
    m_stopping = false;
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(root);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#if defined(Q_OS_LINUX)
    const QString var = QStringLiteral("LD_LIBRARY_PATH");
#elif defined(Q_OS_MACOS)
    const QString var = QStringLiteral("DYLD_LIBRARY_PATH");
#endif
#if defined(Q_OS_LINUX) || defined(Q_OS_MACOS)
    const QString old = env.value(var);
    env.insert(var, old.isEmpty() ? root : root + QLatin1Char(':') + old);
#endif
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyRead, this, [this] {
        m_log += QString::fromLocal8Bit(m_process->readAll());
        if (m_log.size() > kLogLimit)
            m_log = m_log.right(kLogLimit);
    });
    QProcess *process = m_process;
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (process == m_process && error == QProcess::FailedToStart)
            fail(tr("Could not start llama-server: %1").arg(process->errorString()));
    });
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus) {
        if (process != m_process)
            return;
        if (m_stopping)
            return;
        fail(tr("llama-server exited (code %1).").arg(code));
    });

    const QStringList args = {QStringLiteral("-m"), modelPath, QStringLiteral("-c"), QStringLiteral("8192"),
                              QStringLiteral("-t"), QString::number(m_threads), QStringLiteral("-np"),
                              QStringLiteral("1"), QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
                              QStringLiteral("--port"), QString::number(m_port), QStringLiteral("--no-webui")};
    m_process->start(serverPathIn(root), args);

    if (!m_net) {
        m_net = new QNetworkAccessManager(this);
        m_net->setProxy(QNetworkProxy::NoProxy);
    }
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    m_healthInFlight = false;
    m_pollTimer->start();
#endif
}

void LlamaServer::pollHealth()
{
    if (m_state != State::Starting || m_healthInFlight)
        return;
    if (QDateTime::currentMSecsSinceEpoch() - m_startedAt > kStartTimeoutMs) {
        fail(tr("llama-server did not become ready within 60 seconds."));
        return;
    }
    m_healthInFlight = true;
    QNetworkReply *reply = m_net->get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(m_port))));
    const int port = m_port;
    connect(reply, &QNetworkReply::finished, this, [this, reply, port] {
        reply->deleteLater();
        m_healthInFlight = false;
        if (m_state != State::Starting || port != m_port)
            return;
        if (reply->error() != QNetworkReply::NoError)
            return;
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        m_pollTimer->stop();
        m_state = State::Running;
        emit ready();
        const QList<Pending> queued = std::exchange(m_pending, {});
        for (const Pending &request : queued)
            send(request);
        touchIdle();
    });
}

void LlamaServer::fail(const QString &reason)
{
    QString detail = reason;
    const QString tail = m_log.trimmed();
    if (!tail.isEmpty())
        detail += QLatin1Char('\n') + tail;
    killProcess();
    const QList<Pending> queued = std::exchange(m_pending, {});
    emit failed(detail);
    for (const Pending &request : queued) {
        Completion c;
        c.error = detail;
        request.callback(c);
    }
}

void LlamaServer::killProcess()
{
    m_pollTimer->stop();
    m_idleTimer->stop();
    m_stopping = true;
#if QT_CONFIG(process)
    if (m_process) {
        QProcess *process = std::exchange(m_process, nullptr);
        process->disconnect(this);
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(2000)) {
                process->kill();
                process->waitForFinished(2000);
            }
        }
        process->deleteLater();
    }
#endif
    m_state = State::Stopped;
}

void LlamaServer::stop()
{
    const bool wasUp = m_state != State::Stopped;
    killProcess();
    const QList<Pending> queued = std::exchange(m_pending, {});
    for (const Pending &request : queued) {
        Completion c;
        c.error = tr("The language model was stopped.");
        request.callback(c);
    }
    if (wasUp)
        emit stopped();
}

void LlamaServer::complete(const QString &prompt, int nPredict, std::function<void(const Completion &)> callback)
{
    const Pending request{prompt, nPredict, std::move(callback)};
    if (m_state == State::Running) {
        send(request);
        return;
    }
    m_pending.append(request);
    if (m_state == State::Stopped)
        start(m_model, m_threads);
}

void LlamaServer::send(const Pending &request)
{
    m_idleTimer->stop();
    ++m_inFlight;
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1/completion").arg(m_port)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(300000);
    const QJsonObject body{{QStringLiteral("prompt"), request.prompt},
                           {QStringLiteral("n_predict"), request.nPredict},
                           {QStringLiteral("temperature"), 0},
                           {QStringLiteral("cache_prompt"), true}};
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    QNetworkReply *reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    const auto callback = request.callback;
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback, clock] {
        reply->deleteLater();
        --m_inFlight;
        Completion c;
        c.totalMs = double(clock->elapsed());
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            c.error = reply->errorString();
        } else {
            const QJsonObject json = QJsonDocument::fromJson(data).object();
            if (!json.contains(QStringLiteral("content"))) {
                c.error = tr("Unexpected reply from llama-server.");
            } else {
                c.ok = true;
                c.content = json.value(QStringLiteral("content")).toString();
                const QJsonObject t = json.value(QStringLiteral("timings")).toObject();
                c.promptTokens = t.value(QStringLiteral("prompt_n")).toInt();
                c.predictedTokens = t.value(QStringLiteral("predicted_n")).toInt();
                c.promptMs = t.value(QStringLiteral("prompt_ms")).toDouble();
                c.predictedMs = t.value(QStringLiteral("predicted_ms")).toDouble();
            }
        }
        touchIdle();
        callback(c);
    });
}

void LlamaServer::touchIdle()
{
    if (m_state != State::Running || m_idleSeconds <= 0)
        m_idleTimer->stop();
    else if (m_inFlight == 0)
        m_idleTimer->start(m_idleSeconds * 1000);
}

void LlamaServer::idleExpired()
{
    if (m_inFlight == 0 && m_pending.isEmpty())
        stop();
}

} // namespace drift::llm
