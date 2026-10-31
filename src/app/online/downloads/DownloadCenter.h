#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

// Every download the app runs, in one list: stock, community and Drift asset installs, app
// updates and addon packages. Producers own the transfer and push their state in with
// upsert(); the center owns presentation (newest first, speed, counts) and routes cancel and
// retry back to whoever registered for the job's kind.
class DownloadCenter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY jobsChanged)
    Q_PROPERTY(int revision READ revision NOTIFY jobsChanged)

public:
    // kind: stock | update | community | drift-asset | sfx | addon
    // status: waiting | queued | processing | downloading | importing | installing | done |
    //         failed | cancelled
    struct Job
    {
        QString id;
        QString kind;
        QString title;
        QString mediaKind;
        QString status;
        QString phase;
        double progress = 0;
        qint64 bytesReceived = 0;
        qint64 bytesTotal = 0;
        QString error;
        QString errorCode;
        bool retryable = false;
        bool cancellable = false;
        QVariantMap result;
        QDateTime startedAt;
        QDateTime finishedAt;
    };

    using Handler = std::function<void(const QString &id)>;

    explicit DownloadCenter(QObject *parent = nullptr);

    static DownloadCenter *instance();

    QVariantList jobs() const;
    int activeCount() const;
    int revision() const { return m_revision; }

    static bool isFinished(const QString &status);

    // Adds the job or replaces its state. A new id emits jobStarted; moving into done, failed
    // or cancelled emits jobFinished; moving out of one (a retry) restarts the clock. Speed is
    // sampled here from bytesReceived, so producers never compute it.
    void upsert(const Job &job);
    bool contains(const QString &id) const { return m_jobs.contains(id); }
    Job job(const QString &id) const;
    QVariantMap jobMap(const QString &id) const;
    void registerHandler(const QString &kind, Handler cancel, Handler retry);

    Q_INVOKABLE void cancel(const QString &id);
    Q_INVOKABLE void retry(const QString &id);
    // Forgets finished, failed and cancelled jobs. Running ones stay.
    Q_INVOKABLE void clearFinished();

signals:
    void jobStarted(const QString &id);
    void jobFinished(const QString &id);
    void jobsChanged();

private:
    struct Entry
    {
        Job job;
        double speed = 0;
        QElapsedTimer sampleClock;
        qint64 sampleBytes = 0;
    };

    static QVariantMap toMap(const Entry &entry);
    void bump();

    QHash<QString, Entry> m_jobs;
    QStringList m_order;
    struct Handlers
    {
        Handler cancel;
        Handler retry;
    };
    QHash<QString, Handlers> m_handlers;
    QElapsedTimer m_lastEmit;
    int m_revision = 0;
};
