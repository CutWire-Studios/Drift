#pragma once

#include "Clip.h"
#include "Transition.h"

#include <QList>
#include <QString>

namespace drift {

enum class TrackType { Video, Audio, Text, Subtitle, Shape };

QString trackTypeToString(TrackType type);
TrackType trackTypeFromString(const QString &type);

struct Track
{
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
    // view-only: multiplies this track's base row height so a single lane can be
    // enlarged (waveform editing) without zooming the whole timeline.
    qreal heightScale = 1.0;

    bool allowsClipType(ClipType clipType) const;
};

} // namespace drift
