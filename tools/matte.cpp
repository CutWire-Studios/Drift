// Headless smoke test for the RVM video matter: run a clip through the recurrent network and write
// out the alpha and the decontaminated foreground.
// Usage: matte [--time SECONDS] [--frames N] [--fps F] [--variant NAME] [--out PREFIX] <media-file>
//        Defaults: --time 0 --frames 1 --fps 30

#include "core/Time.h"
#include "engine/ClipReaderPool.h"
#include "engine/MediaProbe.h"
#include "engine/RvmMatter.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
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
    int frames = 1;
    double fps = 30.0;
    QString variant;
    QString prefix = QStringLiteral("matte");
    QString path;

    auto usage = [&err]() {
        err << "usage: matte [--time SECONDS] [--frames N] [--fps F] [--variant NAME] "
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
        } else if (a == QLatin1String("--variant") && i + 1 < args.size()) {
            variant = args.at(++i);
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

    drift::RvmMatter &rvm = drift::RvmMatter::instance();
    out << "installed: " << drift::RvmMatter::installedVariants().join(QLatin1String(", ")) << "\n";

    std::unique_ptr<drift::RvmMatter::Track> track = rvm.newTrack(variant);
    if (!track) {
        err << "rvm unavailable: " << rvm.lastError() << "\n";
        return 1;
    }
    out << "variant: " << track->variant() << "\n";

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
        const drift::RvmResult result = track->step(frame);
        const qint64 ms = timer.elapsed();
        if (!result.ok) {
            err << "frame " << i << " failed: " << result.error << "\n";
            return 1;
        }

        // Coverage and the soft-edge count together say whether this is really a matte: a run that
        // has collapsed to a hard mask reports almost no partial pixels.
        qint64 covered = 0;
        qint64 partial = 0;
        for (int y = 0; y < result.alpha.height(); ++y) {
            const uchar *a = result.alpha.constScanLine(y);
            for (int x = 0; x < result.alpha.width(); ++x) {
                if (a[x] >= 128)
                    ++covered;
                if (a[x] > 8 && a[x] < 248)
                    ++partial;
            }
        }
        const double total = double(result.alpha.width()) * result.alpha.height();

        out << "frame " << i << ": " << ms << " ms, " << result.alpha.width() << "x"
            << result.alpha.height() << ", coverage "
            << QString::number(covered / total * 100.0, 'f', 2) << "%, soft edge "
            << QString::number(partial / total * 100.0, 'f', 2) << "%\n";
        out.flush();

        const QString suffix =
            frames == 1 ? QString() : QStringLiteral("-%1").arg(i, 3, 10, QLatin1Char('0'));
        if (!result.alpha.save(prefix + suffix + QStringLiteral("-alpha.png"))
            || !result.foreground.save(prefix + suffix + QStringLiteral("-fgr.png"))) {
            err << "failed to write output for frame " << i << "\n";
            return 1;
        }
    }

    out << "done\n";
    return 0;
}
