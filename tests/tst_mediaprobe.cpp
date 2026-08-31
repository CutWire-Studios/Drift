#include <QtTest>

#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "engine/MediaProbe.h"

class TestMediaProbe : public QObject
{
    Q_OBJECT

private slots:
    void missingFileFails();
    void readsDisplayMatrixRotation();
    void multiTrackAudioProbing();
};

void TestMediaProbe::missingFileFails()
{
    const MediaInfo info = MediaProbe::probe(QStringLiteral("/nonexistent/path/does-not-exist.mp4"));
    QVERIFY(!info.ok);
    QVERIFY(!info.errorString.isEmpty());
    QCOMPARE(info.streams.size(), 0);
}

void TestMediaProbe::readsDisplayMatrixRotation()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a rotated test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString flat = dir.filePath(QStringLiteral("flat.mp4"));
    QProcess make;
    make.start(ffmpeg,
               {QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                QStringLiteral("-i"), QStringLiteral("color=c=red:s=64x32:r=25:d=1"),
                QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
                QStringLiteral("yuv420p"), flat});
    QVERIFY(make.waitForFinished(30000));
    QCOMPARE(make.exitCode(), 0);

    // -display_rotation is an input option, so tagging needs a second stream-copy pass.
    const QString rotated = dir.filePath(QStringLiteral("rotated.mp4"));
    QProcess tag;
    tag.start(ffmpeg,
              {QStringLiteral("-y"), QStringLiteral("-display_rotation:v:0"),
               QStringLiteral("-90"), QStringLiteral("-i"), flat, QStringLiteral("-c"),
               QStringLiteral("copy"), rotated});
    QVERIFY(tag.waitForFinished(30000));
    QCOMPARE(tag.exitCode(), 0);

    const MediaInfo info = MediaProbe::probe(rotated);
    QVERIFY(info.ok);

    bool sawVideo = false;
    for (const StreamInfo &stream : info.streams) {
        if (stream.type != StreamInfo::Type::Video)
            continue;
        sawVideo = true;
        // The coded size stays as stored; only rotationDegrees describes the display turn.
        QCOMPARE(stream.width, 64);
        QCOMPARE(stream.height, 32);
        QCOMPARE(stream.rotationDegrees, 90);
    }
    QVERIFY(sawVideo);

    // An untagged source must stay at zero.
    const MediaInfo plain = MediaProbe::probe(flat);
    QVERIFY(plain.ok);
    for (const StreamInfo &stream : plain.streams) {
        if (stream.type == StreamInfo::Type::Video)
            QCOMPARE(stream.rotationDegrees, 0);
    }
}

void TestMediaProbe::multiTrackAudioProbing()
{
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        QSKIP("ffmpeg not available to generate a multi-track test clip");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString multiTrack = dir.filePath(QStringLiteral("multitrack.mkv"));
    QProcess make;
    make.start(ffmpeg,
               {QStringLiteral("-y"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("color=c=black:s=64x32:r=25:d=1"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=440:d=1"),
                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("sine=frequency=880:d=1"),
                QStringLiteral("-map"), QStringLiteral("0:v"),
                QStringLiteral("-map"), QStringLiteral("1:a"),
                QStringLiteral("-map"), QStringLiteral("2:a"),
                QStringLiteral("-metadata:s:a:0"), QStringLiteral("title=Game Audio"),
                QStringLiteral("-metadata:s:a:1"), QStringLiteral("title=Microphone"),
                QStringLiteral("-c:v"), QStringLiteral("libx264"),
                QStringLiteral("-c:a"), QStringLiteral("aac"),
                multiTrack});
    QVERIFY(make.waitForFinished(30000));
    QCOMPARE(make.exitCode(), 0);

    const MediaInfo info = MediaProbe::probe(multiTrack);
    QVERIFY(info.ok);

    const QList<StreamInfo> audio = MediaProbe::audioStreams(multiTrack);
    QCOMPARE(audio.size(), 2);
    QCOMPARE(audio[0].audioStreamOrdinal, 0);
    QCOMPARE(audio[0].title, QStringLiteral("Game Audio"));
    QCOMPARE(audio[1].audioStreamOrdinal, 1);
    QCOMPARE(audio[1].title, QStringLiteral("Microphone"));
}

QTEST_MAIN(TestMediaProbe)
#include "tst_mediaprobe.moc"
