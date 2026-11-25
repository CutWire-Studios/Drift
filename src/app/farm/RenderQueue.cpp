#include "app/farm/RenderQueue.h"

#include "app/farm/WorkerProcess.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>
#include <QtConcurrent>

namespace drift::farm {

namespace {

bool isTerminal(JobState state)
{
    return state == JobState::Done || state == JobState::Failed || state == JobState::Cancelled;
}

} // namespace

RenderQueue::RenderQueue(const QString &directory, QObject *parent)
    : QAbstractListModel(parent),
      m_directory(directory.isEmpty()
                      ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
                            .filePath(QStringLiteral("render-queue"))
                      : directory)
{
    QDir().mkpath(m_directory);
    QSettings settings;
    m_autoStart = settings.value(QStringLiteral("renderQueue/autoStart"), false).toBool();
    m_useProcess = settings.value(QStringLiteral("renderQueue/useWorkerProcess"), true).toBool();
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &RenderQueue::save);
    load();
}

RenderQueue::~RenderQueue()
{
    if (m_saveTimer.isActive())
        save();
    if (m_cancelFlag)
        m_cancelFlag->store(true);
}

int RenderQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_jobs.size());
}

QHash<int, QByteArray> RenderQueue::roleNames() const
{
    return {{IdRole, "jobId"},           {NameRole, "name"},
            {StateRole, "jobState"},        {ProgressRole, "progress"},
            {ErrorRole, "error"},        {OutputRole, "output"},
            {TargetRole, "target"},      {LogRole, "log"},
            {WorkerRole, "worker"},      {ChunkDoneRole, "chunkDone"},
            {ChunkTotalRole, "chunkTotal"}, {CreatedRole, "created"},
            {StartedMsRole, "startedMs"}, {FinishedMsRole, "finishedMs"}};
}

QVariant RenderQueue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size())
        return {};
    const RenderJob &job = m_jobs.at(index.row());
    switch (role) {
    case IdRole: return job.id;
    case NameRole: return job.name;
    case StateRole: return jobStateName(job.state);
    case ProgressRole: return job.progress;
    case ErrorRole: return job.error;
    case OutputRole: return job.output;
    case TargetRole: return job.target == JobTarget::Farm ? QStringLiteral("farm") : QStringLiteral("local");
    case LogRole: return job.log.join(QLatin1Char('\n'));
    case WorkerRole: return job.worker;
    case ChunkDoneRole: {
        int done = 0;
        for (const JobChunk &chunk : job.chunks)
            done += chunk.state == JobChunk::State::Done ? 1 : 0;
        return done;
    }
    case ChunkTotalRole: return static_cast<int>(job.chunks.size());
    case CreatedRole: return job.created;
    case StartedMsRole: return job.started.isValid() ? job.started.toMSecsSinceEpoch() : 0;
    case FinishedMsRole: return job.finished.isValid() ? job.finished.toMSecsSinceEpoch() : 0;
    }
    return {};
}

int RenderQueue::activeCount() const
{
    int active = 0;
    for (const RenderJob &job : m_jobs)
        active += (job.state == JobState::Queued || job.state == JobState::Running) ? 1 : 0;
    return active;
}

void RenderQueue::setAutoStart(bool on)
{
    if (m_autoStart == on)
        return;
    m_autoStart = on;
    QSettings().setValue(QStringLiteral("renderQueue/autoStart"), on);
    emit autoStartChanged();
    // Turning it on releases what was waiting: that is what "start immediately" means.
    if (on)
        startAll();
}

void RenderQueue::setPaused(bool on)
{
    if (m_paused == on)
        return;
    m_paused = on;
    emit pausedChanged();
    pump();
    emit farmWorkChanged();
}

void RenderQueue::setUseWorkerProcess(bool on)
{
    if (m_useProcess == on)
        return;
    m_useProcess = on;
    QSettings().setValue(QStringLiteral("renderQueue/useWorkerProcess"), on);
    emit useWorkerProcessChanged();
}

int RenderQueue::indexOf(const QString &id) const
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == id)
            return i;
    }
    return -1;
}

const RenderJob *RenderQueue::find(const QString &id) const
{
    const int row = indexOf(id);
    return row < 0 ? nullptr : &m_jobs.at(row);
}

QList<const RenderJob *> RenderQueue::jobs() const
{
    QList<const RenderJob *> out;
    for (const RenderJob &job : m_jobs)
        out.append(&job);
    return out;
}

void RenderQueue::update(const QString &id, const std::function<void(RenderJob &)> &fn)
{
    const int row = indexOf(id);
    if (row < 0)
        return;
    const JobState before = m_jobs.at(row).state;
    fn(m_jobs[row]);
    const QModelIndex index = this->index(row);
    emit dataChanged(index, index);
    scheduleSave();
    if (m_jobs.at(row).state != before) {
        emit countChanged();
        emit farmWorkChanged();
        if (isTerminal(m_jobs.at(row).state))
            emit jobFinished(id, m_jobs.at(row).state == JobState::Done);
    }
}

QString RenderQueue::addProjectFile(const QString &projectFile, const QString &output,
                                    const QVariantMap &settings, JobTarget target, bool hold,
                                    const QJsonObject &source)
{
    RenderJob job;
    job.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDir().mkpath(m_directory + QStringLiteral("/jobs"));
    job.projectPath = QStringLiteral("%1/jobs/%2.%3").arg(
        m_directory, job.id, projectFile.endsWith(QLatin1String(".drift")) ? QStringLiteral("drift")
                                                                            : QStringLiteral("json"));
    if (!QFile::copy(projectFile, job.projectPath)) {
        emit message(tr("Couldn’t copy the project into the render queue"), QStringLiteral("error"));
        return {};
    }
    job.name = QFileInfo(output).completeBaseName();
    job.settings = settings;
    job.output = output;
    job.target = target;
    job.source = source;
    job.created = QDateTime::currentDateTime();
    job.state = (hold || !m_autoStart) ? JobState::Held : JobState::Queued;
    job.appendLog(job.state == JobState::Held ? QStringLiteral("Added, waiting to start")
                                              : QStringLiteral("Added"));

    beginInsertRows({}, static_cast<int>(m_jobs.size()), static_cast<int>(m_jobs.size()));
    m_jobs.append(job);
    endInsertRows();
    emit countChanged();
    save();
    pump();
    emit farmWorkChanged();
    return job.id;
}

QString RenderQueue::addCurrent(const QUrl &output, const QVariantMap &settings, const QString &target, bool hold)
{
    if (!m_snapshot) {
        emit message(tr("There is no project to queue"), QStringLiteral("error"));
        return {};
    }
    const QString path = output.isLocalFile() ? output.toLocalFile() : output.toString();
    if (path.isEmpty()) {
        emit message(tr("That save location isn’t valid"), QStringLiteral("error"));
        return {};
    }
    // Written beside the queue first so addProjectFile's copy has something to read.
    const QString staging = m_directory + QStringLiteral("/staging-") + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".json");
    QFile file(staging);
    if (!file.open(QIODevice::WriteOnly) || file.write(m_snapshot()) < 0) {
        emit message(tr("Couldn’t write the render queue"), QStringLiteral("error"));
        return {};
    }
    file.close();
    const QString id = addProjectFile(staging, path, settings,
                                      target == QLatin1String("farm") ? JobTarget::Farm : JobTarget::Local, hold);
    QFile::remove(staging);
    return id;
}

void RenderQueue::start(const QString &id)
{
    update(id, [](RenderJob &job) {
        if (job.state == JobState::Held) {
            job.state = JobState::Queued;
            job.appendLog(QStringLiteral("Started"));
        }
    });
    pump();
}

void RenderQueue::startAll()
{
    for (const RenderJob &job : m_jobs) {
        if (job.state == JobState::Held)
            start(job.id);
    }
}

void RenderQueue::hold(const QString &id)
{
    update(id, [](RenderJob &job) {
        if (job.state == JobState::Queued) {
            job.state = JobState::Held;
            job.appendLog(QStringLiteral("Held"));
        }
    });
}

void RenderQueue::cancel(const QString &id)
{
    const RenderJob *job = find(id);
    if (!job)
        return;
    if (job->state == JobState::Running && job->target == JobTarget::Local && id == m_runningId) {
        if (m_cancelFlag)
            m_cancelFlag->store(true);
        if (m_worker)
            m_worker->cancel();
        return;
    }
    update(id, [](RenderJob &j) {
        if (j.state == JobState::Running)
            j.cancelRequested = true;
        else if (!isTerminal(j.state)) {
            j.state = JobState::Cancelled;
            j.finished = QDateTime::currentDateTime();
            j.appendLog(QStringLiteral("Cancelled"));
        }
    });
}

void RenderQueue::retry(const QString &id)
{
    update(id, [this](RenderJob &job) {
        if (!isTerminal(job.state) || job.state == JobState::Done)
            return;
        job.state = (m_autoStart) ? JobState::Queued : JobState::Held;
        job.progress = 0;
        job.error.clear();
        job.chunks.clear();
        job.cancelRequested = false;
        job.worker.clear();
        job.appendLog(QStringLiteral("Retry"));
    });
    pump();
}

void RenderQueue::remove(const QString &id)
{
    const int row = indexOf(id);
    if (row < 0 || m_jobs.at(row).state == JobState::Running)
        return;
    beginRemoveRows({}, row, row);
    const QString snapshot = m_jobs.at(row).projectPath;
    m_jobs.removeAt(row);
    endRemoveRows();
    QFile::remove(snapshot);
    emit countChanged();
    save();
}

void RenderQueue::clearFinished()
{
    for (int i = static_cast<int>(m_jobs.size()) - 1; i >= 0; --i) {
        if (isTerminal(m_jobs.at(i).state))
            remove(m_jobs.at(i).id);
    }
}

void RenderQueue::move(const QString &id, int delta)
{
    const int row = indexOf(id);
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= m_jobs.size() || delta == 0)
        return;
    beginMoveRows({}, row, row, {}, delta > 0 ? target + 1 : target);
    m_jobs.move(row, target);
    endMoveRows();
    scheduleSave();
    pump();
}

void RenderQueue::openOutput(const QString &id) const
{
    if (const RenderJob *job = find(id))
        QDesktopServices::openUrl(QUrl::fromLocalFile(job->output));
}

void RenderQueue::showOutputFolder(const QString &id) const
{
    if (const RenderJob *job = find(id))
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(job->output).absolutePath()));
}

void RenderQueue::load()
{
    QFile file(m_directory + QStringLiteral("/queue.json"));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    for (const QJsonValue &value : array) {
        RenderJob job = RenderJob::fromJson(value.toObject());
        if (job.id.isEmpty() || !QFileInfo::exists(job.projectPath))
            continue;
        // A render cut short by quitting did not finish: it goes back to waiting, never to "running".
        if (job.state == JobState::Running) {
            job.state = m_autoStart ? JobState::Queued : JobState::Held;
            job.progress = 0;
            job.appendLog(QStringLiteral("Interrupted by a restart"));
        }
        m_jobs.append(job);
    }
}

void RenderQueue::save()
{
    m_saveTimer.stop();
    QJsonArray array;
    for (const RenderJob &job : m_jobs)
        array.append(job.toJson());
    QSaveFile file(m_directory + QStringLiteral("/queue.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void RenderQueue::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void RenderQueue::setProgress(const QString &id, double fraction)
{
    update(id, [fraction](RenderJob &job) { job.progress = fraction; });
}

void RenderQueue::pump()
{
    if (m_paused || !m_runningId.isEmpty())
        return;
    for (const RenderJob &job : m_jobs) {
        if (job.target == JobTarget::Local && job.state == JobState::Queued) {
            startLocal(job.id);
            return;
        }
    }
}

void RenderQueue::startLocal(const QString &id)
{
    const RenderJob *job = find(id);
    if (!job)
        return;
    RenderTask task;
    task.id = id;
    task.projectPath = job->projectPath;
    task.settings = job->settings;
    task.outputPath = job->output;

    m_runningId = id;
    update(id, [this](RenderJob &j) {
        j.state = JobState::Running;
        j.started = QDateTime::currentDateTime();
        j.progress = 0;
        j.worker = m_useProcess ? tr("This computer (worker process)") : tr("This computer (editor)");
        j.appendLog(QStringLiteral("Rendering on %1").arg(j.worker));
    });

    if (m_useProcess) {
        if (!m_worker) {
            m_worker = new WorkerProcess(this);
            connect(m_worker, &WorkerProcess::progress, this, [this](double fraction) {
                if (!m_runningId.isEmpty())
                    setProgress(m_runningId, fraction);
            });
            connect(m_worker, &WorkerProcess::finished, this, [this](const TaskResult &result) {
                finishLocal(m_runningId, result);
            });
        }
        m_worker->run(task);
        return;
    }

    m_cancelFlag = std::make_shared<std::atomic<bool>>(false);
    const auto cancelFlag = m_cancelFlag;
    (void)QtConcurrent::run([this, task, cancelFlag, id] {
        const TaskResult result = runTask(task, [this, cancelFlag, id](double fraction) {
            QMetaObject::invokeMethod(this, [this, id, fraction] { setProgress(id, fraction); },
                                      Qt::QueuedConnection);
            return !cancelFlag->load();
        });
        QMetaObject::invokeMethod(this, [this, id, result] { finishLocal(id, result); }, Qt::QueuedConnection);
    });
}

void RenderQueue::finishLocal(const QString &id, const TaskResult &result)
{
    const bool cancelled = !result.ok && m_cancelFlag && m_cancelFlag->load();
    const bool cancelledByWorker = !result.ok && result.error.contains(QLatin1String("cancel"), Qt::CaseInsensitive);
    m_runningId.clear();
    m_cancelFlag.reset();
    update(id, [&](RenderJob &job) {
        job.finished = QDateTime::currentDateTime();
        if (result.ok) {
            job.state = JobState::Done;
            job.progress = 1.0;
            job.appendLog(QStringLiteral("Done (%1 frames)").arg(result.frames));
        } else if (cancelled || cancelledByWorker || job.cancelRequested) {
            job.state = JobState::Cancelled;
            job.appendLog(QStringLiteral("Cancelled"));
        } else {
            job.state = JobState::Failed;
            job.error = result.error;
            job.appendLog(QStringLiteral("Failed: %1").arg(result.error));
        }
    });
    pump();
}

} // namespace drift::farm
