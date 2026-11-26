#include "app/farm/WorkerProcess.h"

#include <QCoreApplication>
#include <QJsonDocument>

namespace drift::farm {

WorkerProcess::WorkerProcess(QObject *parent) : QObject(parent) {}

WorkerProcess::~WorkerProcess()
{
    if (m_process) {
        m_process->disconnect(this);
        send(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("quit")}});
        m_process->closeWriteChannel();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            m_process->waitForFinished(1000);
        }
    }
}

void WorkerProcess::send(const QJsonObject &message)
{
    if (m_process && m_process->state() == QProcess::Running)
        m_process->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
}

void WorkerProcess::run(const RenderTask &task)
{
    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProgram(QCoreApplication::applicationFilePath());
        m_process->setArguments({QStringLiteral("--headless"), QStringLiteral("--worker")});
        m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        connect(m_process, &QProcess::readyReadStandardOutput, this, &WorkerProcess::onOutput);
        connect(m_process, &QProcess::finished, this, &WorkerProcess::onExit);
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                onExit();
        });
        m_process->start();
    }
    m_busy = true;
    send(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("run")}, {QStringLiteral("task"), task.toJson()}});
}

void WorkerProcess::cancel()
{
    send(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("cancel")}});
}

void WorkerProcess::onOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    int newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline);
        m_buffer.remove(0, newline + 1);
        const QJsonObject message = QJsonDocument::fromJson(line).object();
        const QString event = message.value(QStringLiteral("event")).toString();
        if (event == QLatin1String("progress")) {
            emit progress(message.value(QStringLiteral("fraction")).toDouble());
        } else if (event == QLatin1String("done")) {
            TaskResult result;
            result.ok = message.value(QStringLiteral("ok")).toBool();
            result.error = message.value(QStringLiteral("error")).toString();
            result.frames = static_cast<qint64>(message.value(QStringLiteral("frames")).toDouble());
            result.outputPath = message.value(QStringLiteral("outputPath")).toString();
            m_busy = false;
            emit finished(result);
        }
    }
}

void WorkerProcess::onExit()
{
    QProcess *process = m_process;
    m_process = nullptr;
    m_buffer.clear();
    if (process)
        process->deleteLater();
    if (m_busy) {
        m_busy = false;
        TaskResult result;
        result.error = QStringLiteral("The render worker process exited unexpectedly");
        emit finished(result);
    }
}

} // namespace drift::farm
