#pragma once

#include <QList>
#include <QtGlobal>

#include <functional>
#include <vector>

namespace drift {

struct SilenceParams
{
    float threshold = 0.02f;     // RMS amplitude, about -34 dBFS
    double minSilence = 0.5;     // seconds of continuous quiet before it counts
    double padding = 0.08;       // seconds of sound kept either side of a cut
    double windowSeconds = 0.01;
};

struct SilenceSpan
{
    double start = 0.0; // seconds from the first sample
    double end = 0.0;
};

// The stretches of mono PCM that can be cut: runs of quiet windows at least minSilence long,
// narrowed by `padding` on each side that borders sound. A run touching the start or end of the
// signal is not padded on that side, so leading and trailing silence goes completely.
QList<SilenceSpan> findSilence(const float *mono, qint64 frames, int sampleRate,
                               const SilenceParams &params = {});

struct AudioOffsetResult
{
    enum class Status { Ok, Silent, TooShort, Ambiguous };
    Status status = Status::Ambiguous;
    // Seconds. Positive: `other` content appears this much later in `anchor` than at the same
    // local position, so `other` starts at anchorStart + lagSeconds.
    double lagSeconds = 0.0;
    double peak = 0.0;       // normalized correlation at the best lag
    double secondPeak = 0.0; // best elsewhere, at least a guard interval away
};

struct AudioOffsetParams
{
    double maxLagSeconds = 60.0;
    double minOverlapSeconds = 2.0;
    double minPeak = 0.1;
    double minPeakRatio = 1.5;
    double silentRms = 0.002;
};

// Cross-correlates two mono signals at the same rate: finds k such that other[n] ~ anchor[n + k].
// `keepGoing` is polled between stages and returns false to abort (status Ambiguous).
AudioOffsetResult findAudioOffset(const std::vector<float> &anchor, const std::vector<float> &other,
                                  int sampleRate, const AudioOffsetParams &params = {},
                                  const std::function<bool()> &keepGoing = {});

} // namespace drift
