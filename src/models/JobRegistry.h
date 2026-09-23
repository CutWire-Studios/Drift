#pragma once

#include <QAtomicInt>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QThreadPool>

#include <functional>
#include <memory>

// Background work driven over MCP and the UI: transcription, diarization, voice generation.
// Each job gets an id an agent can poll (get_job) and cancel (cancel_job).
class JobContext
{
public:
    // Thread-safe; fraction is 0–1.
    void progress(double fraction, const QString &status = {});
    bool cancelled() const { return m_cancel.loadRelaxed() != 0; }
    void fail(const QString &code, const QString &message);
    void succeed(const QJsonObject &result);

private:
    friend class JobRegistry;
    struct State;
    std::shared_ptr<State> m_state;
    QAtomicInt m_cancel;
};

class JobRegistry : public QObject
{
    Q_OBJECT
public:
    // Local models share one lane so two big ONNX sessions never load at once; network jobs
    // run beside them.
    enum class Lane { Model, Network };

    explicit JobRegistry(QObject *parent = nullptr);
    ~JobRegistry() override;

    // `work` runs on a pool thread. `done` runs on this object's thread afterwards with the final
    // job JSON; use it to land results in the project. Whatever it returns is merged into the
    // job's result (ids of what it created, say).
    QString start(const QString &kind, const QString &target, Lane lane,
                  std::function<void(JobContext &)> work,
                  std::function<QJsonObject(const QJsonObject &job)> done = {});
    bool cancel(const QString &id);
    QJsonObject job(const QString &id) const;
    QJsonArray jobs() const;
    bool hasActive(const QString &kind, const QString &target) const;

signals:
    void jobChanged(const QString &id);
    void jobFinished(const QString &id, bool ok);

private:
    struct Entry;
    QHash<QString, std::shared_ptr<Entry>> m_jobs;
    QStringList m_order;
    QThreadPool m_modelPool;
    QThreadPool m_networkPool;
};
