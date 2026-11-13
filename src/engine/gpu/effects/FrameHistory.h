#pragma once

#include "core/base/Time.h"
#include "engine/gpu/GlRuntime.h"

#include <QMap>
#include <QRectF>
#include <QSize>
#include <QString>

#include <map>
#include <utility>

// What a clip remembers between composites, for effects that look back in time and for
// compositor passes that want the previous source frame:
//   - a ring of the clip's last source frames, downscaled into one atlas texture when they would
//     not fit the memory budget at full size, and
//   - per effect, that effect's own output from the previous frame.
//
// Everything is keyed by (scope, key). The scope is one FrameCompositor (and preview stream), so
// the preview, an export and a thumbnail pass never share a history; the key is the clip
// instance. A key is stepped once per composite from the clip's time in project frames:
//   same frame again  -> the frame recorded last time is replaced (a paused edit re-renders it),
//   the next frame    -> appended (one frame later, or two when playback dropped one),
//   anything else     -> forgotten, so a seek, a scrub or a loop starts the history over.
// Export composes frame after frame in order, so what an export remembers is exactly the frames
// before it, whatever the preview did meanwhile.
//
// GL thread only.
namespace drift::gl {

// A recorded source frame inside the history atlas. `uv` is the frame's tile in the atlas's own
// texture space, which samples like the source: (0, 0) of the frame is its top-left. The tile can
// be smaller than the source when the budget forced a downscale; `size` is its pixels.
struct PastFrame
{
    GLuint texture = 0;
    QSize atlasSize;
    QSize size;
    QRectF uv;
    bool premultiplied = false;

    bool isValid() const { return texture != 0; }
};

struct FrameHistoryStore
{
    struct SourceRing
    {
        GlTarget atlas;
        QSize frame; // the source size the ring was planned for
        QSize tile;
        int columns = 1;
        int ringSize = 0;
        int head = -1;  // slot of the newest recorded frame (the current one)
        int count = 0;  // recorded frames, the current one included
        bool premultiplied = false;
        qint64 bytes = 0;
    };

    // Two textures so a repeat of the same frame can drop the newest and still read the one before.
    struct OutputRing
    {
        GlTarget texture[2];
        QString effectId;
        int newest = 0;
        int count = 0;
        qint64 bytes = 0;
    };

    struct Entry
    {
        SourceRing source;
        std::map<int, OutputRing> outputs;
        qint64 lastFrame = 0;
        bool hasFrame = false;
        quint64 lastUse = 0;
    };

    using Key = std::pair<quint64, quint64>;
    std::map<Key, Entry> entries;
    quint64 serial = 0;
    int depth = 0;

    qint64 totalBytes() const;
    void evictOthers(const Entry *keep, qint64 needed);
};

// The store, created on first use.
FrameHistoryStore &frameHistory(GlRuntime &rt);

// Drops every recorded frame. Context current.
void releaseFrameHistory(GlRuntime &rt);

// Brackets one top-level composite of `scope`: when it ends, the scope's histories nothing touched
// were of clips that left the frame, so they are dropped. Nested composites (a composite clip's
// timeline) re-enter without sweeping.
class HistoryComposite
{
public:
    HistoryComposite(GlRuntime &rt, quint64 scope);
    ~HistoryComposite();
    HistoryComposite(const HistoryComposite &) = delete;
    HistoryComposite &operator=(const HistoryComposite &) = delete;

private:
    FrameHistoryStore &m_store;
    quint64 m_scope;
};

// One clip's history, stepped to the frame being rendered.
class ClipHistory
{
public:
    ClipHistory() = default;

    // Steps the history keyed by (scope, key) to the frame at `clipTimeUs` and records `source`
    // into it when `sourceFrames` > 0. `outputEffects` maps an effect's index in the layer chain to
    // its catalog id, for each effect that reads its own previous output. Does nothing (the
    // history stays inactive) when `key` is 0, nothing is asked for, or the budget refuses.
    ClipHistory(GlRuntime &rt, QOpenGLExtraFunctions *gl, quint64 scope, quint64 key,
                drift::TimeUs clipTimeUs, int fps, const GlTarget &source, int sourceFrames,
                const QMap<int, QString> &outputEffects);

    // The recorded frames before the current one, as "history" packages sample them. count 0
    // when there are none yet.
    HistoryBinding sourceBinding() const;

    // The effect's output one frame ago, or 0 for none.
    GLuint previousOutput(int effectIndex) const;

    // Remembers `out` as the effect's output for this frame.
    void recordOutput(GlRuntime &rt, QOpenGLExtraFunctions *gl, int effectIndex, const GlTarget &out);

private:
    FrameHistoryStore::Entry *m_entry = nullptr;
    FrameHistoryStore *m_store = nullptr;
};

// The clip's source frame `age` project frames before the one being rendered (1 = the previous
// frame), or an invalid one while fewer are recorded: the first frame, after a seek, or when the
// layer did not ask for that many (GpuLayer::sourceHistoryFrames). Valid once buildLayerTarget has
// run for the layer this composite.
PastFrame pastSourceFrame(GlRuntime &rt, quint64 scope, quint64 key, int age = 1);

} // namespace drift::gl
