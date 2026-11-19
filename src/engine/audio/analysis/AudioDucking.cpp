#include "engine/audio/analysis/AudioDucking.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

constexpr double kMinDipSeconds = 0.05;
constexpr double kOwnedPadSeconds = 2.0;

QList<DuckSpan> mergeWithin(QList<DuckSpan> spans, double gap)
{
    std::sort(spans.begin(), spans.end(),
              [](const DuckSpan &a, const DuckSpan &b) { return a.start < b.start; });
    QList<DuckSpan> out;
    for (const DuckSpan &span : std::as_const(spans)) {
        if (span.end < span.start)
            continue;
        if (!out.isEmpty() && span.start - out.last().end < gap)
            out.last().end = std::max(out.last().end, span.end);
        else
            out.append(span);
    }
    return out;
}

} // namespace

QList<DuckSpan> mergeDuckSpans(QList<DuckSpan> speech, const DuckSettings &settings)
{
    const double gap = settings.holdSeconds + settings.attackSeconds + settings.releaseSeconds;
    QList<DuckSpan> out;
    for (const DuckSpan &span : mergeWithin(std::move(speech), gap)) {
        if (span.end - span.start >= kMinDipSeconds)
            out.append(span);
    }
    return out;
}

QList<DuckSpan> ownedDuckRanges(QList<DuckSpan> overlaps, const DuckSettings &settings)
{
    const double pad = std::max({kOwnedPadSeconds, settings.attackSeconds, settings.releaseSeconds});
    for (DuckSpan &span : overlaps) {
        span.start -= pad;
        span.end += pad;
    }
    return mergeWithin(std::move(overlaps), 0.0);
}

QList<DuckSpan> levelSpeechSpans(const float *mono, qint64 frames, int rate, double startSeconds,
                                 double sensitivity)
{
    QList<DuckSpan> out;
    const qint64 window = std::max<qint64>(1, qint64(rate * 0.02));
    if (!mono || frames < window)
        return out;
    const double dbfs = -20.0 - 30.0 * std::clamp(sensitivity, 0.0, 1.0);
    const double threshold = std::pow(10.0, dbfs / 20.0);
    const double windowSeconds = double(window) / rate;

    QList<DuckSpan> runs;
    int runStart = -1;
    const qint64 count = frames / window;
    for (qint64 w = 0; w <= count; ++w) {
        bool loud = false;
        if (w < count) {
            double sum = 0.0;
            for (qint64 i = w * window; i < (w + 1) * window; ++i)
                sum += double(mono[i]) * double(mono[i]);
            loud = std::sqrt(sum / double(window)) >= threshold;
        }
        if (loud && runStart < 0)
            runStart = int(w);
        if (!loud && runStart >= 0) {
            runs.append({startSeconds + runStart * windowSeconds, startSeconds + w * windowSeconds});
            runStart = -1;
        }
    }
    for (const DuckSpan &run : mergeWithin(std::move(runs), 0.15)) {
        if (run.end - run.start >= 0.1)
            out.append(run);
    }
    return out;
}

float vadThresholdForSensitivity(double sensitivity)
{
    return float(std::clamp(0.8 - 0.6 * sensitivity, 0.1, 0.9));
}

int applyDuckEnvelope(Clip &music, const QList<DuckSpan> &dips, const QList<DuckSpan> &owned,
                      const DuckSettings &settings, double baseVolume)
{
    const double attack = std::max(0.0, settings.attackSeconds);
    const double release = std::max(0.0, settings.releaseSeconds);
    const auto relUs = [&music](double seconds) {
        return std::clamp<TimeUs>(secondsToUs(seconds) - music.timelineStart, 0,
                                  music.timelineDuration);
    };
    const auto rawRelUs = [&music](double seconds) { return secondsToUs(seconds) - music.timelineStart; };

    QList<DuckSpan> doomed = owned;
    for (const DuckSpan &dip : dips)
        doomed.append({dip.start - attack, dip.end + release});
    QList<TimeUs> remove;
    for (auto it = music.volume.keyframes().constBegin(); it != music.volume.keyframes().constEnd();
         ++it) {
        for (const DuckSpan &span : std::as_const(doomed)) {
            if (it.key() >= rawRelUs(span.start) && it.key() <= rawRelUs(span.end)) {
                remove.append(it.key());
                break;
            }
        }
    }
    for (const TimeUs at : std::as_const(remove))
        music.volume.removeKeyframe(at);

    struct Dip
    {
        TimeUs pre, start, end, post;
        double rest;
    };
    QList<Dip> plan;
    for (const DuckSpan &dip : dips) {
        if (secondsToUs(dip.end) <= music.timelineStart || secondsToUs(dip.start) >= music.timelineEnd())
            continue;
        const double rest = music.volume.isEmpty()
                                ? baseVolume
                                : music.volume.evaluateAt(relUs(dip.start - attack));
        plan.append({relUs(dip.start - attack), relUs(dip.start), relUs(dip.end),
                     relUs(dip.end + release), rest});
    }

    const double gain = std::pow(10.0, std::min(0.0, settings.reduceDb) / 20.0);
    int keys = 0;
    for (const Dip &d : std::as_const(plan)) {
        const double ducked = std::clamp(d.rest * gain, 0.0, 2.0);
        if (d.pre < d.start) {
            music.volume.setKeyframe(d.pre, d.rest);
            ++keys;
        }
        music.volume.setKeyframe(d.start, ducked);
        ++keys;
        if (d.end > d.start) {
            music.volume.setKeyframe(d.end, ducked);
            ++keys;
        }
        if (d.post > d.end) {
            music.volume.setKeyframe(d.post, d.rest);
            ++keys;
        }
    }
    return keys;
}

} // namespace drift
