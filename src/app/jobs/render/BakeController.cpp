#include "app/jobs/render/BakeController.h"

#include "app/editor/AppController.h"
#include "app/jobs/JobRegistry.h"
#include "app/jobs/render/BakeFingerprint.h"
#include "core/base/CacheRoot.h"
#include "core/project/Project.h"
#include "engine/encode/bake/BakeFile.h"
#include "engine/encode/bake/BakeReader.h"
#include "engine/encode/bake/BakeWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

constexpr qint64 kCompactMinBytes = 256ll * 1024 * 1024;
constexpr auto kProgressInterval = std::chrono::milliseconds(250);
constexpr int kStaleDebounceMs = 300;

QString bakePath(const QString &projectId)
{
    return QDir(drift::cacheRoot()).filePath(QStringLiteral("bakes/%1.driftbake").arg(projectId));
}

qint64 usForFrame(qint64 frame, int fps)
{
    return std::llround(double(frame) * 1e6 / qMax(1, fps));
}

QString qualityLabel(int preset)
{
    switch (preset) {
    case 0:
        return QObject::tr("Compact");
    case 2:
        return QObject::tr("High");
    default:
        return QObject::tr("Balanced");
    }
}

} // namespace

struct BakeController::JobState
{
    struct Pre
    {
        qint64 startFrame = 0;
        qint64 endFrame = 0;
        bool fresh = false;
    };

    std::shared_ptr<const drift::Project> snapshot;
    std::shared_ptr<drift::BakeFile> file;
    QByteArray fingerprint;
    Request request;
    int preset = 1;
    QHash<QString, Pre> pre;
    drift::BakeWriter::Result result;
};

BakeController::BakeController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_staleTimer.setSingleShot(true);
    m_staleTimer.setInterval(kStaleDebounceMs);
    connect(&m_staleTimer, &QTimer::timeout, this, &BakeController::recomputeStale);
    connect(m_app, &AppController::tracksChanged, this, [this]() {
        syncProject();
        scheduleStaleCheck();
    });
    connect(m_app, &AppController::projectReset, this, [this]() {
        m_queue.clear();
        m_afterJob.clear();
        m_staleTimer.stop();
        if (m_busy) {
            // The job still holds the file; it is released when the job ends.
            m_detached = true;
            m_app->m_jobs->cancel(m_jobId);
            emit bakesChanged();
        } else {
            releaseFile();
        }
    });
}

// --- file and snapshot ------------------------------------------------------

void BakeController::releaseFile()
{
    const bool had = m_file || m_index;
    m_file.reset();
    m_boundId.clear();
    m_staleIds.clear();
    m_index.reset();
    if (had)
        m_app->playback()->setBakeIndex({}, nullptr);
    emit bakesChanged();
}

void BakeController::syncProject()
{
    if (m_detached || m_busy)
        return;
    const QString id = m_app->m_project.id();
    if (id == m_boundId)
        return;
    releaseFile();
    m_boundId = id;
    if (id.isEmpty())
        return;
    // Opening creates the file, so a project that never bakes leaves nothing behind.
    const QString path = bakePath(id);
    if (QFile::exists(path))
        m_file = drift::BakeFile::open(path);
    publish();
    scheduleStaleCheck();
}

bool BakeController::ensureFile()
{
    if (m_file)
        return true;
    if (m_boundId.isEmpty())
        return false;
    const QString path = bakePath(m_boundId);
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file = drift::BakeFile::open(path);
    if (!m_file)
        return false;
    m_file->setDefaultPreset(bakeQuality());
    m_file->commit();
    publish();
    return true;
}

void BakeController::publish()
{
    const auto index = m_file ? m_file->snapshot() : nullptr;
    if (index != m_index) {
        m_index = index;
        m_app->playback()->setBakeIndex(m_file ? m_file->path() : QString(), index);
    }
    emit bakesChanged();
}

// --- staleness --------------------------------------------------------------

void BakeController::scheduleStaleCheck()
{
    if (m_index && !m_index->bakes.empty())
        m_staleTimer.start();
}

void BakeController::recomputeStale()
{
    m_staleTimer.stop();
    QSet<QString> stale;
    if (m_index) {
        const drift::Project &project = m_app->m_project;
        QList<QPair<qint64, qint64>> ranges;
        QList<const drift::Bake *> checked;
        for (const drift::Bake &bake : m_index->bakes) {
            if (bake.fps != project.fps()) {
                stale.insert(bake.id);
                continue;
            }
            ranges.append({bake.startFrame, bake.endFrame});
            checked.append(&bake);
        }
        if (!ranges.isEmpty()) {
            const QList<QByteArray> now = drift::bakeFingerprints(project, ranges);
            for (qsizetype i = 0; i < checked.size(); ++i) {
                if (now.at(i) != checked.at(i)->fingerprint)
                    stale.insert(checked.at(i)->id);
            }
        }
    }
    if (stale != m_staleIds) {
        m_staleIds = stale;
        emit bakesChanged();
    }
}

// --- QML model --------------------------------------------------------------

QVariantMap BakeController::bakeMap(const drift::Bake &bake, const QString &state) const
{
    quint64 bytes = 0;
    for (const drift::BakeGop &gop : bake.gops)
        bytes += gop.size;
    const double fps = qMax(1, bake.fps);
    return {
        {QStringLiteral("id"), bake.id},
        {QStringLiteral("start"), double(bake.startFrame) / fps},
        {QStringLiteral("end"), double(bake.endFrame) / fps},
        {QStringLiteral("startFrame"), bake.startFrame},
        {QStringLiteral("endFrame"), bake.endFrame},
        {QStringLiteral("state"), state},
        {QStringLiteral("progress"), state == QLatin1String("baking") ? m_progress : 0.0},
        {QStringLiteral("bytes"), double(bytes)},
        {QStringLiteral("quality"), bake.preset},
        {QStringLiteral("qualityLabel"), qualityLabel(bake.preset)},
        {QStringLiteral("width"), bake.width},
        {QStringLiteral("height"), bake.height},
    };
}

QVariantList BakeController::bakes() const
{
    if (m_detached || !m_index)
        return {};
    QList<QVariantMap> rows;
    bool sawActive = false;
    for (const drift::Bake &bake : m_index->bakes) {
        const bool baking = m_busy && !m_activePreIds.contains(bake.id) && bake.startFrame >= m_activeStart
            && bake.endFrame <= m_activeEnd;
        sawActive = sawActive || baking;
        rows.append(bakeMap(bake, baking                           ? QStringLiteral("baking")
                                  : m_staleIds.contains(bake.id) ? QStringLiteral("stale")
                                                                 : QStringLiteral("fresh")));
    }
    if (m_busy && !sawActive) {
        drift::Bake pending;
        pending.id = QStringLiteral("pending");
        pending.startFrame = m_activeStart;
        pending.endFrame = m_activeEnd;
        pending.fps = m_app->m_project.fps();
        pending.width = m_app->m_project.width();
        pending.height = m_app->m_project.height();
        pending.preset = bakeQuality();
        rows.append(bakeMap(pending, QStringLiteral("baking")));
    }
    std::stable_sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("start")).toDouble() < b.value(QStringLiteral("start")).toDouble();
    });
    QVariantList out;
    out.reserve(rows.size());
    for (const QVariantMap &row : std::as_const(rows))
        out.append(row);
    return out;
}

QVariantList BakeController::staleBakesIn(double startSeconds, double endSeconds)
{
    if (m_staleTimer.isActive())
        recomputeStale();
    QVariantList out;
    if (m_detached || !m_index)
        return out;
    for (const drift::Bake &bake : m_index->bakes) {
        if (!m_staleIds.contains(bake.id))
            continue;
        const double fps = qMax(1, bake.fps);
        if (double(bake.startFrame) / fps < endSeconds && double(bake.endFrame) / fps > startSeconds)
            out.append(bakeMap(bake, QStringLiteral("stale")));
    }
    return out;
}

double BakeController::totalBytes() const
{
    return m_file && !m_detached ? double(m_file->fileBytes()) : 0.0;
}

int BakeController::bakeQuality() const
{
    if (m_file)
        return m_file->defaultPreset();
    return qBound(0, QSettings().value(QStringLiteral("bake/defaultQuality"), 1).toInt(), 2);
}

void BakeController::setBakeQuality(int preset)
{
    preset = qBound(0, preset, 2);
    if (preset == bakeQuality())
        return;
    QSettings().setValue(QStringLiteral("bake/defaultQuality"), preset);
    if (m_file) {
        m_file->setDefaultPreset(preset);
        m_file->commit();
        publish();
    }
    emit bakeQualityChanged();
}

qint64 BakeController::estimateBytesPerMinute(int preset) const
{
    static constexpr double megabytes[] = {40.0, 80.0, 150.0};
    const drift::Project &project = m_app->m_project;
    const double scale = double(project.width()) * project.height() / (1920.0 * 1080.0) * project.fps() / 30.0;
    return qint64(megabytes[qBound(0, preset, 2)] * 1024.0 * 1024.0 * scale);
}

// --- jobs -------------------------------------------------------------------

BakeController::Request BakeController::requestForUs(qint64 startUs, qint64 endUs) const
{
    const int fps = qMax(1, m_app->m_project.fps());
    qint64 start = drift::BakeReaderPool::frameForTime(startUs, fps);
    if (usForFrame(start, fps) < startUs)
        ++start;
    const qint64 limitUs = qMin<qint64>(endUs, m_app->m_project.sequenceDurationUs({}));
    qint64 end = drift::BakeReaderPool::frameForTime(limitUs, fps);
    if (usForFrame(end, fps) < limitUs)
        ++end;
    return end > start ? Request{start, end} : Request{};
}

void BakeController::bakeWorkArea()
{
    if (!m_app->m_project.hasWorkArea()) {
        m_app->setLastMessage(tr("Set a work area to bake"), QStringLiteral("warning"));
        return;
    }
    enqueue(requestForUs(m_app->m_project.workAreaInUs(), m_app->m_project.workAreaOutUs()));
}

void BakeController::rebake(const QString &id)
{
    if (!m_index)
        return;
    if (const drift::Bake *bake = m_index->bakeById(id))
        enqueue(requestForUs(usForFrame(bake->startFrame, bake->fps), usForFrame(bake->endFrame, bake->fps)));
}

void BakeController::rebakeAllStale()
{
    if (!m_index)
        return;
    for (const drift::Bake &bake : m_index->bakes) {
        if (m_staleIds.contains(bake.id))
            enqueue(requestForUs(usForFrame(bake.startFrame, bake.fps), usForFrame(bake.endFrame, bake.fps)));
    }
}

void BakeController::enqueue(Request request)
{
    if (request.endFrame <= request.startFrame)
        return;
    for (const Request &queued : std::as_const(m_queue)) {
        if (queued.startFrame == request.startFrame && queued.endFrame == request.endFrame)
            return;
    }
    if (m_busy && m_activeStart == request.startFrame && m_activeEnd == request.endFrame)
        return;
    m_queue.append(request);
    startNext();
}

void BakeController::cancelBake()
{
    m_queue.clear();
    if (m_busy)
        m_app->m_jobs->cancel(m_jobId);
}

void BakeController::startNext()
{
    if (m_busy || m_detached || m_queue.isEmpty())
        return;
    if (!ensureFile()) {
        m_queue.clear();
        m_app->setLastMessage(tr("Could not open the bake cache"), QStringLiteral("error"));
        return;
    }
    const Request request = m_queue.takeFirst();

    // Same reason as export: the bake walks the decode pools from its own thread.
    m_app->setPlaying(false);
    recomputeStale();

    auto state = std::make_shared<JobState>();
    state->file = m_file;
    state->request = request;
    state->preset = bakeQuality();
    state->snapshot = std::make_shared<const drift::Project>(m_app->m_project.sequenceView({}).detachedCopy());
    state->fingerprint = drift::bakeFingerprint(*state->snapshot, request.startFrame, request.endFrame);
    m_activePreIds.clear();
    for (const drift::Bake &bake : m_index->bakes) {
        m_activePreIds.insert(bake.id);
        state->pre.insert(bake.id, {bake.startFrame, bake.endFrame,
                                    !m_staleIds.contains(bake.id) && bake.fps == m_app->m_project.fps()});
    }

    m_busy = true;
    m_progress = 0.0;
    m_activeStart = request.startFrame;
    m_activeEnd = request.endFrame;

    QPointer<BakeController> self(this);
    m_jobId = m_app->m_jobs->start(
        QStringLiteral("bake"), QStringLiteral("%1-%2").arg(request.startFrame).arg(request.endFrame),
        JobRegistry::Lane::Render,
        [self, state](JobContext &ctx) {
            auto last = std::chrono::steady_clock::now();
            state->result = drift::BakeWriter::bake(
                *state->file, *state->snapshot, state->request.startFrame, state->request.endFrame,
                state->preset, state->fingerprint, [&](double fraction) {
                    ctx.progress(fraction);
                    const auto now = std::chrono::steady_clock::now();
                    if (now - last >= kProgressInterval) {
                        last = now;
                        QMetaObject::invokeMethod(
                            self.data(),
                            [self, fraction]() {
                                if (self)
                                    self->onProgress(fraction);
                            },
                            Qt::QueuedConnection);
                    }
                    return !ctx.cancelled();
                });
            if (state->result.ok)
                ctx.succeed({{QStringLiteral("bakeId"), state->result.bakeId}});
            else if (!state->result.cancelled)
                ctx.fail(QStringLiteral("bake_error"), state->result.error);
        },
        [this, state](const QJsonObject &job) {
            const QJsonObject error = job.value(QStringLiteral("error")).toObject();
            onJobDone(state, error.value(QStringLiteral("code")).toString() == QLatin1String("cancelled"),
                      error.value(QStringLiteral("message")).toString());
            return QJsonObject{};
        });
    m_app->setLastMessage(tr("Baking…"));
    emit busyChanged();
    emit progressChanged();
    emit bakesChanged();
}

void BakeController::onProgress(double fraction)
{
    if (!m_busy)
        return;
    m_progress = fraction;
    publish();
    emit progressChanged();
}

void BakeController::onJobDone(const std::shared_ptr<JobState> &state, bool cancelled, const QString &error)
{
    const drift::BakeWriter::Result &result = state->result;
    const drift::Project &snapshot = *state->snapshot;

    // A fingerprint covers a range, so any bake whose range this job changed needs one for the new
    // range: a cancelled bake that came out short, and old bakes that were trimmed or split but
    // whose frames the edit did not touch.
    QStringList ids;
    QList<QPair<qint64, qint64>> ranges;
    const auto index = state->file->snapshot();
    if (!result.bakeId.isEmpty() && (result.startFrame != state->request.startFrame
                                     || result.endFrame != state->request.endFrame)) {
        ids.append(result.bakeId);
        ranges.append({result.startFrame, result.endFrame});
    }
    for (const drift::Bake &bake : index->bakes) {
        if (bake.id == result.bakeId)
            continue;
        bool originFresh = false;
        const auto pre = state->pre.constFind(bake.id);
        if (pre != state->pre.constEnd()) {
            if (pre->startFrame == bake.startFrame && pre->endFrame == bake.endFrame)
                continue;
            originFresh = pre->fresh;
        } else {
            for (const JobState::Pre &candidate : std::as_const(state->pre)) {
                if (candidate.fresh && candidate.startFrame <= bake.startFrame && candidate.endFrame >= bake.endFrame)
                    originFresh = true;
            }
        }
        if (originFresh && bake.fps == snapshot.fps()) {
            ids.append(bake.id);
            ranges.append({bake.startFrame, bake.endFrame});
        }
    }
    if (!ids.isEmpty()) {
        const QList<QByteArray> fingerprints = drift::bakeFingerprints(snapshot, ranges);
        bool changed = false;
        for (qsizetype i = 0; i < ids.size(); ++i) {
            const drift::Bake *bake = index->bakeById(ids.at(i));
            if (bake && bake->fingerprint != fingerprints.at(i))
                changed = state->file->setFingerprint(ids.at(i), fingerprints.at(i)) || changed;
        }
        if (changed)
            state->file->commit();
    }

    m_busy = false;
    m_jobId.clear();
    m_progress = 0.0;
    m_activePreIds.clear();

    if (!m_detached) {
        if (cancelled)
            m_app->setLastMessage(tr("Bake cancelled"));
        else if (!result.ok)
            m_app->setLastMessage(error.isEmpty() ? tr("Bake failed") : error, QStringLiteral("error"));
        else
            m_app->setLastMessage(tr("Bake complete"));
    }

    publish();
    recomputeStale();

    std::list<std::function<void()>> ops;
    ops.swap(m_afterJob);
    for (const auto &op : ops)
        op();
    if (m_detached) {
        m_detached = false;
        releaseFile();
    }
    syncProject();
    maybeCompact();

    emit busyChanged();
    emit progressChanged();
    startNext();
}

void BakeController::whenIdle(std::function<void()> op)
{
    if (m_busy)
        m_afterJob.push_back(std::move(op));
    else
        op();
}

void BakeController::deleteBake(const QString &id)
{
    whenIdle([this, id]() {
        if (!m_file)
            return;
        m_file->deleteBake(id);
        m_file->commit();
        publish();
        recomputeStale();
        maybeCompact();
    });
}

void BakeController::deleteAllBakes()
{
    m_queue.clear();
    if (m_busy)
        m_app->m_jobs->cancel(m_jobId);
    whenIdle([this]() {
        if (!m_file)
            return;
        for (const drift::Bake &bake : m_file->snapshot()->bakes)
            m_file->deleteBake(bake.id);
        m_file->commit();
        publish();
        recomputeStale();
        maybeCompact();
    });
}

void BakeController::maybeCompact()
{
    if (!m_file || m_busy || m_app->m_exportInProgress)
        return;
    const quint64 file = m_file->fileBytes();
    if (file <= quint64(kCompactMinBytes) || m_file->liveBytes() * 2 >= file)
        return;
    // Fails on Windows while a reader holds the file; the next delete tries again.
    if (m_file->compact())
        publish();
}

void BakeController::setWorkAreaToBake(const QString &id)
{
    if (!m_index)
        return;
    const drift::Bake *bake = m_index->bakeById(id);
    if (!bake)
        return;
    const drift::Project before = m_app->m_project;
    m_app->m_project.setWorkAreaInUs(usForFrame(bake->startFrame, bake->fps));
    m_app->m_project.setWorkAreaOutUs(usForFrame(bake->endFrame, bake->fps));
    m_app->pushProjectEdit(before, tr("Set work area to bake"));
    m_app->finishEdit(tr("Work area set"));
    emit m_app->workAreaChanged();
}

std::shared_ptr<drift::BakeReaderPool> BakeController::exportPool(bool includeStale)
{
    if (m_detached || !m_file || !m_index || m_index->bakes.empty())
        return nullptr;
    if (includeStale)
        return m_app->playback()->bakeReaderPool();
    if (m_staleTimer.isActive())
        recomputeStale();
    auto filtered = std::make_shared<drift::BakeIndex>(*m_index);
    std::erase_if(filtered->bakes, [this](const drift::Bake &bake) { return m_staleIds.contains(bake.id); });
    auto pool = std::make_shared<drift::BakeReaderPool>();
    pool->setSource(m_file->path(), std::move(filtered));
    return pool;
}

#include "moc_BakeController.cpp"
