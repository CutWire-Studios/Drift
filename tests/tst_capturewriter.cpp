#include <QtTest>

#include "engine/capture/CaptureWriter.h"

#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <cmath>

extern "C" {
#include <libavformat/avformat.h>
}

using namespace drift;

class CaptureWriterTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsBadSettings();
    void writesAPlayableVideo();
    void irregularTimestampsSurvive();
    void nonAdvancingTimestampsAreDropped();
    void writesVideoWithAudio();
    void abortLeavesNoFile();
    void partialFileIsNeverLeftBehind();
};

namespace {

// A frame that changes every time, so the encoder cannot collapse the whole recording into one
// keyframe and hide a timestamp bug behind it.
QImage frameAt(int index, QSize size)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(QColor::fromHsv((index * 37) % 360, 200, 200));
    return image;
}

struct Probe
{
    bool ok = false;
    int videoStreams = 0;
    int audioStreams = 0;
    int width = 0;
    int height = 0;
    QString videoCodec;
    QString audioCodec;
    int videoPixFmt = -1;
    double durationSeconds = 0.0;
};

Probe probeFile(const QString &path)
{
    Probe probe;
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return probe;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return probe;
    }
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        const AVCodecParameters *par = fmt->streams[i]->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
            ++probe.videoStreams;
            probe.width = par->width;
            probe.height = par->height;
            probe.videoPixFmt = par->format;
            if (const AVCodec *c = avcodec_find_decoder(par->codec_id))
                probe.videoCodec = QString::fromUtf8(c->name);
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            ++probe.audioStreams;
            if (const AVCodec *c = avcodec_find_decoder(par->codec_id))
                probe.audioCodec = QString::fromUtf8(c->name);
        }
    }
    if (fmt->duration > 0)
        probe.durationSeconds = double(fmt->duration) / AV_TIME_BASE;
    probe.ok = true;
    avformat_close_input(&fmt);
    return probe;
}

} // namespace

void CaptureWriterTest::rejectsBadSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("r.mp4"));

    CaptureWriter::Settings settings;
    QString error;

    CaptureWriter noSize;
    QVERIFY(!noSize.open(path, settings, &error));
    QVERIFY(!error.isEmpty());

    // H.264 chroma is half resolution, so an odd dimension has to be refused here rather than
    // failing deep inside the encoder.
    settings.size = QSize(641, 480);
    CaptureWriter odd;
    QVERIFY(!odd.open(path, settings, &error));

    settings.size = QSize(640, 481);
    CaptureWriter oddHeight;
    QVERIFY(!oddHeight.open(path, settings, &error));

    settings.size = QSize(640, 480);
    CaptureWriter noPath;
    QVERIFY(!noPath.open(QString(), settings, &error));
}

void CaptureWriterTest::writesAPlayableVideo()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("screen.mp4"));

    CaptureWriter::Settings settings;
    settings.size = QSize(320, 240);
    settings.fps = 25;

    CaptureWriter writer;
    QString error;
    QVERIFY2(writer.open(path, settings, &error), qPrintable(error));
    for (int i = 0; i < 25; ++i) {
        const TimeUs stamp = TimeUs(i) * (kUsPerSecond / 25);
        QVERIFY2(writer.writeVideoFrame(frameAt(i, settings.size), stamp, &error),
                 qPrintable(error));
    }
    QVERIFY2(writer.finish(&error), qPrintable(error));

    // The ".part" discipline: nothing is left half-written beside the result.
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".part")));

    const Probe probe = probeFile(path);
    QVERIFY(probe.ok);
    QCOMPARE(probe.videoStreams, 1);
    QCOMPARE(probe.audioStreams, 0);
    QCOMPARE(probe.width, 320);
    QCOMPARE(probe.height, 240);
    QCOMPARE(probe.videoCodec, QStringLiteral("h264"));
    // 8-bit 4:2:0 and nothing more exotic: this is what decodes on a phone and in a browser.
    QCOMPARE(probe.videoPixFmt, int(AV_PIX_FMT_YUV420P));
    QVERIFY(probe.durationSeconds > 0.5);
}

void CaptureWriterTest::irregularTimestampsSurvive()
{
    // What a screen grab really delivers: nothing for a while, then a burst. The file still has to
    // come out with the right overall length rather than being timed as if it were a fixed rate.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bursty.mp4"));

    CaptureWriter::Settings settings;
    settings.size = QSize(160, 120);
    settings.fps = 30;

    CaptureWriter writer;
    QString error;
    QVERIFY2(writer.open(path, settings, &error), qPrintable(error));

    const TimeUs stamps[] = {0,       500'000, 520'000, 540'000, 1'500'000,
                             1'505'000, 2'000'000, 2'900'000};
    int index = 0;
    for (TimeUs stamp : stamps)
        QVERIFY2(writer.writeVideoFrame(frameAt(index++, settings.size), stamp, &error),
                 qPrintable(error));
    QVERIFY2(writer.finish(&error), qPrintable(error));
    QCOMPARE(writer.droppedFrames(), 0);

    const Probe probe = probeFile(path);
    QVERIFY(probe.ok);
    QCOMPARE(probe.videoStreams, 1);
    // Timed by the stamps, not by 8 frames at 30 fps (which would be a quarter of a second).
    QVERIFY2(probe.durationSeconds > 2.5,
             qPrintable(QStringLiteral("duration %1").arg(probe.durationSeconds)));
}

void CaptureWriterTest::nonAdvancingTimestampsAreDropped()
{
    // Two frames inside one clock tick is normal for a capture backend. A backwards or repeated
    // PTS would make the file unseekable, so such a frame is counted and skipped, not muxed.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("dupes.mp4"));

    CaptureWriter::Settings settings;
    settings.size = QSize(160, 120);
    CaptureWriter writer;
    QString error;
    QVERIFY2(writer.open(path, settings, &error), qPrintable(error));

    QVERIFY(writer.writeVideoFrame(frameAt(0, settings.size), 0, &error));
    QVERIFY(writer.writeVideoFrame(frameAt(1, settings.size), 40'000, &error));
    QVERIFY(writer.writeVideoFrame(frameAt(2, settings.size), 40'000, &error)); // repeat
    QVERIFY(writer.writeVideoFrame(frameAt(3, settings.size), 30'000, &error)); // backwards
    QVERIFY(writer.writeVideoFrame(frameAt(4, settings.size), 80'000, &error));
    QCOMPARE(writer.droppedFrames(), 2);
    QVERIFY2(writer.finish(&error), qPrintable(error));
    QVERIFY(probeFile(path).ok);
}

void CaptureWriterTest::writesVideoWithAudio()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("av.mp4"));

    CaptureWriter::Settings settings;
    settings.size = QSize(320, 240);
    settings.fps = 25;
    settings.audio = true;
    settings.audioSampleRate = 48000;
    settings.audioChannels = 2;

    CaptureWriter writer;
    QString error;
    QVERIFY2(writer.open(path, settings, &error), qPrintable(error));

    // Deliberately not a multiple of AAC's 1024-sample frame, so the FIFO has to carry a
    // remainder across every call — which is exactly what a real device does.
    const int chunk = 700;
    double phase = 0.0;
    for (int i = 0; i < 25; ++i) {
        QVERIFY2(writer.writeVideoFrame(frameAt(i, settings.size),
                                        TimeUs(i) * (kUsPerSecond / 25), &error),
                 qPrintable(error));
        std::vector<float> samples(size_t(chunk) * 2, 0.0f);
        for (int s = 0; s < chunk; ++s) {
            const float v = float(std::sin(phase));
            phase += 2.0 * M_PI * 440.0 / 48000.0;
            samples[size_t(s) * 2] = v;
            samples[size_t(s) * 2 + 1] = v;
        }
        QVERIFY2(writer.writeAudio(samples.data(), chunk, &error), qPrintable(error));
    }
    QVERIFY2(writer.finish(&error), qPrintable(error));

    const Probe probe = probeFile(path);
    QVERIFY(probe.ok);
    QCOMPARE(probe.videoStreams, 1);
    QCOMPARE(probe.audioStreams, 1);
    QCOMPARE(probe.audioCodec, QStringLiteral("aac"));
}

void CaptureWriterTest::abortLeavesNoFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("gone.mp4"));

    CaptureWriter::Settings settings;
    settings.size = QSize(160, 120);
    CaptureWriter writer;
    QString error;
    QVERIFY2(writer.open(path, settings, &error), qPrintable(error));
    QVERIFY(writer.writeVideoFrame(frameAt(0, settings.size), 0, &error));
    writer.abort();

    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".part")));
}

void CaptureWriterTest::partialFileIsNeverLeftBehind()
{
    // A recording that died without finish() must not leave something at the final path that
    // looks like footage — the ".part" is what guarantees that.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("crash.mp4"));

    CaptureWriter::Settings settings;
    settings.size = QSize(160, 120);
    {
        CaptureWriter writer;
        QString error;
        QVERIFY2(writer.open(path, settings, &error), qPrintable(error));
        QVERIFY(writer.writeVideoFrame(frameAt(0, settings.size), 0, &error));
        // Destructor runs without finish().
    }
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".part")));
}

QTEST_MAIN(CaptureWriterTest)
#include "tst_capturewriter.moc"
