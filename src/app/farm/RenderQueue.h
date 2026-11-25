#pragma once

#include "app/farm/RenderJob.h"
#include "app/farm/RenderTask.h"

#include <QAbstractListModel>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <functional>
#include <memory>

namespace drift::farm {

class WorkerProcess;

// The persistent list of renders waiting, running and finished. Jobs aimed at this machine are run
// one at a time here, in the editor's process or in a `--headless --worker` child; jobs aimed at
// the farm are only stored here and driven by FarmCoordinator through update().
class RenderQueue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool autoStart READ autoStart WRITE setAutoStart NOTIFY autoStartChanged)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
    Q_PROPERTY(bool useWorkerProcess READ useWorkerProcess WRITE setUseWorkerProcess NOTIFY useWorkerProcessChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        StateRole,
        ProgressRole,
        ErrorRole,
        OutputRole,
        TargetRole,
        LogRole,
        WorkerRole,
        ChunkDoneRole,
        ChunkTotalRole,
        CreatedRole,
        StartedMsRole,
        FinishedMsRole,
    };

    // `directory` holds queue.json and the project snapshots; empty uses the app data folder.
    explicit RenderQueue(const QString &directory = {}, QObject *parent = nullptr);
    ~RenderQueue() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool autoStart() const { return m_autoStart; }
    void setAutoStart(bool on);
    bool paused() const { return m_paused; }
    void setPaused(bool on);
    bool useWorkerProcess() const { return m_useProcess; }
    void setUseWorkerProcess(bool on);
    int activeCount() const;

    // What `addCurrent` snapshots; set by the app, which owns the open project.
    void setSnapshotProvider(std::function<QByteArray()> provider) { m_snapshot = std::move(provider); }

    // From the export dialog. `target` is "local" or "farm"; `hold` forces the job to wait for
    // start() even when the queue auto-starts. Returns the job id, empty if nothing could be queued.
    Q_INVOKABLE QString addCurrent(const QUrl &output, const QVariantMap &settings, const QString &target,
                                   bool hold = false);
    // A job from a project file already on disk (the CLI, a template batch row). The file is copied
    // into the queue's own folder.
    QString addProjectFile(const QString &projectFile, const QString &output, const QVariantMap &settings,
                           JobTarget target, bool hold, const QJsonObject &source = {});
    Q_INVOKABLE void start(const QString &id);
    Q_INVOKABLE void startAll();
    Q_INVOKABLE void hold(const QString &id);
    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void retry(const QString &id);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void move(const QString &id, int delta);
    Q_INVOKABLE void openOutput(const QString &id) const;
    Q_INVOKABLE void showOutputFolder(const QString &id) const;

    // For FarmCoordinator and tests. update() applies `fn` to the job, notifies views and saves
    // soon; it does nothing for an unknown id.
    const RenderJob *find(const QString &id) const;
    QList<const RenderJob *> jobs() const;
    void update(const QString &id, const std::function<void(RenderJob &)> &fn);

signals:
    void autoStartChanged();
    void pausedChanged();
    void useWorkerProcessChanged();
    void countChanged();
    void message(const QString &text, const QString &severity);
    // A farm job entered or left Queued, or was asked to stop.
    void farmWorkChanged();
    void jobFinished(const QString &id, bool ok);

private:
    int indexOf(const QString &id) const;
    void load();
    void save();
    void scheduleSave();
    void pump();
    void startLocal(const QString &id);
    void finishLocal(const QString &id, const TaskResult &result);
    void setProgress(const QString &id, double fraction);
    QString directory() const { return m_directory; }

    QString m_directory;
    QList<RenderJob> m_jobs;
    bool m_autoStart = false;
    bool m_paused = false;
    bool m_useProcess = true;
    std::function<QByteArray()> m_snapshot;
    QTimer m_saveTimer;

    QString m_runningId;
    std::shared_ptr<std::atomic<bool>> m_cancelFlag;
    WorkerProcess *m_worker = nullptr;
};

} // namespace drift::farm
