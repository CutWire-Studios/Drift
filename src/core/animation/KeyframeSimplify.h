#pragma once

#include "core/animation/Keyframe.h"

#include <QList>
#include <QPair>

#include <cmath>

namespace drift {

// Drops the keys strictly between two times, for a recording that replaces what was there.
inline void removeKeyframesBetween(KeyframeTrack<double> &track, TimeUs from, TimeUs to)
{
    const QList<TimeUs> times = track.keyframes().keys();
    for (TimeUs time : times) {
        if (time > from && time < to)
            track.removeKeyframe(time);
    }
}

// Ramer-Douglas-Peucker over the keys inside [from, to]: a key survives only when the curve
// through its neighbours would be more than `tolerance` (in the track's own units) away from it.
// The first and last key of the range always stay, so the recorded span keeps its end poses.
// With `smooth` the survivors get Catmull-Rom tangents, so the thinned curve flows instead of
// showing the corners the straight segments between them would have.
inline void simplifyKeyframes(KeyframeTrack<double> &track, TimeUs from, TimeUs to, double tolerance, bool smooth)
{
    QList<TimeUs> times;
    for (auto it = track.keyframes().lowerBound(from); it != track.keyframes().cend() && it.key() <= to; ++it)
        times.append(it.key());
    if (times.size() < 3)
        return;

    QList<double> values;
    values.reserve(times.size());
    for (TimeUs time : std::as_const(times))
        values.append(track.keyframes().value(time).value);

    QList<bool> keep(times.size(), false);
    keep.first() = keep.last() = true;
    QList<QPair<int, int>> pending{{0, int(times.size()) - 1}};
    while (!pending.isEmpty()) {
        const auto [a, b] = pending.takeLast();
        double worst = 0.0;
        int at = -1;
        for (int i = a + 1; i < b; ++i) {
            const double t = double(times[i] - times[a]) / double(times[b] - times[a]);
            const double error = std::abs(values[i] - (values[a] + (values[b] - values[a]) * t));
            if (error > worst) {
                worst = error;
                at = i;
            }
        }
        if (at >= 0 && worst > tolerance) {
            keep[at] = true;
            pending.append({a, at});
            pending.append({at, b});
        }
    }

    for (int i = 0; i < times.size(); ++i) {
        if (!keep[i])
            track.removeKeyframe(times[i]);
    }
    if (smooth) {
        for (int i = 0; i < times.size(); ++i) {
            if (keep[i])
                track.setSmooth(times[i]);
        }
    }
}

} // namespace drift
