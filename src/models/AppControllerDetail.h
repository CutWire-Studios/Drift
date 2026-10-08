#pragma once

#include "core/Clip.h"
#include "core/Mask.h"
#include "core/Project.h"
#include "core/Transition.h"

#include <QSet>
#include <QString>

#include <QMatrix4x4>
#include <QTransform>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

// Helpers private to AppController.cpp that PreviewController shares. Not API.
namespace drift::appdetail {

double clipTransformValue(const drift::KeyframeTrack<double> &track, drift::TimeUs relative,
                          double defaultValue);
bool writeKeyframeValue(drift::KeyframeTrack<double> &track, drift::TimeUs relative, double value,
                        bool autoKey, bool force);
bool clipAcceptsPreviewTransform(const drift::Clip &clip);
void setClipLayoutPixels(drift::Clip &clip, double x, double y, double w, double h);
QVariantMap maskToMap(const drift::Mask &m, drift::TimeUs timelineStart = 0);
QVariantList transformToList(const QTransform &t);
// A 2D homography as a 4x4 on (x, y, z, w) that leaves z alone: what a QtQuick Matrix4x4 needs.
QMatrix4x4 liftHomography(const QTransform &t);
QTransform previewBoxParent(const QVariantMap &box);
std::optional<drift::ClipType> placeableClipType(const QString &kind);
bool isClipTargetedKind(const QString &kind);
QVariantMap rejectDrop(const QString &message = QString());
// Carry keyframe times from a clip onto the retimed copy through the source moment each key sits on.
void remapKeyframesForRetime(drift::Clip &dst, const drift::Clip &src);

Transition *findTransition(Track &track, const QString &transitionId);
Transition *findTransitionBetween(Track &track, const QString &fromId, const QString &toId);

// The clips a video transition's two clips are linked to, when those sit back to back on one audio
// track: where the same transition's sound belongs once the audio has been separated out.
struct LinkedAudioPair
{
    int track = -1;
    QString fromId;
    QString toId;
    bool valid() const { return track >= 0; }
};
LinkedAudioPair linkedAudioPairFor(const Project &project, const Track &track,
                                   const QString &fromClipId, const QString &toClipId);
// The audio-track twin of a video transition, or null.
Transition *mirroredAudioTransition(Project &project, int trackIndex, const Transition &t);

void syncLinkedPartnersFrom(Project &project, const Clip &source,
                            const QSet<QString> &skipClipIds = {});

} // namespace drift::appdetail
