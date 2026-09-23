#include "Transition.h"

#include "Track.h"

namespace drift {

TransitionAudioGains transitionAudioGains(const QString &curve, double progress)
{
    const double p = qBound(0.0, progress, 1.0);
    TransitionAudioGains gains;

    if (curve == QLatin1String("hold")) {
        gains.outgoing = 1.0;
        gains.incoming = 1.0;
    } else if (curve == QLatin1String("dip")) {
        // Fully out by the midpoint, then in — matches the visual dip through black/white.
        gains.outgoing = p < 0.5 ? 1.0 - p * 2.0 : 0.0;
        gains.incoming = p >= 0.5 ? (p - 0.5) * 2.0 : 0.0;
    } else { // "crossfade"
        gains.outgoing = 1.0 - p;
        gains.incoming = p;
    }
    return gains;
}

TimeUs clipHandleBeforeUs(const Clip &clip, TimeUs mediaDurationUs)
{
    if (clip.hasSpeedCurve())
        return 0;
    const TimeUs source = clip.reverse ? qMax<TimeUs>(0, mediaDurationUs - clip.srcOut) : qMax<TimeUs>(0, clip.srcIn);
    return static_cast<TimeUs>(static_cast<double>(source) / clip.effectiveSpeed());
}

TimeUs clipHandleAfterUs(const Clip &clip, TimeUs mediaDurationUs)
{
    if (clip.hasSpeedCurve())
        return 0;
    const TimeUs source = clip.reverse ? qMax<TimeUs>(0, clip.srcIn) : qMax<TimeUs>(0, mediaDurationUs - clip.srcOut);
    return static_cast<TimeUs>(static_cast<double>(source) / clip.effectiveSpeed());
}

AudioTransitionEdge audioTransitionEdgeFor(const Track &track, const Clip &clip, TimeUs mediaDurationUs)
{
    AudioTransitionEdge edge;
    for (const Transition &transition : track.transitions) {
        const bool isFrom = transition.fromClipId == clip.id;
        const bool isTo = transition.toClipId == clip.id;
        if (!isFrom && !isTo)
            continue;
        const Clip *from = clipById(track, transition.fromClipId);
        const Clip *to = clipById(track, transition.toClipId);
        if (!from || !to || clipsPhysicallyOverlap(*from, *to))
            continue;
        TimeUs ws = 0;
        TimeUs we = 0;
        if (!transitionWindow(track, transition, ws, we))
            continue;
        if (isFrom) {
            edge.ownsOut = true;
            const TimeUs need = qMax<TimeUs>(0, we - clip.timelineEnd());
            if (need > 0 && clipHandleAfterUs(clip, mediaDurationUs) >= need) {
                edge.extendAfterUs = need;
                edge.outHasHandle = true;
            }
        }
        if (isTo) {
            edge.ownsIn = true;
            const TimeUs need = qMax<TimeUs>(0, clip.timelineStart - ws);
            if (need > 0 && clipHandleBeforeUs(clip, mediaDurationUs) >= need) {
                edge.extendBeforeUs = need;
                edge.inHasHandle = true;
            }
        }
    }
    return edge;
}

QString effectiveAudioCurve(const QString &curve, bool hasHandle)
{
    // A crossfade needs both sounds across the whole window; without the media for it, fade
    // through silence rather than cut a half-level sound off at the edit.
    if (!hasHandle && (curve.isEmpty() || curve == QLatin1String("crossfade")))
        return QStringLiteral("dip");
    return curve.isEmpty() ? QStringLiteral("crossfade") : curve;
}

const Clip *clipById(const Track &track, const QString &clipId)
{
    for (const Clip &clip : track.clips) {
        if (clip.id == clipId)
            return &clip;
    }
    return nullptr;
}

bool clipsEligibleForTransition(const Clip &fromClip, const Clip &toClip)
{
    // Partner must start at/after the outgoing clip and abut or overlap it.
    if (toClip.timelineStart < fromClip.timelineStart)
        return false;
    const TimeUs gap = toClip.timelineStart - fromClip.timelineEnd();
    return gap <= secondsToUs(0.001);
}

bool clipsPhysicallyOverlap(const Clip &fromClip, const Clip &toClip)
{
    return toClip.timelineStart < fromClip.timelineEnd() && toClip.timelineStart >= fromClip.timelineStart;
}

TimeUs physicalOverlapDurationUs(const Clip &fromClip, const Clip &toClip)
{
    if (!clipsPhysicallyOverlap(fromClip, toClip))
        return 0;
    return fromClip.timelineEnd() - toClip.timelineStart;
}

TimeUs transitionCenterUs(const Track &track, const Transition &transition)
{
    const Clip *fromClip = clipById(track, transition.fromClipId);
    const Clip *toClip = clipById(track, transition.toClipId);
    if (!fromClip)
        return 0;
    if (toClip && clipsPhysicallyOverlap(*fromClip, *toClip))
        return toClip->timelineStart + physicalOverlapDurationUs(*fromClip, *toClip) / 2;
    return fromClip->timelineEnd();
}

bool transitionWindow(const Track &track, const Transition &transition, TimeUs &startUs, TimeUs &endUs)
{
    const Clip *fromClip = clipById(track, transition.fromClipId);
    const Clip *toClip = clipById(track, transition.toClipId);
    if (!fromClip || !toClip)
        return false;

    // Physical overlap on the timeline is the transition window.
    if (clipsPhysicallyOverlap(*fromClip, *toClip)) {
        startUs = toClip->timelineStart;
        endUs = fromClip->timelineEnd();
        return endUs > startUs;
    }

    if (transition.durationUs <= 0)
        return false;

    // Adjacent clips: virtual window centered on the cut, never outside the two clips
    // (a stale durationUs from a former overlap must not start before t=0 or outrun them).
    const TimeUs center = fromClip->timelineEnd();
    const TimeUs half = transition.durationUs / 2;
    startUs = qMax(fromClip->timelineStart, center - half);
    endUs = qMin(toClip->timelineEnd(), center + half);
    return endUs > startUs;
}

double transitionProgress(const Transition &transition, TimeUs timelineUs, TimeUs windowStartUs,
                          TimeUs windowEndUs)
{
    const double linear = transitionProgress(timelineUs, windowStartUs, windowEndUs);
    return shapedProgress(linear, transition.easingCurve, transition.easingShape);
}

double transitionProgress(TimeUs timelineUs, TimeUs windowStartUs, TimeUs windowEndUs)
{
    if (windowEndUs <= windowStartUs)
        return 0.0;
    const double p = static_cast<double>(timelineUs - windowStartUs)
                     / static_cast<double>(windowEndUs - windowStartUs);
    return qBound(0.0, p, 1.0);
}

const Transition *activeTransitionAt(const Track &track, TimeUs timelineUs, TimeUs &windowStartUs,
                                     TimeUs &windowEndUs)
{
    for (const Transition &transition : track.transitions) {
        TimeUs startUs = 0;
        TimeUs endUs = 0;
        if (!transitionWindow(track, transition, startUs, endUs))
            continue;
        if (timelineUs >= startUs && timelineUs < endUs) {
            windowStartUs = startUs;
            windowEndUs = endUs;
            return &transition;
        }
    }
    return nullptr;
}

} // namespace drift
