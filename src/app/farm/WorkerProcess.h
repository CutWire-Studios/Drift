#pragma once

#include "app/farm/RenderTask.h"

#include <QObject>
#include <QProcess>

namespace drift::farm {

// A `drift --headless --worker` child the editor hands one task at a time, so rendering does not
// share the editor's process. Talks JSON lines: {"cmd":"run","task":{...}} / {"cmd":"cancel"} in,
// {"event":"progress"|"done",...} out.
class WorkerProcess : public QObject
{
    Q_OBJECT

public:
    explicit WorkerProcess(QObject *parent = nullptr);
    ~WorkerProcess() override;

    bool busy() const { return m_busy; }
    void run(const RenderTask &task);
    void cancel();

signals:
    void progress(double fraction);
    void finished(const drift::farm::TaskResult &result);

private:
    void onOutput();
    void onExit();
    void send(const QJsonObject &message);

    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    bool m_busy = false;
};

} // namespace drift::farm
