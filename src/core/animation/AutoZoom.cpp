#include "core/animation/AutoZoom.h"

#include <algorithm>

namespace drift {

namespace {

// Where the clip's anchor has to sit for the frame point `click` (normalized) to land on
// `restPosition` at scale `scale`.
//
// The frame is placed by its anchor and scaled about it, so a frame point c (in frame pixels)
// lands at position + scale * (c - anchor). With the anchor at its default — the frame centre —
// putting c on `restPosition` means position = restPosition + scale * (centre - c). At scale 1
// that reduces to a plain pan, which is why the rest keyframes use restPosition unchanged.
QPointF positionForClick(const QPointF &click, double scale, const AutoZoomOptions &options)
{
    const QPointF centre(options.frame.width() / 2.0, options.frame.height() / 2.0);
    const QPointF point(click.x() * options.frame.width(), click.y() * options.frame.height());
    return options.restPosition + (centre - point) * scale;
}

// A key that eases out of and into its neighbours. Written as a function easing rather than
// tangents so the graph editor shows it as "Ease", which is what the user would have picked.
Keyframe<double> easedKey(double value)
{
    Keyframe<double> key;
    key.value = value;
    key.fn = EasingFn::None;
    // Symmetric handles, flat at the key: the push settles instead of arriving at full speed.
    key.corner = false;
    return key;
}

void putEased(KeyframeTrack<double> *track, TimeUs time, double value)
{
    track->setKeyframe(time, easedKey(value));
}

} // namespace

AutoZoomTracks autoZoomFromClicks(const QList<InputEvent> &clicks, TimeUs clipDurationUs,
                                  const AutoZoomOptions &options)
{
    AutoZoomTracks tracks;
    if (clipDurationUs <= 0 || options.frame.isEmpty())
        return tracks;
    const double zoom = std::max(1.0, options.zoom);
    if (qFuzzyCompare(zoom, 1.0))
        return tracks; // nothing to animate; writing flat keys would only clutter the graph
    const TimeUs ramp = std::max<TimeUs>(1, options.rampUs);
    const TimeUs hold = std::max<TimeUs>(0, options.holdUs);
    const TimeUs groupWithin = std::max<TimeUs>(0, options.groupWithinUs);

    // In range and in order. The caller may hand over a whole recording's log for a clip that
    // covers part of it.
    QList<InputEvent> usable;
    for (const InputEvent &click : clicks) {
        if (click.timeUs >= 0 && click.timeUs < clipDurationUs)
            usable.append(click);
    }
    if (usable.isEmpty())
        return tracks;
    std::stable_sort(usable.begin(), usable.end(),
                     [](const InputEvent &a, const InputEvent &b) { return a.timeUs < b.timeUs; });

    // Group runs of clicks that are close together. Each group becomes one push in, a pan across
    // its clicks, and one push out.
    QList<QList<InputEvent>> groups;
    for (const InputEvent &click : usable) {
        if (!groups.isEmpty() && click.timeUs - groups.last().last().timeUs <= groupWithin)
            groups.last().append(click);
        else
            groups.append({click});
    }

    for (int g = 0; g < groups.size(); ++g) {
        const QList<InputEvent> &group = groups.at(g);
        const TimeUs firstClick = group.first().timeUs;
        const TimeUs lastClick = group.last().timeUs;

        // Start the push early so the zoom has arrived by the time the click happens — landing
        // mid-push means the viewer's eye is still moving when the thing they should see happens.
        TimeUs zoomIn = std::max<TimeUs>(0, firstClick - ramp);
        // Never start before the previous group has finished pulling out, or the two fight.
        if (g > 0) {
            const TimeUs previousEnd = groups.at(g - 1).last().timeUs + hold + ramp;
            zoomIn = std::max(zoomIn, std::min(previousEnd, firstClick));
        }
        const TimeUs holdEnd = std::min(clipDurationUs - 1, lastClick + hold);
        const TimeUs zoomOut = std::min(clipDurationUs - 1, holdEnd + ramp);

        // Rest before the push. Skipped for a group that starts at the very beginning of the clip,
        // where there is no room: it simply opens zoomed.
        if (zoomIn > 0) {
            putEased(&tracks.scaleX, zoomIn, 1.0);
            putEased(&tracks.scaleY, zoomIn, 1.0);
            putEased(&tracks.positionX, zoomIn, options.restPosition.x());
            putEased(&tracks.positionY, zoomIn, options.restPosition.y());
        }

        // In at the first click, then one key per later click so the frame pans to follow.
        for (const InputEvent &click : group) {
            const TimeUs at = std::min(clipDurationUs - 1, click.timeUs);
            const QPointF position = positionForClick(click.position, zoom, options);
            putEased(&tracks.scaleX, at, zoom);
            putEased(&tracks.scaleY, at, zoom);
            putEased(&tracks.positionX, at, position.x());
            putEased(&tracks.positionY, at, position.y());
        }

        // Hold on the last click, then pull back out.
        if (holdEnd > lastClick) {
            const QPointF position = positionForClick(group.last().position, zoom, options);
            putEased(&tracks.scaleX, holdEnd, zoom);
            putEased(&tracks.scaleY, holdEnd, zoom);
            putEased(&tracks.positionX, holdEnd, position.x());
            putEased(&tracks.positionY, holdEnd, position.y());
        }
        if (zoomOut > holdEnd) {
            putEased(&tracks.scaleX, zoomOut, 1.0);
            putEased(&tracks.scaleY, zoomOut, 1.0);
            putEased(&tracks.positionX, zoomOut, options.restPosition.x());
            putEased(&tracks.positionY, zoomOut, options.restPosition.y());
        }
    }

    return tracks;
}

} // namespace drift
