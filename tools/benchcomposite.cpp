// Composite throughput benchmark. Builds a fixed stress timeline over two source
// clips and times FrameCompositor::compositeAt across a playback-like sweep.
//
//   benchcomposite <a.mp4> <b.mp4> [previewScale]
#include "core/Clip.h"
#include "core/Project.h"
#include "core/Track.h"
#include "core/Transition.h"
#include "engine/FontCatalog.h"
#include "engine/FrameCompositor.h"

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QTextStream>

#include <algorithm>
#include <vector>

namespace {

drift::Effect makeEffect(const QString &catalogId)
{
    drift::Effect effect;
    effect.catalogId = catalogId;
    return effect;
}

drift::Clip makeVideoClip(const QString &id, const QString &path, drift::TimeUs start,
                          drift::TimeUs duration)
{
    drift::Clip clip;
    clip.id = id;
    clip.type = drift::ClipType::Video;
    clip.path = path;
    clip.timelineStart = start;
    clip.timelineDuration = duration;
    clip.srcIn = 0;
    clip.srcOut = duration;
    return clip;
}

} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    reloadFontCatalog();
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList args = app.arguments();
    if (args.size() < 3) {
        err << "usage: benchcomposite <a.mp4> <b.mp4> [previewScale]\n";
        return 1;
    }
    const QString pathA = args.at(1);
    const QString pathB = args.at(2);
    const double previewScale = args.size() > 3 ? args.at(3).toDouble() : 1.0;

    drift::Project project;
    project.setResolution(1920, 1080);
    project.setFps(30);
    project.tracks().clear();

    // Top track: two clips with a crossfade between them. The first carries a
    // scale animation (the case that thrashes the decoder's size-keyed cache)
    // plus a three-effect chain (the case that stacks GPU round trips).
    drift::Track top;
    top.type = drift::TrackType::Video;

    drift::Clip a = makeVideoClip(QStringLiteral("clipA"), pathA, 0, 5'000'000);
    a.transformW.setKeyframe(0, 1920.0);
    a.transformW.setKeyframe(5'000'000, 2400.0);
    a.transformH.setKeyframe(0, 1080.0);
    a.transformH.setKeyframe(5'000'000, 1350.0);
    a.effects.append(makeEffect(QStringLiteral("adjust.contrast")));
    a.effects.append(makeEffect(QStringLiteral("stylize.vignette")));
    a.effects.append(makeEffect(QStringLiteral("rgb_split")));

    drift::Clip b = makeVideoClip(QStringLiteral("clipB"), pathB, 4'500'000, 5'500'000);
    b.effects.append(makeEffect(QStringLiteral("adjust.saturation")));

    top.clips << a << b;

    drift::Transition t;
    t.id = QStringLiteral("t0");
    t.fromClipId = a.id;
    t.toClipId = b.id;
    t.kindId = QStringLiteral("crossfade");
    t.durationUs = 1'000'000;
    top.transitions << t;

    // Bottom track: a full-canvas layer under everything, so every frame
    // composites at least two decoded sources.
    drift::Track bottom;
    bottom.type = drift::TrackType::Video;
    drift::Clip base = makeVideoClip(QStringLiteral("clipBase"), pathB, 0, 10'000'000);
    bottom.clips << base;

    project.tracks() << top << bottom;

    // DRIFT_BENCH_SAVE=<path> writes the stress timeline out as a project file,
    // so the same scene can be opened in the app.
    if (qEnvironmentVariableIsSet("DRIFT_BENCH_SAVE")) {
        const QString path = qEnvironmentVariable("DRIFT_BENCH_SAVE");
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(project.toJson()).toJson());
            out << "wrote project: " << path << "\n";
        }
        return 0;
    }

    FrameCompositor compositor;
    compositor.setProject(&project);

    FrameCompositor::RenderOptions options;
    options.previewScale = previewScale;

    const int fps = project.fps();
    const int frames = 150; // 5 s of a playback-like forward sweep
    const drift::TimeUs step = 1'000'000 / fps;

    // Warm up decoders, GL context and shader compilation.
    for (int i = 0; i < 5; ++i)
        compositor.compositeAt(i * step, options);

    std::vector<double> samples;
    samples.reserve(frames);

    // DRIFT_BENCH_TEXTURE=1 times the preview path (composite to a GL texture,
    // no readback) instead of the export path (composite and read back).
    const bool texturePath = qEnvironmentVariableIsSet("DRIFT_BENCH_TEXTURE");

    QElapsedTimer timer;
    for (int i = 0; i < frames; ++i) {
        const drift::TimeUs t = static_cast<drift::TimeUs>(i) * step;
        timer.start();
        if (texturePath) {
            const GpuFrameTexture frame = compositor.compositeToTextureAt(t, options);
            samples.push_back(timer.nsecsElapsed() / 1e6);
            if (!frame.isValid()) {
                err << "null texture at " << t << "\n";
                return 1;
            }
        } else {
            const QImage frame = compositor.compositeAt(t, options);
            samples.push_back(timer.nsecsElapsed() / 1e6);
            if (frame.isNull()) {
                err << "null frame at " << t << "\n";
                return 1;
            }
        }
    }

    // Optional golden dump: benchcomposite <a> <b> <scale> <out_prefix>
    if (args.size() > 4) {
        const QString prefix = args.at(4);
        for (const drift::TimeUs t : {drift::TimeUs(1'000'000), drift::TimeUs(4'800'000),
                                      drift::TimeUs(7'000'000)}) {
            const QImage frame = compositor.compositeAt(t, options);
            frame.save(QStringLiteral("%1_%2.png").arg(prefix).arg(t));
        }
    }

    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    double total = 0.0;
    for (double s : samples)
        total += s;

    const double mean = total / samples.size();
    const double median = sorted[sorted.size() / 2];
    const double p95 = sorted[static_cast<size_t>(sorted.size() * 0.95)];

    out << Qt::fixed;
    out.setRealNumberPrecision(2);
    out << "scale=" << previewScale << "  frames=" << frames << "\n";
    out << "  mean   " << mean << " ms  (" << (1000.0 / mean) << " fps)\n";
    out << "  median " << median << " ms\n";
    out << "  p95    " << p95 << " ms\n";
    out << "  min    " << sorted.front() << " ms\n";
    out << "  max    " << sorted.back() << " ms\n";
    out << "  budget " << (1000.0 / fps) << " ms/frame at " << fps << " fps\n";
    return 0;
}
