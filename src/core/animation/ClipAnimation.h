#pragma once

#include "core/animation/FadeShape.h"
#include "core/base/Time.h"

#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

#include <optional>

namespace drift {

// Legacy ease names, read only so old projects without a "curve" key keep their feel.
enum class ClipAnimEase { Linear, EaseOut, EaseInOut, Back };

// Body intro/outro for a whole clip, transform only. Opacity and volume belong to the edge fade
// (Clip::fadeInUs/fadeOutUs). Mirrors drift::Transition: the identity is a string id so a
// shader-backed animation id from a newer build survives a load/save round trip untouched.
struct ClipAnimation
{
    // "" = none. Built-ins: slideUp, slideDown, slideLeft, slideRight, zoomIn, zoomOut, pop,
    // spinCW, spinCCW, bounce.
    QString kindId;
    QMap<QString, QVariant> parameters;
    TimeUs durationUs = 500000; // 0.5s
    FadeCurve curve = FadeCurve::Smooth;
    FadeShape shape; // when curve == Custom / Bezier

    bool isNone() const { return kindId.isEmpty(); }
};

struct ClipAnimSample
{
    double dx = 0.0;
    double dy = 0.0;
    double scale = 1.0;
    double rotationDeg = 0.0;
};

// True for the ids evaluateClipAnimation knows how to draw.
bool isBuiltinClipAnimation(const QString &kindId);

struct ClipAnimationKindInfo
{
    QString id;
    // Untranslated; the UI layer runs it through QCoreApplication::translate("ClipAnimation", ...).
    const char *label = "";
};

// Every animation kind the UI offers, in menu order. A shader-backed package appends here.
QList<ClipAnimationKindInfo> clipAnimationKinds();

QString clipAnimEaseToString(ClipAnimEase ease);
ClipAnimEase clipAnimEaseFromString(const QString &ease);

FadeCurve clipAnimEaseToCurve(ClipAnimEase ease);

QJsonObject clipAnimationToJson(const ClipAnimation &anim);
ClipAnimation clipAnimationFromJson(const QJsonObject &object);

// Settled sample for the clip at `timelineUs`. `layoutW`/`layoutH` are canvas pixels
// (already render-scaled) so slide travel matches what the user sees.
// Unknown kind ids are ignored.
ClipAnimSample evaluateClipAnimation(TimeUs timelineStart, TimeUs timelineDuration,
                                     const ClipAnimation &animIn, const ClipAnimation &animOut,
                                     TimeUs timelineUs, double layoutW, double layoutH);

// Eased 0..1 progress of one edge animation in time order across its window (intro: start to
// start+dur, outro: end-dur to end). nullopt when the animation is none or timelineUs is outside it.
std::optional<double> clipAnimationProgress(TimeUs timelineStart, TimeUs timelineDuration,
                                            const ClipAnimation &anim, bool isIn, TimeUs timelineUs);

} // namespace drift
