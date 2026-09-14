#include "TextParamSpec.h"

#include <QJsonArray>

namespace drift {

namespace {

const char *const kParamTypeNames[] = {"scalar", "color", "vec2", "text", "enum", "bool"};

} // namespace

QJsonValue slotValueToJson(const VectorSlotValue &v)
{
    switch (v.type) {
    case VectorSlotValue::Type::Color:
        return v.color.name(QColor::HexArgb);
    case VectorSlotValue::Type::Scalar:
        return v.scalar;
    case VectorSlotValue::Type::Vec2:
        return QJsonArray{v.vec2.x(), v.vec2.y()};
    case VectorSlotValue::Type::Text:
        return v.text;
    case VectorSlotValue::Type::Image:
        return v.image;
    }
    return QJsonValue();
}

VectorSlotValue paramValueFromJson(TextAnimParamSpec::Type type, const QJsonValue &v)
{
    switch (type) {
    case TextAnimParamSpec::Type::Scalar:
        return VectorSlotValue::fromScalar(v.toDouble());
    case TextAnimParamSpec::Type::Bool:
        return VectorSlotValue::fromScalar(v.toBool() || v.toDouble() > 0.5 ? 1.0 : 0.0);
    case TextAnimParamSpec::Type::Color:
        return VectorSlotValue::fromColor(QColor(v.toString()));
    case TextAnimParamSpec::Type::Vec2: {
        const QJsonArray a = v.toArray();
        return VectorSlotValue::fromVec2(QPointF(a.at(0).toDouble(), a.at(1).toDouble()));
    }
    case TextAnimParamSpec::Type::Text:
    case TextAnimParamSpec::Type::Enum:
        return VectorSlotValue::fromText(v.toString());
    }
    return VectorSlotValue::fromScalar(0.0);
}

QString textAnimParamTypeToString(TextAnimParamSpec::Type type)
{
    const size_t i = static_cast<size_t>(type);
    return QLatin1String(i < std::size(kParamTypeNames) ? kParamTypeNames[i] : kParamTypeNames[0]);
}

TextAnimParamSpec::Type textAnimParamTypeFromString(const QString &type)
{
    for (size_t i = 0; i < std::size(kParamTypeNames); ++i)
        if (type == QLatin1String(kParamTypeNames[i]))
            return static_cast<TextAnimParamSpec::Type>(i);
    return TextAnimParamSpec::Type::Scalar;
}

QJsonObject TextAnimParamSpec::toJson() const
{
    QJsonObject o{
        {QStringLiteral("id"), id},
        {QStringLiteral("type"), textAnimParamTypeToString(type)},
        {QStringLiteral("label"), label},
        {QStringLiteral("default"), slotValueToJson(defaultValue)},
    };
    if (!unit.isEmpty())
        o.insert(QStringLiteral("unit"), unit);
    if (!group.isEmpty())
        o.insert(QStringLiteral("group"), group);
    if (type == Type::Scalar) {
        o.insert(QStringLiteral("min"), min);
        o.insert(QStringLiteral("max"), max);
        if (step > 0.0)
            o.insert(QStringLiteral("step"), step);
    }
    if (type == Type::Enum)
        o.insert(QStringLiteral("values"), QJsonArray::fromStringList(enumValues));
    return o;
}

TextAnimParamSpec TextAnimParamSpec::fromJson(const QJsonObject &o)
{
    TextAnimParamSpec spec;
    spec.id = o.value(QStringLiteral("id")).toString();
    spec.type = textAnimParamTypeFromString(o.value(QStringLiteral("type")).toString());
    spec.label = o.value(QStringLiteral("label")).toString(spec.id);
    spec.unit = o.value(QStringLiteral("unit")).toString();
    spec.group = o.value(QStringLiteral("group")).toString();
    spec.min = o.value(QStringLiteral("min")).toDouble(0.0);
    spec.max = o.value(QStringLiteral("max")).toDouble(1.0);
    spec.step = o.value(QStringLiteral("step")).toDouble(0.0);
    for (const QJsonValue &v : o.value(QStringLiteral("values")).toArray())
        spec.enumValues.append(v.toString());
    spec.defaultValue = paramValueFromJson(spec.type, o.value(QStringLiteral("default")));
    if (spec.type == Type::Enum && spec.defaultValue.text.isEmpty() && !spec.enumValues.isEmpty())
        spec.defaultValue = VectorSlotValue::fromText(spec.enumValues.first());
    return spec;
}

double scalarParam(const QMap<QString, VectorSlotValue> &params, const QString &id, double fallback)
{
    const auto it = params.constFind(id);
    return it != params.constEnd() && it->type == VectorSlotValue::Type::Scalar ? it->scalar : fallback;
}

QColor colorParam(const QMap<QString, VectorSlotValue> &params, const QString &id, const QColor &fallback)
{
    const auto it = params.constFind(id);
    return it != params.constEnd() && it->type == VectorSlotValue::Type::Color && it->color.isValid() ? it->color : fallback;
}

} // namespace drift
