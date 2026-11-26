#include "app/farm/ChunkJoin.h"
#include "app/farm/FarmHttp.h"
#include "app/farm/RenderQueue.h"
#include "app/farm/RenderTask.h"
#include "app/farm/TemplateBatch.h"
#include "core/project/Project.h"
#include "core/project/folder/ProjectFolder.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace drift::farm;

class TestFarm : public QObject
{
    Q_OBJECT

private slots:
    void queueHoldsPersistsAndReorders();
    void queueStartReleasesHeld();
    void httpRequiresToken();
    void joinedChunksKeepEveryFrame();
    void templateBatchQueuesOneUnchunkedJobPerRow();
};

namespace {

QString writeProject(const QTemporaryDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write("{}");
    return path;
}

} // namespace

void TestFarm::queueHoldsPersistsAndReorders()
{
    QTemporaryDir dir;
    const QString project = writeProject(dir, QStringLiteral("p.json"));
    QString first;
    QString second;
    {
        RenderQueue queue(dir.filePath(QStringLiteral("q")));
        queue.setPaused(true);
        first = queue.addProjectFile(project, dir.filePath(QStringLiteral("a.mp4")), {}, JobTarget::Local, false);
        second = queue.addProjectFile(project, dir.filePath(QStringLiteral("b.mp4")), {}, JobTarget::Farm, false);
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        // Held unless the queue starts immediately.
        QCOMPARE(queue.find(first)->state, JobState::Held);
        QCOMPARE(queue.rowCount(), 2);

        queue.move(second, -1);
        QCOMPARE(queue.jobs().first()->id, second);
        queue.cancel(first);
        QCOMPARE(queue.find(first)->state, JobState::Cancelled);
        queue.retry(first);
        QCOMPARE(queue.find(first)->state, JobState::Held);
    }
    RenderQueue reloaded(dir.filePath(QStringLiteral("q")));
    QCOMPARE(reloaded.rowCount(), 2);
    QCOMPARE(reloaded.jobs().first()->id, second);
    QCOMPARE(reloaded.find(second)->target, JobTarget::Farm);
    reloaded.remove(second);
    QCOMPARE(reloaded.rowCount(), 1);
}

void TestFarm::queueStartReleasesHeld()
{
    QTemporaryDir dir;
    const QString project = writeProject(dir, QStringLiteral("p.json"));
    RenderQueue queue(dir.filePath(QStringLiteral("q")));
    queue.setPaused(true);
    const QString farmJob = queue.addProjectFile(project, dir.filePath(QStringLiteral("a.mp4")), {}, JobTarget::Farm, false);
    queue.start(farmJob);
    QCOMPARE(queue.find(farmJob)->state, JobState::Queued);
    queue.hold(farmJob);
    QCOMPARE(queue.find(farmJob)->state, JobState::Held);
    queue.startAll();
    QCOMPARE(queue.find(farmJob)->state, JobState::Queued);
}

void TestFarm::httpRequiresToken()
{
    FarmHttpServer server;
    server.setToken(QStringLiteral("secret"));
    server.setHandler([](const FarmHttpServer::Request &request, FarmHttpServer::Reply reply) {
        reply.json(200, QJsonObject{{QStringLiteral("path"), request.path}});
    });
    QVERIFY(server.listen(0));

    QNetworkAccessManager nam;
    const auto get = [&](const QByteArray &auth) {
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/v1/ping").arg(server.port())));
        if (!auth.isEmpty())
            request.setRawHeader("Authorization", auth);
        QNetworkReply *reply = nam.get(request);
        QSignalSpy done(reply, &QNetworkReply::finished);
        done.wait(5000);
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        return qMakePair(status, body);
    };
    QCOMPARE(get({}).first, 401);
    QCOMPARE(get("Bearer wrong").first, 401);
    const auto ok = get("Bearer secret");
    QCOMPARE(ok.first, 200);
    QVERIFY(ok.second.contains("/v1/ping"));
}

void TestFarm::joinedChunksKeepEveryFrame()
{
    if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty())
        QSKIP("ffmpeg is needed to make the test chunks");
    QTemporaryDir dir;
    QStringList chunks;
    const QList<qint64> frames{45, 30};
    for (int i = 0; i < 2; ++i) {
        const QString path = dir.filePath(QStringLiteral("c%1.mp4").arg(i));
        QProcess ffmpeg;
        ffmpeg.start(QStringLiteral("ffmpeg"),
                     {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-f"),
                      QStringLiteral("lavfi"), QStringLiteral("-i"),
                      QStringLiteral("testsrc2=size=160x90:rate=30"), QStringLiteral("-frames:v"),
                      QString::number(frames.at(i)), QStringLiteral("-c:v"), QStringLiteral("libx264"),
                      QStringLiteral("-g"), QStringLiteral("30"), QStringLiteral("-bf"), QStringLiteral("2"),
                      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), path});
        QVERIFY(ffmpeg.waitForFinished(30000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        QCOMPARE(countVideoFrames(path), frames.at(i));
        chunks.append(path);
    }
    const QString audio = dir.filePath(QStringLiteral("a.m4a"));
    QProcess ffmpeg;
    ffmpeg.start(QStringLiteral("ffmpeg"),
                 {QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-f"),
                  QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:duration=2.5"),
                  QStringLiteral("-c:a"), QStringLiteral("aac"), audio});
    QVERIFY(ffmpeg.waitForFinished(30000));

    const QString output = dir.filePath(QStringLiteral("joined.mp4"));
    QString error;
    QVERIFY2(joinChunks(chunks, frames, 30, 1, audio, output, &error), qPrintable(error));
    QCOMPARE(countVideoFrames(output), qint64(75));
}


void TestFarm::templateBatchQueuesOneUnchunkedJobPerRow()
{
    QTemporaryDir dir;
    const QString root = dir.filePath(QStringLiteral("tpl"));
    drift::Project project;
    project.setName(QStringLiteral("Template"));
    project.tracks().clear();
    drift::projdir::FileMap files = drift::projdir::splitProject(project.toJson(true), root);
    files.insert(QStringLiteral("scripts/main.lua"),
                 QByteArray("title { id = \"headline\", text = param \"headline\", duration = sec(param \"seconds\") }\n"));
    files.insert(QStringLiteral("template.json"), QByteArray(R"({
  "params": [
    {"name": "headline", "type": "text", "default": "Hello"},
    {"name": "seconds", "type": "number", "default": 2}
  ],
  "data": "data/rows.csv"
})"));
    files.insert(QStringLiteral("data/rows.csv"), QByteArray("headline,seconds\nFirst,3\nBroken,x\nThird,4\n"));
    QVERIFY(drift::projdir::writeProjectFolder(root, files).error.isEmpty());

    RenderQueue queue(dir.filePath(QStringLiteral("q")));
    TemplateBatch batch;
    batch.folder = root;
    batch.output = dir.filePath(QStringLiteral("out-{row}.mp4"));
    batch.all = true;
    batch.target = JobTarget::Farm;
    batch.hold = true;
    const TemplateBatchResult result = queueTemplateBatch(queue, batch);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QCOMPARE(result.jobs.size(), 2);
    QCOMPARE(result.failures.size(), 1);
    QVERIFY(result.failures.first().startsWith(QLatin1String("row 2")));
    QCOMPARE(result.jobs.at(0).row, 1);
    QCOMPARE(result.jobs.at(1).row, 3);
    QCOMPARE(result.jobs.at(1).output, dir.filePath(QStringLiteral("out-3.mp4")));

    const RenderJob *job = queue.find(result.jobs.at(1).id);
    QVERIFY(job);
    QCOMPARE(job->target, JobTarget::Farm);
    QCOMPARE(job->state, JobState::Held);
    QCOMPARE(job->source.value(QStringLiteral("row")).toObject().value(QStringLiteral("headline")).toString(),
             QStringLiteral("Third"));
    QCOMPARE(QFileInfo(job->source.value(QStringLiteral("template")).toString()), QFileInfo(root));
    QVERIFY(QFileInfo::exists(job->projectPath));
    QFile snapshot(job->projectPath);
    QVERIFY(snapshot.open(QIODevice::ReadOnly));
    QVERIFY(snapshot.readAll().contains("Third"));

    batch.all = false;
    batch.row = 9;
    QVERIFY(!queueTemplateBatch(queue, batch).error.isEmpty());
}

QTEST_GUILESS_MAIN(TestFarm)
#include "tst_farm.moc"
