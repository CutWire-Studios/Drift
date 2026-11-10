#include "core/project/sources/ProceduralSource.h"

#include "core/effects/Effect.h"

#include <QColor>
#include <QSet>

#include <cmath>

namespace drift {

bool ProceduralSource::isAnimated() const
{
    for (auto it = paramKeyframes.constBegin(); it != paramKeyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            return true;
    }
    return false;
}

QVariant ProceduralSource::valueAt(const QString &key, TimeUs clipTimeUs) const
{
    const auto it = paramKeyframes.constFind(key);
    if (it == paramKeyframes.constEnd() || it->isEmpty() || !it->enabled())
        return parameters.value(key);
    return it->evaluateAt(clipTimeUs);
}

ProceduralSource ProceduralSource::resolvedAt(TimeUs clipTimeUs) const
{
    ProceduralSource out = *this;
    // A colour animates as channel tracks ("<key>.r/.g/.b/.a", 0..1); fold them back into the hex
    // stored under the colour's own key instead of leaving "<key>.r" entries in the map.
    QSet<QString> colors;
    for (auto it = paramKeyframes.constBegin(); it != paramKeyframes.constEnd(); ++it) {
        if (it->isEmpty() || !it->enabled())
            continue;
        const QString base = it.key().chopped(2);
        if (it.key().size() > 2 && QStringLiteral("rgba").contains(it.key().back()) && it.key().at(it.key().size() - 2) == QLatin1Char('.')
            && parameters.value(base).toString().startsWith(QLatin1Char('#'))) {
            colors.insert(base);
            continue;
        }
        out.parameters.insert(it.key(), it->evaluateAt(clipTimeUs));
    }
    for (const QString &base : colors) {
        const QString hex = parameters.value(base).toString();
        const QColor base0(hex.left(7));
        const auto channel = [&](const char *suffix, double fallback) {
            const auto t = paramKeyframes.constFind(base + QLatin1String(suffix));
            const double v = (t == paramKeyframes.constEnd() || t->isEmpty() || !t->enabled()) ? fallback
                                                                                                : t->evaluateAt(clipTimeUs);
            return qBound(0, int(std::lround(v * 255.0)), 255);
        };
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
    out.paramKeyframes.clear();
    return out;
}

QJsonObject ProceduralSource::toJson() const
{
    QJsonObject params;
    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it)
        params.insert(it.key(), QJsonValue::fromVariant(it.value()));
    QJsonObject keys;
    for (auto it = paramKeyframes.constBegin(); it != paramKeyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            keys.insert(it.key(), keyframesToJson(it.value()));
    }
    QJsonObject o{
        {QStringLiteral("packageId"), packageId},
        {QStringLiteral("parameters"), params},
        {QStringLiteral("seed"), seed},
    };
    if (!keys.isEmpty())
        o.insert(QStringLiteral("paramKeyframes"), keys);
    return o;
}

ProceduralSource ProceduralSource::fromJson(const QJsonObject &o)
{
    ProceduralSource s;
    if (o.isEmpty())
        return s;
    s.packageId = o.value(QStringLiteral("packageId")).toString();
    s.seed = o.value(QStringLiteral("seed")).toInt();
    const QJsonObject params = o.value(QStringLiteral("parameters")).toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        s.parameters.insert(it.key(), it.value().toVariant());
    const QJsonObject keys = o.value(QStringLiteral("paramKeyframes")).toObject();
    for (auto it = keys.constBegin(); it != keys.constEnd(); ++it)
        s.paramKeyframes.insert(it.key(), keyframesFromJson(it.value().toObject()));
    return s;
}

} // namespace drift
