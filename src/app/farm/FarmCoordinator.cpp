#include "app/farm/FarmCoordinator.h"

#include "app/farm/ChunkJoin.h"
#include "app/farm/RenderQueue.h"
#include "app/farm/RenderTask.h"
#include "core/project/Project.h"
#include "engine/encode/export/Exporter.h"
#include "engine/project/ProjectDependencies.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutex>
#include <QNetworkInterface>
#include <QFutureWatcher>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QtConcurrent>

extern "C" {
#include <libavutil/rational.h>
}

#include <algorithm>
#include <climits>
#include <cmath>

namespace drift::farm {

namespace {

constexpr int kWorkerLostMs = 20000;
constexpr int kMaxAttempts = 3;
constexpr int kMaxReassigns = 8;

// Codecs whose every chunk starts on a keyframe and is decodable alone, and that the software
// encoders on every worker produce identically. Hardware encoders are left out: workers may have
// different ones, or none, and "identical encoder settings" could not be promised.
bool chunkSafeCodec(const QString &id)
{
    static const QSet<QString> safe{QStringLiteral("h264"),     QStringLiteral("h264_10"),
                                    QStringLiteral("h265"),     QStringLiteral("h265_10"),
                                    QStringLiteral("h265_12"),  QStringLiteral("prores"),
                                    QStringLiteral("prores_4444"), QStringLiteral("dnxhr"),
                                    QStringLiteral("dnxhr_10"), QStringLiteral("ffv1")};
    return safe.contains(id);
}

QString fileHash(const QString &path)
{
    static QMutex mutex;
    static QHash<QString, QString> cache;
    const QFileInfo info(path);
    const QString key = QStringLiteral("%1|%2|%3").arg(info.canonicalFilePath()).arg(info.size()).arg(
        info.lastModified().toMSecsSinceEpoch());
    {
        QMutexLocker lock(&mutex);
        if (const auto it = cache.constFind(key); it != cache.constEnd())
            return it.value();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    const QString hex = QString::fromLatin1(hash.result().toHex());
    QMutexLocker lock(&mutex);
    cache.insert(key, hex);
    return hex;
}

QString taskId(const QString &jobId, int chunk)
{
    return QStringLiteral("%1~%2").arg(jobId).arg(chunk);
}

FarmCoordinator::JobPlan prepareJobPlan(const RenderJob &job, const QString &workRoot)
{
    FarmCoordinator::JobPlan plan;
    plan.jobId = job.id;
    plan.workDir = QDir(workRoot).filePath(job.id);
    QDir().mkpath(plan.workDir);
    plan.output = job.output;
    plan.wholeOnly = !job.source.isEmpty();

    drift::Project project;
    if (!loadProjectDocument(job.projectPath, &project, &plan.error))
        return plan;

    for (const drift::bundle::MediaEntry &entry : drift::bundle::collectMedia(project, false)) {
        if (!entry.resourceOf.isEmpty()) {
            plan.notes.append(QStringLiteral("%1 loads files from beside itself; it will not be found on workers")
                                  .arg(QFileInfo(entry.originalPath).fileName()));
            continue;
        }
        const QFileInfo info(entry.originalPath);
        if (!info.isFile())
            continue;
        const QString hash = fileHash(entry.originalPath);
        if (hash.isEmpty())
            continue;
        plan.media.append(QJsonObject{{QStringLiteral("path"), entry.originalPath},
                                      {QStringLiteral("hash"), hash},
                                      {QStringLiteral("size"), static_cast<double>(info.size())},
                                      {QStringLiteral("name"), info.fileName()}});
        plan.mediaPaths.insert(hash, entry.originalPath);
    }
    for (const drift::bundle::AddonRef &addon : drift::bundle::collectAddons(project)) {
        plan.addons.append(QJsonObject{{QStringLiteral("id"), addon.id},
                                       {QStringLiteral("version"), addon.version},
                                       {QStringLiteral("name"), addon.name}});
    }

    plan.projectFile = QDir(plan.workDir).filePath(QStringLiteral("project.json"));
    QFile out(plan.projectFile);
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(project.toJson()).toJson(QJsonDocument::Compact)) < 0) {
        plan.error = QStringLiteral("Could not write the job's project copy");
        return plan;
    }
    out.close();

    const drift::Project view = project.exportView({});
    ExportSettings settings = Exporter::settingsFromMap(job.settings);
    drift::TimeUs start = 0;
    drift::TimeUs end = 0;
    resolveExportRange(view, settings, &start, &end, &plan.error);
    if (end <= start) {
        if (plan.error.isEmpty())
            plan.error = QStringLiteral("The project has nothing to render");
        return plan;
    }
    plan.startUs = start;
    plan.endUs = end;
    int num = qMax(1, view.fps());
    int den = 1;
    if (settings.fpsNum > 0 && settings.fpsDen > 0) {
        num = settings.fpsNum;
        den = settings.fpsDen;
    }
    av_reduce(&num, &den, num, den, INT_MAX);
    plan.fpsNum = num;
    plan.fpsDen = den;
    plan.totalFrames = expectedFrames(view, job.settings);
    plan.settings = job.settings;
    plan.settings.insert(QStringLiteral("startUs"), static_cast<double>(start));
    plan.settings.insert(QStringLiteral("endUs"), static_cast<double>(end));

    plan.ext = QFileInfo(withContainerSuffix(job.settings, job.output)).suffix();
    view.forEachTrackList([&](const QList<drift::Track> &tracks) {
        for (const drift::Track &track : tracks) {
            for (const drift::Clip &clip : track.clips) {
                for (const qint64 edge : {clip.timelineStart, clip.timelineEnd()}) {
                    if (edge > start && edge < end)
                        plan.edgesUs.append(edge);
                }
            }
        }
    });
    std::sort(plan.edgesUs.begin(), plan.edgesUs.end());
    plan.preparing = false;
    return plan;
}

} // namespace

FarmCoordinator::FarmCoordinator(RenderQueue *queue, QObject *parent) : QObject(parent), m_queue(queue)
{
    QSettings settings;
    m_token = settings.value(QStringLiteral("farm/token")).toString();
    if (m_token.isEmpty()) {
        m_token = generateToken();
        settings.setValue(QStringLiteral("farm/token"), m_token);
    }
    m_http.setToken(m_token);
    m_enabled = settings.value(QStringLiteral("farm/enabled"), false).toBool();
    m_listenPort = settings.value(QStringLiteral("farm/port"), 4800).toInt();
    m_http.setHandler([this](const FarmHttpServer::Request &request, FarmHttpServer::Reply reply) {
        route(request, reply);
    });
    m_http.setBodySink([this](const QString &path, const QUrlQuery &) -> QString {
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() != 4 || parts.at(1) != QLatin1String("tasks") || parts.at(3) != QLatin1String("result"))
            return {};
        JobPlan *plan = nullptr;
        FarmTask *task = findTask(parts.at(2), &plan);
        if (!task)
            return {};
        return QDir(plan->workDir).filePath(QStringLiteral("chunk-%1.%2").arg(task->chunk).arg(plan->ext));
    });
    m_tick.setInterval(500);
    connect(&m_tick, &QTimer::timeout, this, &FarmCoordinator::tick);
    connect(m_queue, &RenderQueue::farmWorkChanged, this, &FarmCoordinator::tick);
}

FarmCoordinator::~FarmCoordinator()
{
    stop();
}

void FarmCoordinator::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    QSettings().setValue(QStringLiteral("farm/enabled"), on);
    emit enabledChanged();
    if (on)
        start(m_listenPort);
    else
        stop();
}

void FarmCoordinator::setListenPort(int port)
{
    if (port < 1 || port > 65535 || port == m_listenPort)
        return;
    m_listenPort = port;
    QSettings().setValue(QStringLiteral("farm/port"), port);
    emit enabledChanged();
    if (m_enabled)
        start(port);
}

int FarmCoordinator::cacheGb() const
{
    return QSettings().value(QStringLiteral("farm/cacheGb"), 20).toInt();
}

void FarmCoordinator::setCacheGb(int gb)
{
    if (gb < 1 || gb == cacheGb())
        return;
    QSettings().setValue(QStringLiteral("farm/cacheGb"), gb);
    emit cacheGbChanged();
}

void FarmCoordinator::restore()
{
    if (m_enabled)
        start(m_listenPort);
}

QString FarmCoordinator::generateToken()
{
    static const char alphabet[] = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    QString token;
    for (int i = 0; i < 24; ++i)
        token.append(QLatin1Char(alphabet[QRandomGenerator::system()->bounded(int(sizeof alphabet) - 1)]));
    return token;
}

void FarmCoordinator::setToken(const QString &token)
{
    if (token.isEmpty() || token == m_token)
        return;
    m_token = token;
    QSettings().setValue(QStringLiteral("farm/token"), m_token);
    m_http.setToken(m_token);
    emit tokenChanged();
}

QString FarmCoordinator::regenerateToken()
{
    setToken(generateToken());
    return m_token;
}

QString FarmCoordinator::address() const
{
    if (!m_listening)
        return {};
    for (const QHostAddress &address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback())
            return QStringLiteral("%1:%2").arg(address.toString()).arg(m_port);
    }
    return QStringLiteral("127.0.0.1:%1").arg(m_port);
}

bool FarmCoordinator::start(int port)
{
    stop();
    if (!m_http.listen(static_cast<quint16>(port))) {
        m_error = m_http.error();
        emit runningChanged();
        return false;
    }
    m_error.clear();
    m_port = m_http.port();
    m_listening = true;
    m_tick.start();
    emit runningChanged();
    tick();
    return true;
}

void FarmCoordinator::stop()
{
    if (!m_listening)
        return;
    m_tick.stop();
    m_http.close();
    m_listening = false;
    m_workers.clear();
    m_workersDirty = true;
    // Whatever was mid-flight goes back in the queue; the plans are rebuilt when it starts again.
    for (JobPlan &plan : m_plans)
        m_queue->update(plan.jobId, [](RenderJob &job) {
            if (job.state == JobState::Running) {
                job.state = JobState::Queued;
                job.progress = 0;
                job.chunks.clear();
                job.appendLog(QStringLiteral("Farm stopped; waiting to start again"));
            }
        });
    m_plans.clear();
    emit runningChanged();
    emit workersChanged();
}

QVariantList FarmCoordinator::workers() const
{
    QVariantList list;
    for (const WorkerInfo &worker : m_workers) {
        QString task;
        if (!worker.taskId.isEmpty()) {
            const QString jobId = worker.taskId.section(QLatin1Char('~'), 0, 0);
            const RenderJob *job = m_queue->find(jobId);
            const int chunk = worker.taskId.section(QLatin1Char('~'), 1).toInt();
            task = (job ? job->name : jobId) + (chunk >= 0 ? QStringLiteral(" #%1").arg(chunk + 1) : QString());
        }
        list.append(QVariantMap{{QStringLiteral("id"), worker.id},
                                {QStringLiteral("name"), worker.name},
                                {QStringLiteral("gpu"), worker.gpu},
                                {QStringLiteral("state"), worker.taskId.isEmpty() ? QStringLiteral("idle")
                                                                                  : QStringLiteral("busy")},
                                {QStringLiteral("task"), task},
                                {QStringLiteral("fraction"), worker.fraction},
                                {QStringLiteral("fps"), worker.fps},
                                {QStringLiteral("tasksDone"), worker.tasksDone}});
    }
    return list;
}

FarmCoordinator::FarmTask *FarmCoordinator::findTask(const QString &id, JobPlan **planOut)
{
    const auto it = m_plans.find(id.section(QLatin1Char('~'), 0, 0));
    if (it == m_plans.end())
        return nullptr;
    for (FarmTask &task : it->tasks) {
        if (task.id == id) {
            if (planOut)
                *planOut = &it.value();
            return &task;
        }
    }
    return nullptr;
}

QString FarmCoordinator::workDirRoot() const
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("render-queue/farm"));
}

void FarmCoordinator::beginPrepare(const QString &jobId)
{
    const RenderJob *job = m_queue->find(jobId);
    if (!job)
        return;
    JobPlan placeholder;
    placeholder.jobId = jobId;
    m_plans.insert(jobId, placeholder);
    m_queue->update(jobId, [](RenderJob &j) {
        j.state = JobState::Running;
        j.started = QDateTime::currentDateTime();
        j.progress = 0;
        j.worker = QStringLiteral("Render farm");
        j.appendLog(QStringLiteral("Preparing: hashing media and listing add-ons"));
    });
    const RenderJob copy = *job;
    const QString root = workDirRoot();
    auto *watcher = new QFutureWatcher<JobPlan>(this);
    connect(watcher, &QFutureWatcher<JobPlan>::finished, this, [this, watcher, jobId] {
        JobPlan plan = watcher->result();
        watcher->deleteLater();
        const auto it = m_plans.find(jobId);
        if (it == m_plans.end())
            return;
        if (!plan.error.isEmpty()) {
            m_plans.erase(it);
            m_queue->update(jobId, [&plan](RenderJob &j) {
                j.state = JobState::Failed;
                j.error = plan.error;
                j.finished = QDateTime::currentDateTime();
                j.appendLog(QStringLiteral("Failed: %1").arg(plan.error));
            });
            return;
        }
        for (auto m = plan.mediaPaths.begin(); m != plan.mediaPaths.end(); ++m)
            m_media.insert(m.key(), m.value());
        planTasks(plan);
        *it = plan;
        m_queue->update(jobId, [&plan](RenderJob &j) {
            for (const QString &note : plan.notes)
                j.appendLog(note);
            j.appendLog(plan.chunked ? QStringLiteral("Split into %1 chunks of about %2 frames")
                                           .arg(plan.tasks.size()).arg(plan.totalFrames / qMax<qint64>(1, plan.tasks.size()))
                                     : QStringLiteral("Rendering whole on one worker"));
        });
        if (plan.chunked)
            startAudio(*it);
        publishJob(*it);
    });
    watcher->setFuture(QtConcurrent::run(prepareJobPlan, copy, root));
}

void FarmCoordinator::planTasks(JobPlan &plan)
{
    const ExportSettings settings = Exporter::settingsFromMap(plan.settings);
    const double fps = double(plan.fpsNum) / double(plan.fpsDen);
    const qint64 minChunk = qMax<qint64>(30, static_cast<qint64>(std::ceil(fps * 2.0)));
    const int workers = qMax<int>(2, static_cast<int>(m_workers.size()));
    const bool safe = !plan.wholeOnly && chunkSafeCodec(settings.videoCodecId) && !settings.audioOnly && !settings.gifExport;
    const qint64 chunks = qMin<qint64>(qint64(workers) * 2, plan.totalFrames / minChunk);

    plan.chunked = false;
    plan.tasks.clear();
    if (safe && chunks >= 2) {
        const auto usAt = [&](qint64 frame) {
            return plan.startUs + static_cast<qint64>(std::llround(double(frame) * 1e6 / fps));
        };
        // Ideal split points, nudged onto the nearest clip edge within a quarter chunk, since a cut is
        // where a slice costs least to start fresh.
        QList<qint64> bounds{0};
        const qint64 ideal = plan.totalFrames / chunks;
        for (qint64 k = 1; k < chunks; ++k) {
            qint64 bound = k * plan.totalFrames / chunks;
            qint64 bestDistance = ideal / 4 + 1;
            for (const qint64 edge : plan.edgesUs) {
                const qint64 frame = static_cast<qint64>(std::llround(double(edge - plan.startUs) * fps / 1e6));
                if (std::llabs(frame - bound) < bestDistance) {
                    bestDistance = std::llabs(frame - bound);
                    bound = frame;
                }
            }
            if (bound >= bounds.last() + minChunk / 2 && bound < plan.totalFrames - minChunk / 2)
                bounds.append(bound);
        }
        bounds.append(plan.totalFrames);

        qint64 sum = 0;
        QList<FarmTask> tasks;
        for (int i = 0; i + 1 < bounds.size(); ++i) {
            FarmTask task;
            task.id = taskId(plan.jobId, i);
            task.chunk = i;
            task.settings = plan.settings;
            const qint64 startUs = usAt(bounds.at(i));
            const qint64 endUs = i + 2 == bounds.size() ? plan.endUs : usAt(bounds.at(i + 1));
            task.settings.insert(QStringLiteral("startUs"), static_cast<double>(startUs));
            task.settings.insert(QStringLiteral("endUs"), static_cast<double>(endUs));
            // Exporter's own rule for how many frames a range holds.
            task.expectedFrames = qMax<qint64>(1, std::llround(double(endUs - startUs) * fps / 1e6));
            sum += task.expectedFrames;
            tasks.append(task);
        }
        if (sum == plan.totalFrames && tasks.size() >= 2) {
            plan.tasks = tasks;
            plan.chunked = true;
            plan.audioDone = false;
            return;
        }
        plan.notes.append(QStringLiteral("Slices came to %1 frames, not %2; rendering whole").arg(sum).arg(plan.totalFrames));
    } else if (!safe && !plan.wholeOnly) {
        plan.notes.append(QStringLiteral("%1 cannot be joined without re-encoding; rendering whole")
                              .arg(settings.videoCodecId));
    }
    FarmTask whole;
    whole.id = taskId(plan.jobId, -1);
    whole.settings = plan.settings;
    whole.expectedFrames = settings.audioOnly || settings.gifExport ? 0 : plan.totalFrames;
    plan.tasks.append(whole);
}

void FarmCoordinator::startAudio(JobPlan &plan)
{
    plan.audioRunning = true;
    plan.audioDone = false;
    plan.audioFile = QDir(plan.workDir).filePath(QStringLiteral("audio"));
    RenderTask task;
    task.id = plan.jobId + QStringLiteral("~audio");
    task.projectPath = plan.projectFile;
    task.settings = plan.settings;
    task.settings.insert(QStringLiteral("audioOnly"), true);
    task.outputPath = plan.audioFile;
    const QString jobId = plan.jobId;
    auto *watcher = new QFutureWatcher<TaskResult>(this);
    connect(watcher, &QFutureWatcher<TaskResult>::finished, this, [this, watcher, jobId] {
        const TaskResult result = watcher->result();
        watcher->deleteLater();
        const auto it = m_plans.find(jobId);
        if (it == m_plans.end())
            return;
        it->audioRunning = false;
        if (!result.ok) {
            failJob(*it, QStringLiteral("Audio render failed: %1").arg(result.error));
            return;
        }
        it->audioFile = result.outputPath;
        it->audioDone = true;
        it->audioFraction = 1;
        m_queue->update(jobId, [](RenderJob &j) { j.appendLog(QStringLiteral("Audio rendered")); });
        maybeFinish(*it);
    });
    watcher->setFuture(QtConcurrent::run(runTask, task, ProgressFn([this, jobId](double fraction) {
        QMetaObject::invokeMethod(this, [this, jobId, fraction] {
            if (const auto it = m_plans.find(jobId); it != m_plans.end())
                it->audioFraction = fraction;
        }, Qt::QueuedConnection);
        return true;
    })));
}

void FarmCoordinator::publishJob(JobPlan &plan)
{
    double done = 0;
    for (const FarmTask &task : plan.tasks) {
        const double weight = plan.chunked ? double(task.expectedFrames) / double(qMax<qint64>(1, plan.totalFrames)) : 1.0;
        done += weight * (task.state == FarmTask::State::Done ? 1.0 : task.fraction);
    }
    const double progress = qMin(0.99, done);
    QList<JobChunk> chunks;
    if (plan.chunked) {
        for (const FarmTask &task : plan.tasks) {
            JobChunk chunk;
            chunk.index = task.chunk;
            chunk.frames = task.expectedFrames;
            chunk.state = task.state == FarmTask::State::Done      ? JobChunk::State::Done
                          : task.state == FarmTask::State::Running ? JobChunk::State::Running
                                                                   : JobChunk::State::Pending;
            chunk.attempts = task.attempts;
            chunk.progress = task.state == FarmTask::State::Done ? 1.0 : task.fraction;
            chunk.worker = task.worker;
            chunks.append(chunk);
        }
    }
    m_queue->update(plan.jobId, [&](RenderJob &job) {
        job.progress = progress;
        job.chunks = chunks;
        job.chunked = plan.chunked;
    });
}

void FarmCoordinator::failJob(JobPlan &plan, const QString &error)
{
    finishJob(plan, false, error);
}

void FarmCoordinator::finishJob(JobPlan &plan, bool ok, const QString &error)
{
    const QString jobId = plan.jobId;
    const QString workDir = plan.workDir;
    const bool cancelled = m_queue->find(jobId) && m_queue->find(jobId)->cancelRequested;
    for (const QJsonValue &media : plan.media)
        m_media.remove(media.toObject().value(QStringLiteral("hash")).toString());
    m_plans.remove(jobId);
    m_queue->update(jobId, [&](RenderJob &job) {
        job.finished = QDateTime::currentDateTime();
        job.worker.clear();
        if (ok) {
            job.state = JobState::Done;
            job.progress = 1.0;
            job.appendLog(QStringLiteral("Done"));
        } else if (cancelled) {
            job.state = JobState::Cancelled;
            job.appendLog(QStringLiteral("Cancelled"));
        } else {
            job.state = JobState::Failed;
            job.error = error;
            job.appendLog(QStringLiteral("Failed: %1").arg(error));
        }
    });
    QDir(workDir).removeRecursively();
}

void FarmCoordinator::maybeFinish(JobPlan &plan)
{
    if (plan.joining)
        return;
    for (const FarmTask &task : plan.tasks) {
        if (task.state != FarmTask::State::Done)
            return;
    }
    if (!plan.audioDone)
        return;

    const QString jobId = plan.jobId;
    const QString finalOutput = withContainerSuffix(plan.settings, plan.output);
    QDir().mkpath(QFileInfo(finalOutput).absolutePath());

    if (!plan.chunked) {
        const QString source = plan.tasks.first().resultFile;
        QFile::remove(finalOutput);
        if (!QFile::rename(source, finalOutput) && !QFile::copy(source, finalOutput)) {
            failJob(plan, QStringLiteral("Could not move the result to %1").arg(finalOutput));
            return;
        }
        finishJob(plan, true, {});
        return;
    }

    plan.joining = true;
    m_queue->update(jobId, [](RenderJob &j) { j.appendLog(QStringLiteral("Joining chunks")); });
    QStringList files;
    QList<qint64> frames;
    for (const FarmTask &task : plan.tasks) {
        files.append(task.resultFile);
        frames.append(task.expectedFrames);
    }
    const int num = plan.fpsNum;
    const int den = plan.fpsDen;
    const QString audio = plan.audioFile;
    const qint64 total = plan.totalFrames;
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, jobId] {
        const QString error = watcher->result();
        watcher->deleteLater();
        const auto it = m_plans.find(jobId);
        if (it == m_plans.end())
            return;
        if (error.isEmpty()) {
            finishJob(*it, true, {});
            return;
        }
        // A join that fails or miscounts is not worth a second guess at the same slices: render the
        // job whole on one worker, which is always correct.
        m_queue->update(jobId, [&error](RenderJob &j) {
            j.appendLog(QStringLiteral("Join failed (%1); rendering whole instead").arg(error));
        });
        it->joining = false;
        it->chunked = false;
        it->audioDone = true;
        it->tasks.clear();
        FarmTask whole;
        whole.id = taskId(jobId, -1);
        whole.settings = it->settings;
        whole.expectedFrames = it->totalFrames;
        it->tasks.append(whole);
        publishJob(*it);
    });
    watcher->setFuture(QtConcurrent::run([files, frames, num, den, audio, finalOutput, total]() -> QString {
        QString error;
        if (!joinChunks(files, frames, num, den, audio, finalOutput, &error))
            return error;
        const qint64 counted = countVideoFrames(finalOutput);
        if (counted != total) {
            QFile::remove(finalOutput);
            return QStringLiteral("joined file has %1 frames, expected %2").arg(counted).arg(total);
        }
        return {};
    }));
}

QJsonObject FarmCoordinator::nextTaskFor(WorkerInfo &worker)
{
    for (const RenderJob *job : m_queue->jobs()) {
        const auto it = m_plans.find(job->id);
        if (it == m_plans.end() || it->preparing || it->joining || job->cancelRequested)
            continue;
        JobPlan &plan = it.value();
        for (FarmTask &task : plan.tasks) {
            if (task.state != FarmTask::State::Pending || task.refused.contains(worker.id))
                continue;
            if (task.avoid.contains(worker.id) && m_workers.size() > 1)
                continue;
            const QString codec = task.settings.value(QStringLiteral("videoCodecId"), QStringLiteral("h264")).toString();
            const bool audioOnly = task.settings.value(QStringLiteral("audioOnly")).toBool();
            if (!audioOnly && !worker.encoders.isEmpty() && !worker.encoders.contains(codec))
                continue;
            task.state = FarmTask::State::Running;
            task.worker = worker.id;
            task.fraction = 0;
            worker.taskId = task.id;
            worker.fraction = 0;
            worker.fps = 0;
            worker.lastBeat.restart();
            m_workersDirty = true;
            return QJsonObject{{QStringLiteral("id"), task.id},
                               {QStringLiteral("settings"), QJsonObject::fromVariantMap(task.settings)},
                               {QStringLiteral("ext"), plan.ext},
                               {QStringLiteral("media"), plan.media},
                               {QStringLiteral("addons"), plan.addons},
                               {QStringLiteral("muteAudio"), plan.chunked},
                               {QStringLiteral("expectedFrames"), static_cast<double>(task.expectedFrames)}};
        }
    }
    return {};
}

void FarmCoordinator::taskDone(WorkerInfo &worker, FarmTask &task, JobPlan &plan, bool ok, const QString &error,
                               qint64 frames)
{
    worker.taskId.clear();
    worker.fraction = 0;
    m_workersDirty = true;
    QString problem = error;
    if (ok && task.expectedFrames > 0 && frames != task.expectedFrames)
        problem = QStringLiteral("frame count mismatch: got %1, expected %2").arg(frames).arg(task.expectedFrames);
    if (ok && !QFileInfo::exists(task.resultFile))
        problem = QStringLiteral("the result file never arrived");
    if (problem.isEmpty() && ok) {
        task.state = FarmTask::State::Done;
        task.fraction = 1;
        ++worker.tasksDone;
        publishJob(plan);
        maybeFinish(plan);
        return;
    }

    task.state = FarmTask::State::Pending;
    task.worker.clear();
    task.fraction = 0;
    task.lastError = problem;
    const bool refusal = problem.startsWith(QLatin1String("Missing add-on")) || problem.startsWith(QLatin1String("No encoder"));
    if (refusal) {
        task.refused.insert(worker.id);
        bool anyoneLeft = false;
        for (const WorkerInfo &other : std::as_const(m_workers))
            anyoneLeft = anyoneLeft || !task.refused.contains(other.id);
        m_queue->update(plan.jobId, [&](RenderJob &j) {
            j.appendLog(QStringLiteral("%1 refused: %2").arg(worker.name, problem));
        });
        if (!anyoneLeft)
            failJob(plan, problem);
        return;
    }
    ++task.attempts;
    task.avoid.insert(worker.id);
    m_queue->update(plan.jobId, [&](RenderJob &j) {
        j.appendLog(QStringLiteral("%1 failed %2 (attempt %3): %4")
                        .arg(worker.name, task.chunk >= 0 ? QStringLiteral("chunk %1").arg(task.chunk + 1)
                                                          : QStringLiteral("the job"))
                        .arg(task.attempts)
                        .arg(problem));
    });
    if (task.attempts >= kMaxAttempts)
        failJob(plan, problem);
    else
        publishJob(plan);
}

void FarmCoordinator::tick()
{
    if (!m_listening)
        return;
    // Updating a job emits farmWorkChanged, which lands back here while the loops below hold
    // iterators; run the pass again afterwards instead.
    if (m_inTick) {
        m_tickAgain = true;
        return;
    }
    m_inTick = true;
    do {
        m_tickAgain = false;
        tickOnce();
    } while (m_tickAgain);
    m_inTick = false;
}

void FarmCoordinator::tickOnce()
{
    // Workers not heard from lose their task to the next one that asks.
    for (auto it = m_workers.begin(); it != m_workers.end();) {
        if (it->seen.elapsed() < kWorkerLostMs) {
            ++it;
            continue;
        }
        const QString id = it->id;
        const QString name = it->name;
        const QString held = it->taskId;
        it = m_workers.erase(it);
        m_workersDirty = true;
        if (held.isEmpty())
            continue;
        JobPlan *plan = nullptr;
        if (FarmTask *task = findTask(held, &plan); task && task->state == FarmTask::State::Running) {
            task->state = FarmTask::State::Pending;
            task->worker.clear();
            task->fraction = 0;
            if (++task->reassigns > kMaxReassigns) {
                failJob(*plan, QStringLiteral("workers kept disappearing"));
                continue;
            }
            m_queue->update(plan->jobId, [&](RenderJob &j) {
                j.appendLog(QStringLiteral("Lost %1; giving %2 to another worker")
                                .arg(name, task->chunk >= 0 ? QStringLiteral("chunk %1").arg(task->chunk + 1)
                                                            : QStringLiteral("the job")));
            });
            publishJob(*plan);
        }
    }

    // Jobs asked to stop wind down once nothing of theirs is running.
    const auto planIds = m_plans.keys();
    for (const QString &jobId : planIds) {
        const RenderJob *job = m_queue->find(jobId);
        const auto it = m_plans.find(jobId);
        if (!job || it == m_plans.end() || !job->cancelRequested || it->preparing)
            continue;
        bool running = it->audioRunning || it->joining;
        for (const FarmTask &task : it->tasks)
            running = running || task.state == FarmTask::State::Running;
        if (!running)
            finishJob(*it, false, QStringLiteral("Cancelled"));
    }

    for (const RenderJob *job : m_queue->jobs()) {
        if (job->target == JobTarget::Farm && job->state == JobState::Queued && !m_plans.contains(job->id)
            && !m_queue->paused())
            beginPrepare(job->id);
    }

    for (JobPlan &plan : m_plans) {
        if (!plan.preparing && !plan.tasks.isEmpty())
            publishJob(plan);
    }
    if (m_workersDirty) {
        m_workersDirty = false;
        emit workersChanged();
    }
}

void FarmCoordinator::route(const FarmHttpServer::Request &request, FarmHttpServer::Reply reply)
{
    const QStringList parts = request.path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    if (parts.size() < 2 || parts.at(0) != QLatin1String("v1"))
        return reply.error(404, QStringLiteral("Unknown endpoint"));
    const QString &area = parts.at(1);

    if (area == QLatin1String("workers") && parts.size() == 3 && parts.at(2) == QLatin1String("register")) {
        WorkerInfo worker;
        worker.id = QStringLiteral("w%1").arg(++m_workerCounter);
        worker.name = body.value(QStringLiteral("name")).toString(worker.id);
        worker.gpu = body.value(QStringLiteral("gpu")).toString();
        for (const QJsonValue &codec : body.value(QStringLiteral("encoders")).toArray())
            worker.encoders.append(codec.toString());
        worker.seen.start();
        worker.lastBeat.start();
        m_workers.insert(worker.id, worker);
        m_workersDirty = true;
        return reply.json(200, QJsonObject{{QStringLiteral("workerId"), worker.id}});
    }

    if (area == QLatin1String("workers") && parts.size() == 4) {
        const auto it = m_workers.find(parts.at(2));
        if (it == m_workers.end())
            return reply.error(404, QStringLiteral("Unknown worker; register again"));
        it->seen.restart();
        if (parts.at(3) == QLatin1String("poll")) {
            const QJsonObject task = nextTaskFor(*it);
            return reply.json(200, QJsonObject{{QStringLiteral("task"), task.isEmpty() ? QJsonValue() : QJsonValue(task)}});
        }
        if (parts.at(3) == QLatin1String("beat")) {
            const QString id = body.value(QStringLiteral("task")).toString();
            const double fraction = body.value(QStringLiteral("fraction")).toDouble();
            bool cancel = !id.isEmpty();
            JobPlan *plan = nullptr;
            if (FarmTask *task = findTask(id, &plan); task && task->worker == it->id) {
                const RenderJob *job = m_queue->find(plan->jobId);
                cancel = job && job->cancelRequested;
                const double delta = fraction - task->fraction;
                const qint64 elapsed = it->lastBeat.restart();
                if (delta > 0 && elapsed > 0 && task->expectedFrames > 0)
                    it->fps = 0.5 * it->fps + 0.5 * (delta * double(task->expectedFrames) * 1000.0 / double(elapsed));
                task->fraction = fraction;
                it->fraction = fraction;
                m_workersDirty = true;
            }
            return reply.json(200, QJsonObject{{QStringLiteral("cancel"), cancel}});
        }
    }

    if (area == QLatin1String("tasks") && parts.size() == 4) {
        JobPlan *plan = nullptr;
        FarmTask *task = findTask(parts.at(2), &plan);
        if (!task)
            return reply.error(404, QStringLiteral("Unknown task"));
        if (parts.at(3) == QLatin1String("project"))
            return reply.file(plan->projectFile);
        if (parts.at(3) == QLatin1String("result") && request.method == "PUT") {
            task->resultFile = request.bodyFile;
            return reply.json(200, {});
        }
        if (parts.at(3) == QLatin1String("done")) {
            const auto it = m_workers.find(body.value(QStringLiteral("worker")).toString());
            if (it == m_workers.end() || task->worker != it->id)
                return reply.error(409, QStringLiteral("That task is not yours"));
            it->seen.restart();
            taskDone(*it, *task, *plan, body.value(QStringLiteral("ok")).toBool(),
                     body.value(QStringLiteral("error")).toString(),
                     static_cast<qint64>(body.value(QStringLiteral("frames")).toDouble()));
            return reply.json(200, {});
        }
    }

    if (area == QLatin1String("media") && parts.size() == 3) {
        const auto it = m_media.constFind(parts.at(2));
        if (it == m_media.constEnd())
            return reply.error(404, QStringLiteral("No such media"));
        return reply.file(it.value());
    }

    if (area == QLatin1String("status")) {
        QJsonArray jobs;
        for (const RenderJob *job : m_queue->jobs())
            jobs.append(QJsonObject{{QStringLiteral("id"), job->id}, {QStringLiteral("name"), job->name},
                                    {QStringLiteral("state"), jobStateName(job->state)},
                                    {QStringLiteral("progress"), job->progress}});
        return reply.json(200, QJsonObject{{QStringLiteral("jobs"), jobs},
                                           {QStringLiteral("workers"), QJsonArray::fromVariantList(workers())}});
    }
    reply.error(404, QStringLiteral("Unknown endpoint"));
}

} // namespace drift::farm
