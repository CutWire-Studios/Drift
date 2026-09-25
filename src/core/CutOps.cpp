#include "CutOps.h"

#include "Project.h"
#include "TimelineOps.h"

#include <QSet>

#include <algorithm>
#include <limits>

namespace drift {

QList<TimeRangeUs> normalizedRanges(QList<TimeRangeUs> ranges)
{
    std::sort(ranges.begin(), ranges.end(),
              [](const TimeRangeUs &a, const TimeRangeUs &b) { return a.startUs < b.startUs; });
    QList<TimeRangeUs> out;
    for (const TimeRangeUs &r : std::as_const(ranges)) {
        if (r.endUs <= r.startUs)
            continue;
        if (!out.isEmpty() && r.startUs <= out.last().endUs)
            out.last().endUs = qMax(out.last().endUs, r.endUs);
        else
            out.append(r);
    }
    return out;
}

QList<TimeRangeUs> keptTimelineIntervals(const Clip &clip, const QList<TimeRangeUs> &removed,
                                         TimeUs minKeepUs)
{
    QList<TimeRangeUs> clipped;
    for (const TimeRangeUs &r : removed) {
        const TimeRangeUs c{qMax(r.startUs, clip.timelineStart), qMin(r.endUs, clip.timelineEnd())};
        if (c.endUs > c.startUs)
            clipped.append(c);
    }
    QList<TimeRangeUs> kept;
    TimeUs cursor = clip.timelineStart;
    for (const TimeRangeUs &r : normalizedRanges(clipped)) {
        if (r.startUs - cursor >= qMax<TimeUs>(1, minKeepUs))
            kept.append({cursor, r.startUs});
        cursor = qMax(cursor, r.endUs);
    }
    if (clip.timelineEnd() - cursor >= qMax<TimeUs>(1, minKeepUs))
        kept.append({cursor, clip.timelineEnd()});
    return kept;
}

bool sliceClipExact(const Clip &src, TimeUs from, TimeUs to, Clip &out)
{
    from = qMax(from, src.timelineStart);
    to = qMin(to, src.timelineEnd());
    if (to - from < kCutMinEdgeUs)
        return false;
    Clip work = src;
    if (from > work.timelineStart) {
        Clip tail;
        if (!splitClipAtOffsetMin(work, tail, from - work.timelineStart, kCutMinEdgeUs))
            return false;
        work = tail;
    }
    if (work.timelineEnd() > to) {
        Clip discarded;
        if (!splitClipAtOffsetMin(work, discarded, to - work.timelineStart, kCutMinEdgeUs))
            return false;
    }
    out = work;
    return true;
}

namespace {

// Outer edges of the whole group behave like a trim: they keep the clip's fades. An edge the cut
// made gets the de-click ramp instead of a visual fade.
void applyEdgeFades(QList<Clip> &segments, const Clip &src, TimeUs declickUs, bool startCut, bool endCut)
{
    for (int i = 0; i < segments.size(); ++i) {
        Clip &seg = segments[i];
        const bool first = i == 0;
        const bool last = i == segments.size() - 1;
        seg.fadeInUs = first ? qMin(src.fadeInUs, seg.timelineDuration) : 0;
        seg.animIn = first ? src.animIn : ClipAnimation{};
        seg.fadeOutUs = last ? qMin(src.fadeOutUs, seg.timelineDuration) : 0;
        seg.animOut = last ? src.animOut : ClipAnimation{};
        seg.audioFadeInUs = first && !startCut ? src.audioFadeInUs : qMax(declickUs, first ? src.audioFadeInUs : 0);
        seg.audioFadeOutUs = last && !endCut ? src.audioFadeOutUs : qMax(declickUs, last ? src.audioFadeOutUs : 0);
    }
}

} // namespace

QList<Clip> packedSegments(const Clip &src, const QList<TimeRangeUs> &kept, TimeUs declickUs)
{
    QList<Clip> out;
    TimeUs cursor = src.timelineStart;
    for (const TimeRangeUs &range : kept) {
        Clip seg;
        if (!sliceClipExact(src, range.startUs, range.endUs, seg))
            continue;
        seg.timelineStart = cursor;
        cursor += seg.timelineDuration;
        out.append(seg);
    }
    if (out.isEmpty())
        return out;
    const bool startCut = kept.first().startUs > src.timelineStart;
    const bool endCut = kept.last().endUs < src.timelineEnd();
    applyEdgeFades(out, src, declickUs, startCut, endCut);
    return out;
}

QList<Clip> segmentsFromSourceRanges(const Clip &src, QList<TimeRangeUs> sourceRanges,
                                     TimeUs mediaDurationUs, TimeUs padUs, TimeUs declickUs,
                                     QString *error)
{
    if (src.hasSpeedCurve()) {
        if (error)
            *error = QStringLiteral("clip has a speed ramp; flatten it first");
        return {};
    }
    const TimeUs maxUs = mediaDurationUs > 0 ? mediaDurationUs : std::numeric_limits<TimeUs>::max();
    QList<TimeRangeUs> ranges;
    for (TimeRangeUs r : std::as_const(sourceRanges)) {
        r.startUs = qBound<TimeUs>(0, r.startUs - padUs, maxUs);
        r.endUs = qBound<TimeUs>(0, r.endUs + padUs, maxUs);
        if (r.length() < kCutMinEdgeUs)
            continue;
        // Consecutive ranges that touch play as one: no cut, so no de-click either.
        if (!ranges.isEmpty() && r.startUs <= ranges.last().endUs && r.startUs >= ranges.last().startUs)
            ranges.last().endUs = qMax(ranges.last().endUs, r.endUs);
        else
            ranges.append(r);
    }
    if (ranges.isEmpty()) {
        if (error)
            *error = QStringLiteral("no usable ranges");
        return {};
    }

    const double speed = src.effectiveSpeed();
    QList<Clip> out;
    TimeUs cursor = src.timelineStart;
    for (const TimeRangeUs &r : std::as_const(ranges)) {
        Clip seg = src;
        seg.srcIn = r.startUs;
        seg.srcOut = r.endUs;
        seg.timelineStart = cursor;
        seg.timelineDuration = qMax<TimeUs>(1, static_cast<TimeUs>(llround(r.length() / speed)));
        // Where this range's first played frame sat on src's own clock, so its curves come along.
        const TimeUs playStartSource = src.reverse ? r.endUs : r.startUs;
        const TimeUs localOffset = static_cast<TimeUs>(llround(
            (src.reverse ? src.srcOut - playStartSource : playStartSource - src.srcIn) / speed));
        shiftClipKeyframes(seg, -localOffset);
        cursor += seg.timelineDuration;
        out.append(seg);
    }
    const bool startCut = src.reverse ? ranges.first().endUs != src.srcOut : ranges.first().startUs != src.srcIn;
    const bool endCut = src.reverse ? ranges.last().startUs != src.srcIn : ranges.last().endUs != src.srcOut;
    applyEdgeFades(out, src, declickUs, startCut, endCut);
    return out;
}

bool sourceRangeToTimeline(const Clip &clip, TimeRangeUs source, TimeRangeUs &timelineOut)
{
    const TimeUs from = qMax(source.startUs, clip.srcIn);
    const TimeUs to = qMin(source.endUs, clip.srcOut);
    if (to <= from)
        return false;
    const TimeUs a = clip.timelineStart + clip.sourceUsToClipLocalUs(from);
    const TimeUs b = clip.timelineStart + clip.sourceUsToClipLocalUs(to);
    timelineOut = {qMin(a, b), qMax(a, b)};
    return timelineOut.endUs > timelineOut.startUs;
}

QList<TimelineWord> transcriptWordsOnTimeline(const Project &project, TimeUs viewStart, TimeUs viewEnd,
                                              const QString &onlyClipId)
{
    QList<TimelineWord> out;
    QSet<QString> seen;
    for (const Track &track : project.tracks()) {
        if (onlyClipId.isEmpty() && track.muted)
            continue;
        for (const Clip &clip : track.clips) {
            if (!onlyClipId.isEmpty() && clip.id != onlyClipId)
                continue;
            if (clip.assetId.isEmpty() || clip.timelineEnd() <= viewStart || clip.timelineStart >= viewEnd)
                continue;
            if (onlyClipId.isEmpty() && clip.suppressEmbeddedAudio && track.type != TrackType::Audio)
                continue;
            const TranscriptPtr t = project.transcript(clip.assetId);
            if (!t)
                continue;
            // A linked pair is one sound; read it once.
            const QString key = clip.assetId + QLatin1Char('@') + QString::number(clip.timelineStart)
                                + QLatin1Char(':') + QString::number(clip.srcIn);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            for (const int i : t->wordsInRange(clip.srcIn, clip.srcOut)) {
                const TranscriptWord &w = t->words.at(i);
                TimeRangeUs tl;
                if (!sourceRangeToTimeline(clip, {w.startUs, qMax(w.endUs, w.startUs + 1)}, tl))
                    continue;
                if (tl.endUs <= viewStart || tl.startUs >= viewEnd)
                    continue;
                TimelineWord tw{w, i, clip.assetId, clip.id};
                tw.word.startUs = tl.startUs;
                tw.word.endUs = tl.endUs;
                out.append(tw);
            }
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const TimelineWord &a, const TimelineWord &b) { return a.word.startUs < b.word.startUs; });
    return out;
}

} // namespace drift
