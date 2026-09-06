#include "PlaybackDiagnostics.h"

#include "PlaybackStats.h"
#include "engine/ClipReader.h"
#include "engine/FrameCompositor.h"
#include "engine/GpuCompositor.h"
#include "engine/GpuStatus.h"
#include "engine/HwAccel.h"
#include "engine/MediaProbe.h"
#include "engine/PreviewVideoFrame.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>

#include <algorithm>
#include <vector>

namespace {

// Non-QObject class, same as DebugReport.
QString trDiag(const char *text)
{
    return QCoreApplication::translate("PlaybackDiagnostics", text);
}

QVariantMap row(const QString &label, const QString &value)
{
    return {{QStringLiteral("label"), label}, {QStringLiteral("value"), value}};
}

// Matches DebugReport's hint shape so the dialog can render both with one delegate.
QVariantMap hint(const QString &id, const QString &title, const QString &detail)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("title"), title},
            {QStringLiteral("detail"), detail}};
}

QString msText(double ms)
{
    return ms > 0.0 ? QStringLiteral("%1 ms").arg(ms, 0, 'f', 2) : QStringLiteral("—");
}

const StreamInfo *firstVideoStream(const MediaInfo &info)
{
    for (const StreamInfo &s : info.streams) {
        if (s.type == StreamInfo::Type::Video && !s.attachedPicture)
            return &s;
    }
    return nullptr;
}

double medianOf(std::vector<double> &samples)
{
    if (samples.empty())
        return 0.0;
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

// Decode the clip's own frame rate rather than the project's: the cost being measured is the
// source's, and a 60 fps clip in a 30 fps project still decodes every frame.
constexpr int kBenchmarkFrames = 40;
constexpr int kBenchmarkWarmup = 5;

} // namespace

QString PlaybackDiagnostics::bundledBenchmarkClip()
{
    // FFmpeg cannot open a qrc path, so the clip has to exist on disk. Cached next to the
    // other app scratch files and reused, rather than re-extracted per run.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir))
        return {};
    const QString out = QDir(dir).filePath(QStringLiteral("bench1080p60.mp4"));

    QFile src(QStringLiteral(":/diagnostics/bench1080p60.mp4"));
    if (!src.open(QIODevice::ReadOnly))
        return {};
    if (QFileInfo::exists(out) && QFileInfo(out).size() == src.size())
        return out;

    QFile dst(out);
    if (!dst.open(QIODevice::WriteOnly))
        return {};
    const bool ok = dst.write(src.readAll()) == src.size();
    dst.close();
    return ok ? out : QString{};
}

QVariantMap PlaybackDiagnostics::benchmarkClip(const QString &path, const QSize &canvas)
{
    QVariantMap out;
    const QString file = path.isEmpty() ? bundledBenchmarkClip() : path;
    out.insert(QStringLiteral("path"), file);
    if (file.isEmpty()) {
        out.insert(QStringLiteral("error"), trDiag("No clip to measure."));
        return out;
    }

    const MediaInfo info = MediaProbe::probe(file);
    const StreamInfo *video = info.ok ? firstVideoStream(info) : nullptr;
    if (!video) {
        out.insert(QStringLiteral("error"), trDiag("That file has no video stream."));
        return out;
    }
    out.insert(QStringLiteral("source"),
               QStringLiteral("%1 %2x%3 @ %4 fps")
                   .arg(video->codecName)
                   .arg(video->width)
                   .arg(video->height)
                   .arg(video->fps, 0, 'f', 2));
    out.insert(QStringLiteral("sourceFps"), video->fps);

    const int w = canvas.width() > 0 ? canvas.width() : video->width;
    const int h = canvas.height() > 0 ? canvas.height() : video->height;

    // Step by the source's own frame duration so each iteration decodes a new frame instead of
    // being served the same one out of the reader's cache.
    const double fps = video->fps > 0.0 ? video->fps : 30.0;
    const auto stepUs = static_cast<drift::TimeUs>(drift::kUsPerSecond / fps);
    const drift::TimeUs duration = video->durationUs > 0 ? video->durationUs : info.durationUs;

    ClipReader reader;
    if (!reader.open(file) || !reader.hasVideo()) {
        out.insert(QStringLiteral("error"), trDiag("Could not open that file for decoding."));
        return out;
    }

    auto sweep = [&](auto &&readOne) {
        std::vector<double> samples;
        samples.reserve(kBenchmarkFrames);
        drift::TimeUs at = 0;
        for (int i = 0; i < kBenchmarkFrames + kBenchmarkWarmup; ++i) {
            if (duration > 0 && at >= duration)
                at = 0; // loop rather than run off the end of a short clip
            QElapsedTimer t;
            t.start();
            const bool ok = readOne(at);
            const double ms = double(t.nsecsElapsed()) / 1'000'000.0;
            // The first few pay for opening and seeking the decoder, which is not what
            // steady-state playback costs.
            if (ok && i >= kBenchmarkWarmup)
                samples.push_back(ms);
            at += stepUs;
        }
        return medianOf(samples);
    };

    // Stage 1: decode as the preview does. Hardware frames stay on the GPU here, so this is
    // decode plus at most a GPU-side scale.
    const double previewMs = sweep([&](drift::TimeUs at) {
        PreviewVideoFrame frame;
        return reader.readPreviewVideoFrame(at, frame, w, h);
    });

    // Stage 2: the same decode forced all the way back to CPU pixels. Against stage 1 this is
    // what a frame costs when it cannot stay on the GPU — the readback the CUDA and VAAPI
    // import paths exist to avoid.
    const double rgbaMs = sweep([&](drift::TimeUs at) {
        QImage image;
        return reader.readVideoFrameAt(at, image, w, h);
    });

    out.insert(QStringLiteral("decodeMedianMs"), previewMs);
    out.insert(QStringLiteral("readbackMedianMs"), rgbaMs);
    out.insert(QStringLiteral("readbackCostMs"), qMax(0.0, rgbaMs - previewMs));
    out.insert(QStringLiteral("hardware"), reader.hardwareAccelActive());
    out.insert(QStringLiteral("decoder"), reader.videoDecoderName());
    out.insert(QStringLiteral("uploadPath"), GpuCompositor::previewUploadPathId());

    // Stage 3: the whole preview frame — decode, upload and composite — through the same call
    // playback makes. Minus stage 1, this is what compositing and getting onto the GPU cost.
    if (GpuCompositor::isAvailable()) {
        drift::Project project;
        project.setResolution(w, h);
        project.setFps(int(std::lround(fps)));
        project.tracks().clear();
        project.tracks().append(drift::Track{.type = drift::TrackType::Video});

        drift::Clip clip;
        clip.id = QStringLiteral("__bench__");
        clip.type = drift::ClipType::Video;
        clip.path = file;
        clip.timelineStart = 0;
        clip.timelineDuration = duration > 0 ? duration : drift::kUsPerSecond;
        project.tracks()[0].clips.append(clip);

        FrameCompositor compositor;
        compositor.setProject(&project);
        FrameCompositor::RenderOptions options;

        const double compositeMs = sweep([&](drift::TimeUs at) {
            return compositor.compositeToTextureAt(at, options).isValid();
        });
        out.insert(QStringLiteral("compositeMedianMs"), compositeMs);
        out.insert(QStringLiteral("compositeCostMs"), qMax(0.0, compositeMs - previewMs));
        // The budget this has to fit inside to play without dropping anything.
        out.insert(QStringLiteral("frameBudgetMs"), 1000.0 / fps);
    }
    return out;
}

QVariantMap PlaybackDiagnostics::collect(const PlaybackStats &stats, const drift::Project *project,
                                         double refreshRate)
{
    QVariantMap info;
    QVariantList rows;
    QVariantList hints;

    const int projectFps = project ? qMax(1, project->fps()) : 0;
    const drift::gl::GlStatusInfo gl = GpuCompositor::status();

    if (projectFps > 0)
        rows.append(row(trDiag("Project frame rate"), QStringLiteral("%1 fps").arg(projectFps)));
    rows.append(row(trDiag("Display refresh"),
                    refreshRate > 0.0 ? QStringLiteral("%1 Hz").arg(refreshRate, 0, 'f', 2)
                                      : trDiag("unknown")));
    rows.append(row(trDiag("Renderer"), drift::gl::describeGl(gl)));

    const std::optional<drift::hwaccel::Backend> active = ClipReader::activeDecodeBackend();
    const QString decodeLabel = !active
        ? trDiag("none yet")
        : (*active == drift::hwaccel::Backend::None
               ? trDiag("Software")
               : QString::fromLatin1(drift::hwaccel::name(*active)));
    rows.append(row(trDiag("Active decode"), decodeLabel));
    rows.append(row(trDiag("Hardware fallbacks"),
                    QString::number(ClipReader::hardwareFallbackCount())));

    rows.append(stats.reportRows());
    info.insert(QStringLiteral("rows"), rows);

    // --- hints: each names one of the mechanisms that produce the same symptom ---

    if (gl.isReady() && drift::gl::isSoftwareRenderer(gl.renderer)) {
        hints.append(hint(QStringLiteral("software-renderer"),
                          trDiag("OpenGL is running in software"),
                          trDiag("Every frame is being composited on the CPU. This dominates "
                                 "everything else below; install a GPU driver first.")));
    } else if (!gl.isReady()) {
        hints.append(hint(QStringLiteral("no-compositor"),
                          trDiag("The GPU compositor is not running"),
                          trDiag("Frames are being composited and uploaded the slow way. The "
                                 "debug report says why it did not start.")));
    }

    // Cadence. A project rate that does not divide the refresh rate cannot be shown evenly,
    // however fast the machine is.
    if (projectFps > 0 && refreshRate > 0.0) {
        const double ratio = refreshRate / projectFps;
        const double nearest = std::round(ratio);
        if (ratio > 1.02 && std::abs(ratio - nearest) > 0.02) {
            hints.append(hint(QStringLiteral("cadence-beat"),
                              trDiag("Project frame rate does not divide the display refresh"),
                              trDiag("%1 fps on a %2 Hz display cannot show every frame for the "
                                     "same length of time, so motion judders no matter how fast "
                                     "frames are produced. Matching the project to a divisor of "
                                     "the refresh rate removes it.")
                                  .arg(projectFps)
                                  .arg(refreshRate, 0, 'f', 0)));
        } else if (projectFps > refreshRate + 1.0) {
            hints.append(hint(QStringLiteral("fps-above-refresh"),
                              trDiag("Project frame rate is higher than the display can show"),
                              trDiag("Frames are being composited that the display never gets "
                                     "to present.")));
        }
    }

    // Decode landing on the wrong GPU of a hybrid pair.
    if (active && *active != drift::hwaccel::Backend::None && !gl.vendor.isEmpty()) {
        const bool decodeIsNvidia = *active == drift::hwaccel::Backend::Cuda;
        const bool renderIsNvidia = gl.vendor.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive);
        if (decodeIsNvidia != renderIsNvidia) {
            hints.append(hint(QStringLiteral("decode-gpu-mismatch"),
                              trDiag("Decoding and drawing are happening on different GPUs"),
                              trDiag("Frames decode on one GPU, cross the PCIe bus into system "
                                     "memory, and are uploaded to the other one to be drawn — "
                                     "twice per frame. Picking a decoder that matches the "
                                     "renderer in the preview toolbar avoids the round trip.")));
        }
    }

    // Frames going through system memory when they did not have to.
    if (GpuCompositor::previewUploadPathId() == QStringLiteral("cpu-roundtrip")
        && active && *active != drift::hwaccel::Backend::None) {
        hints.append(hint(QStringLiteral("cpu-roundtrip"),
                          trDiag("Hardware-decoded frames are being copied through system memory"),
                          trDiag("The frame is decoded on the GPU, downloaded, and uploaded "
                                 "again for every frame shown. The debug report's zero-copy row "
                                 "says which step declined.")));
    }

    // The frame rate that doubles decode cost for no visible benefit.
    if (project && projectFps > 0) {
        for (const drift::Track &track : project->tracks()) {
            bool flagged = false;
            for (const drift::Clip &clip : track.clips) {
                if (clip.type != drift::ClipType::Video || clip.path.isEmpty())
                    continue;
                const MediaInfo probed = MediaProbe::probe(clip.path);
                const StreamInfo *video = probed.ok ? firstVideoStream(probed) : nullptr;
                if (video && video->fps > projectFps * 1.5) {
                    hints.append(
                        hint(QStringLiteral("source-fps-above-project"),
                             trDiag("Source frame rate is higher than the project's"),
                             trDiag("%1 is %2 fps in a %3 fps project. Every frame is still "
                                    "decoded and then half of them are discarded, so it costs "
                                    "roughly twice what the same footage at the project rate "
                                    "would — and lowering the preview quality does not reduce "
                                    "it, because that only shrinks scaling and compositing.")
                                 .arg(QFileInfo(clip.path).fileName())
                                 .arg(video->fps, 0, 'f', 0)
                                 .arg(projectFps)));
                    flagged = true;
                    break;
                }
            }
            if (flagged)
                break;
        }
    }

    if (ClipReader::hardwareFallbackCount() > 0) {
        hints.append(hint(QStringLiteral("hw-fallback"),
                          trDiag("Hardware decoding failed and fell back to software"),
                          trDiag("A decoder gave up mid-playback and every clip since has been "
                                 "decoded on the CPU.")));
    }

    info.insert(QStringLiteral("hints"), hints);
    return info;
}

QString PlaybackDiagnostics::formatPlainText(const QVariantMap &info)
{
    QString out;
    out += QStringLiteral("# Drift playback diagnostics\n\n");

    const QVariantList rows = info.value(QStringLiteral("rows")).toList();
    if (!rows.isEmpty()) {
        out += QStringLiteral("## Playback\n\n");
        for (const QVariant &v : rows) {
            const QVariantMap m = v.toMap();
            out += QStringLiteral("- %1: %2\n")
                       .arg(m.value(QStringLiteral("label")).toString(),
                            m.value(QStringLiteral("value")).toString());
        }
        out += QLatin1Char('\n');
    }

    const auto appendBenchmark = [&out](const QString &title, const QVariantMap &b) {
        if (b.isEmpty())
            return;
        out += QStringLiteral("## %1\n\n").arg(title);
        if (b.contains(QStringLiteral("error"))) {
            out += QStringLiteral("- %1\n\n").arg(b.value(QStringLiteral("error")).toString());
            return;
        }
        out += QStringLiteral("- Source: %1\n").arg(b.value(QStringLiteral("source")).toString());
        out += QStringLiteral("- Decoder: %1 (%2)\n")
                   .arg(b.value(QStringLiteral("decoder")).toString(),
                        b.value(QStringLiteral("hardware")).toBool() ? QStringLiteral("hardware")
                                                                     : QStringLiteral("software"));
        out += QStringLiteral("- Preview upload: %1\n")
                   .arg(b.value(QStringLiteral("uploadPath")).toString());
        out += QStringLiteral("- Decode: %1\n")
                   .arg(msText(b.value(QStringLiteral("decodeMedianMs")).toDouble()));
        out += QStringLiteral("- Decode + readback to CPU: %1 (readback costs %2)\n")
                   .arg(msText(b.value(QStringLiteral("readbackMedianMs")).toDouble()),
                        msText(b.value(QStringLiteral("readbackCostMs")).toDouble()));
        if (b.contains(QStringLiteral("compositeMedianMs"))) {
            out += QStringLiteral("- Full composite: %1 (compositing costs %2)\n")
                       .arg(msText(b.value(QStringLiteral("compositeMedianMs")).toDouble()),
                            msText(b.value(QStringLiteral("compositeCostMs")).toDouble()));
            out += QStringLiteral("- Frame budget at this rate: %1\n")
                       .arg(msText(b.value(QStringLiteral("frameBudgetMs")).toDouble()));
        }
        out += QLatin1Char('\n');
    };

    appendBenchmark(QStringLiteral("Reference clip (1080p60)"),
                    info.value(QStringLiteral("reference")).toMap());
    appendBenchmark(QStringLiteral("Timeline clip"),
                    info.value(QStringLiteral("timeline")).toMap());

    const QVariantList hints = info.value(QStringLiteral("hints")).toList();
    if (!hints.isEmpty()) {
        out += QStringLiteral("## Findings\n\n");
        for (const QVariant &v : hints) {
            const QVariantMap m = v.toMap();
            out += QStringLiteral("- **%1** — %2\n")
                       .arg(m.value(QStringLiteral("title")).toString(),
                            m.value(QStringLiteral("detail")).toString());
        }
    }
    return out;
}
