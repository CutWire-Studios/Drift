#include "Effect.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>

#include <cmath>

namespace drift {

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

void rescaleEffectKeyframes(QList<Effect> &effects, TimeUs fromDurationUs, TimeUs toDurationUs)
{
    if (fromDurationUs <= 0 || toDurationUs <= 0 || fromDurationUs == toDurationUs)
        return;

    const double scale = static_cast<double>(toDurationUs) / static_cast<double>(fromDurationUs);
    for (Effect &effect : effects) {
        for (auto it = effect.paramKeyframes.begin(); it != effect.paramKeyframes.end(); ++it) {
            const KeyframeTrack<double> &track = it.value();
            if (track.isEmpty())
                continue;

            KeyframeTrack<double> out;
            // A track switched off keeps its keys, and must keep being switched off.
            out.setEnabled(track.enabled());
            for (auto k = track.keyframes().constBegin(); k != track.keyframes().constEnd(); ++k) {
                Keyframe<double> key = k.value();
                // Handles live in curve space: dx is microseconds on the same axis as the key
                // time, dy is in the parameter's own units. Stretching x alone is exactly the
                // affine map applied to the times, so the curve keeps its shape; scaling dy too
                // would flatten or exaggerate every ease.
                key.inDx *= scale;
                key.outDx *= scale;
                out.setKeyframe(
                    static_cast<TimeUs>(llround(static_cast<double>(k.key()) * scale)), key);
            }
            it.value() = out;
        }
    }
}

QString Effect::filterGraphString() const
{
    if (name.isEmpty())
        return {};

    QStringList parts;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        const QString value = it.value().toString();
        if (!value.isEmpty())
            parts.append(QStringLiteral("%1=%2").arg(it.key(), value));
    }

    if (parts.isEmpty())
        return name;

    return QStringLiteral("%1=%2").arg(name, parts.join(QLatin1Char(':')));
}

QVariant Effect::valueAt(const QString &key, TimeUs clipTimeUs) const
{
    const auto it = paramKeyframes.constFind(key);
    if (it == paramKeyframes.constEnd() || it->isEmpty())
        return parameters.value(key);
    return it->evaluateAt(clipTimeUs);
}

Effect Effect::resolvedAt(TimeUs clipTimeUs) const
{
    Effect out = *this;
    for (auto it = paramKeyframes.constBegin(); it != paramKeyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            out.parameters.insert(it.key(), it->evaluateAt(clipTimeUs));
    }
    // A colour animates as four channel tracks, "<key>.r/.g/.b/.a" in 0..1, and is folded back
    // into the hex the rest of the pipeline reads. #rrggbbaa is kept when the static value had
    // an alpha, or when the alpha channel itself is animated.
    for (auto it = paramKeyframes.constBegin(); it != paramKeyframes.constEnd(); ++it) {
        if (!it.key().endsWith(QLatin1String(".r")) || it->isEmpty())
            continue;
        const QString base = it.key().chopped(2);
        const QString hex = parameters.value(base).toString();
        if (!hex.startsWith(QLatin1Char('#')))
            continue;
        const auto channel = [&](const char *suffix, double fallback) {
            const auto t = paramKeyframes.constFind(base + QLatin1String(suffix));
            const double v = (t == paramKeyframes.constEnd() || t->isEmpty()) ? fallback
                                                                              : t->evaluateAt(clipTimeUs);
            return qBound(0, int(std::lround(v * 255.0)), 255);
        };
        const QColor base0(hex.left(7));
        const bool hasAlpha = hex.size() == 9 || paramKeyframes.contains(base + QLatin1String(".a"));
        const double a0 = hex.size() == 9 ? hex.right(2).toInt(nullptr, 16) / 255.0 : 1.0;
        QString composed = QStringLiteral("#%1%2%3")
                               .arg(channel(".r", base0.redF()), 2, 16, QLatin1Char('0'))
                               .arg(channel(".g", base0.greenF()), 2, 16, QLatin1Char('0'))
                               .arg(channel(".b", base0.blueF()), 2, 16, QLatin1Char('0'));
        if (hasAlpha)
            composed += QStringLiteral("%1").arg(channel(".a", a0), 2, 16, QLatin1Char('0'));
        out.parameters.insert(base, composed);
    }
    return out;
}

} // namespace drift
