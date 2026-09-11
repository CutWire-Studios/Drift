#pragma once

#include "FadeShape.h"
#include "Time.h"

#include <QMap>
#include <QString>
#include <QVariant>

namespace drift {

struct Clip;
struct Track;

// Gains applied to the outgoing/incoming clip's audio across a transition. The video look is
// entirely a shader (see transitions/<kindId>/), but audio still needs a curve, which each
// transition package declares via "audioCurve".
struct TransitionAudioGains
{
    double outgoing = 1.0;
    double incoming = 0.0;
};

// curve: "crossfade" (linear) | "dip" (out then in, silent at the midpoint) | "hold" (no ducking).
TransitionAudioGains transitionAudioGains(const QString &curve, double progress);

struct Transition
{
    QString id;
    QString fromClipId; // outgoing
    QString toClipId;   // incoming
    // Transition package id, e.g. "crossfade", "wipe_left", "plasma_burn".
    QString kindId = QStringLiteral("crossfade");
    QMap<QString, QVariant> parameters; // instance overrides of the package's parameter defaults
    TimeUs durationUs = 500'000;        // 0.5s default
    // Remaps the linear window position before it reaches the shader, so a transition can ease
    // in or out instead of running at a constant rate. Linear is the historical behaviour.
    FadeCurve easingCurve = FadeCurve::Linear;
    FadeShape easingShape; // only consulted when easingCurve is Custom
};

const Clip *clipById(const Track &track, const QString &clipId);
bool clipsEligibleForTransition(const Clip &fromClip, const Clip &toClip);
bool clipsPhysicallyOverlap(const Clip &fromClip, const Clip &toClip);
TimeUs physicalOverlapDurationUs(const Clip &fromClip, const Clip &toClip);
TimeUs transitionCenterUs(const Track &track, const Transition &transition);
bool transitionWindow(const Track &track, const Transition &transition, TimeUs &startUs, TimeUs &endUs);
double transitionProgress(TimeUs timelineUs, TimeUs windowStartUs, TimeUs windowEndUs);
// Same, with the transition's easing curve applied. Both the picture and the audio ducking go
// through this, so they cannot drift apart.
double transitionProgress(const Transition &transition, TimeUs timelineUs, TimeUs windowStartUs,
                          TimeUs windowEndUs);
const Transition *activeTransitionAt(const Track &track, TimeUs timelineUs, TimeUs &windowStartUs,
                                     TimeUs &windowEndUs);

} // namespace drift
