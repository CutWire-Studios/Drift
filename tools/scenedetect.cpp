// Headless shot-boundary detection: scan a media file and print where the shots begin.
// Exists so thresholds can be tuned against real footage without a GUI round-trip.
// Usage: scenedetect [--threshold T] [--min-scene S] [--no-adaptive] [--start SEC]
//                    [--duration SEC] [--csv] <media-file>

#include "engine/MediaProbe.h"
#include "engine/SceneDetect.h"
#include "core/Time.h"

#include <unistd.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>

namespace {

// Rounded to whole milliseconds as an integer before anything is split off, so a time a
// hair under a second boundary cannot render its seconds truncated and its fraction
// rounded up — 2.9997 s is 00:03.000, never 00:02.000.
QString timecode(drift::TimeUs us)
{
    const qint64 totalMs = (us + 500) / 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(totalMs / 60000, 2, 10, QLatin1Char('0'))
        .arg((totalMs / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalMs % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // Must match src/main.cpp, or AppDataLocation points somewhere else and the tool cannot
    // see models installed as addons.
    QCoreApplication::setApplicationName("CutWire Drift");
    QCoreApplication::setOrganizationName("CutWire Drift");

    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = app.arguments();
    drift::SceneDetectOptions options;
    bool useCache = true;
    double startSeconds = 0.0;
    double durationSeconds = 0.0; // 0 means "to the end"
    bool csv = false;
    QString path;

    auto usage = [&err]() {
        err << "usage: scenedetect [--threshold T] [--min-scene S] [--no-adaptive] "
               "[--no-audio] [--no-cache] [--objects] [--start SEC] [--duration SEC] "
               "[--csv] <media-file>\n";
        return 1;
    };

    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QLatin1String("--threshold") && i + 1 < args.size()) {
            options.threshold = args.at(++i).toDouble();
        } else if (a == QLatin1String("--min-scene") && i + 1 < args.size()) {
            options.minSceneSeconds = args.at(++i).toDouble();
        } else if (a == QLatin1String("--no-adaptive")) {
            options.allowAdaptive = false;
        } else if (a == QLatin1String("--no-audio")) {
            options.analyzeAudio = false;
        } else if (a == QLatin1String("--no-cache")) {
            useCache = false;
        } else if (a == QLatin1String("--objects")) {
            options.detectObjects = true;
        } else if (a == QLatin1String("--start") && i + 1 < args.size()) {
            startSeconds = args.at(++i).toDouble();
        } else if (a == QLatin1String("--duration") && i + 1 < args.size()) {
            durationSeconds = args.at(++i).toDouble();
        } else if (a == QLatin1String("--csv")) {
            csv = true;
        } else if (path.isEmpty() && !a.startsWith(QLatin1Char('-'))) {
            path = a;
        } else {
            return usage();
        }
    }
    if (path.isEmpty() || options.threshold <= 0.0 || options.minSceneSeconds <= 0.0)
        return usage();

    const MediaInfo info = MediaProbe::probe(path);
    if (!info.ok || info.durationUs <= 0) {
        err << "probe failed: " << info.errorString << "\n";
        return 1;
    }

    drift::SceneDetectRequest request;
    request.path = path;
    request.sourceIn = drift::secondsToUs(startSeconds);
    request.sourceOut = durationSeconds > 0.0
                            ? request.sourceIn + drift::secondsToUs(durationSeconds)
                            : info.durationUs;
    request.sourceOut = qMin(request.sourceOut, drift::TimeUs(info.durationUs));
    request.options = options;

    QElapsedTimer timer;
    timer.start();

    bool fromCache = false;
    drift::SceneAnalysis cached;
    if (useCache && drift::loadCachedAnalysis(request, &cached)) {
        fromCache = true;
        if (!csv) {
            out << "cache:     hit (" << drift::sceneCachePath(request) << ")\n";
        }
    }

    QString error;
    // Progress goes to stderr so it never pollutes --csv on stdout, and only when that is a
    // terminal — redirected, the carriage returns would pile up into one unreadable line.
    const bool showProgress = !csv && ::isatty(fileno(stderr));
    int lastPercent = -1;
    const drift::SceneAnalysis analysis = fromCache ? cached : drift::detectScenes(
        request,
        [&](double fraction, const QString &) {
            const int percent = int(fraction * 100);
            if (showProgress && percent != lastPercent) {
                lastPercent = percent;
                err << "\rscanning " << percent << "%" << Qt::flush;
            }
            return true;
        },
        &error);
    const qint64 elapsedMs = timer.elapsed();

    if (showProgress)
        err << "\r" << Qt::flush;

    if (analysis.isEmpty()) {
        err << "failed: " << (error.isEmpty() ? QStringLiteral("no scenes") : error) << "\n";
        return 1;
    }

    if (useCache && !fromCache)
        drift::storeCachedAnalysis(request, analysis);

    if (csv) {
        out << "index,start,end,duration,motion,loudness,objects,score,labels\n";
        for (int i = 0; i < analysis.scenes.size(); ++i) {
            const drift::Scene &s = analysis.scenes.at(i);
            out << i << "," << QString::number(drift::usToSeconds(s.sourceIn), 'f', 3) << ","
                << QString::number(drift::usToSeconds(s.sourceOut), 'f', 3) << ","
                << QString::number(drift::usToSeconds(s.duration()), 'f', 3) << ","
                << QString::number(s.motion, 'f', 4) << ","
                << QString::number(s.loudness, 'f', 4) << ","
                << QString::number(s.objects, 'f', 4) << ","
                << QString::number(s.score, 'f', 4) << ","
                << s.labels.join(QLatin1Char(' ')) << "\n";
        }
        return 0;
    }

    const double scannedSeconds = drift::usToSeconds(request.sourceOut - request.sourceIn);
    out << "detector:  " << analysis.detector << "\n";
    out << "threshold: " << QString::number(analysis.thresholdUsed, 'f', 2)
        << (analysis.adaptiveUsed ? "  (adaptive — the fixed threshold found too little)"
                                  : "  (fixed)")
        << "\n";
    out << "scanned:   " << QString::number(scannedSeconds, 'f', 1) << "s in "
        << QString::number(elapsedMs / 1000.0, 'f', 1) << "s ("
        << QString::number(scannedSeconds / qMax(1.0, elapsedMs / 1000.0), 'f', 1) << "x realtime)\n";
    out << "scenes:    " << analysis.scenes.size() << " (" << analysis.cuts.size() << " cuts)\n";

    if (analysis.objectsScanned)
        out << "objects:   labelled\n";
    out << "\n  #  start        end          dur      motion   loud     obj    score   cut   labels\n";
    for (int i = 0; i < analysis.scenes.size(); ++i) {
        const drift::Scene &s = analysis.scenes.at(i);
        // The cut that opened this scene; the first scene was not opened by one.
        const QString strength =
            i > 0 && i - 1 < analysis.cuts.size()
                ? QString::number(analysis.cuts.at(i - 1).strength, 'f', 2)
                : QStringLiteral("—");
        out << QStringLiteral("%1  %2  %3  %4  %5  %6  %7  %8  %9  %10\n")
                   .arg(i, 3)
                   .arg(timecode(s.sourceIn), -11)
                   .arg(timecode(s.sourceOut), -11)
                   .arg(QString::number(drift::usToSeconds(s.duration()), 'f', 2), 7)
                   .arg(QString::number(s.motion, 'f', 3), 6)
                   .arg(QString::number(s.loudness, 'f', 3), 6)
                   .arg(QString::number(s.objects, 'f', 3), 6)
                   .arg(QString::number(s.score, 'f', 3), 6)
                   .arg(strength, 4)
                   .arg(s.labels.join(QLatin1String(", ")));
    }

    return 0;
}
