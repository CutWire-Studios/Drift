#pragma once

#include "Clip.h"
#include "Transition.h"

#include <QList>
#include <QString>

namespace drift {

enum class TrackType { Video, Audio, Text, Subtitle, Shape, Adjustment };

QString trackTypeToString(TrackType type);
TrackType trackTypeFromString(const QString &type);

// What an adjustment track applies to. AllBelow is the standalone case: it snapshots the
// canvas composited so far, so it treats every track under it as one flattened image.
// ParentTrack is the nested lane: it contributes its effects to each clip of `parentTrackId`
// it overlaps, inside that clip's own layer pass, so the clip's transform still carries them.
enum class AdjustmentScope { AllBelow, ParentTrack };

QString adjustmentScopeToString(AdjustmentScope scope);
AdjustmentScope adjustmentScopeFromString(const QString &scope);

struct Track
{
    // Stable across reorders, unlike the array index. Nested lanes point at their parent by
    // this, so moving a track cannot silently reparent them. Minted on load for v3 projects.
    QString id;
    TrackType type = TrackType::Video;
    QList<Clip> clips;
    QList<Transition> transitions;
    // User-given label, shown instead of the type+position fallback ("Video 1") once set.
    // Empty by default — most tracks never get a custom name.
    QString name;
    bool muted = false;
    bool hidden = false;
    bool locked = false;
    bool showWaveform = false; // view-only: show audio waveform instead of filmstrip for this track's clips
    // view-only: draw a multi-channel clip's waveform as one lane per source channel instead
    // of the single max-across-channels envelope. Opt-in because a 5.1 clip in a standard row
    // gives each lane a few pixels — pair it with heightScale.
    bool showChannelWaveforms = false;
    // view-only: multiplies this track's base row height so a single lane can be
    // enlarged (waveform editing) without zooming the whole timeline.
    qreal heightScale = 1.0;

    // Adjustment tracks only.
    AdjustmentScope adjustmentScope = AdjustmentScope::AllBelow;
    // Set iff type == Adjustment && adjustmentScope == ParentTrack.
    QString parentTrackId;

    bool allowsClipType(ClipType clipType) const;

    bool isAdjustment() const { return type == TrackType::Adjustment; }
    // A nested lane draws inside its parent's row rather than occupying one of its own.
    bool isAdjustmentLane() const
    {
        return type == TrackType::Adjustment && adjustmentScope == AdjustmentScope::ParentTrack
               && !parentTrackId.isEmpty();
    }
};

} // namespace drift
