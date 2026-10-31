#include "app/online/downloads/DownloadCenter.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

DownloadCenter::Job make(const QString &id, const QString &status, const QString &kind = QStringLiteral("stock"))
{
    DownloadCenter::Job job;
    job.id = id;
    job.kind = kind;
    job.title = id;
    job.status = status;
    job.cancellable = true;
    job.retryable = true;
    return job;
}

} // namespace

class TestDownloads : public QObject
{
    Q_OBJECT

private slots:
    void newestFirstAndStartedSignal()
    {
        DownloadCenter center;
        QSignalSpy started(&center, &DownloadCenter::jobStarted);
        center.upsert(make(QStringLiteral("a"), QStringLiteral("waiting")));
        center.upsert(make(QStringLiteral("b"), QStringLiteral("queued")));
        center.upsert(make(QStringLiteral("b"), QStringLiteral("downloading")));
        QCOMPARE(started.size(), 2);
        const QVariantList jobs = center.jobs();
        QCOMPARE(jobs.size(), 2);
        QCOMPARE(jobs.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("b"));
        QCOMPARE(jobs.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("a"));
        QCOMPARE(center.activeCount(), 2);
    }

    void finishTransitions()
    {
        DownloadCenter center;
        QSignalSpy finished(&center, &DownloadCenter::jobFinished);
        QSignalSpy started(&center, &DownloadCenter::jobStarted);
        center.upsert(make(QStringLiteral("a"), QStringLiteral("downloading")));
        QVERIFY(!center.job(QStringLiteral("a")).finishedAt.isValid());
        center.upsert(make(QStringLiteral("a"), QStringLiteral("done")));
        QCOMPARE(finished.size(), 1);
        QVERIFY(center.job(QStringLiteral("a")).finishedAt.isValid());
        QVERIFY(!center.job(QStringLiteral("a")).cancellable);
        QCOMPARE(center.activeCount(), 0);

        center.upsert(make(QStringLiteral("a"), QStringLiteral("done")));
        QCOMPARE(finished.size(), 1);

        center.upsert(make(QStringLiteral("a"), QStringLiteral("queued")));
        QCOMPARE(started.size(), 2);
        QVERIFY(!center.job(QStringLiteral("a")).finishedAt.isValid());
        QCOMPARE(center.activeCount(), 1);
    }

    void speedIsSampledFromBytes()
    {
        DownloadCenter center;
        DownloadCenter::Job job = make(QStringLiteral("a"), QStringLiteral("downloading"));
        job.bytesTotal = 10'000'000;
        job.bytesReceived = 1000;
        center.upsert(job);
        QCOMPARE(center.jobMap(QStringLiteral("a")).value(QStringLiteral("speed")).toDouble(), 0.0);
        QTest::qWait(450);
        job.bytesReceived = 1000 + 450'000;
        center.upsert(job);
        const double speed = center.jobMap(QStringLiteral("a")).value(QStringLiteral("speed")).toDouble();
        QVERIFY2(speed > 400'000 && speed < 1'200'000, qPrintable(QString::number(speed)));
        job.status = QStringLiteral("done");
        center.upsert(job);
        QCOMPARE(center.jobMap(QStringLiteral("a")).value(QStringLiteral("speed")).toDouble(), 0.0);
    }

    void clearFinishedKeepsRunning()
    {
        DownloadCenter center;
        center.upsert(make(QStringLiteral("a"), QStringLiteral("done")));
        center.upsert(make(QStringLiteral("b"), QStringLiteral("failed")));
        center.upsert(make(QStringLiteral("c"), QStringLiteral("cancelled")));
        center.upsert(make(QStringLiteral("d"), QStringLiteral("downloading")));
        QSignalSpy changed(&center, &DownloadCenter::jobsChanged);
        center.clearFinished();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(center.jobs().size(), 1);
        QVERIFY(center.contains(QStringLiteral("d")));
        center.clearFinished();
        QCOMPARE(changed.size(), 1);
    }

    void handlersRouteByKind()
    {
        DownloadCenter center;
        QStringList log;
        center.registerHandler(QStringLiteral("stock"),
                               [&](const QString &id) { log << QStringLiteral("stock-cancel:") + id; },
                               [&](const QString &id) { log << QStringLiteral("stock-retry:") + id; });
        center.registerHandler(QStringLiteral("addon"),
                               [&](const QString &id) { log << QStringLiteral("addon-cancel:") + id; }, {});

        center.upsert(make(QStringLiteral("s"), QStringLiteral("downloading")));
        center.upsert(make(QStringLiteral("x"), QStringLiteral("downloading"), QStringLiteral("addon")));
        center.cancel(QStringLiteral("s"));
        center.cancel(QStringLiteral("x"));
        center.retry(QStringLiteral("s"));
        QCOMPARE(log, (QStringList{QStringLiteral("stock-cancel:s"), QStringLiteral("addon-cancel:x")}));

        center.upsert(make(QStringLiteral("s"), QStringLiteral("failed")));
        center.cancel(QStringLiteral("s"));
        center.retry(QStringLiteral("s"));
        center.retry(QStringLiteral("x"));
        QCOMPARE(log.last(), QStringLiteral("stock-retry:s"));
        QCOMPARE(log.size(), 3);

        DownloadCenter::Job fixed = make(QStringLiteral("n"), QStringLiteral("failed"));
        fixed.retryable = false;
        center.upsert(fixed);
        center.retry(QStringLiteral("n"));
        QCOMPARE(log.size(), 3);
    }
};

QTEST_MAIN(TestDownloads)
#include "tst_downloads.moc"
