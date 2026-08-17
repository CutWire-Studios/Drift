#include "Project.h"

#include "Clip.h"
#include "SubtitleCue.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QtMath>

namespace drift {

namespace {

QJsonObject keyframesToJson(const KeyframeTrack<double> &track)
{
    QJsonArray keyframes;
    for (auto it = track.keyframes().constBegin(); it != track.keyframes().constEnd(); ++it) {
        const Keyframe<double> &key = it.value();
        QJsonObject object{
            {QStringLiteral("timeUs"), static_cast<double>(it.key())},
            {QStringLiteral("value"), key.value},
        };
        // Tangents are omitted when they are the straight-line default, which keeps files
        // written by the common case no larger than they were before handles existed.
        if (!qFuzzyIsNull(key.inDx) || !qFuzzyIsNull(key.inDy) || !qFuzzyIsNull(key.outDx)
            || !qFuzzyIsNull(key.outDy)) {
            object.insert(QStringLiteral("inDx"), key.inDx);
            object.insert(QStringLiteral("inDy"), key.inDy);
            object.insert(QStringLiteral("outDx"), key.outDx);
            object.insert(QStringLiteral("outDy"), key.outDy);
        }
        if (key.corner)
            object.insert(QStringLiteral("corner"), true);
        if (key.hold)
            object.insert(QStringLiteral("hold"), true);
        keyframes.append(object);
    }
    QJsonObject out{{QStringLiteral("keyframes"), keyframes}};
    // Written only when switched off, so files from the common case are byte-identical to before.
    if (!track.enabled())
        out.insert(QStringLiteral("enabled"), false);
    return out;
}

KeyframeTrack<double> keyframesFromJson(const QJsonObject &object)
{
    KeyframeTrack<double> track;
    track.setEnabled(object.value(QStringLiteral("enabled")).toBool(true));

    // Projects written before keyframes had tangents carry one interpolation mode for the
    // whole track. Both legacy shapes are reproduced exactly by handles — Linear by
    // zero-length ones, Ease by flat tangents at a third of each gap — so the migration is
    // applied after loading, once every neighbour is known, and changes nothing on screen.
    const QString legacyMode = object.value(QStringLiteral("interpolation")).toString();

    for (const QJsonValue &value : object.value(QStringLiteral("keyframes")).toArray()) {
        const QJsonObject keyframe = value.toObject();
        Keyframe<double> key;
        key.value = keyframe.value(QStringLiteral("value")).toDouble(1.0);
        key.inDx = keyframe.value(QStringLiteral("inDx")).toDouble(0.0);
        key.inDy = keyframe.value(QStringLiteral("inDy")).toDouble(0.0);
        key.outDx = keyframe.value(QStringLiteral("outDx")).toDouble(0.0);
        key.outDy = keyframe.value(QStringLiteral("outDy")).toDouble(0.0);
        key.corner = keyframe.value(QStringLiteral("corner")).toBool(false);
        key.hold = keyframe.value(QStringLiteral("hold")).toBool(false);
        track.setKeyframe(static_cast<TimeUs>(keyframe.value(QStringLiteral("timeUs")).toDouble()),
                          key);
    }

    if (!legacyMode.isEmpty()) {
        const Interpolation mode = interpolationFromString(legacyMode);
        if (mode != Interpolation::Linear) {
            const QList<TimeUs> times = track.keyframes().keys();
            for (TimeUs at : times)
                track.setEasing(at, mode);
        }
    }
    return track;
}

QJsonArray effectsToJson(const QList<Effect> &effects)
{
    QJsonArray array;
    for (const Effect &effect : effects) {
        QJsonObject params;
        for (auto it = effect.parameters.constBegin(); it != effect.parameters.constEnd(); ++it)
            params.insert(it.key(), QJsonValue::fromVariant(it.value()));
        QJsonObject paramKeyframes;
        for (auto it = effect.paramKeyframes.constBegin(); it != effect.paramKeyframes.constEnd(); ++it) {
            // Tangents live on the keys now, so an empty track no longer carries a user choice
            // worth persisting the way a track-wide interpolation mode used to.
            if (!it->isEmpty())
                paramKeyframes.insert(it.key(), keyframesToJson(it.value()));
        }
        QJsonObject object{
            {QStringLiteral("name"), effect.name},
            {QStringLiteral("catalogId"), effect.catalogId},
            {QStringLiteral("parameters"), params},
        };
        if (!effect.enabled)
            object.insert(QStringLiteral("enabled"), false);
        if (!paramKeyframes.isEmpty())
            object.insert(QStringLiteral("paramKeyframes"), paramKeyframes);
        array.append(object);
    }
    return array;
}

QList<Effect> effectsFromJson(const QJsonArray &array)
{
    QList<Effect> effects;
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        Effect effect;
        effect.name = object.value(QStringLiteral("name")).toString();
        effect.catalogId = object.value(QStringLiteral("catalogId")).toString();
        effect.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        const QJsonObject params = object.value(QStringLiteral("parameters")).toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            effect.parameters.insert(it.key(), it.value().toVariant());
        const QJsonObject paramKeyframes = object.value(QStringLiteral("paramKeyframes")).toObject();
        for (auto it = paramKeyframes.constBegin(); it != paramKeyframes.constEnd(); ++it)
            effect.paramKeyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));
        effects.append(effect);
    }
    return effects;
}

QJsonObject textHighlightToJson(const TextHighlight &h)
{
    return QJsonObject{
        {QStringLiteral("enabled"), h.enabled},
        {QStringLiteral("color"), h.color.name(QColor::HexArgb)},
        {QStringLiteral("padding"), h.padding},
        {QStringLiteral("radius"), h.radius},
    };
}

TextHighlight textHighlightFromJson(const QJsonObject &o, const TextHighlight &fallback)
{
    TextHighlight h = fallback;
    if (o.isEmpty())
        return h;
    h.enabled = o.value(QStringLiteral("enabled")).toBool(h.enabled);
    h.color = QColor(o.value(QStringLiteral("color")).toString(h.color.name(QColor::HexArgb)));
    h.padding = o.value(QStringLiteral("padding")).toDouble(h.padding);
    h.radius = o.value(QStringLiteral("radius")).toDouble(h.radius);
    return h;
}

QJsonObject wordAccentToJson(const WordAccent &a)
{
    return QJsonObject{
        {QStringLiteral("rule"), wordAccentRuleToString(a.rule)},
        {QStringLiteral("n"), a.n},
        {QStringLiteral("phase"), a.phase},
        {QStringLiteral("colorEnabled"), a.colorEnabled},
        {QStringLiteral("color"), a.color.name(QColor::HexArgb)},
        {QStringLiteral("sizeScale"), a.sizeScale},
        {QStringLiteral("outlineEnabled"), a.outlineEnabled},
        {QStringLiteral("outlineWidth"), a.outlineWidth},
        {QStringLiteral("outlineColor"), a.outlineColor.name(QColor::HexArgb)},
        {QStringLiteral("highlight"), textHighlightToJson(a.highlight)},
    };
}

WordAccent wordAccentFromJson(const QJsonObject &o)
{
    WordAccent a;
    if (o.isEmpty())
        return a; // projects predating style packs: no accent at all
    a.rule = wordAccentRuleFromString(o.value(QStringLiteral("rule")).toString());
    a.n = o.value(QStringLiteral("n")).toInt(a.n);
    a.phase = o.value(QStringLiteral("phase")).toInt(a.phase);
    a.colorEnabled = o.value(QStringLiteral("colorEnabled")).toBool(a.colorEnabled);
    a.color = QColor(o.value(QStringLiteral("color")).toString(a.color.name(QColor::HexArgb)));
    a.sizeScale = o.value(QStringLiteral("sizeScale")).toDouble(a.sizeScale);
    a.outlineEnabled = o.value(QStringLiteral("outlineEnabled")).toBool(a.outlineEnabled);
    a.outlineWidth = o.value(QStringLiteral("outlineWidth")).toDouble(a.outlineWidth);
    a.outlineColor = QColor(o.value(QStringLiteral("outlineColor")).toString(a.outlineColor.name(QColor::HexArgb)));
    a.highlight = textHighlightFromJson(o.value(QStringLiteral("highlight")).toObject(), a.highlight);
    return a;
}

QJsonObject textStyleToJson(const TextStyle &s)
{
    return QJsonObject{
        {QStringLiteral("packId"), s.packId},
        {QStringLiteral("fontFamily"), s.fontFamily},
        {QStringLiteral("pixelSize"), s.pixelSize},
        {QStringLiteral("fontWeight"), s.fontWeight},
        {QStringLiteral("italic"), s.italic},
        {QStringLiteral("color"), s.color.name(QColor::HexArgb)},
        {QStringLiteral("align"), textAlignToString(s.align)},
        {QStringLiteral("valign"), textVAlignToString(s.valign)},
        {QStringLiteral("wordWrap"), s.wordWrap},
        {QStringLiteral("lineHeight"), s.lineHeight},
        {QStringLiteral("letterSpacing"), s.letterSpacing},
        {QStringLiteral("outlineEnabled"), s.outlineEnabled},
        {QStringLiteral("outlineWidth"), s.outlineWidth},
        {QStringLiteral("outlineColor"), s.outlineColor.name(QColor::HexArgb)},
        {QStringLiteral("shadowEnabled"), s.shadowEnabled},
        {QStringLiteral("shadowOffsetX"), s.shadowOffsetX},
        {QStringLiteral("shadowOffsetY"), s.shadowOffsetY},
        {QStringLiteral("shadowBlur"), s.shadowBlur},
        {QStringLiteral("shadowOpacity"), s.shadowOpacity},
        {QStringLiteral("shadowColor"), s.shadowColor.name(QColor::HexArgb)},
        {QStringLiteral("glowEnabled"), s.glowEnabled},
        {QStringLiteral("glowColor"), s.glowColor.name(QColor::HexArgb)},
        {QStringLiteral("glowRadius"), s.glowRadius},
        {QStringLiteral("glowOpacity"), s.glowOpacity},
        {QStringLiteral("boxEnabled"), s.boxEnabled},
        {QStringLiteral("boxColor"), s.boxColor.name(QColor::HexArgb)},
        {QStringLiteral("boxPadding"), s.boxPadding},
        {QStringLiteral("boxRadius"), s.boxRadius},
        {QStringLiteral("wordHighlight"), textHighlightToJson(s.wordHighlight)},
        {QStringLiteral("underlineEnabled"), s.underlineEnabled},
        {QStringLiteral("underlineColor"), s.underlineColor.name(QColor::HexArgb)},
        {QStringLiteral("underlineWidth"), s.underlineWidth},
        {QStringLiteral("underlineOffset"), s.underlineOffset},
        {QStringLiteral("accent"), wordAccentToJson(s.accent)},
        {QStringLiteral("animInKind"), textAnimKindToString(s.animIn.kind)},
        {QStringLiteral("animInDurationUs"), static_cast<qint64>(s.animIn.durationUs)},
        {QStringLiteral("animInEase"), textEaseToString(s.animIn.ease)},
        {QStringLiteral("animInUnit"), textAnimUnitToString(s.animIn.unit)},
        {QStringLiteral("animInStaggerUs"), static_cast<qint64>(s.animIn.staggerUs)},
        {QStringLiteral("animInOrder"), textAnimOrderToString(s.animIn.order)},
        {QStringLiteral("animOutKind"), textAnimKindToString(s.animOut.kind)},
        {QStringLiteral("animOutDurationUs"), static_cast<qint64>(s.animOut.durationUs)},
        {QStringLiteral("animOutEase"), textEaseToString(s.animOut.ease)},
        {QStringLiteral("animOutUnit"), textAnimUnitToString(s.animOut.unit)},
        {QStringLiteral("animOutStaggerUs"), static_cast<qint64>(s.animOut.staggerUs)},
        {QStringLiteral("animOutOrder"), textAnimOrderToString(s.animOut.order)},
    };
}

TextStyle textStyleFromJson(const QJsonObject &o)
{
    TextStyle s;
    if (o.isEmpty())
        return s; // old projects: keep defaults
    s.packId = o.value(QStringLiteral("packId")).toString(s.packId);
    s.fontFamily = o.value(QStringLiteral("fontFamily")).toString(s.fontFamily);
    s.pixelSize = o.value(QStringLiteral("pixelSize")).toInt(s.pixelSize);
    // Projects written before the weight ladder only had a bold flag.
    if (o.contains(QStringLiteral("fontWeight")))
        s.fontWeight = qBound(100, o.value(QStringLiteral("fontWeight")).toInt(s.fontWeight), 900);
    else
        s.fontWeight = o.value(QStringLiteral("bold")).toBool(true) ? 700 : 400;
    s.italic = o.value(QStringLiteral("italic")).toBool(s.italic);
    s.color = QColor(o.value(QStringLiteral("color")).toString(s.color.name(QColor::HexArgb)));
    s.align = textAlignFromString(o.value(QStringLiteral("align")).toString());
    s.valign = textVAlignFromString(o.value(QStringLiteral("valign")).toString());
    s.wordWrap = o.value(QStringLiteral("wordWrap")).toBool(s.wordWrap);
    s.lineHeight = o.value(QStringLiteral("lineHeight")).toDouble(s.lineHeight);
    s.letterSpacing = o.value(QStringLiteral("letterSpacing")).toDouble(s.letterSpacing);
    s.outlineWidth = o.value(QStringLiteral("outlineWidth")).toDouble(s.outlineWidth);
    s.outlineColor = QColor(o.value(QStringLiteral("outlineColor")).toString(s.outlineColor.name(QColor::HexArgb)));
    // Projects written before outlineEnabled treated any positive width as on.
    if (o.contains(QStringLiteral("outlineEnabled")))
        s.outlineEnabled = o.value(QStringLiteral("outlineEnabled")).toBool(s.outlineEnabled);
    else
        s.outlineEnabled = s.outlineWidth > 0.0;
    s.shadowEnabled = o.value(QStringLiteral("shadowEnabled")).toBool(s.shadowEnabled);
    s.shadowOffsetX = o.value(QStringLiteral("shadowOffsetX")).toDouble(s.shadowOffsetX);
    s.shadowOffsetY = o.value(QStringLiteral("shadowOffsetY")).toDouble(s.shadowOffsetY);
    s.shadowBlur = o.value(QStringLiteral("shadowBlur")).toDouble(s.shadowBlur);
    s.shadowOpacity = o.value(QStringLiteral("shadowOpacity")).toDouble(s.shadowOpacity);
    s.shadowColor = QColor(o.value(QStringLiteral("shadowColor")).toString(s.shadowColor.name(QColor::HexArgb)));
    s.glowEnabled = o.value(QStringLiteral("glowEnabled")).toBool(s.glowEnabled);
    s.glowColor = QColor(o.value(QStringLiteral("glowColor")).toString(s.glowColor.name(QColor::HexArgb)));
    s.glowRadius = o.value(QStringLiteral("glowRadius")).toDouble(s.glowRadius);
    s.glowOpacity = o.value(QStringLiteral("glowOpacity")).toDouble(s.glowOpacity);
    s.boxEnabled = o.value(QStringLiteral("boxEnabled")).toBool(s.boxEnabled);
    s.boxColor = QColor(o.value(QStringLiteral("boxColor")).toString(s.boxColor.name(QColor::HexArgb)));
    s.boxPadding = o.value(QStringLiteral("boxPadding")).toDouble(s.boxPadding);
    s.boxRadius = o.value(QStringLiteral("boxRadius")).toDouble(s.boxRadius);
    s.wordHighlight =
        textHighlightFromJson(o.value(QStringLiteral("wordHighlight")).toObject(), s.wordHighlight);
    s.underlineEnabled = o.value(QStringLiteral("underlineEnabled")).toBool(s.underlineEnabled);
    s.underlineColor = QColor(o.value(QStringLiteral("underlineColor")).toString(s.underlineColor.name(QColor::HexArgb)));
    s.underlineWidth = o.value(QStringLiteral("underlineWidth")).toDouble(s.underlineWidth);
    s.underlineOffset = o.value(QStringLiteral("underlineOffset")).toDouble(s.underlineOffset);
    s.accent = wordAccentFromJson(o.value(QStringLiteral("accent")).toObject());
    s.animIn.kind = textAnimKindFromString(o.value(QStringLiteral("animInKind")).toString());
    s.animIn.durationUs = o.value(QStringLiteral("animInDurationUs")).toInteger(s.animIn.durationUs);
    s.animIn.ease = textEaseFromString(o.value(QStringLiteral("animInEase")).toString());
    // Missing keys keep the Block/default reveal, so projects predating per-span animation are
    // deserialized identically to how they render today.
    s.animIn.unit = textAnimUnitFromString(o.value(QStringLiteral("animInUnit")).toString());
    s.animIn.staggerUs = o.value(QStringLiteral("animInStaggerUs")).toInteger(s.animIn.staggerUs);
    s.animIn.order = textAnimOrderFromString(o.value(QStringLiteral("animInOrder")).toString());
    s.animOut.kind = textAnimKindFromString(o.value(QStringLiteral("animOutKind")).toString());
    s.animOut.durationUs = o.value(QStringLiteral("animOutDurationUs")).toInteger(s.animOut.durationUs);
    s.animOut.ease = textEaseFromString(o.value(QStringLiteral("animOutEase")).toString());
    s.animOut.unit = textAnimUnitFromString(o.value(QStringLiteral("animOutUnit")).toString());
    s.animOut.staggerUs = o.value(QStringLiteral("animOutStaggerUs")).toInteger(s.animOut.staggerUs);
    s.animOut.order = textAnimOrderFromString(o.value(QStringLiteral("animOutOrder")).toString());
    return s;
}

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

    return QJsonObject{
        {QStringLiteral("shape"), maskShapeToString(m.shape)},
        {QStringLiteral("x"), m.x},
        {QStringLiteral("y"), m.y},
        {QStringLiteral("w"), m.w},
        {QStringLiteral("h"), m.h},
        {QStringLiteral("rotation"), m.rotation},
        {QStringLiteral("feather"), m.feather},
        {QStringLiteral("invert"), m.invert},
        {QStringLiteral("points"), points},
        {QStringLiteral("mattePath"), m.mattePath},
        {QStringLiteral("matteSrcOffsetUs"), qint64(m.matteSrcOffsetUs)},
    };
}

Mask maskFromJson(const QJsonObject &o)
{
    Mask m;
    if (o.isEmpty())
        return m;
    m.shape = maskShapeFromString(o.value(QStringLiteral("shape")).toString());
    m.x = o.value(QStringLiteral("x")).toDouble(m.x);
    m.y = o.value(QStringLiteral("y")).toDouble(m.y);
    m.w = o.value(QStringLiteral("w")).toDouble(m.w);
    m.h = o.value(QStringLiteral("h")).toDouble(m.h);
    m.rotation = o.value(QStringLiteral("rotation")).toDouble(m.rotation);
    m.feather = o.value(QStringLiteral("feather")).toDouble(m.feather);
    m.invert = o.value(QStringLiteral("invert")).toBool(m.invert);
    m.mattePath = o.value(QStringLiteral("mattePath")).toString(m.mattePath);
    m.matteSrcOffsetUs =
        TimeUs(o.value(QStringLiteral("matteSrcOffsetUs")).toInteger(m.matteSrcOffsetUs));
    const QJsonArray points = o.value(QStringLiteral("points")).toArray();
    for (const QJsonValue &value : points) {
        const QJsonArray pair = value.toArray();
        if (pair.size() >= 2)
            m.points.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
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
    return QJsonObject{
        {QStringLiteral("id"), t.id},
        {QStringLiteral("fromClipId"), t.fromClipId},
        {QStringLiteral("toClipId"), t.toClipId},
        {QStringLiteral("kind"), t.kindId},
        {QStringLiteral("parameters"), params},
        {QStringLiteral("durationUs"), static_cast<double>(t.durationUs)},
    };
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
        {QStringLiteral("type"), clipTypeToString(clip.type)},
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
        {QStringLiteral("opacity"), keyframesToJson(clip.opacity)},
        {QStringLiteral("x"), keyframesToJson(clip.transformX)},
        {QStringLiteral("y"), keyframesToJson(clip.transformY)},
        {QStringLiteral("width"), keyframesToJson(clip.transformW)},
        {QStringLiteral("height"), keyframesToJson(clip.transformH)},
        {QStringLiteral("rotation"), keyframesToJson(clip.rotation)},
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
    clip.type = clipTypeFromString(object.value(QStringLiteral("type")).toString());
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
    clip.fadeInUs = static_cast<TimeUs>(object.value(QStringLiteral("fadeInUs")).toDouble());
    clip.fadeOutUs = static_cast<TimeUs>(object.value(QStringLiteral("fadeOutUs")).toDouble());
    clip.fadeCurve = fadeCurveFromString(object.value(QStringLiteral("fadeCurve")).toString());
    clip.fadeShape = FadeShape::fromJson(object.value(QStringLiteral("fadeShape")).toArray());
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
    clip.opacity = keyframesFromJson(object.value(QStringLiteral("opacity")).toObject());
    clip.rotation = keyframesFromJson(object.value(QStringLiteral("rotation")).toObject());
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
        {QStringLiteral("sampleRate"), asset.sampleRate},
        {QStringLiteral("channels"), asset.channels},
        {QStringLiteral("codecName"), asset.codecName},
        {QStringLiteral("thumbnailPath"), asset.thumbnailPath},
        {QStringLiteral("filmstripPath"), asset.filmstripPath},
    };
    if (asset.hasAudioKnown)
        object.insert(QStringLiteral("hasAudio"), asset.hasAudio);
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
    asset.width = object.value(QStringLiteral("width")).toInt();
    asset.height = object.value(QStringLiteral("height")).toInt();
    asset.fps = object.value(QStringLiteral("fps")).toDouble();
    asset.rotationDegrees = object.value(QStringLiteral("rotationDegrees")).toInt();
    asset.sampleRate = object.value(QStringLiteral("sampleRate")).toInt();
    asset.channels = object.value(QStringLiteral("channels")).toInt();
    asset.codecName = object.value(QStringLiteral("codecName")).toString();
    asset.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
    asset.filmstripPath = object.value(QStringLiteral("filmstripPath")).toString();
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
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_createdAt = QDateTime::currentDateTimeUtc();
    m_modifiedAt = m_createdAt;
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
            track.type = trackTypeFromString(
                trackObject.value(QStringLiteral("type")).toString(QStringLiteral("video")));
            track.muted = trackObject.value(QStringLiteral("muted")).toBool(false);
            track.hidden = trackObject.value(QStringLiteral("hidden")).toBool(false);
            track.locked = trackObject.value(QStringLiteral("locked")).toBool(false);
            track.showWaveform = trackObject.value(QStringLiteral("showWaveform")).toBool(false);
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

    QJsonArray tracksArray;
    for (const Track &track : m_tracks) {
        QJsonArray clipsArray;
        for (const Clip &clip : track.clips)
            clipsArray.append(clipToJson(clip));

        QJsonArray transitionsArray;
        for (const Transition &transition : track.transitions)
            transitionsArray.append(transitionToJson(transition));

        tracksArray.append(QJsonObject{
            {QStringLiteral("type"), trackTypeToString(track.type)},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("hidden"), track.hidden},
            {QStringLiteral("locked"), track.locked},
            {QStringLiteral("showWaveform"), track.showWaveform},
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

} // namespace drift
