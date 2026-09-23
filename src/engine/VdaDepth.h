#pragma once

#include "core/Time.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace drift {

// Video Depth Anything's windowing, lifted out of the model so it can be tested without one.
//
// The network sees 32 frames at a time. Every window after the first opens with ten frames carried
// over from the one before (kVdaKeyframes: the clip's very first frame, one from the middle, and
// the last eight), then continues with 22 new ones. Its output is relative, so each window is
// fitted onto the previous one by a least-squares scale and shift over the first two carried
// frames, and the next eight — which both windows estimated — are cross-faded. This is upstream's
// infer_video_depth restated as a stream: frames come out as soon as no later window can change
// them, instead of after the whole clip.
constexpr int kVdaWindow = 32;
constexpr int kVdaOverlap = 10;
constexpr int kVdaInterp = 8;
constexpr int kVdaStride = kVdaWindow - kVdaOverlap;
constexpr int kVdaKeyframes[kVdaOverlap] = {0, 12, 24, 25, 26, 27, 28, 29, 30, 31};

class VdaStitcher
{
public:
    // frameIndex counts from 0 across the whole stream; `disparity` is one frame.
    using Emit = std::function<void(int frameIndex, const float *disparity)>;

    explicit VdaStitcher(size_t pixelsPerFrame);

    // One window's raw network output: kVdaWindow frames back to back.
    void addWindow(const float *raw, const Emit &out);
    // Emits whatever the last window left pending.
    void finish(const Emit &out);

    // The fit applied to the most recent window, for tests.
    double lastScale() const { return m_scale; }
    double lastShift() const { return m_shift; }

private:
    size_t m_pixels;
    bool m_first = true;
    int m_emitted = 0;
    std::deque<std::vector<float>> m_tail; // aligned, not yet emitted
    std::vector<float> m_refFirst;         // aligned depth of the clip's first frame
    std::vector<float> m_refMiddle;        // aligned depth at keyframe 12 of the latest window
    double m_scale = 1.0;
    double m_shift = 0.0;

    void emitSettled(size_t keep, const Emit &out);
};

// Video Depth Anything (Small) on ONNX Runtime, as exported by drift-addons' fetch-vda.py: a
// per-frame DINOv2 encoder and a temporal head run once per window.
//
// Output is relative inverse depth — larger is nearer, no units — at inference resolution, which
// is the frame resized (never cropped or padded) so its short side is the requested size and both
// sides are multiples of 14. Resize-only is what lets the map be laid back over the frame 1:1.
//
// All work is synchronous on the calling thread; callers run it off the GUI thread.
class VdaDepth
{
    struct Impl;

public:
    static VdaDepth &instance();

    // Cheap file-existence check that constructs no ONNX session. This is what UI gating must use.
    static bool modelPresent();

    // Loads the sessions on first use. False if the model is missing or failed to load; see
    // lastError(). Blocks — never call this from the GUI thread.
    bool available();
    QString lastError() const;
    // Which exported model is loaded ("vda-small"), for cache keys. Empty before available().
    QString variant() const;

    static QSize inferenceSize(const QSize &frame, int shortSide);

    // One pass over one clip. Not thread-safe.
    class Pass
    {
    public:
        // Called in frame order with each finished frame; return false to stop the pass.
        using Emit = std::function<bool(TimeUs ptsUs, const float *disparity)>;

        ~Pass();

        // Every frame in order. All frames must be the same size as the first.
        bool push(const QImage &frame, TimeUs ptsUs);
        // Runs the final, padded window and emits everything still pending.
        bool flush();

        QSize size() const;
        QString error() const;

        Pass(const Pass &) = delete;
        Pass &operator=(const Pass &) = delete;

    private:
        friend class VdaDepth;
        struct State;
        explicit Pass(std::unique_ptr<State> state);
        std::unique_ptr<State> s;
    };

    // A pass bound to the loaded sessions, or nullptr when the model is unavailable.
    std::unique_ptr<Pass> newPass(int shortSide, Pass::Emit onFrame);

    VdaDepth(const VdaDepth &) = delete;
    VdaDepth &operator=(const VdaDepth &) = delete;

private:
    VdaDepth();
    ~VdaDepth();

    std::unique_ptr<Impl> d;
};

} // namespace drift
