#include "core/project/Track.h"

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
    case AdjustmentScope::Range:
        return QStringLiteral("range");
    case AdjustmentScope::Camera:
        return QStringLiteral("camera");
    }
    return QStringLiteral("allBelow");
}

AdjustmentScope adjustmentScopeFromString(const QString &scope)
{
    if (scope == QStringLiteral("parentTrack"))
        return AdjustmentScope::ParentTrack;
    if (scope == QStringLiteral("range"))
        return AdjustmentScope::Range;
    if (scope == QStringLiteral("camera"))
        return AdjustmentScope::Camera;
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
        return clipType == ClipType::Image || clipType == ClipType::Shape
            || clipType == ClipType::Vector || clipType == ClipType::Model3d
            || clipType == ClipType::Pdf || clipType == ClipType::Procedural;
    case TrackType::Video:
        return clipType == ClipType::Video || clipType == ClipType::Composite;
    case TrackType::Adjustment:
        return clipType == ClipType::Adjustment;
    }
    return false;
}

bool Track::acceptsClip(const Clip &clip) const
{
    if (!allowsClipType(clip.type))
        return false;
    if (clip.type != ClipType::Adjustment)
        return true;
    if ((clip.adjustmentKind == AdjustmentKind::Camera) != isCameraLayer())
        return false;
    return (clip.adjustmentKind == AdjustmentKind::Transform) == isTransformLayer();
}

QString trackNumberKind(const Track &track)
{
    if (track.isAdjustmentLane() || track.isCameraLayer())
        return {};
    if (track.isTransformLayer())
        return QStringLiteral("transform");
    return trackTypeToString(track.type);
}

QString trackDefaultLabel(const Track &track)
{
    if (!track.name.isEmpty())
        return track.name;
    if (track.isCameraLayer())
        return QStringLiteral("Camera");
    // A nested lane is named after what it holds, as its row in the timeline is.
    if (track.isAdjustmentLane()) {
        const AdjustmentKind kind = track.clips.isEmpty() ? AdjustmentKind::VideoEffects
                                                          : track.clips.first().adjustmentKind;
        return kind == AdjustmentKind::AudioEffects ? QStringLiteral("Audio FX")
               : kind == AdjustmentKind::Mask       ? QStringLiteral("Mask")
                                                    : QStringLiteral("Effects");
    }
    QString type;
    if (track.isTransformLayer()) {
        type = QStringLiteral("Transform");
    } else {
        switch (track.type) {
        case TrackType::Video: type = QStringLiteral("Video"); break;
        case TrackType::Audio: type = QStringLiteral("Audio"); break;
        case TrackType::Text: type = QStringLiteral("Text"); break;
        case TrackType::Subtitle: type = QStringLiteral("Subtitle"); break;
        case TrackType::Shape: type = QStringLiteral("Graphic"); break;
        case TrackType::Adjustment: type = QStringLiteral("Adjustment"); break;
        }
    }
    return track.number > 0 ? QStringLiteral("%1 %2").arg(type).arg(track.number) : type;
}

} // namespace drift
