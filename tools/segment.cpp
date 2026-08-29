// Headless smoke test for the SAM2 video tracker: seed an object with a point on one frame, then
// propagate through the clip and write out tinted overlays.
// Usage: segment [--time SECONDS] [--point X,Y] [--frames N] [--fps F] [--out PREFIX] <media-file>
//        X,Y are normalized 0..1 frame coordinates. Defaults: --time 0 --point 0.5,0.5 --frames 1

#include "engine/ClipReaderPool.h"
#include "engine/MediaProbe.h"
#include "engine/Sam2Segmenter.h"
#include "core/Time.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // Must match src/main.cpp, or AppDataLocation points somewhere else and the tool cannot see
    // models installed as addons.
    QCoreApplication::setApplicationName("CutWire Drift");
    QCoreApplication::setOrganizationName("CutWire Drift");

    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = app.arguments();
    double seconds = 0.0;
    double px = 0.5;
    double py = 0.5;
    int frames = 1;
    double fps = 30.0;
    QString prefix = QStringLiteral("segment");
    QString path;

    auto usage = [&err]() {
        err << "usage: segment [--time SECONDS] [--point X,Y] [--frames N] [--fps F] "
               "[--out PREFIX] <media-file>\n";
        return 1;
    };

    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QLatin1String("--time") && i + 1 < args.size()) {
            seconds = args.at(++i).toDouble();
        } else if (a == QLatin1String("--point") && i + 1 < args.size()) {
            const QStringList parts = args.at(++i).split(QLatin1Char(','));
            if (parts.size() != 2)
                return usage();
            px = parts.at(0).toDouble();
            py = parts.at(1).toDouble();
        } else if (a == QLatin1String("--frames") && i + 1 < args.size()) {
            frames = args.at(++i).toInt();
        } else if (a == QLatin1String("--fps") && i + 1 < args.size()) {
            fps = args.at(++i).toDouble();
        } else if (a == QLatin1String("--out") && i + 1 < args.size()) {
            prefix = args.at(++i);
        } else if (path.isEmpty() && !a.startsWith(QLatin1Char('-'))) {
            path = a;
        } else {
            return usage();
        }
    }
    if (path.isEmpty() || frames < 1 || fps <= 0.0)
        return usage();

    const MediaInfo info = MediaProbe::probe(path);
    if (!info.ok || info.durationUs <= 0) {
        err << "probe failed: " << info.errorString << "\n";
        return 1;
    }

    drift::Sam2Segmenter &sam = drift::Sam2Segmenter::instance();
    if (!sam.available()) {
        err << "sam2 unavailable: " << sam.lastError() << "\n";
        return 1;
    }
    out << "variant: " << sam.modelVariant() << "\n";

    std::unique_ptr<drift::Sam2Segmenter::Track> track = sam.newTrack();
    if (!track) {
        err << "could not create a track: " << sam.lastError() << "\n";
        return 1;
    }

    const drift::TimeUs step = drift::TimeUs(drift::kUsPerSecond / fps);
    QElapsedTimer timer;

    for (int i = 0; i < frames; ++i) {
        const drift::TimeUs at = drift::secondsToUs(seconds) + drift::TimeUs(i) * step;
        const QImage frame = ClipReaderPool::instance().readVideoFrame(path, 1, at, 0, 0);
        if (frame.isNull()) {
            err << "no frame decoded at index " << i << "\n";
            return 1;
        }

        timer.restart();
        const drift::Sam2Embedding embedding = sam.encode(frame);
        const qint64 encodeMs = timer.elapsed();
        if (!embedding.valid) {
            err << "encode failed: " << sam.lastError() << "\n";
            return 1;
        }

        timer.restart();
        drift::Sam2Result result;
        if (i == 0) {
            drift::Sam2Prompt prompt;
            prompt.points << QPointF(px * frame.width(), py * frame.height());
            prompt.labels << 1;
            result = track->seed(embedding, prompt);
        } else {
            result = track->step(embedding);
        }
        const qint64 trackMs = timer.elapsed();

        if (!result.ok) {
            err << "frame " << i << " failed: " << result.error << "\n";
            return 1;
        }

        out << "frame " << i << ": encode " << encodeMs << " ms, track " << trackMs << " ms, iou "
            << QString::number(result.iou, 'f', 3) << ", coverage "
            << QString::number(result.maskFraction * 100.0, 'f', 2) << "%"
            << (result.occluded ? "  [occluded]" : "") << "\n";
        out.flush();

        // Tinted overlay, so a mask that has drifted off the subject is obvious at a glance.
        QImage overlay = frame.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < overlay.height(); ++y) {
            const uchar *m = result.mask.constScanLine(y);
            uchar *o = overlay.scanLine(y);
            for (int x = 0; x < overlay.width(); ++x) {
                if (m[x] < 128)
                    continue;
                o[x * 4 + 0] = uchar((o[x * 4 + 0] + 255) / 2);
                o[x * 4 + 2] = uchar(o[x * 4 + 2] / 2);
            }
        }
        if (i == 0) {
            QPainter p(&overlay);
            p.setPen(Qt::green);
            p.drawEllipse(QPointF(px * frame.width(), py * frame.height()), 6.0, 6.0);
            p.end();
        }

        const QString name =
            frames == 1 ? prefix + QStringLiteral("-overlay.png")
                        : QStringLiteral("%1-%2.png").arg(prefix).arg(i, 3, 10, QLatin1Char('0'));
        if (!overlay.save(name)) {
            err << "failed to write " << name << "\n";
            return 1;
        }
    }

    out << "done\n";
    return 0;
}
