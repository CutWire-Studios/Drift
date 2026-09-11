#include "ClipAnimation.h"

#include <QEasingCurve>

#include <algorithm>
#include <cmath>

namespace drift {

QString clipAnimKindToString(ClipAnimKind kind)
{
    switch (kind) {
    case ClipAnimKind::None:
        return QStringLiteral("none");
    case ClipAnimKind::Fade:
        return QStringLiteral("fade");
    case ClipAnimKind::SlideUp:
        return QStringLiteral("slideUp");
    case ClipAnimKind::SlideDown:
        return QStringLiteral("slideDown");
    case ClipAnimKind::SlideLeft:
        return QStringLiteral("slideLeft");
    case ClipAnimKind::SlideRight:
        return QStringLiteral("slideRight");
    case ClipAnimKind::ZoomIn:
        return QStringLiteral("zoomIn");
    case ClipAnimKind::ZoomOut:
        return QStringLiteral("zoomOut");
    case ClipAnimKind::Pop:
        return QStringLiteral("pop");
    case ClipAnimKind::SpinCW:
        return QStringLiteral("spinCW");
    case ClipAnimKind::SpinCCW:
        return QStringLiteral("spinCCW");
    case ClipAnimKind::Bounce:
        return QStringLiteral("bounce");
    }
    return QStringLiteral("none");
}

ClipAnimKind clipAnimKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("fade"))
        return ClipAnimKind::Fade;
    if (kind == QStringLiteral("slideUp"))
        return ClipAnimKind::SlideUp;
    if (kind == QStringLiteral("slideDown"))
        return ClipAnimKind::SlideDown;
    if (kind == QStringLiteral("slideLeft"))
        return ClipAnimKind::SlideLeft;
    if (kind == QStringLiteral("slideRight"))
        return ClipAnimKind::SlideRight;
    if (kind == QStringLiteral("zoomIn"))
        return ClipAnimKind::ZoomIn;
    if (kind == QStringLiteral("zoomOut"))
        return ClipAnimKind::ZoomOut;
    if (kind == QStringLiteral("pop"))
        return ClipAnimKind::Pop;
    if (kind == QStringLiteral("spinCW"))
        return ClipAnimKind::SpinCW;
    if (kind == QStringLiteral("spinCCW"))
        return ClipAnimKind::SpinCCW;
    if (kind == QStringLiteral("bounce"))
        return ClipAnimKind::Bounce;
    return ClipAnimKind::None;
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

ClipAnimEase clipAnimCurveToEase(FadeCurve curve)
{
    switch (curve) {
    case FadeCurve::Linear:
        return ClipAnimEase::Linear;
    case FadeCurve::Smooth:
        return ClipAnimEase::EaseInOut;
    case FadeCurve::EqualPower:
        return ClipAnimEase::EaseOut;
    case FadeCurve::Custom:
        return ClipAnimEase::EaseInOut;
    case FadeCurve::Bezier:
        // The legacy ease enum predates both hand-drawn modes and has no member for either.
        return ClipAnimEase::EaseInOut;
    }
    return ClipAnimEase::EaseOut;
}

QJsonObject clipAnimationToJson(const ClipAnimation &anim)
{
    QJsonObject object{
        {QStringLiteral("kind"), clipAnimKindToString(anim.kind)},
        {QStringLiteral("durationUs"), static_cast<double>(anim.durationUs)},
        {QStringLiteral("ease"), clipAnimEaseToString(anim.ease)},
        {QStringLiteral("curve"), fadeCurveToString(anim.curve)},
    };
    if (anim.curve == FadeCurve::Custom && !anim.shape.isEmpty())
        object.insert(QStringLiteral("shape"), anim.shape.toJson());
    return object;
}

ClipAnimation clipAnimationFromJson(const QJsonObject &object)
{
    ClipAnimation anim;
    anim.kind = clipAnimKindFromString(object.value(QStringLiteral("kind")).toString());
    if (object.contains(QStringLiteral("durationUs")))
        anim.durationUs = static_cast<TimeUs>(object.value(QStringLiteral("durationUs")).toDouble());
    else if (object.contains(QStringLiteral("duration")))
        anim.durationUs = secondsToUs(object.value(QStringLiteral("duration")).toDouble(0.5));
    anim.ease = clipAnimEaseFromString(object.value(QStringLiteral("ease")).toString());
    if (object.contains(QStringLiteral("curve"))) {
        anim.curve = fadeCurveFromString(object.value(QStringLiteral("curve")).toString());
    } else {
        // Older projects only stored ease — map into the shared style system.
        anim.curve = clipAnimEaseToCurve(anim.ease);
    }
    if (object.contains(QStringLiteral("shape")))
        anim.shape = FadeShape::fromJson(object.value(QStringLiteral("shape")));
    anim.ease = clipAnimCurveToEase(anim.curve);
    return anim;
}

// settled: 0 = fully away, 1 = in place. entering flips slide/spin departure direction.
void applyKind(ClipAnimKind kind, double settled, bool entering, double layoutW, double layoutH,
               FadeCurve curve, const FadeShape &shape, ClipAnimSample *out)
{
    if (kind == ClipAnimKind::None || kind == ClipAnimKind::Fade || !out)
        return;

    const double a = shapedProgress(qBound(0.0, settled, 1.0), curve, shape);
    const double away = 1.0 - a;
    const double travelX = 0.35 * layoutW;
    const double travelY = 0.35 * layoutH;
    const double sign = entering ? 1.0 : -1.0;

    switch (kind) {
    case ClipAnimKind::None:
    case ClipAnimKind::Fade:
        break;
    case ClipAnimKind::SlideUp:
        out->dy += sign * away * travelY;
        out->opacity *= a;
        break;
    case ClipAnimKind::SlideDown:
        out->dy -= sign * away * travelY;
        out->opacity *= a;
        break;
    case ClipAnimKind::SlideLeft:
        out->dx += sign * away * travelX;
        out->opacity *= a;
        break;
    case ClipAnimKind::SlideRight:
        out->dx -= sign * away * travelX;
        out->opacity *= a;
        break;
    case ClipAnimKind::ZoomIn:
        out->scale *= 0.6 + 0.4 * a;
        out->opacity *= a;
        break;
    case ClipAnimKind::ZoomOut:
        out->scale *= entering ? (1.4 - 0.4 * a) : (1.0 + 0.4 * away);
        out->opacity *= a;
        break;
    case ClipAnimKind::Pop:
        out->scale *= 0.6 + 0.4 * a;
        out->opacity *= qBound(0.0, a, 1.0);
        break;
    case ClipAnimKind::SpinCW:
        out->rotationDeg += sign * away * -90.0;
        out->opacity *= a;
        break;
    case ClipAnimKind::SpinCCW:
        out->rotationDeg += sign * away * 90.0;
        out->opacity *= a;
        break;
    case ClipAnimKind::Bounce: {
        const double b = QEasingCurve(QEasingCurve::OutBounce).valueForProgress(a);
        out->dy += sign * (1.0 - b) * travelY;
        out->opacity *= qBound(0.0, a * 4.0, 1.0);
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

    if (animIn.kind != ClipAnimKind::None && animIn.kind != ClipAnimKind::Fade
        && animIn.durationUs > 0 && rel < animIn.durationUs) {
        const double settled = static_cast<double>(rel) / static_cast<double>(animIn.durationUs);
        applyKind(animIn.kind, settled, true, layoutW, layoutH, animIn.curve, animIn.shape, &sample);
    }

    if (animOut.kind != ClipAnimKind::None && animOut.kind != ClipAnimKind::Fade
        && animOut.durationUs > 0) {
        const TimeUs fromEnd = timelineDuration - rel;
        if (fromEnd < animOut.durationUs) {
            const double settled =
                static_cast<double>(fromEnd) / static_cast<double>(animOut.durationUs);
            applyKind(animOut.kind, settled, false, layoutW, layoutH, animOut.curve, animOut.shape,
                      &sample);
        }
    }

    return sample;
}

} // namespace drift
