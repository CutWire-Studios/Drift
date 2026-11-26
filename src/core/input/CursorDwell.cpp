#include "core/input/CursorDwell.h"

#include <algorithm>
#include <cmath>

namespace drift {

QList<InputEvent> cursorDwellPoints(const QList<CursorSample> &samples,
                                    const CursorDwellOptions &options)
{
    QList<InputEvent> out;
    if (samples.size() < 2)
        return out;

    QList<CursorSample> ordered = samples;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const CursorSample &a, const CursorSample &b) {
                         return a.timeUs < b.timeUs;
                     });

    const double tolerance = std::max(0.0, options.moveTolerance);
    const TimeUs minDwell = std::max<TimeUs>(0, options.minDwellUs);
    const TimeUs minGap = std::max<TimeUs>(0, options.minGapUs);

    auto distance = [](const QPointF &a, const QPointF &b) {
        const double dx = a.x() - b.x();
        const double dy = a.y() - b.y();
        return std::sqrt(dx * dx + dy * dy);
    };

    // Walks runs of samples that stay within `tolerance` of where the run started. The anchor is
    // the run's first position rather than the previous sample, so a slow continuous drift never
    // accumulates into a "dwell" one small step at a time.
    int runStart = 0;
    bool emittedForRun = false;
    for (int i = 1; i <= ordered.size(); ++i) {
        const bool broke = i == ordered.size()
                           || distance(ordered.at(i).position, ordered.at(runStart).position)
                                  > tolerance;
        if (!broke) {
            // Emit as soon as the run is long enough, rather than waiting for it to end: a cursor
            // that settles and stays put for ten seconds should zoom at the start of that, not
            // after it.
            const TimeUs held = ordered.at(i).timeUs - ordered.at(runStart).timeUs;
            if (!emittedForRun && held >= minDwell) {
                const CursorSample &arrival = ordered.at(runStart);
                if (out.isEmpty() || arrival.timeUs - out.last().timeUs >= minGap) {
                    InputEvent event;
                    event.kind = InputEventKind::Press;
                    event.timeUs = arrival.timeUs;
                    event.position = arrival.position;
                    // No button: this was inferred from where the cursor stopped, not observed.
                    // The overlay uses that to tell a real click from a guess.
                    event.button = 0;
                    out.append(event);
                }
                emittedForRun = true;
            }
            continue;
        }
        runStart = i;
        emittedForRun = false;
    }

    return out;
}

} // namespace drift
