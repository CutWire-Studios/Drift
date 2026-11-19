#pragma once

#include "core/project/clip/Clip.h"

#include <QList>

namespace drift {

struct DuckSettings
{
    double reduceDb = -12.0;      // <= 0; gain = 10^(reduceDb/20) times the rest level
    double attackSeconds = 0.2;   // ramp down, ends where speech starts
    double releaseSeconds = 0.5;  // ramp up, starts where speech ends
    double holdSeconds = 0.5;     // shorter pauses stay ducked
};

struct DuckSpan
{
    double start = 0.0; // timeline seconds
    double end = 0.0;
};

// Sorted, with neighbours closer than hold + attack + release merged so two ramps never overlap
// and the music does not pump between words. Spans under 50 ms are dropped.
QList<DuckSpan> mergeDuckSpans(QList<DuckSpan> speech, const DuckSettings &settings);

// The ranges one pass takes ownership of: each overlap padded generously (a re-run with a shorter
// ramp must still reach the keys a longer one wrote) and merged.
QList<DuckSpan> ownedDuckRanges(QList<DuckSpan> overlaps, const DuckSettings &settings);

// Speech as "louder than a threshold" on 20 ms RMS windows. sensitivity 0..1 maps the threshold
// from -20 dBFS to -50 dBFS. Gaps under 150 ms are bridged and runs under 100 ms dropped. Times
// are startSeconds plus the offset into `mono`.
QList<DuckSpan> levelSpeechSpans(const float *mono, qint64 frames, int rate, double startSeconds,
                                 double sensitivity);

// Silero speech-probability threshold for a 0..1 sensitivity.
float vadThresholdForSensitivity(double sensitivity);

// Replaces the volume keys inside `owned` (and the dips' own ramps) with one dip per span: rest
// level at start - attack, ducked from start to end, rest again at end + release. The rest level
// is whatever the envelope holds going into the dip once the old keys are gone, or `baseVolume`
// when nothing is left. Returns the keys written.
int applyDuckEnvelope(Clip &music, const QList<DuckSpan> &dips, const QList<DuckSpan> &owned,
                      const DuckSettings &settings, double baseVolume);

} // namespace drift
