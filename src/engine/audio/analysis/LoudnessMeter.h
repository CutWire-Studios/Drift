#pragma once

#include "engine/media/waveforms/MediaWaveform.h"

#include <QList>
#include <QString>
#include <QVector>

namespace drift {

// Integrated loudness (ITU-R BS.1770 / EBU R128, simplified) plus a 4×-interpolated
// true-peak estimate. Built on the same pull-chunk PCM source MediaWaveform uses so a
// timeline mix and a clip decode share one code path.
struct LoudnessResult
{
    bool ok = false;
    double integratedLufs = 0.0;
    double truePeakDb = -120.0;
    double durationSeconds = 0.0;
};

LoudnessResult measureLoudness(qint64 totalFrames, int sampleRate,
                               const MediaWaveform::FillChunk &fill);

// A loudness picture beyond the single integrated number: the BS.1770 curves, loudness range,
// clipping and the stretches where the programme drops out. Times are seconds from the start
// of the measured span.
struct LoudnessCurve
{
    double t0 = 0.0; // centre of the first window
    double dt = 0.0;
    QVector<float> lufs; // floored at the -70 LUFS absolute gate
};

struct LoudnessGap
{
    double start = 0.0;
    double end = 0.0;
    double levelLufs = -70.0;
    QString kind; // "digital silence", "near silence" or "drop"
    QString where; // "lead-in", "tail", "whole file" when it touches the span's edge
};

struct ClippingRun
{
    double start = 0.0;
    int samples = 0;
};

struct LoudnessReport
{
    bool ok = false;
    double durationSeconds = 0.0;
    bool hasIntegrated = false; // false when everything is gated out or the span is under 0.4 s
    double integratedLufs = -70.0;
    bool hasLra = false; // needs a span of at least 6 s
    double lraLu = 0.0;
    bool hasTruePeak = false; // false for digital silence
    double truePeakDb = -120.0;
    double samplePeakDb = -120.0;
    LoudnessCurve momentary;  // 0.4 s windows every 0.1 s
    LoudnessCurve shortTerm;  // 3 s windows every 0.5 s
    double maxMomentaryLufs = -70.0;
    double maxMomentaryAt = 0.0;
    double maxShortTermLufs = -70.0;
    qint64 clippedSamples = 0; // samples at or above 0.999 in either channel
    QList<ClippingRun> clippingRuns; // runs of at least 3 samples
    QList<LoudnessGap> gaps; // 0.1 s loudness under max(-60, integrated - 20) LUFS for >= 0.25 s
};

// One pass over the pull-chunk source. Same K-weighting and 4x true peak as measureLoudness.
LoudnessReport analyzeLoudness(qint64 totalFrames, int sampleRate,
                               const MediaWaveform::FillChunk &fill);

} // namespace drift
