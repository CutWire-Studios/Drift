#include "app/online/downloads/DownloadCenter.h"

#include <QCoreApplication>

namespace {

// Speed is sampled over a window rather than averaged over the whole transfer, so it tracks a
// source that slows down. Progress-only updates are also held to this rate: a transfer reports
// per network chunk, and rebuilding the QML list that often would respin it hundreds of times
// a second.
constexpr int kSampleMs = 400;

} // namespace

DownloadCenter::DownloadCenter(QObject *parent)
    : QObject(parent)
{
}

DownloadCenter *DownloadCenter::instance()
{
    static DownloadCenter *shared = new DownloadCenter(QCoreApplication::instance());
    return shared;
}

bool DownloadCenter::isFinished(const QString &status)
{
    return status == QLatin1String("done") || status == QLatin1String("failed")
        || status == QLatin1String("cancelled");
}

void DownloadCenter::upsert(const Job &incoming)
{
    if (incoming.id.isEmpty())
        return;
    const bool isNew = !m_jobs.contains(incoming.id);
    Entry &entry = m_jobs[incoming.id];
    const bool wasFinished = !isNew && isFinished(entry.job.status);
    const bool wasRunning = !isNew && !wasFinished;
    const QString previousStatus = entry.job.status;
    const QString previousPhase = entry.job.phase;
    const QDateTime started = entry.job.startedAt;
    const QDateTime previousFinished = entry.job.finishedAt;

    entry.job = incoming;
    const bool finished = isFinished(incoming.status);
    const bool restarted = wasFinished && !finished;
    if (isNew)
        m_order.prepend(incoming.id);

    if (isNew || restarted) {
        entry.job.startedAt = incoming.startedAt.isValid() ? incoming.startedAt : QDateTime::currentDateTime();
        entry.job.finishedAt = {};
        entry.speed = 0;
        entry.sampleClock.invalidate();
    } else {
        entry.job.startedAt = started;
    }

    bool structural = isNew || restarted || previousStatus != incoming.status
                      || previousPhase != incoming.phase || finished;
    if (finished) {
        entry.job.finishedAt = incoming.finishedAt.isValid() ? incoming.finishedAt
                               : wasFinished                ? previousFinished
                                                            : QDateTime::currentDateTime();
        entry.job.cancellable = false;
        entry.speed = 0;
        entry.sampleClock.invalidate();
    } else if (incoming.bytesReceived > 0) {
        if (!entry.sampleClock.isValid()) {
            entry.sampleClock.start();
            entry.sampleBytes = incoming.bytesReceived;
        } else if (entry.sampleClock.elapsed() >= kSampleMs) {
            const qint64 delta = incoming.bytesReceived - entry.sampleBytes;
            const qint64 ms = entry.sampleClock.restart();
            if (ms > 0 && delta >= 0)
                entry.speed = double(delta) * 1000.0 / double(ms);
            entry.sampleBytes = incoming.bytesReceived;
            structural = true;
        }
    }

    if (isNew || restarted)
        emit jobStarted(incoming.id);
    if (structural || !m_lastEmit.isValid() || m_lastEmit.elapsed() >= kSampleMs)
        bump();
    if (finished && (isNew || wasRunning))
        emit jobFinished(incoming.id);
}

DownloadCenter::Job DownloadCenter::job(const QString &id) const
{
    const auto it = m_jobs.constFind(id);
    return it == m_jobs.constEnd() ? Job() : it->job;
}

QVariantMap DownloadCenter::jobMap(const QString &id) const
{
    const auto it = m_jobs.constFind(id);
    return it == m_jobs.constEnd() ? QVariantMap() : toMap(*it);
}

QVariantMap DownloadCenter::toMap(const Entry &entry)
{
    const Job &job = entry.job;
    const bool finished = isFinished(job.status);
    return {
        {QStringLiteral("id"), job.id},
        {QStringLiteral("itemId"), job.id},
        {QStringLiteral("kind"), job.kind},
        {QStringLiteral("title"), job.title.isEmpty() ? job.id : job.title},
        {QStringLiteral("mediaKind"), job.mediaKind},
        {QStringLiteral("status"), job.status},
        {QStringLiteral("phase"), job.phase},
        {QStringLiteral("progress"), job.progress},
        {QStringLiteral("bytesReceived"), job.bytesReceived},
        {QStringLiteral("bytesTotal"), job.bytesTotal},
        {QStringLiteral("speed"), entry.speed},
        {QStringLiteral("error"), job.error},
        {QStringLiteral("errorMessage"), job.error},
        {QStringLiteral("errorCode"), job.errorCode},
        {QStringLiteral("retryable"), job.retryable && (job.status == QLatin1String("failed") || job.status == QLatin1String("cancelled"))},
        {QStringLiteral("cancellable"), job.cancellable && !finished},
        {QStringLiteral("running"), !finished && job.status != QLatin1String("waiting")},
        {QStringLiteral("finished"), finished},
        {QStringLiteral("startedAt"), job.startedAt},
        {QStringLiteral("finishedAt"), job.finishedAt},
        {QStringLiteral("result"), job.result},
        {QStringLiteral("filePath"), job.result.value(QStringLiteral("path"))},
        {QStringLiteral("assetId"), job.result.value(QStringLiteral("assetId"))},
        {QStringLiteral("destinationDir"), job.result.value(QStringLiteral("destinationDir"))},
    };
}

QVariantList DownloadCenter::jobs() const
{
    QVariantList out;
    out.reserve(m_order.size());
    for (const QString &id : m_order)
        out.append(toMap(m_jobs.value(id)));
    return out;
}

int DownloadCenter::activeCount() const
{
    int n = 0;
    for (const Entry &entry : m_jobs) {
        if (!isFinished(entry.job.status))
            ++n;
    }
    return n;
}

void DownloadCenter::registerHandler(const QString &kind, Handler cancel, Handler retry)
{
    m_handlers.insert(kind, {std::move(cancel), std::move(retry)});
}

void DownloadCenter::cancel(const QString &id)
{
    const auto it = m_jobs.constFind(id);
    if (it == m_jobs.constEnd() || isFinished(it->job.status) || !it->job.cancellable)
        return;
    const Handler fn = m_handlers.value(it->job.kind).cancel;
    if (fn)
        fn(id);
}

void DownloadCenter::retry(const QString &id)
{
    const auto it = m_jobs.constFind(id);
    if (it == m_jobs.constEnd() || !it->job.retryable
        || (it->job.status != QLatin1String("failed") && it->job.status != QLatin1String("cancelled")))
        return;
    const Handler fn = m_handlers.value(it->job.kind).retry;
    if (fn)
        fn(id);
}

void DownloadCenter::clearFinished()
{
    QStringList kept;
    for (const QString &id : std::as_const(m_order)) {
        if (isFinished(m_jobs.value(id).job.status)) {
            m_jobs.remove(id);
            continue;
        }
        kept.append(id);
    }
    if (kept.size() == m_order.size())
        return;
    m_order = kept;
    bump();
}

void DownloadCenter::bump()
{
    ++m_revision;
    m_lastEmit.start();
    emit jobsChanged();
}
