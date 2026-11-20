#include <QtTest>

#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "core/project/media/ImageSequence.h"
#include "engine/decode/readers/ClipReader.h"
#include "engine/decode/probe/MediaProbe.h"

class TestMediaProbe : public QObject
{
    Q_OBJECT

private slots:
    void missingFileFails();
    void readsDisplayMatrixRotation();
    void multiTrackAudioProbing();
    void probesImageSequence();
    void identifiesEditFriendlyCodecs();
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

void TestMediaProbe::probesImageSequence()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QColor colors[] = {Qt::red, Qt::green, Qt::blue, Qt::yellow, Qt::white};
    for (int i = 0; i < 5; ++i) {
        QImage img(64, 48, QImage::Format_RGB32);
        img.fill(colors[i]);
        QVERIFY(img.save(dir.filePath(QStringLiteral("f_%1.png").arg(i + 1, 4, 10, QLatin1Char('0')))));
    }

    drift::ImageSequence seq;
    seq.pattern = dir.filePath(QStringLiteral("f_%04d.png"));
    seq.start = 1;
    drift::setImageSequenceFps(seq, 10.0);
    const QString path = drift::imageSequencePath(seq);

    const MediaInfo info = MediaProbe::probe(path);
    QVERIFY2(info.ok, qPrintable(info.errorString));
    int videoStreams = 0;
    for (const StreamInfo &s : info.streams) {
        if (s.type != StreamInfo::Type::Video)
            continue;
        ++videoStreams;
        QCOMPARE(s.width, 64);
        QCOMPARE(s.height, 48);
        QVERIFY(qAbs(s.fps - 10.0) < 0.01);
    }
    QCOMPARE(videoStreams, 1);
    QVERIFY(qAbs(info.durationUs - 500000) < 20000);

    ClipReader reader;
    QVERIFY(reader.open(path));
    QImage frame;
    QVERIFY(reader.readVideoFrameAt(200000, frame, 64, 48));
    QVERIFY(!frame.isNull());
    const QColor c = frame.pixelColor(32, 24);
    QVERIFY2(c.blue() > 200 && c.red() < 50 && c.green() < 50, qPrintable(c.name()));
}


void TestMediaProbe::identifiesEditFriendlyCodecs()
{
    QVERIFY(MediaProbe::isEditFriendlyCodec(QStringLiteral("prores")));
    QVERIFY(MediaProbe::isEditFriendlyCodec(QStringLiteral("ProRes")));
    QVERIFY(MediaProbe::isEditFriendlyCodec(QStringLiteral("dnxhd")));
    QVERIFY(MediaProbe::isEditFriendlyCodec(QStringLiteral("DNxHD")));
    QVERIFY(MediaProbe::isEditFriendlyCodec(QStringLiteral("ffv1")));
    QVERIFY(MediaProbe::isEditFriendlyCodec(QStringLiteral("rawvideo")));
    QVERIFY(MediaProbe::isEditFriendlyCodec(QStringLiteral("cfhd")));

    QVERIFY(!MediaProbe::isEditFriendlyCodec(QStringLiteral("h264")));
    QVERIFY(!MediaProbe::isEditFriendlyCodec(QStringLiteral("hevc")));
    QVERIFY(!MediaProbe::isEditFriendlyCodec(QStringLiteral("vp9")));
    QVERIFY(!MediaProbe::isEditFriendlyCodec(QStringLiteral("av1")));
    QVERIFY(!MediaProbe::isEditFriendlyCodec(QStringLiteral("")));
}

QTEST_MAIN(TestMediaProbe)
#include "tst_mediaprobe.moc"
