#pragma once

#include <QList>

namespace drift {

// Every `every`-th beat. With a downbeat anchor the count starts on the bar line, so "every 2"
// keeps the strong beats rather than whichever beat the analysis window began on.
QList<double> beatsEvery(const QList<double> &beats, int every, int anchorIndex);

struct BeatCutOptions
{
    int beatsPerCut = 2;
    bool syncToDownbeats = true;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int firstDownbeat = 0;
    int beatsPerBar = 4;
};

struct BeatCutSlot
{
    int clip = 0;           // index into the clip list handed to planBeatCut
    double start = 0.0;
    double duration = 0.0;
    bool shorterThanSlot = false; // the clip has less footage than the beat slot
};

// Lays clips end to end, one per cut interval, so every cut except the first start lands on a
// beat (or a bar line when syncing). The first clip starts at `startSeconds` and runs to the
// first cut at least half a beat later. `available` is each clip's usable length in seconds.
// Stops when the music ends or the clips run out; a clip longer than its slot is trimmed to it.
QList<BeatCutSlot> planBeatCut(const QList<double> &beats, const QList<double> &available,
                               const BeatCutOptions &options);

} // namespace drift
