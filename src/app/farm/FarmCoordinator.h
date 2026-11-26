#pragma once

#include "app/farm/FarmHttp.h"
#include "app/farm/RenderJob.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantList>

namespace drift::farm {

class RenderQueue;

// Hands the queue's farm-targeted jobs to workers over HTTP. A job is prepared once (media hashed,
// add-ons listed, project copied), then either split into chunks that workers render in parallel
// and that are joined losslessly here, or, where splitting is not safe, rendered whole by one
// worker. Workers poll for tasks; a worker that stops being heard from has its task given to
// another.
class FarmCoordinator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int listenPort READ listenPort WRITE setListenPort NOTIFY enabledChanged)
    Q_PROPERTY(int cacheGb READ cacheGb WRITE setCacheGb NOTIFY cacheGbChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(int port READ port NOTIFY runningChanged)
    Q_PROPERTY(QString address READ address NOTIFY runningChanged)
    Q_PROPERTY(QString token READ token NOTIFY tokenChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY runningChanged)
    Q_PROPERTY(QVariantList workers READ workers NOTIFY workersChanged)

public:
    explicit FarmCoordinator(RenderQueue *queue, QObject *parent = nullptr);
    ~FarmCoordinator() override;

    // The user's switch, remembered across launches; start()/stop() are the mechanism under it.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    int listenPort() const { return m_listenPort; }
    void setListenPort(int port);
    // How much media a worker started on this machine keeps cached (read by --farm-worker).
    int cacheGb() const;
    void setCacheGb(int gb);
    // Brings the coordinator up if it was left enabled.
    void restore();
    bool running() const { return m_listening; }
    int port() const { return m_port; }
    QString address() const;
    QString token() const { return m_token; }
    QString lastError() const { return m_error; }
    QVariantList workers() const;

    Q_INVOKABLE bool start(int port);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setToken(const QString &token);
    Q_INVOKABLE QString regenerateToken();

    static QString generateToken();

signals:
    void runningChanged();
    void enabledChanged();
    void cacheGbChanged();
    void tokenChanged();
    void workersChanged();

    // Public so the prepare step, a free function on a worker thread, can fill them in.
public:
    struct WorkerInfo
    {
        QString id;
        QString name;
        QString gpu;
        QStringList encoders;
        QElapsedTimer seen;
        QString taskId;
        double fps = 0;
        double fraction = 0;
        qint64 lastFramesDone = 0;
        QElapsedTimer lastBeat;
        int tasksDone = 0;
    };
    struct FarmTask
    {
        enum class State { Pending, Running, Done };
        QString id;
        int chunk = -1; // -1: the whole job
        QVariantMap settings;
        State state = State::Pending;
        QString worker;
        int attempts = 0;
        int reassigns = 0;
        QSet<QString> refused;
        QSet<QString> avoid;
        QString resultFile;
        qint64 expectedFrames = 0;
        double fraction = 0;
        QString lastError;
    };
    struct JobPlan
    {
        QString jobId;
        bool preparing = true;
        bool chunked = false;
        bool wholeOnly = false;
        bool joining = false;
        bool audioRunning = false;
        bool audioDone = true;
        double audioFraction = 0;
        QString error;
        QStringList notes;
        QString workDir;
        QString projectFile;
        QString ext;
        QString audioFile;
        QString output;
        QVariantMap settings;
        QJsonArray media;
        QJsonArray addons;
        qint64 totalFrames = 0;
        int fpsNum = 30;
        int fpsDen = 1;
        qint64 startUs = 0;
        qint64 endUs = 0;
        QList<qint64> edgesUs;
        QHash<QString, QString> mediaPaths;
        QList<FarmTask> tasks;
    };

private:
    void tick();
    void tickOnce();
    void route(const FarmHttpServer::Request &request, FarmHttpServer::Reply reply);
    void beginPrepare(const QString &jobId);
    void planTasks(JobPlan &plan);
    void startAudio(JobPlan &plan);
    void maybeFinish(JobPlan &plan);
    void failJob(JobPlan &plan, const QString &error);
    void finishJob(JobPlan &plan, bool ok, const QString &error);
    void publishJob(JobPlan &plan);
    FarmTask *findTask(const QString &taskId, JobPlan **planOut = nullptr);
    QJsonObject nextTaskFor(WorkerInfo &worker);
    void taskDone(WorkerInfo &worker, FarmTask &task, JobPlan &plan, bool ok, const QString &error, qint64 frames);
    QString workDirRoot() const;

    RenderQueue *m_queue;
    FarmHttpServer m_http;
    QTimer m_tick;
    QString m_token;
    QString m_error;
    int m_port = 0;
    int m_listenPort = 4800;
    bool m_enabled = false;
    bool m_listening = false;
    QHash<QString, WorkerInfo> m_workers;
    QHash<QString, JobPlan> m_plans;
    QHash<QString, QString> m_media; // content hash -> path, for the files jobs asked workers to fetch
    int m_workerCounter = 0;
    bool m_workersDirty = false;
    bool m_inTick = false;
    bool m_tickAgain = false;
};

} // namespace drift::farm
