#pragma once

#include <QString>
#include <QStringList>

namespace drift::projdir {

// Clip keys that move into sidecars/<clipId>/<group>.json once their JSON gets big. The folder reader
// and writer, the schema and the lint all read this one table.
struct SidecarGroup
{
    QString name;
    QStringList keys;
};

inline const QList<SidecarGroup> &sidecarGroups()
{
    static const QList<SidecarGroup> groups{
        {QStringLiteral("keyframes"),
         {QStringLiteral("volume"), QStringLiteral("opacity"), QStringLiteral("positionX"),
          QStringLiteral("positionY"), QStringLiteral("scaleX"), QStringLiteral("scaleY"),
          QStringLiteral("rotation"), QStringLiteral("rotationX"), QStringLiteral("rotationY"),
          QStringLiteral("z"), QStringLiteral("perspective"), QStringLiteral("anchorX"),
          QStringLiteral("anchorY"), QStringLiteral("anchorZ"), QStringLiteral("speedCurve")}},
        {QStringLiteral("masks"), {QStringLiteral("mask")}},
        {QStringLiteral("effects"), {QStringLiteral("effects"), QStringLiteral("audioEffects")}},
        {QStringLiteral("payload"),
         {QStringLiteral("vector"), QStringLiteral("model3d"), QStringLiteral("pdf"),
          QStringLiteral("motionTrackers"), QStringLiteral("subtitleCues")}},
    };
    return groups;
}

// A group's keys stay inline in the track file until their compact JSON is larger than this.
constexpr int kSidecarThresholdBytes = 600;

} // namespace drift::projdir
