#include "Project.h"

#include "Clip.h"
#include "SubtitleCue.h"
#include "TimelineOps.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUuid>
#include <QtMath>

namespace drift {

namespace {

QJsonObject shapeStyleToJson(const ShapeStyle &s)
{
    return QJsonObject{
        {QStringLiteral("kind"), shapeKindToString(s.kind)},
        {QStringLiteral("fillKind"), shapeFillKindToString(s.fillKind)},
        {QStringLiteral("fill"), s.fill.name(QColor::HexArgb)},
        {QStringLiteral("fillSecondary"), s.fillSecondary.name(QColor::HexArgb)},
        {QStringLiteral("gradientAngle"), s.gradientAngle},
        {QStringLiteral("stroke"), s.stroke.name(QColor::HexArgb)},
        {QStringLiteral("strokeWidth"), s.strokeWidth},
        {QStringLiteral("strokeStyle"), shapeStrokeStyleToString(s.strokeStyle)},
        {QStringLiteral("cornerRadius"), s.cornerRadius},
        {QStringLiteral("points"), s.points},
        {QStringLiteral("innerRatio"), s.innerRatio},
        {QStringLiteral("headSize"), s.headSize},
        {QStringLiteral("thickness"), s.thickness},
        {QStringLiteral("tailX"), s.tailX},
        {QStringLiteral("tailSize"), s.tailSize},
    };
}

ShapeStyle shapeStyleFromJson(const QJsonObject &o)
{
    ShapeStyle s;
    if (o.isEmpty())
        return s;
    s.kind = shapeKindFromString(o.value(QStringLiteral("kind")).toString());
    // Everything below defaults to the struct value, so a project saved before shapes gained
    // gradients, dashes and geometry knobs still loads.
    s.fillKind = shapeFillKindFromString(
        o.value(QStringLiteral("fillKind")).toString(shapeFillKindToString(s.fillKind)));
    s.fill = QColor(o.value(QStringLiteral("fill")).toString(s.fill.name(QColor::HexArgb)));
    s.fillSecondary = QColor(
        o.value(QStringLiteral("fillSecondary")).toString(s.fillSecondary.name(QColor::HexArgb)));
    s.gradientAngle = o.value(QStringLiteral("gradientAngle")).toDouble(s.gradientAngle);
    s.stroke = QColor(o.value(QStringLiteral("stroke")).toString(s.stroke.name(QColor::HexArgb)));
    s.strokeWidth = o.value(QStringLiteral("strokeWidth")).toDouble(s.strokeWidth);
    s.strokeStyle = shapeStrokeStyleFromString(
        o.value(QStringLiteral("strokeStyle")).toString(shapeStrokeStyleToString(s.strokeStyle)));
    s.cornerRadius = o.value(QStringLiteral("cornerRadius")).toDouble(s.cornerRadius);
    s.points = o.value(QStringLiteral("points")).toInt(s.points);
    s.innerRatio = o.value(QStringLiteral("innerRatio")).toDouble(s.innerRatio);
    s.headSize = o.value(QStringLiteral("headSize")).toDouble(s.headSize);
    s.thickness = o.value(QStringLiteral("thickness")).toDouble(s.thickness);
    s.tailX = o.value(QStringLiteral("tailX")).toDouble(s.tailX);
    s.tailSize = o.value(QStringLiteral("tailSize")).toDouble(s.tailSize);
    return s;
}

QJsonObject maskToJson(const Mask &m)
{
    QJsonArray points;
    for (const QPointF &pt : m.points)
        points.append(QJsonArray{pt.x(), pt.y()});

    QJsonObject maskKeyframesJson;
    for (auto it = m.keyframes.constBegin(); it != m.keyframes.constEnd(); ++it) {
        if (it->isEmpty())
            continue;
        maskKeyframesJson.insert(it.key(), keyframesToJson(it.value()));
    }

    // One entry per shape key: [timeUs, [[x, y], ...]]. An array rather than an object because
    // the key is a time, and JSON object keys would force it through a string round trip.
    QJsonArray pathKeys;
    for (auto it = m.pathKeys.constBegin(); it != m.pathKeys.constEnd(); ++it) {
        QJsonArray shape;
        for (const QPointF &pt : it.value())
            shape.append(QJsonArray{pt.x(), pt.y()});
        pathKeys.append(QJsonArray{qint64(it.key()), shape});
    }

    return QJsonObject{
        {QStringLiteral("shape"), maskShapeToString(m.shape)},
        {QStringLiteral("op"), maskOpToString(m.op)},
        {QStringLiteral("enabled"), m.enabled},
        {QStringLiteral("name"), m.name},
        {QStringLiteral("x"), m.x},
        {QStringLiteral("y"), m.y},
        {QStringLiteral("w"), m.w},
        {QStringLiteral("h"), m.h},
        {QStringLiteral("rotation"), m.rotation},
        {QStringLiteral("feather"), m.feather},
        {QStringLiteral("invert"), m.invert},
        {QStringLiteral("points"), points},
        {QStringLiteral("mediaPath"), m.mediaPath},
        {QStringLiteral("mediaFgrPath"), m.mediaFgrPath},
        {QStringLiteral("mediaSrcOffsetUs"), qint64(m.mediaSrcOffsetUs)},
        {QStringLiteral("mediaFit"), maskMediaFitToString(m.mediaFit)},
        {QStringLiteral("mediaChannel"), maskMediaChannelToString(m.mediaChannel)},
        {QStringLiteral("mediaLoop"), m.mediaLoop},
        {QStringLiteral("keyframes"), maskKeyframesJson},
        {QStringLiteral("pathKeys"), pathKeys},
    };
}

Mask maskFromJson(const QJsonObject &o)
{
    Mask m;
    if (o.isEmpty())
        return m;
    const QString shapeName = o.value(QStringLiteral("shape")).toString();
    m.shape = maskShapeFromString(shapeName);
    m.x = o.value(QStringLiteral("x")).toDouble(m.x);
    m.y = o.value(QStringLiteral("y")).toDouble(m.y);
    m.w = o.value(QStringLiteral("w")).toDouble(m.w);
    m.h = o.value(QStringLiteral("h")).toDouble(m.h);
    m.rotation = o.value(QStringLiteral("rotation")).toDouble(m.rotation);
    m.feather = o.value(QStringLiteral("feather")).toDouble(m.feather);
    m.invert = o.value(QStringLiteral("invert")).toBool(m.invert);
    m.op = maskOpFromString(o.value(QStringLiteral("op")).toString());
    m.enabled = o.value(QStringLiteral("enabled")).toBool(m.enabled);
    m.name = o.value(QStringLiteral("name")).toString(m.name);
    // Media was called "matte" before v5 and only ever backed a segmentation cutout, so the old
    // keys map straight across.
    m.mediaPath = o.value(QStringLiteral("mediaPath"))
                      .toString(o.value(QStringLiteral("mattePath")).toString(m.mediaPath));
    m.mediaFgrPath = o.value(QStringLiteral("mediaFgrPath"))
                         .toString(o.value(QStringLiteral("matteFgrPath")).toString(m.mediaFgrPath));
    m.mediaSrcOffsetUs = TimeUs(
        o.value(QStringLiteral("mediaSrcOffsetUs"))
            .toInteger(o.value(QStringLiteral("matteSrcOffsetUs")).toInteger(m.mediaSrcOffsetUs)));
    m.mediaFit = maskMediaFitFromString(o.value(QStringLiteral("mediaFit")).toString());
    m.mediaChannel = maskMediaChannelFromString(o.value(QStringLiteral("mediaChannel")).toString());
    m.mediaLoop = o.value(QStringLiteral("mediaLoop")).toBool(m.mediaLoop);
    const QJsonArray points = o.value(QStringLiteral("points")).toArray();
    for (const QJsonValue &value : points) {
        const QJsonArray pair = value.toArray();
        if (pair.size() >= 2)
            m.points.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
    }

    const QJsonObject maskKeyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = maskKeyframesJson.constBegin(); it != maskKeyframesJson.constEnd(); ++it)
        m.keyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));

    for (const QJsonValue &value : o.value(QStringLiteral("pathKeys")).toArray()) {
        const QJsonArray entry = value.toArray();
        if (entry.size() < 2)
            continue;
        QVector<QPointF> shape;
        for (const QJsonValue &pointValue : entry.at(1).toArray()) {
            const QJsonArray pair = pointValue.toArray();
            if (pair.size() >= 2)
                shape.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
        }
        m.pathKeys.insert(TimeUs(entry.at(0).toInteger()), shape);
    }

    // A pre-v5 "matte" had no geometry: every consumer bailed out before reading the rect and
    // bound the coverage map over the whole frame. Media *is* placed by that rect, so the
    // serialized defaults (w = h = 0.6) would suddenly shrink an old cutout to 60% and crop the
    // subject. Full-frame is what it always rendered as.
    if (shapeName == QStringLiteral("matte")) {
        m.x = 0.5;
        m.y = 0.5;
        m.w = 1.0;
        m.h = 1.0;
        m.rotation = 0.0;
        m.feather = 0.0;
    }
    return m;
}

QJsonObject transitionToJson(const Transition &t)
{
    QJsonObject params;
    for (auto it = t.parameters.constBegin(); it != t.parameters.constEnd(); ++it)
        params.insert(it.key(), QJsonValue::fromVariant(it.value()));

    // "kind" holds the transition package id. The pre-shader enum serialized the same strings,
    // so projects written by older builds keep loading.
    QJsonObject o{
        {QStringLiteral("id"), t.id},
        {QStringLiteral("fromClipId"), t.fromClipId},
        {QStringLiteral("toClipId"), t.toClipId},
        {QStringLiteral("kind"), t.kindId},
        {QStringLiteral("parameters"), params},
        {QStringLiteral("durationUs"), static_cast<double>(t.durationUs)},
    };
    // Written only when set, so a project with no eased transition round-trips byte-identically
    // to what older builds produced.
    if (t.easingCurve != FadeCurve::Linear) {
        o.insert(QStringLiteral("easingCurve"), fadeCurveToString(t.easingCurve));
        if ((t.easingCurve == FadeCurve::Custom && !t.easingShape.isEmpty())
            || t.easingCurve == FadeCurve::Bezier) {
            o.insert(QStringLiteral("easingShape"), t.easingShape.toJson());
        }
    }
    return o;
}

Transition transitionFromJson(const QJsonObject &o)
{
    Transition t;
    if (o.isEmpty())
        return t;
    t.id = o.value(QStringLiteral("id")).toString();
    t.fromClipId = o.value(QStringLiteral("fromClipId")).toString();
    t.toClipId = o.value(QStringLiteral("toClipId")).toString();
    const QString kind = o.value(QStringLiteral("kind")).toString();
    if (!kind.isEmpty())
        t.kindId = kind;
    const QJsonObject params = o.value(QStringLiteral("parameters")).toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        t.parameters.insert(it.key(), it.value().toVariant());
    t.durationUs = static_cast<TimeUs>(o.value(QStringLiteral("durationUs")).toDouble(t.durationUs));
    if (o.contains(QStringLiteral("easingCurve")))
        t.easingCurve = fadeCurveFromString(o.value(QStringLiteral("easingCurve")).toString());
    t.easingShape = FadeShape::fromJson(o.value(QStringLiteral("easingShape")));
    return t;
}

QJsonObject backgroundToJson(const Background &bg)
{
    return QJsonObject{
        {QStringLiteral("kind"),
         bg.kind == BackgroundKind::Blur ? QStringLiteral("blur") : QStringLiteral("color")},
        {QStringLiteral("color"), bg.color.name(QColor::HexArgb)},
        {QStringLiteral("blurStrength"), bg.blurStrength},
    };
}

Background backgroundFromJson(const QJsonObject &o)
{
    Background bg;
    if (o.isEmpty())
        return bg; // old projects: default solid black
    bg.kind = o.value(QStringLiteral("kind")).toString() == QStringLiteral("blur")
                  ? BackgroundKind::Blur
                  : BackgroundKind::Color;
    bg.color = QColor(o.value(QStringLiteral("color")).toString(QStringLiteral("#ff000000")));
    bg.blurStrength = o.value(QStringLiteral("blurStrength")).toDouble(bg.blurStrength);
    return bg;
}

QJsonArray subtitleCuesToJson(const QList<SubtitleCue> &cues)
{
    QJsonArray array;
    for (const SubtitleCue &cue : cues) {
        array.append(QJsonObject{
            {QStringLiteral("startUs"), static_cast<double>(cue.startUs)},
            {QStringLiteral("endUs"), static_cast<double>(cue.endUs)},
            {QStringLiteral("text"), cue.text},
        });
    }
    return array;
}

QList<SubtitleCue> subtitleCuesFromJson(const QJsonArray &array)
{
    QList<SubtitleCue> cues;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        SubtitleCue cue;
        cue.startUs = static_cast<TimeUs>(object.value(QStringLiteral("startUs")).toDouble());
        cue.endUs = static_cast<TimeUs>(object.value(QStringLiteral("endUs")).toDouble());
        cue.text = object.value(QStringLiteral("text")).toString();
        cues.append(cue);
    }
    sortSubtitleCues(cues);
    return cues;
}

QJsonObject clipToJson(const Clip &clip)
{
    return QJsonObject{
        {QStringLiteral("id"), clip.id},
        {QStringLiteral("assetId"), clip.assetId},
        {QStringLiteral("linkId"), clip.linkId},
        {QStringLiteral("suppressEmbeddedAudio"), clip.suppressEmbeddedAudio},
        {QStringLiteral("audioStreamIndex"), clip.audioStreamIndex},
        {QStringLiteral("type"), clipTypeToString(clip.type)},
        {QStringLiteral("adjustmentKind"), adjustmentKindToString(clip.adjustmentKind)},
        {QStringLiteral("linkedClipId"), clip.linkedClipId},
        {QStringLiteral("name"), clip.name},
        {QStringLiteral("textContent"), clip.textContent},
        {QStringLiteral("textStyle"), textStyleToJson(clip.textStyle)},
        {QStringLiteral("subtitleCues"), subtitleCuesToJson(clip.subtitleCues)},
        {QStringLiteral("shapeStyle"), shapeStyleToJson(clip.shapeStyle)},
        {QStringLiteral("path"), clip.path},
        {QStringLiteral("thumbnailPath"), clip.thumbnailPath},
        {QStringLiteral("filmstripPath"), clip.filmstripPath},
        {QStringLiteral("emoji"), clip.emoji},
        {QStringLiteral("blendMode"), blendModeToString(clip.blendMode)},
        {QStringLiteral("speed"), clip.speed},
        {QStringLiteral("speedCurve"), clip.speedCurve.toJson()},
        {QStringLiteral("reverse"), clip.reverse},
        {QStringLiteral("flipH"), clip.flipH},
        {QStringLiteral("flipV"), clip.flipV},
        {QStringLiteral("mask"), maskToJson(clip.mask)},
        {QStringLiteral("faceTrackPath"), clip.faceTrackPath},
        {QStringLiteral("faceTrackSrcOffsetUs"), qint64(clip.faceTrackSrcOffsetUs)},
        {QStringLiteral("stabilizePath"), clip.stabilizePath},
        {QStringLiteral("stabilizeMode"), stabilizeModeToString(clip.stabilizeMode)},
        {QStringLiteral("stabilizeSmoothing"), clip.stabilizeSmoothing},
        {QStringLiteral("stabilizeTripod"), clip.stabilizeTripod},
        {QStringLiteral("stabilizeAppliedSmoothing"), clip.stabilizeAppliedSmoothing},
        {QStringLiteral("stabilizeAppliedTripod"), clip.stabilizeAppliedTripod},
        {QStringLiteral("stabilizeAppliedMode"), stabilizeModeToString(clip.stabilizeAppliedMode)},
        {QStringLiteral("stabilizeHasRestPose"), clip.stabilizeHasRestPose},
        {QStringLiteral("stabilizeRestX"), clip.stabilizeRestX},
        {QStringLiteral("stabilizeRestY"), clip.stabilizeRestY},
        {QStringLiteral("stabilizeRestW"), clip.stabilizeRestW},
        {QStringLiteral("stabilizeRestH"), clip.stabilizeRestH},
        {QStringLiteral("stabilizeRestRot"), clip.stabilizeRestRot},
        {QStringLiteral("fadeInUs"), static_cast<double>(clip.fadeInUs)},
        {QStringLiteral("fadeOutUs"), static_cast<double>(clip.fadeOutUs)},
        {QStringLiteral("fadeCurve"), fadeCurveToString(clip.fadeCurve)},
        {QStringLiteral("fadeShape"), clip.fadeShape.toJson()},
        {QStringLiteral("animIn"), clipAnimationToJson(clip.animIn)},
        {QStringLiteral("animOut"), clipAnimationToJson(clip.animOut)},
        {QStringLiteral("timelineStartUs"), static_cast<double>(clip.timelineStart)},
        {QStringLiteral("timelineDurationUs"), static_cast<double>(clip.timelineDuration)},
        {QStringLiteral("srcInUs"), static_cast<double>(clip.srcIn)},
        {QStringLiteral("srcOutUs"), static_cast<double>(clip.srcOut)},
        {QStringLiteral("volume"), keyframesToJson(clip.volume)},
        {QStringLiteral("pan"), clip.pan},
        {QStringLiteral("opacity"), keyframesToJson(clip.opacity)},
        {QStringLiteral("x"), keyframesToJson(clip.transformX)},
        {QStringLiteral("y"), keyframesToJson(clip.transformY)},
        {QStringLiteral("width"), keyframesToJson(clip.transformW)},
        {QStringLiteral("height"), keyframesToJson(clip.transformH)},
        {QStringLiteral("rotation"), keyframesToJson(clip.rotation)},
        {QStringLiteral("rotationCorrection"), clip.rotationCorrection},
        {QStringLiteral("effects"), effectsToJson(clip.effects)},
        {QStringLiteral("audioEffects"), effectsToJson(clip.audioEffects)},
    };
}

KeyframeTrack<double> singleKeyframe(double value)
{
    KeyframeTrack<double> track;
    track.setKeyframe(0, value);
    return track;
}

void applyLegacyFractionalLayout(Clip &clip, const KeyframeTrack<double> &posX,
                                 const KeyframeTrack<double> &posY, const KeyframeTrack<double> &scale,
                                 int canvasW, int canvasH)
{
    const double cx = (posX.isEmpty() ? 0.5 : posX.evaluateAt(0)) * canvasW;
    const double cy = (posY.isEmpty() ? 0.5 : posY.evaluateAt(0)) * canvasH;
    const double s = scale.isEmpty() ? 1.0 : scale.evaluateAt(0);
    const double w = qMax(1.0, canvasW * s);
    const double h = qMax(1.0, canvasH * s);
    clip.transformX = singleKeyframe(cx - w * 0.5);
    clip.transformY = singleKeyframe(cy - h * 0.5);
    clip.transformW = singleKeyframe(w);
    clip.transformH = singleKeyframe(h);

    // Preserve the shape of the primary legacy track. Only Hold actually survives the collapse
    // to a single key — with nothing to interpolate towards, Linear and Ease are the same
    // thing — but Hold means "stay here", which still reads on a lone key.
    if (!posX.isEmpty()) {
        const Interpolation mode = posX.easingAt(posX.keyframes().firstKey());
        for (KeyframeTrack<double> *kt :
             {&clip.transformX, &clip.transformY, &clip.transformW, &clip.transformH}) {
            if (!kt->isEmpty())
                kt->setEasing(kt->keyframes().firstKey(), mode);
        }
    }
}

Clip clipFromJsonV2(const QJsonObject &object, int canvasW = 1920, int canvasH = 1080)
{
    Clip clip;
    clip.id = object.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    clip.assetId = object.value(QStringLiteral("assetId")).toString();
    clip.linkId = object.value(QStringLiteral("linkId")).toString();
    clip.suppressEmbeddedAudio = object.value(QStringLiteral("suppressEmbeddedAudio")).toBool(false);
    clip.audioStreamIndex = object.value(QStringLiteral("audioStreamIndex")).toInt(0);
    clip.type = clipTypeFromString(object.value(QStringLiteral("type")).toString());
    clip.adjustmentKind =
        adjustmentKindFromString(object.value(QStringLiteral("adjustmentKind")).toString());
    clip.linkedClipId = object.value(QStringLiteral("linkedClipId")).toString();
    clip.name = object.value(QStringLiteral("name")).toString();
    clip.textContent = object.value(QStringLiteral("textContent")).toString();
    clip.textStyle = textStyleFromJson(object.value(QStringLiteral("textStyle")).toObject());
    clip.subtitleCues = subtitleCuesFromJson(object.value(QStringLiteral("subtitleCues")).toArray());
    clip.shapeStyle = shapeStyleFromJson(object.value(QStringLiteral("shapeStyle")).toObject());
    clip.path = object.value(QStringLiteral("path")).toString();
    clip.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
    clip.filmstripPath = object.value(QStringLiteral("filmstripPath")).toString();
    clip.emoji = object.value(QStringLiteral("emoji")).toString();
    clip.blendMode = blendModeFromString(object.value(QStringLiteral("blendMode")).toString());
    clip.speed = object.value(QStringLiteral("speed")).toDouble(1.0);
    clip.speedCurve = SpeedCurve::fromJson(object.value(QStringLiteral("speedCurve")).toArray());
    clip.reverse = object.value(QStringLiteral("reverse")).toBool(false);
    clip.flipH = object.value(QStringLiteral("flipH")).toBool(false);
    clip.flipV = object.value(QStringLiteral("flipV")).toBool(false);
    clip.mask = maskFromJson(object.value(QStringLiteral("mask")).toObject());
    clip.faceTrackPath = object.value(QStringLiteral("faceTrackPath")).toString();
    clip.faceTrackSrcOffsetUs =
        TimeUs(object.value(QStringLiteral("faceTrackSrcOffsetUs")).toInteger(0));
    clip.stabilizePath = object.value(QStringLiteral("stabilizePath")).toString();
    clip.stabilizeMode =
        stabilizeModeFromString(object.value(QStringLiteral("stabilizeMode")).toString());
    clip.stabilizeSmoothing = object.value(QStringLiteral("stabilizeSmoothing")).toInt(15);
    clip.stabilizeTripod = object.value(QStringLiteral("stabilizeTripod")).toBool(false);
    clip.stabilizeAppliedSmoothing = object.value(QStringLiteral("stabilizeAppliedSmoothing")).toInt(-1);
    clip.stabilizeAppliedTripod = object.value(QStringLiteral("stabilizeAppliedTripod")).toBool(false);
    clip.stabilizeAppliedMode =
        stabilizeModeFromString(object.value(QStringLiteral("stabilizeAppliedMode")).toString());
    clip.stabilizeHasRestPose = object.value(QStringLiteral("stabilizeHasRestPose")).toBool(false);
    clip.stabilizeRestX = object.value(QStringLiteral("stabilizeRestX")).toDouble(0.0);
    clip.stabilizeRestY = object.value(QStringLiteral("stabilizeRestY")).toDouble(0.0);
    clip.stabilizeRestW = object.value(QStringLiteral("stabilizeRestW")).toDouble(0.0);
    clip.stabilizeRestH = object.value(QStringLiteral("stabilizeRestH")).toDouble(0.0);
    clip.stabilizeRestRot = object.value(QStringLiteral("stabilizeRestRot")).toDouble(0.0);
    // Older projects stored a bake without recording which settings produced it.
    // Treat the current sliders as applied so the inspector does not warn spuriously.
    if (!clip.stabilizePath.isEmpty() && clip.stabilizeAppliedSmoothing < 0) {
        clip.stabilizeAppliedSmoothing = clip.stabilizeSmoothing;
        clip.stabilizeAppliedTripod = clip.stabilizeTripod;
        clip.stabilizeAppliedMode = StabilizeMode::Bake;
    }
    clip.fadeInUs = static_cast<TimeUs>(object.value(QStringLiteral("fadeInUs")).toDouble());
    clip.fadeOutUs = static_cast<TimeUs>(object.value(QStringLiteral("fadeOutUs")).toDouble());
    clip.fadeCurve = fadeCurveFromString(object.value(QStringLiteral("fadeCurve")).toString());
    clip.fadeShape = FadeShape::fromJson(object.value(QStringLiteral("fadeShape")));
    clip.animIn = clipAnimationFromJson(object.value(QStringLiteral("animIn")).toObject());
    clip.animOut = clipAnimationFromJson(object.value(QStringLiteral("animOut")).toObject());
    clip.timelineStart = static_cast<TimeUs>(object.value(QStringLiteral("timelineStartUs")).toDouble());
    clip.timelineDuration = static_cast<TimeUs>(object.value(QStringLiteral("timelineDurationUs")).toDouble());
    clip.srcIn = static_cast<TimeUs>(object.value(QStringLiteral("srcInUs")).toDouble());
    clip.srcOut = static_cast<TimeUs>(object.value(QStringLiteral("srcOutUs")).toDouble());
    if (object.value(QStringLiteral("volume")).isObject()) {
        clip.volume = keyframesFromJson(object.value(QStringLiteral("volume")).toObject());
    } else {
        clip.volume.setKeyframe(0, object.value(QStringLiteral("volume")).toDouble(1.0));
    }
    clip.pan = object.value(QStringLiteral("pan")).toDouble(0.0);
    clip.opacity = keyframesFromJson(object.value(QStringLiteral("opacity")).toObject());
    clip.rotation = keyframesFromJson(object.value(QStringLiteral("rotation")).toObject());
    clip.rotationCorrection = object.value(QStringLiteral("rotationCorrection")).toInt(0);
    clip.effects = effectsFromJson(object.value(QStringLiteral("effects")).toArray());
    clip.audioEffects = effectsFromJson(object.value(QStringLiteral("audioEffects")).toArray());

    const bool hasPixelLayout = object.value(QStringLiteral("x")).isObject()
                                || object.value(QStringLiteral("width")).isObject();
    if (hasPixelLayout) {
        clip.transformX = keyframesFromJson(object.value(QStringLiteral("x")).toObject());
        clip.transformY = keyframesFromJson(object.value(QStringLiteral("y")).toObject());
        clip.transformW = keyframesFromJson(object.value(QStringLiteral("width")).toObject());
        clip.transformH = keyframesFromJson(object.value(QStringLiteral("height")).toObject());
    } else {
        applyLegacyFractionalLayout(clip,
                                    keyframesFromJson(object.value(QStringLiteral("posX")).toObject()),
                                    keyframesFromJson(object.value(QStringLiteral("posY")).toObject()),
                                    keyframesFromJson(object.value(QStringLiteral("scale")).toObject()),
                                    qMax(1, canvasW), qMax(1, canvasH));
    }
    return clip;
}

Clip clipFromJsonV1(const QJsonObject &object, const QList<QString> &assetOrder)
{
    Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.name = object.value(QStringLiteral("name")).toString();
    clip.path = object.value(QStringLiteral("path")).toString();
    clip.type = clipTypeFromString(object.value(QStringLiteral("kind")).toString());
    clip.textContent = object.value(QStringLiteral("textContent")).toString();
    clip.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
    clip.filmstripPath = object.value(QStringLiteral("filmstripPath")).toString();
    clip.timelineStart = secondsToUs(object.value(QStringLiteral("start")).toDouble());
    clip.timelineDuration = secondsToUs(object.value(QStringLiteral("duration")).toDouble());
    clip.srcIn = secondsToUs(object.value(QStringLiteral("inPoint")).toDouble());
    clip.srcOut = secondsToUs(object.value(QStringLiteral("outPoint")).toDouble());

    const int assetIndex = object.value(QStringLiteral("assetIndex")).toInt(-1);
    if (assetIndex >= 0 && assetIndex < assetOrder.size())
        clip.assetId = assetOrder.at(assetIndex);

    return clip;
}

QJsonObject assetToJson(const MediaAsset &asset)
{
    QJsonObject object{
        {QStringLiteral("id"), asset.id},
        {QStringLiteral("name"), asset.name},
        {QStringLiteral("kind"), mediaKindToString(asset.kind)},
        {QStringLiteral("durationUs"), static_cast<double>(asset.durationUs)},
        {QStringLiteral("duration"), asset.durationLabel},
        {QStringLiteral("path"), asset.path},
        {QStringLiteral("width"), asset.width},
        {QStringLiteral("height"), asset.height},
        {QStringLiteral("fps"), asset.fps},
        {QStringLiteral("rotationDegrees"), asset.rotationDegrees},
        {QStringLiteral("rotationOverride"), asset.rotationOverride},
        {QStringLiteral("trimInUs"), static_cast<double>(asset.trimInUs)},
        {QStringLiteral("trimOutUs"), static_cast<double>(asset.trimOutUs)},
        {QStringLiteral("sampleRate"), asset.sampleRate},
        {QStringLiteral("channels"), asset.channels},
        {QStringLiteral("codecName"), asset.codecName},
        {QStringLiteral("thumbnailPath"), asset.thumbnailPath},
        {QStringLiteral("filmstripPath"), asset.filmstripPath},
    };
    if (asset.hasAudioKnown)
        object.insert(QStringLiteral("hasAudio"), asset.hasAudio);
    // Only when set, so a desktop project's JSON is byte-for-byte what it was before the key
    // existed. Older builds ignore the key; a project without it simply reads back empty.
    if (!asset.sourceUri.isEmpty())
        object.insert(QStringLiteral("sourceUri"), asset.sourceUri);
    if (!asset.folderId.isEmpty())
        object.insert(QStringLiteral("folderId"), asset.folderId);
    return object;
}

MediaAsset assetFromJsonV2(const QJsonObject &object)
{
    MediaAsset asset;
    asset.id = object.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    asset.name = object.value(QStringLiteral("name")).toString();
    asset.kind = mediaKindFromString(object.value(QStringLiteral("kind")).toString());
    asset.durationUs = static_cast<TimeUs>(object.value(QStringLiteral("durationUs")).toDouble());
    asset.durationLabel = object.value(QStringLiteral("duration")).toString();
    asset.path = object.value(QStringLiteral("path")).toString();
    asset.sourceUri = object.value(QStringLiteral("sourceUri")).toString();
    asset.width = object.value(QStringLiteral("width")).toInt();
    asset.height = object.value(QStringLiteral("height")).toInt();
    asset.fps = object.value(QStringLiteral("fps")).toDouble();
    asset.rotationDegrees = object.value(QStringLiteral("rotationDegrees")).toInt();
    asset.rotationOverride = object.value(QStringLiteral("rotationOverride")).toInt(-1);
    asset.trimInUs = static_cast<TimeUs>(object.value(QStringLiteral("trimInUs")).toDouble(0));
    asset.trimOutUs = static_cast<TimeUs>(object.value(QStringLiteral("trimOutUs")).toDouble(-1));
    asset.sampleRate = object.value(QStringLiteral("sampleRate")).toInt();
    asset.channels = object.value(QStringLiteral("channels")).toInt();
    asset.codecName = object.value(QStringLiteral("codecName")).toString();
    asset.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
    asset.filmstripPath = object.value(QStringLiteral("filmstripPath")).toString();
    asset.folderId = object.value(QStringLiteral("folderId")).toString();
    if (object.contains(QStringLiteral("hasAudio"))) {
        asset.hasAudioKnown = true;
        asset.hasAudio = object.value(QStringLiteral("hasAudio")).toBool();
    } else if (asset.channels > 0 || asset.sampleRate > 0) {
        asset.hasAudioKnown = true;
        asset.hasAudio = true;
    }
    return asset;
}

MediaAsset assetFromJsonV1(const QJsonObject &object)
{
    MediaAsset asset;
    asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    asset.name = object.value(QStringLiteral("name")).toString();
    asset.kind = mediaKindFromString(object.value(QStringLiteral("kind")).toString());
    asset.durationLabel = object.value(QStringLiteral("duration")).toString();
    asset.durationUs = secondsToUs(object.value(QStringLiteral("durationSeconds")).toDouble());
    asset.path = object.value(QStringLiteral("path")).toString();
    asset.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
    asset.filmstripPath = object.value(QStringLiteral("filmstripPath")).toString();
    return asset;
}

} // namespace

void Project::resetToDefaultTimeline()
{
    m_tracks = {
        {.type = TrackType::Video},
    };
    ensureTrackIds();
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_createdAt = QDateTime::currentDateTimeUtc();
    m_modifiedAt = m_createdAt;
}

void Project::ensureTrackIds()
{
    QSet<QString> seen;
    for (Track &track : m_tracks) {
        // A duplicated id is as bad as a missing one — a copy/paste of a whole track would
        // otherwise give two tracks the same parent handle.
        if (track.id.isEmpty() || seen.contains(track.id))
            track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        seen.insert(track.id);
    }

    for (Track &track : m_tracks) {
        if (track.parentTrackId.isEmpty())
            continue;
        // An orphaned lane becomes a standalone adjustment track rather than vanishing: losing
        // the parent must not silently delete the user's effects.
        if (!seen.contains(track.parentTrackId) || track.parentTrackId == track.id) {
            track.parentTrackId.clear();
            track.adjustmentScope = AdjustmentScope::AllBelow;
        }
    }
}

int Project::trackIndexById(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks.at(i).id == id)
            return i;
    }
    return -1;
}

TimeUs Project::durationUs() const
{
    TimeUs maxEnd = 0;
    for (const Track &track : m_tracks) {
        for (const Clip &clip : track.clips)
            maxEnd = qMax(maxEnd, clip.timelineEnd());
    }
    return maxEnd;
}

namespace {

void detachEffect(Effect &effect)
{
    effect.parameters.detach();
    for (auto it = effect.parameters.begin(); it != effect.parameters.end(); ++it)
        it.value().detach();
    effect.paramKeyframes.detach();
    for (auto it = effect.paramKeyframes.begin(); it != effect.paramKeyframes.end(); ++it)
        it.value().detachSharedData();
}

void detachClip(Clip &clip)
{
    clip.opacity.detachSharedData();
    clip.transformX.detachSharedData();
    clip.transformY.detachSharedData();
    clip.transformW.detachSharedData();
    clip.transformH.detachSharedData();
    clip.rotation.detachSharedData();
    clip.volume.detachSharedData();
    clip.speedCurve.detachSharedData();
    clip.mask.points.detach();
    clip.mask.pathKeys.detach();
    for (auto it = clip.mask.pathKeys.begin(); it != clip.mask.pathKeys.end(); ++it)
        it.value().detach();
    clip.mask.keyframes.detach();
    for (auto it = clip.mask.keyframes.begin(); it != clip.mask.keyframes.end(); ++it)
        it.value().detachSharedData();
    clip.subtitleCues.detach();
    clip.effects.detach();
    for (Effect &effect : clip.effects)
        detachEffect(effect);
    clip.audioEffects.detach();
    for (Effect &effect : clip.audioEffects)
        detachEffect(effect);
}

void detachTrack(Track &track)
{
    track.clips.detach();
    for (Clip &clip : track.clips)
        detachClip(clip);
    track.transitions.detach();
    for (Transition &transition : track.transitions) {
        transition.parameters.detach();
        for (auto it = transition.parameters.begin(); it != transition.parameters.end(); ++it)
            it.value().detach();
    }
}

} // namespace

Project Project::detachedCopy() const
{
    Project out = *this;
    out.m_tracks.detach();
    for (Track &track : out.m_tracks)
        detachTrack(track);
    out.m_bookmarks.detach();
    out.m_assetOrder.detach();
    out.m_assetsById.detach();
    out.m_binFolderOrder.detach();
    out.m_binFoldersById.detach();
    return out;
}

QString Project::addAsset(MediaAsset asset)
{
    if (asset.id.isEmpty())
        asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    m_assetsById.insert(asset.id, asset);
    if (!m_assetOrder.contains(asset.id))
        m_assetOrder.append(asset.id);
    return asset.id;
}

MediaAsset *Project::asset(const QString &id)
{
    auto it = m_assetsById.find(id);
    return it == m_assetsById.end() ? nullptr : &it.value();
}

const MediaAsset *Project::asset(const QString &id) const
{
    auto it = m_assetsById.constFind(id);
    return it == m_assetsById.constEnd() ? nullptr : &it.value();
}

int Project::assetIndex(const QString &id) const
{
    return m_assetOrder.indexOf(id);
}

QString Project::assetIdAt(int index) const
{
    if (index < 0 || index >= m_assetOrder.size())
        return {};
    return m_assetOrder.at(index);
}

QString Project::addBinFolder(BinFolder folder)
{
    if (folder.id.isEmpty())
        folder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    m_binFoldersById.insert(folder.id, folder);
    if (!m_binFolderOrder.contains(folder.id))
        m_binFolderOrder.append(folder.id);
    return folder.id;
}

BinFolder *Project::binFolder(const QString &id)
{
    auto it = m_binFoldersById.find(id);
    return it == m_binFoldersById.end() ? nullptr : &it.value();
}

const BinFolder *Project::binFolder(const QString &id) const
{
    auto it = m_binFoldersById.constFind(id);
    return it == m_binFoldersById.constEnd() ? nullptr : &it.value();
}

int Project::binFolderIndex(const QString &id) const
{
    return m_binFolderOrder.indexOf(id);
}

QString Project::binFolderIdAt(int index) const
{
    if (index < 0 || index >= m_binFolderOrder.size())
        return {};
    return m_binFolderOrder.at(index);
}

namespace {

} // namespace

Project Project::fromJson(const QJsonObject &object, QString *errorOut)
{
    const auto fail = [errorOut](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return Project{};
    };

    // The earliest format did not write this key, hence the default rather than a required field.
    const int version = object.value(QStringLiteral("version")).toInt(1);

    // Document format, which is bumped independently of the .drift container revision
    // ProjectBundle gates on — a newer document inside a 1.x container passes that check. Without
    // this, whatever the newer version added is dropped on read and can then be saved back over
    // the original, which looks like a successful open.
    if (version > kCurrentVersion) {
        return fail(QCoreApplication::translate(
            "Project",
            "This project was saved by a newer version of Drift "
            "(project format %1; this build reads up to %2).")
                        .arg(version)
                        .arg(kCurrentVersion));
    }

    // Every version writes a tracks array — an empty timeline included, as `[]`. Without this any
    // JSON object at all, `{}` included, parses into a plausible-looking empty project.
    if (!object.value(QStringLiteral("tracks")).isArray())
        return fail(QCoreApplication::translate("Project", "This file isn’t a Drift project."));

    Project project;

    project.setName(object.value(QStringLiteral("projectName")).toString(QStringLiteral("Untitled Project")));
    project.setFps(object.value(QStringLiteral("fps")).toInt(30));
    project.setResolution(object.value(QStringLiteral("width")).toInt(1920),
                          object.value(QStringLiteral("height")).toInt(1080));
    project.setSampleRate(object.value(QStringLiteral("sampleRate")).toInt(48000));
    project.setBackground(backgroundFromJson(object.value(QStringLiteral("background")).toObject()));

    const QJsonArray assetsArray = object.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue &value : assetsArray) {
        const QJsonObject assetObject = value.toObject();
        if (version >= 2)
            project.addAsset(assetFromJsonV2(assetObject));
        else
            project.addAsset(assetFromJsonV1(assetObject));
    }

    const QJsonArray binFoldersArray = object.value(QStringLiteral("binFolders")).toArray();
    for (const QJsonValue &value : binFoldersArray) {
        const QJsonObject folderObject = value.toObject();
        BinFolder folder;
        folder.id = folderObject.value(QStringLiteral("id")).toString();
        folder.name = folderObject.value(QStringLiteral("name")).toString();
        folder.parentId = folderObject.value(QStringLiteral("parentId")).toString();
        if (!folder.id.isEmpty())
            project.addBinFolder(folder);
    }

    // Rebuild tracks from JSON. The Project ctor seeds a 1-track default, so
    // clearing first is required — otherwise only the first saved track loads.
    project.m_tracks.clear();
    const QJsonArray tracksArray = object.value(QStringLiteral("tracks")).toArray();
    if (tracksArray.isEmpty()) {
        project.resetToDefaultTimeline();
    } else {
        project.m_tracks.reserve(tracksArray.size());
        for (const QJsonValue &value : tracksArray) {
            const QJsonObject trackObject = value.toObject();
            Track track;
            track.id = trackObject.value(QStringLiteral("id")).toString();
            track.type = trackTypeFromString(
                trackObject.value(QStringLiteral("type")).toString(QStringLiteral("video")));
            track.adjustmentScope = adjustmentScopeFromString(
                trackObject.value(QStringLiteral("adjustmentScope")).toString());
            track.parentTrackId = trackObject.value(QStringLiteral("parentTrackId")).toString();
            track.name = trackObject.value(QStringLiteral("name")).toString();
            track.muted = trackObject.value(QStringLiteral("muted")).toBool(false);
            track.hidden = trackObject.value(QStringLiteral("hidden")).toBool(false);
            track.locked = trackObject.value(QStringLiteral("locked")).toBool(false);
            track.showWaveform = trackObject.value(QStringLiteral("showWaveform")).toBool(false);
            track.showChannelWaveforms =
                trackObject.value(QStringLiteral("showChannelWaveforms")).toBool(false);
            track.heightScale = qBound(
                0.6, trackObject.value(QStringLiteral("heightScale")).toDouble(1.0), 4.0);

            const QJsonArray clipsArray = trackObject.value(QStringLiteral("clips")).toArray();
            for (const QJsonValue &clipValue : clipsArray) {
                const QJsonObject clipObject = clipValue.toObject();
                if (version >= 2)
                    track.clips.append(clipFromJsonV2(clipObject, project.width(), project.height()));
                else
                    track.clips.append(clipFromJsonV1(clipObject, project.m_assetOrder));
            }

            const QJsonArray transitionsArray = trackObject.value(QStringLiteral("transitions")).toArray();
            for (const QJsonValue &transitionValue : transitionsArray)
                track.transitions.append(transitionFromJson(transitionValue.toObject()));

            project.m_tracks.append(track);
        }
    }

    // Lanes address their parent by id, so ids must exist before the migration runs; the second
    // pass covers the tracks the migration itself mints.
    project.ensureTrackIds();
    if (version < 4) {
        liftAdjustmentClipsToOwnTracks(project);
        // Same pass the editor runs after every edit, so load and runtime cannot drift apart on
        // where a stack is allowed to live.
        hoistClipEffectsToAdjustmentLanes(project);
    }
    if (version < 5) {
        // Masks moved off the clip onto their own adjustment lane, so they became timed and
        // combinable. Runs after the v4 pass, which is what mints the track ids a lane needs.
        migrateClipMasksToAdjustmentLanes(project);
    }

    project.m_bookmarks.clear();
    const QJsonArray bookmarksArray = object.value(QStringLiteral("bookmarks")).toArray();
    for (const QJsonValue &value : bookmarksArray) {
        const QJsonObject bookmarkObject = value.toObject();
        Bookmark bookmark;
        if (version >= 2) {
            bookmark.timeUs = static_cast<TimeUs>(bookmarkObject.value(QStringLiteral("timeUs")).toDouble());
        } else {
            bookmark.timeUs = secondsToUs(bookmarkObject.value(QStringLiteral("seconds")).toDouble());
        }
        bookmark.label = bookmarkObject.value(QStringLiteral("label")).toString();
        project.m_bookmarks.append(bookmark);
    }

    project.clearWorkArea();
    if (object.contains(QStringLiteral("workAreaInUs"))) {
        project.setWorkAreaInUs(static_cast<TimeUs>(object.value(QStringLiteral("workAreaInUs")).toDouble(-1)));
        project.setWorkAreaOutUs(static_cast<TimeUs>(object.value(QStringLiteral("workAreaOutUs")).toDouble(-1)));
        if (!project.hasWorkArea())
            project.clearWorkArea();
    }

    // After the track block: an empty timeline routes through resetToDefaultTimeline(), which mints
    // a fresh id and timestamps. Read them last so a saved project keeps its own.
    const QString savedId = object.value(QStringLiteral("id")).toString();
    if (!savedId.isEmpty())
        project.m_id = savedId;
    project.m_author = object.value(QStringLiteral("author")).toString();
    project.m_description = object.value(QStringLiteral("description")).toString();
    const QDateTime created =
        QDateTime::fromString(object.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    if (created.isValid())
        project.m_createdAt = created;
    const QDateTime modified =
        QDateTime::fromString(object.value(QStringLiteral("modifiedAt")).toString(), Qt::ISODate);
    project.m_modifiedAt = modified.isValid() ? modified : project.m_createdAt;

    if (errorOut)
        errorOut->clear();
    return project;
}

QJsonObject Project::toJson() const
{
    QJsonArray assetsArray;
    for (const QString &id : m_assetOrder) {
        const MediaAsset *assetPtr = asset(id);
        if (assetPtr)
            assetsArray.append(assetToJson(*assetPtr));
    }

    QJsonArray binFoldersArray;
    for (const QString &id : m_binFolderOrder) {
        const BinFolder *folderPtr = binFolder(id);
        if (folderPtr) {
            binFoldersArray.append(QJsonObject{
                {QStringLiteral("id"), folderPtr->id},
                {QStringLiteral("name"), folderPtr->name},
                {QStringLiteral("parentId"), folderPtr->parentId},
            });
        }
    }

    QJsonArray tracksArray;
    for (const Track &track : m_tracks) {
        QJsonArray clipsArray;
        for (const Clip &clip : track.clips)
            clipsArray.append(clipToJson(clip));

        QJsonArray transitionsArray;
        for (const Transition &transition : track.transitions)
            transitionsArray.append(transitionToJson(transition));

        tracksArray.append(QJsonObject{
            {QStringLiteral("id"), track.id},
            {QStringLiteral("type"), trackTypeToString(track.type)},
            {QStringLiteral("adjustmentScope"), adjustmentScopeToString(track.adjustmentScope)},
            {QStringLiteral("parentTrackId"), track.parentTrackId},
            {QStringLiteral("name"), track.name},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("hidden"), track.hidden},
            {QStringLiteral("locked"), track.locked},
            {QStringLiteral("showWaveform"), track.showWaveform},
            {QStringLiteral("showChannelWaveforms"), track.showChannelWaveforms},
            {QStringLiteral("heightScale"), track.heightScale},
            {QStringLiteral("clips"), clipsArray},
            {QStringLiteral("transitions"), transitionsArray},
        });
    }

    QJsonArray bookmarksArray;
    for (const Bookmark &bookmark : m_bookmarks) {
        bookmarksArray.append(QJsonObject{
            {QStringLiteral("timeUs"), static_cast<double>(bookmark.timeUs)},
            {QStringLiteral("label"), bookmark.label},
        });
    }

    QJsonObject root{
        {QStringLiteral("version"), kCurrentVersion},
        {QStringLiteral("projectName"), m_name},
        {QStringLiteral("id"), m_id},
        {QStringLiteral("author"), m_author},
        {QStringLiteral("description"), m_description},
        {QStringLiteral("createdAt"), m_createdAt.toString(Qt::ISODate)},
        {QStringLiteral("modifiedAt"), m_modifiedAt.toString(Qt::ISODate)},
        {QStringLiteral("fps"), m_fps},
        {QStringLiteral("width"), m_width},
        {QStringLiteral("height"), m_height},
        {QStringLiteral("sampleRate"), m_sampleRate},
        {QStringLiteral("assets"), assetsArray},
        {QStringLiteral("binFolders"), binFoldersArray},
        {QStringLiteral("tracks"), tracksArray},
        {QStringLiteral("bookmarks"), bookmarksArray},
        {QStringLiteral("background"), backgroundToJson(m_background)},
    };
    if (hasWorkArea()) {
        root.insert(QStringLiteral("workAreaInUs"), static_cast<double>(m_workAreaInUs));
        root.insert(QStringLiteral("workAreaOutUs"), static_cast<double>(m_workAreaOutUs));
    }
    return root;
}

QByteArray Project::toCompactJson() const
{
    return QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
}

QString Project::contentHash() const
{
    return QString::fromLatin1(
        QCryptographicHash::hash(toCompactJson(), QCryptographicHash::Sha256).toHex());
}

} // namespace drift
