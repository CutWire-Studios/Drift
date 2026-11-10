#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

namespace drift {

class BakeFile;
class Project;

// Renders a frame range through Exporter's packet sink (software x264, fixed closed GOPs, no
// B-frames, live compositing) and stores the GOPs in a BakeFile.
class BakeWriter
{
public:
    // Fraction in [0,1]; return false to cancel.
    using ProgressFn = std::function<bool(double)>;

    struct Result
    {
        bool ok = false;
        QString error;
        QString bakeId; // empty when no frame was baked
        qint64 startFrame = 0;
        qint64 endFrame = 0; // half-open; what was actually baked, which can be short of the request
        bool cancelled = false;
    };

    static constexpr int kPresetCount = 3; // 0 compact, 1 balanced, 2 high
    static int gopSize(int preset);

    // Bakes project frames [startFrame, endFrame) at the project's size and integer fps; frame i of
    // the bake is timeline frame startFrame + i. `project` must not change while this runs (pass a
    // detached copy). Runs on the calling thread. Everything is published through commit() about
    // every two seconds of frames and once more at the end, a cancelled or failed run keeps the
    // frames baked so far. Old bakes are only replaced over frames that were actually written, so a
    // run that fails before its first GOP leaves them alone.
    static Result bake(BakeFile &file, const Project &project, qint64 startFrame, qint64 endFrame,
                       int preset, const QByteArray &fingerprint, const ProgressFn &progress = {});
};

} // namespace drift
