#pragma once

#include "engine/audio/analysis/AudioEvents.h"

#include <QList>
#include <QString>

namespace drift {

// How a cut (or any cue time) is answered by the soundtrack.
struct AudioSyncItem
{
    enum class Status { Ok, Near, Swell, Peak, Off, NoSound };

    double seconds = 0.0; // the cue
    Status status = Status::NoSound;
    bool answered = false;
    double offsetMs = 0.0; // sound minus picture; negative means the sound is early
    double eventSeconds = 0.0;
    double eventProminenceDb = 0.0;
    bool hasAccent = false; // a strong accent within 100 ms
    double accentOffsetMs = 0.0;
    bool hasBeat = false;
    double beatOffsetMs = 0.0;
};

struct AudioSyncStats
{
    int n = 0;
    double toleranceMs = 40.0;
    double hitWithinTolerance = 0.0;
    double hitWithin100 = 0.0;
    bool hasMedianOffset = false;
    double medianAbsOffsetMs = 0.0;
    int answeredBySwell = 0;
    int answeredByLevelPeak = 0;
    QList<double> noSound; // cue times answered by nothing, or only by a distant strong onset
    bool hasChance = false; // events exist
    double chanceTolerance = 0.0;
    double chance100 = 0.0;
    bool hasAccents = false;
    double accentMinDb = 0.0;
    double accentWithinTolerance = 0.0;
    double accentChanceTolerance = 0.0;
    double accentWithin100 = 0.0;
    double accentChance100 = 0.0;
    bool hasBeats = false;
    double onBeat = 0.0;
    double onBeatChance = 0.0;
};

struct AudioSyncResult
{
    QList<AudioSyncItem> items;
    AudioSyncStats stats;
};

QString syncStatusName(AudioSyncItem::Status status);

struct AudioSyncOptions
{
    double toleranceSeconds = 0.04; // "ok": an onset within this (a frame, or 40 ms)
    const AudioLevelEnvelopes *levels = nullptr; // also look for soft level peaks where no onset answers
    QList<double> beats;
};

// Each cue is matched to the event that answers it: any onset within the tolerance (ok), else
// the nearest strong one within 100 ms, else any within 100 ms (near), else a swell peaking
// within 300 ms (swell), else a level peak within 150 ms that the onset detector misses (peak),
// else a strong onset within 250 ms (off); nothing at all is no_sound. The chance figures say
// what randomly placed cues would score against the same event density.
AudioSyncResult matchSync(const QList<double> &cues, const QList<AudioEvent> &events,
                          const AudioSyncOptions &options = {});

} // namespace drift
