// Headless smoke test for Video Depth Anything: estimate depth over a run of frames, write the
// sidecar, then read it back and dump each frame as a 16-bit PNG.
// Usage: depth [--time SECONDS] [--frames N] [--fps F] [--short-side PX] [--out PREFIX] <media-file>
//        Defaults: --time 0 --frames 64 --fps 30 --short-side 392
//
// Per frame it prints the mean depth and the mean change from the frame before. Flicker or a jump
// at a window seam (frames 24..31, 46..53, ...) shows up as a spike in the change column.

#include "core/Time.h"
#include "engine/ClipReaderPool.h"
#include "engine/DepthSidecar.h"
#include "engine/MediaProbe.h"
#include "engine/VdaDepth.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QTextStream>

#include <cmath>
#include <cstring>

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
    int frames = 64;
    double fps = 30.0;
    int shortSide = 392;
    QString prefix = QStringLiteral("depth");
    QString path;

    auto usage = [&err]() {
        err << "usage: depth [--time SECONDS] [--frames N] [--fps F] [--short-side PX] "
               "[--out PREFIX] <media-file>\n";
        return 1;
    };

    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QLatin1String("--time") && i + 1 < args.size()) {
            seconds = args.at(++i).toDouble();
        } else if (a == QLatin1String("--frames") && i + 1 < args.size()) {
            frames = args.at(++i).toInt();
        } else if (a == QLatin1String("--fps") && i + 1 < args.size()) {
            fps = args.at(++i).toDouble();
        } else if (a == QLatin1String("--short-side") && i + 1 < args.size()) {
            shortSide = args.at(++i).toInt();
        } else if (a == QLatin1String("--out") && i + 1 < args.size()) {
            prefix = args.at(++i);
        } else if (path.isEmpty() && !a.startsWith(QLatin1Char('-'))) {
            path = a;
        } else {
            return usage();
        }
    }
    if (path.isEmpty() || frames < 1 || fps <= 0.0 || shortSide < 112)
        return usage();

    const MediaInfo info = MediaProbe::probe(path);
    if (!info.ok) {
        err << "probe failed: " << info.errorString << "\n";
        return 1;
    }

    drift::VdaDepth &vda = drift::VdaDepth::instance();
    const QString sidecarPath = prefix + QStringLiteral(".driftdepth");
    drift::DepthSidecarWriter writer;
    bool writerOpen = false;
    QString error;
    int emitted = 0;

    std::unique_ptr<drift::VdaDepth::Pass> pass;
    pass = vda.newPass(shortSide, [&](drift::TimeUs ptsUs, const float *disparity) {
        if (!writerOpen) {
            writerOpen = writer.open(sidecarPath, pass->size(), vda.variant(), &error);
            if (!writerOpen)
                return false;
        }
        ++emitted;
        return writer.writeFrame(ptsUs, disparity, &error);
    });
    if (!pass) {
        err << "depth model unavailable: " << vda.lastError() << "\n";
        return 1;
    }
    out << "variant: " << vda.variant() << "\n";

    const drift::TimeUs step = drift::TimeUs(drift::kUsPerSecond / fps);
    QElapsedTimer total;
    total.start();
    for (int i = 0; i < frames; ++i) {
        const drift::TimeUs at = drift::secondsToUs(seconds) + drift::TimeUs(i) * step;
        const QImage frame = ClipReaderPool::instance().readVideoFrame(path, 1, at, 0, 0);
        if (frame.isNull()) {
            err << "no frame decoded at index " << i << "\n";
            return 1;
        }
        if (!pass->push(frame, at)) {
            err << "frame " << i << " failed: " << (error.isEmpty() ? pass->error() : error) << "\n";
            return 1;
        }
    }
    if (!pass->flush() || !writerOpen || !writer.finish(&error)) {
        err << "flush failed: " << (error.isEmpty() ? pass->error() : error) << "\n";
        return 1;
    }
    const qint64 ms = total.elapsed();
    out << "inference " << pass->size().width() << "x" << pass->size().height() << ", " << emitted
        << " frames in " << ms << " ms (" << ms / std::max(1, emitted) << " ms/frame, decode "
        << "included)\n";

    const std::shared_ptr<const drift::DepthSidecar> sidecar =
        drift::DepthSidecar::open(sidecarPath, &error);
    if (!sidecar) {
        err << "reading the sidecar back failed: " << error << "\n";
        return 1;
    }

    std::shared_ptr<const drift::DepthFrame> previous;
    for (int i = 0; i < sidecar->frameCount(); ++i) {
        const std::shared_ptr<const drift::DepthFrame> frame = sidecar->frameAt(sidecar->ptsAt(i));
        if (!frame) {
            err << "frame " << i << " failed to decode\n";
            return 1;
        }
        double mean = 0.0;
        double change = 0.0;
        for (size_t k = 0; k < frame->values.size(); ++k) {
            mean += frame->values[k];
            if (previous)
                change += std::abs(double(frame->values[k]) - previous->values[k]);
        }
        mean /= double(frame->values.size()) * 65535.0;
        change /= double(frame->values.size()) * 65535.0;
        out << "frame " << i << ": mean " << QString::number(mean, 'f', 4) << ", change "
            << (previous ? QString::number(change, 'f', 4) : QStringLiteral("-")) << "\n";
        previous = frame;

        QImage png(frame->size, QImage::Format_Grayscale16);
        for (int y = 0; y < frame->size.height(); ++y)
            std::memcpy(png.scanLine(y), frame->values.data() + size_t(y) * frame->size.width(),
                        size_t(frame->size.width()) * 2);
        if (!png.save(prefix + QStringLiteral("-%1.png").arg(i, 3, 10, QLatin1Char('0')))) {
            err << "failed to write frame " << i << "\n";
            return 1;
        }
    }

    out << "sidecar: " << sidecarPath << "\n";
    return 0;
}
