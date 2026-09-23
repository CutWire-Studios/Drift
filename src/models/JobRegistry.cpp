#include "JobRegistry.h"

#include <QDateTime>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPointer>
#include <QRunnable>
#include <QUuid>

struct JobContext::State
{
    mutable QMutex mutex;
    double progress = 0.0;
    QString status;
    bool finished = false;
    bool ok = false;
    QString errorCode;
    QString error;
    QJsonObject result;
};

void JobContext::progress(double fraction, const QString &status)
{
    QMutexLocker lock(&m_state->mutex);
    m_state->progress = qBound(0.0, fraction, 1.0);
    if (!status.isEmpty())
        m_state->status = status;
}

void JobContext::fail(const QString &code, const QString &message)
{
    QMutexLocker lock(&m_state->mutex);
    m_state->finished = true;
    m_state->ok = false;
    m_state->errorCode = code;
    m_state->error = message;
}

void JobContext::succeed(const QJsonObject &result)
{
    QMutexLocker lock(&m_state->mutex);
    m_state->finished = true;
    m_state->ok = true;
    m_state->result = result;
}

struct JobRegistry::Entry
{
    QString id;
    QString kind;
    QString target;
    qint64 startedMs = 0;
    bool active = true;
    JobContext context;
};

namespace {
constexpr int kMaxRetainedJobs = 64;
}

JobRegistry::JobRegistry(QObject *parent)
    : QObject(parent)
{
    m_modelPool.setMaxThreadCount(1);
    m_networkPool.setMaxThreadCount(2);
}

JobRegistry::~JobRegistry()
{
    for (const auto &entry : std::as_const(m_jobs))
        entry->context.m_cancel.storeRelaxed(1);
    m_modelPool.waitForDone();
    m_networkPool.waitForDone();
}

QString JobRegistry::start(const QString &kind, const QString &target, Lane lane,
                           std::function<void(JobContext &)> work,
                           std::function<QJsonObject(const QJsonObject &)> done)
{
    auto entry = std::make_shared<Entry>();
    entry->id = QUuid::createUuid().toString(QUuid::Id128).left(12);
    entry->kind = kind;
    entry->target = target;
    entry->startedMs = QDateTime::currentMSecsSinceEpoch();
    entry->context.m_state = std::make_shared<JobContext::State>();
    entry->context.m_state->status = tr("Queued");
    m_jobs.insert(entry->id, entry);
    m_order.append(entry->id);

    // Forget the oldest finished jobs so a long session doesn't grow this without bound.
    for (int i = 0; m_order.size() > kMaxRetainedJobs && i < m_order.size();) {
        const auto old = m_jobs.value(m_order.at(i));
        if (old && !old->active) {
            m_jobs.remove(m_order.at(i));
            m_order.removeAt(i);
        } else {
            ++i;
        }
    }

    QPointer<JobRegistry> self(this);
    const QString id = entry->id;
    QThreadPool &pool = lane == Lane::Model ? m_modelPool : m_networkPool;
    pool.start(QRunnable::create([self, entry, id, work = std::move(work), done = std::move(done)]() mutable {
        if (!entry->context.cancelled())
            work(entry->context);
        {
            QMutexLocker lock(&entry->context.m_state->mutex);
            if (entry->context.cancelled() && !entry->context.m_state->ok) {
                entry->context.m_state->errorCode = QStringLiteral("cancelled");
                entry->context.m_state->error = QStringLiteral("Cancelled");
            } else if (!entry->context.m_state->finished) {
                entry->context.m_state->errorCode = QStringLiteral("internal");
                entry->context.m_state->error = QStringLiteral("Job ended without a result");
            }
            entry->context.m_state->finished = true;
        }
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(),
            [self, entry, id, done = std::move(done)]() {
                if (!self)
                    return;
                entry->active = false;
                if (done) {
                    const QJsonObject extra = done(self->job(id));
                    QMutexLocker lock(&entry->context.m_state->mutex);
                    for (auto it = extra.begin(); it != extra.end(); ++it)
                        entry->context.m_state->result.insert(it.key(), it.value());
                }
                emit self->jobChanged(id);
                emit self->jobFinished(id, self->job(id).value(QStringLiteral("ok")).toBool());
            },
            Qt::QueuedConnection);
    }));
    emit jobChanged(id);
    return id;
}

bool JobRegistry::cancel(const QString &id)
{
    const auto entry = m_jobs.value(id);
    if (!entry || !entry->active)
        return false;
    entry->context.m_cancel.storeRelaxed(1);
    emit jobChanged(id);
    return true;
}

QJsonObject JobRegistry::job(const QString &id) const
{
    const auto entry = m_jobs.value(id);
    if (!entry)
        return {};
    const JobContext::State &state = *entry->context.m_state;
    QMutexLocker lock(&state.mutex);
    QJsonObject json{
        {QStringLiteral("id"), entry->id},
        {QStringLiteral("kind"), entry->kind},
        {QStringLiteral("target"), entry->target},
        {QStringLiteral("active"), entry->active},
        {QStringLiteral("progress"), state.progress},
        {QStringLiteral("status"), state.status},
        {QStringLiteral("elapsed"),
         (QDateTime::currentMSecsSinceEpoch() - entry->startedMs) / 1000.0},
    };
    if (!entry->active) {
        json.insert(QStringLiteral("ok"), state.ok);
        if (state.ok)
            json.insert(QStringLiteral("result"), state.result);
        else
            json.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), state.errorCode},
                                                             {QStringLiteral("message"), state.error}});
    }
    return json;
}

QJsonArray JobRegistry::jobs() const
{
    QJsonArray out;
    for (const QString &id : m_order)
        out.append(job(id));
    return out;
}

bool JobRegistry::hasActive(const QString &kind, const QString &target) const
{
    for (const auto &entry : m_jobs) {
        if (entry->active && entry->kind == kind && entry->target == target)
            return true;
    }
    return false;
}
