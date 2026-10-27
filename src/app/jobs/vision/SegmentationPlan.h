#pragma once

#include "core/base/Time.h"

#include <QList>
#include <QString>
#include <QtGlobal>

// Pure planning for an object-mask propagation: which matte frame comes from where, and in what
// order the reverse pass decodes. Kept apart from the job so it can be tested without a model.
namespace drift::segplan {

// The matte is indexed by source time at srcIn + i * step, the same grid segmentClip has always
// used, so frame i of the file is source time srcIn + i * step.
inline int frameCount(TimeUs srcIn, TimeUs srcOut, TimeUs step)
{
    return step > 0 && srcOut > srcIn ? int((srcOut - srcIn + step - 1) / step) : 0;
}

// The frame a prompt at `sourceUs` lands on, snapped to the grid and clamped into the clip.
inline int frameIndexAt(TimeUs srcIn, TimeUs step, int total, TimeUs sourceUs)
{
    if (total <= 0 || step <= 0)
        return 0;
    const TimeUs rounded = (sourceUs - srcIn + step / 2) / step;
    return int(qBound<TimeUs>(0, sourceUs < srcIn ? 0 : rounded, TimeUs(total - 1)));
}

struct Range
{
    int first = 0; // inclusive
    int last = -1; // inclusive
    bool isEmpty() const { return last < first; }
    int size() const { return isEmpty() ? 0 : last - first + 1; }
    bool contains(int i) const { return i >= first && i <= last; }
};

// Frames the SAM2 passes cover. "forward" is the prompt frame to the end, "backward" the start to
// the prompt frame, anything else both.
inline Range propagationRange(int total, int promptIndex, const QString &direction)
{
    if (direction == QLatin1String("forward"))
        return {promptIndex, total - 1};
    if (direction == QLatin1String("backward"))
        return {0, promptIndex};
    return {0, total - 1};
}

enum class FrameSource {
    Pad, // black: nothing known about this frame
    Old, // copied from the matte being refined
    New, // produced by the propagation
};

// What each of the `total` matte frames is made of. The file always spans the whole clip: the
// compositor clamps a media time before the first frame to the first frame and holds the last one
// after the end, so a shorter matte would paint stale coverage outside its range.
inline QList<FrameSource> matteLayout(int total, const Range &processed, bool haveOld)
{
    QList<FrameSource> out;
    out.reserve(total);
    for (int i = 0; i < total; ++i) {
        out.append(processed.contains(i) ? FrameSource::New
                                         : (haveOld ? FrameSource::Old : FrameSource::Pad));
    }
    return out;
}

struct Chunk
{
    int first = 0;
    int last = 0;
};

// The reverse pass cannot decode backwards, so it walks [first, last] in chunks from the top: each
// chunk is decoded forwards, then processed back to front. Chunks come out in processing order.
inline QList<Chunk> reverseChunks(int first, int last, int chunkSize)
{
    QList<Chunk> out;
    const int size = qMax(1, chunkSize);
    for (int top = last; top >= first; top -= size)
        out.append({qMax(first, top - size + 1), top});
    return out;
}

// Frame indices in the order they are decoded (each chunk ascending, chunks from the top down).
inline QList<int> reverseDecodeOrder(int first, int last, int chunkSize)
{
    QList<int> out;
    for (const Chunk &chunk : reverseChunks(first, last, chunkSize)) {
        for (int i = chunk.first; i <= chunk.last; ++i)
            out.append(i);
    }
    return out;
}

} // namespace drift::segplan
