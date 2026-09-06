#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <memory>

namespace drift {

struct RvmResult
{
    QImage alpha;      // Grayscale8 at frame size, soft 0..255 — deliberately not thresholded
    QImage foreground; // RGB888 at frame size: the colour-decontaminated foreground
    bool ok = false;
    QString error;
};

// Robust Video Matting on ONNX Runtime, using the official export from PeterL1n/RobustVideoMatting.
//
// The counterpart to Sam2Segmenter rather than a replacement for it. RVM takes no prompt and knows
// only one subject — people — so it cannot be told which object to cut out. In exchange it needs no
// interaction at all, runs an order of magnitude faster, and produces soft alpha plus a
// colour-decontaminated foreground, which is what makes hair survive the cutout.
//
// Tracking is the recurrent state's job: the network carries four hidden tensors from frame to
// frame, so frames must be fed in order and a Track cannot be rewound.
//
// All work is synchronous on the calling thread; callers run it off the GUI thread.
class RvmMatter
{
    struct Impl;

public:
    static RvmMatter &instance();

    // Cheap file-existence checks that construct no ONNX session. This is what UI gating must use.
    static bool modelPresent();
    // Variant tokens of every installed model root, best first: {"mobilenetv3", "resnet50"}.
    static QStringList installedVariants();

    // Loads the session for `variant` (empty picks the default) on first use. False if the model is
    // missing or failed to load; see lastError(). Blocks — never call this from the GUI thread.
    bool available(const QString &variant = {});
    QString lastError() const;

    // A pass over one clip. The recurrent state lives in here, so frames must be fed in order and
    // one Track serves exactly one pass. Not thread-safe.
    class Track
    {
    public:
        ~Track();

        // Every frame, in order. The first call establishes the state from zeros.
        RvmResult step(const QImage &frame);

        QString variant() const;

        Track(const Track &) = delete;
        Track &operator=(const Track &) = delete;

    private:
        friend class RvmMatter;
        struct State;
        explicit Track(std::unique_ptr<State> state);

        std::unique_ptr<State> s;
    };

    // A tracker bound to a loaded session, or nullptr when the model is unavailable.
    std::unique_ptr<Track> newTrack(const QString &variant = {});

    RvmMatter(const RvmMatter &) = delete;
    RvmMatter &operator=(const RvmMatter &) = delete;

private:
    RvmMatter();
    ~RvmMatter();

    std::unique_ptr<Impl> d;
};

} // namespace drift
