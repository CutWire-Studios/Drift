#include "Track.h"

namespace drift {

QString trackTypeToString(TrackType type)
{
    switch (type) {
    case TrackType::Video:
        return QStringLiteral("video");
    case TrackType::Audio:
        return QStringLiteral("audio");
    case TrackType::Text:
        return QStringLiteral("text");
    case TrackType::Subtitle:
        return QStringLiteral("subtitle");
    case TrackType::Shape:
        return QStringLiteral("shape");
    case TrackType::Adjustment:
        return QStringLiteral("adjustment");
    }
    return QStringLiteral("video");
}

TrackType trackTypeFromString(const QString &type)
{
    if (type == QStringLiteral("audio"))
        return TrackType::Audio;
    if (type == QStringLiteral("text"))
        return TrackType::Text;
    if (type == QStringLiteral("subtitle"))
        return TrackType::Subtitle;
    if (type == QStringLiteral("shape"))
        return TrackType::Shape;
    if (type == QStringLiteral("adjustment"))
        return TrackType::Adjustment;
    return TrackType::Video;
}

QString adjustmentScopeToString(AdjustmentScope scope)
{
    switch (scope) {
    case AdjustmentScope::AllBelow:
        return QStringLiteral("allBelow");
    case AdjustmentScope::ParentTrack:
        return QStringLiteral("parentTrack");
    }
    return QStringLiteral("allBelow");
}

AdjustmentScope adjustmentScopeFromString(const QString &scope)
{
    if (scope == QStringLiteral("parentTrack"))
        return AdjustmentScope::ParentTrack;
    return AdjustmentScope::AllBelow;
}

bool Track::allowsClipType(ClipType clipType) const
{
    switch (type) {
    case TrackType::Audio:
        return clipType == ClipType::Audio;
    case TrackType::Text:
        return clipType == ClipType::Text;
    case TrackType::Subtitle:
        return clipType == ClipType::Subtitle;
    case TrackType::Shape:
        return clipType == ClipType::Image || clipType == ClipType::Shape;
    case TrackType::Video:
        return clipType == ClipType::Video;
    case TrackType::Adjustment:
        return clipType == ClipType::Adjustment;
    }
    return false;
}

} // namespace drift
