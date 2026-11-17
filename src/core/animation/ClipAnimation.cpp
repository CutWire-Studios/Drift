#include "core/animation/ClipAnimation.h"

#include <QCoreApplication>
#include <QEasingCurve>
#include <QHash>

#include <algorithm>
#include <cmath>

namespace drift {

namespace {

enum class Builtin {
    None,
    SlideUp,
    SlideDown,
    SlideLeft,
    SlideRight,
    ZoomIn,
    ZoomOut,
    Pop,
    SpinCW,
    SpinCCW,
    Bounce
};

Builtin builtinFor(const QString &id)
{
    static const QHash<QString, Builtin> table{
        {QStringLiteral("slideUp"), Builtin::SlideUp},
        {QStringLiteral("slideDown"), Builtin::SlideDown},
        {QStringLiteral("slideLeft"), Builtin::SlideLeft},
        {QStringLiteral("slideRight"), Builtin::SlideRight},
        {QStringLiteral("zoomIn"), Builtin::ZoomIn},
        {QStringLiteral("zoomOut"), Builtin::ZoomOut},
        {QStringLiteral("pop"), Builtin::Pop},
        {QStringLiteral("spinCW"), Builtin::SpinCW},
        {QStringLiteral("spinCCW"), Builtin::SpinCCW},
        {QStringLiteral("bounce"), Builtin::Bounce},
    };
    return table.value(id, Builtin::None);
}

} // namespace

bool isBuiltinClipAnimation(const QString &kindId)
{
    return builtinFor(kindId) != Builtin::None;
}

QList<ClipAnimationKindInfo> clipAnimationKinds()
{
    return {
        {QStringLiteral("slideUp"), QT_TRANSLATE_NOOP("ClipAnimation", "Slide up")},
        {QStringLiteral("slideDown"), QT_TRANSLATE_NOOP("ClipAnimation", "Slide down")},
        {QStringLiteral("slideLeft"), QT_TRANSLATE_NOOP("ClipAnimation", "Slide left")},
        {QStringLiteral("slideRight"), QT_TRANSLATE_NOOP("ClipAnimation", "Slide right")},
        {QStringLiteral("zoomIn"), QT_TRANSLATE_NOOP("ClipAnimation", "Zoom in")},
        {QStringLiteral("zoomOut"), QT_TRANSLATE_NOOP("ClipAnimation", "Zoom out")},
        {QStringLiteral("pop"), QT_TRANSLATE_NOOP("ClipAnimation", "Pop")},
        {QStringLiteral("spinCW"), QT_TRANSLATE_NOOP("ClipAnimation", "Spin CW")},
        {QStringLiteral("spinCCW"), QT_TRANSLATE_NOOP("ClipAnimation", "Spin CCW")},
        {QStringLiteral("bounce"), QT_TRANSLATE_NOOP("ClipAnimation", "Bounce")},
    };
}

QString clipAnimEaseToString(ClipAnimEase ease)
{
    switch (ease) {
    case ClipAnimEase::Linear:
        return QStringLiteral("linear");
    case ClipAnimEase::EaseInOut:
        return QStringLiteral("easeInOut");
    case ClipAnimEase::Back:
        return QStringLiteral("back");
    case ClipAnimEase::EaseOut:
        return QStringLiteral("easeOut");
    }
    return QStringLiteral("easeOut");
}

ClipAnimEase clipAnimEaseFromString(const QString &ease)
{
    if (ease == QStringLiteral("linear"))
        return ClipAnimEase::Linear;
    if (ease == QStringLiteral("easeInOut"))
        return ClipAnimEase::EaseInOut;
    if (ease == QStringLiteral("back"))
        return ClipAnimEase::Back;
    return ClipAnimEase::EaseOut;
}

FadeCurve clipAnimEaseToCurve(ClipAnimEase ease)
{
    switch (ease) {
    case ClipAnimEase::Linear:
        return FadeCurve::Linear;
    case ClipAnimEase::EaseInOut:
        return FadeCurve::Smooth;
    case ClipAnimEase::Back:
        return FadeCurve::Smooth;
    case ClipAnimEase::EaseOut:
        return FadeCurve::EqualPower;
    }
    return FadeCurve::Smooth;
}

QJsonObject clipAnimationToJson(const ClipAnimation &anim)
{
    QJsonObject object{
        {QStringLiteral("kind"), anim.kindId.isEmpty() ? QStringLiteral("none") : anim.kindId},
        {QStringLiteral("durationUs"), static_cast<double>(anim.durationUs)},
        {QStringLiteral("curve"), fadeCurveToString(anim.curve)},
    };
    if (!anim.parameters.isEmpty())
        object.insert(QStringLiteral("parameters"), QJsonObject::fromVariantMap(anim.parameters));
    if ((anim.curve == FadeCurve::Custom && !anim.shape.isEmpty())
        || (anim.curve == FadeCurve::Bezier && anim.shape.hasHandles()))
        object.insert(QStringLiteral("shape"), anim.shape.toJson());
    return object;
}

ClipAnimation clipAnimationFromJson(const QJsonObject &object)
{
    ClipAnimation anim;
    const QString kind = object.value(QStringLiteral("kind")).toString();
    anim.kindId = kind == QLatin1String("none") ? QString() : kind;
    if (object.contains(QStringLiteral("durationUs")))
        anim.durationUs = static_cast<TimeUs>(object.value(QStringLiteral("durationUs")).toDouble());
    else if (object.contains(QStringLiteral("duration")))
        anim.durationUs = secondsToUs(object.value(QStringLiteral("duration")).toDouble(0.5));
    if (object.contains(QStringLiteral("curve")))
        anim.curve = fadeCurveFromString(object.value(QStringLiteral("curve")).toString());
    else
        anim.curve = clipAnimEaseToCurve(clipAnimEaseFromString(object.value(QStringLiteral("ease")).toString()));
    if (object.contains(QStringLiteral("shape")))
        anim.shape = FadeShape::fromJson(object.value(QStringLiteral("shape")));
    anim.parameters = object.value(QStringLiteral("parameters")).toObject().toVariantMap();
    return anim;
}

// settled: 0 = fully away, 1 = in place. entering flips slide/spin departure direction.
static void applyKind(Builtin kind, double settled, bool entering, double layoutW, double layoutH,
               FadeCurve curve, const FadeShape &shape, ClipAnimSample *out)
{
    if (kind == Builtin::None || !out)
        return;

    const double a = shapedProgress(qBound(0.0, settled, 1.0), curve, shape);
    const double away = 1.0 - a;
    const double travelX = 0.35 * layoutW;
    const double travelY = 0.35 * layoutH;
    const double sign = entering ? 1.0 : -1.0;

    switch (kind) {
    case Builtin::None:
        break;
    case Builtin::SlideUp:
        out->dy += sign * away * travelY;
        break;
    case Builtin::SlideDown:
        out->dy -= sign * away * travelY;
        break;
    case Builtin::SlideLeft:
        out->dx += sign * away * travelX;
        break;
    case Builtin::SlideRight:
        out->dx -= sign * away * travelX;
        break;
    case Builtin::ZoomIn:
        out->scale *= 0.6 + 0.4 * a;
        break;
    case Builtin::ZoomOut:
        out->scale *= entering ? (1.4 - 0.4 * a) : (1.0 + 0.4 * away);
        break;
    case Builtin::Pop:
        out->scale *= 0.6 + 0.4 * a;
        break;
    case Builtin::SpinCW:
        out->rotationDeg += sign * away * -90.0;
        break;
    case Builtin::SpinCCW:
        out->rotationDeg += sign * away * 90.0;
        break;
    case Builtin::Bounce: {
        const double b = QEasingCurve(QEasingCurve::OutBounce).valueForProgress(a);
        out->dy += sign * (1.0 - b) * travelY;
        break;
    }
    }
}

ClipAnimSample evaluateClipAnimation(TimeUs timelineStart, TimeUs timelineDuration,
                                     const ClipAnimation &animIn, const ClipAnimation &animOut,
                                     TimeUs timelineUs, double layoutW, double layoutH)
{
    ClipAnimSample sample;
    if (timelineDuration <= 0)
        return sample;

    const TimeUs rel = qBound(TimeUs{0}, timelineUs - timelineStart, timelineDuration);

    if (animIn.durationUs > 0 && rel < animIn.durationUs) {
        const double settled = static_cast<double>(rel) / static_cast<double>(animIn.durationUs);
        applyKind(builtinFor(animIn.kindId), settled, true, layoutW, layoutH, animIn.curve, animIn.shape, &sample);
    }

    if (animOut.durationUs > 0) {
        const TimeUs fromEnd = timelineDuration - rel;
        if (fromEnd < animOut.durationUs) {
            const double settled =
                static_cast<double>(fromEnd) / static_cast<double>(animOut.durationUs);
            applyKind(builtinFor(animOut.kindId), settled, false, layoutW, layoutH, animOut.curve, animOut.shape,
                      &sample);
        }
    }

    return sample;
}

std::optional<double> clipAnimationProgress(TimeUs timelineStart, TimeUs timelineDuration,
                                            const ClipAnimation &anim, bool isIn, TimeUs timelineUs)
{
    if (anim.isNone() || timelineDuration <= 0)
        return std::nullopt;

    const TimeUs dur = std::min(anim.durationUs, timelineDuration);
    if (dur <= 0)
        return std::nullopt;

    const TimeUs rel = timelineUs - timelineStart;
    if (isIn) {
        if (rel < 0 || rel > dur)
            return std::nullopt;
        return shapedProgress(static_cast<double>(rel) / static_cast<double>(dur), anim.curve, anim.shape);
    }

    const TimeUs fromEnd = timelineDuration - rel;
    if (fromEnd < 0 || fromEnd > dur)
        return std::nullopt;
    // Mirrors applyKind's "away" amount, so a custom curve shapes the outro the same way.
    return 1.0 - shapedProgress(static_cast<double>(fromEnd) / static_cast<double>(dur), anim.curve, anim.shape);
}

} // namespace drift
