#pragma once

#include "VectorSource.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

// A typed parameter a preset, a look or a shader effect exposes to the inspector, and the value
// carrier (VectorSlotValue) the UI renders generically.

namespace drift {

struct TextAnimParamSpec
{
    enum class Type { Scalar, Color, Vec2, Text, Enum, Bool };

    QString id;
    QString label;
    QString unit;  // "s", "px", "%", "em", "°"
    QString group;
    Type type = Type::Scalar;
    double min = 0.0;
    double max = 1.0;
    double step = 0.0;
    VectorSlotValue defaultValue; // Enum → Text, Bool → Scalar 0/1
    QStringList enumValues;

    QJsonObject toJson() const;
    static TextAnimParamSpec fromJson(const QJsonObject &o);
};

QString textAnimParamTypeToString(TextAnimParamSpec::Type type);
TextAnimParamSpec::Type textAnimParamTypeFromString(const QString &type);

// A slot value as the plain JSON a recipe expects (number, colour string, [x, y], text).
QJsonValue slotValueToJson(const VectorSlotValue &value);
VectorSlotValue paramValueFromJson(TextAnimParamSpec::Type type, const QJsonValue &value);
// A scalar param from a typed map, or the fallback.
double scalarParam(const QMap<QString, VectorSlotValue> &params, const QString &id, double fallback);
QColor colorParam(const QMap<QString, VectorSlotValue> &params, const QString &id, const QColor &fallback);

} // namespace drift
