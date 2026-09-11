#include "mcp/McpValidate.h"
#include "mcp/McpJson.h"

#include <QJsonArray>
#include <QVariant>

#include <cmath>

namespace drift::mcp {
namespace {

QString describeValue(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::String:
        return QStringLiteral("string \"%1\"").arg(v.toString());
    case QJsonValue::Double:
        return QStringLiteral("number %1").arg(QString::number(v.toDouble()));
    case QJsonValue::Bool:
        return QStringLiteral("boolean");
    case QJsonValue::Array:
        return QStringLiteral("array");
    case QJsonValue::Object:
        return QStringLiteral("object");
    default:
        return QStringLiteral("null");
    }
}

bool numericString(const QJsonValue &v, double *out)
{
    if (!v.isString())
        return false;
    bool ok = false;
    const double n = v.toString().trimmed().toDouble(&ok);
    if (ok && out)
        *out = n;
    return ok;
}

bool matchesType(const QString &type, const QJsonValue &v, double *number)
{
    if (type == QLatin1String("number")) {
        if (v.isDouble()) {
            *number = v.toDouble();
            return true;
        }
        return numericString(v, number);
    }
    if (type == QLatin1String("integer")) {
        double n = 0;
        if (v.isDouble())
            n = v.toDouble();
        else if (!numericString(v, &n))
            return false;
        if (n != std::floor(n))
            return false;
        *number = n;
        return true;
    }
    if (type == QLatin1String("string"))
        return v.isString();
    if (type == QLatin1String("boolean")) {
        if (v.isBool())
            return true;
        if (v.isDouble())
            return v.toDouble() == 0 || v.toDouble() == 1;
        if (v.isString()) {
            const QString s = v.toString().toLower();
            return s == QLatin1String("true") || s == QLatin1String("false") || s == QLatin1String("1")
                   || s == QLatin1String("0");
        }
        return false;
    }
    if (type == QLatin1String("array"))
        return v.isArray();
    if (type == QLatin1String("object"))
        return v.isObject();
    return true;
}

QString numberText(double v)
{
    return QString::number(v, 'g', 10);
}

QJsonObject validateObject(const QString &path, const QJsonObject &schema, QJsonObject &args,
                           QStringList *ignored);

QJsonObject validateValue(const QString &path, const QJsonObject &prop, QJsonValue &value,
                          QStringList *ignored)
{
    if (value.isNull() || value.isUndefined())
        return {};

    const QJsonValue typeValue = prop.value(QStringLiteral("type"));
    QStringList types;
    if (typeValue.isString())
        types.append(typeValue.toString());
    else
        for (const QJsonValue &t : typeValue.toArray())
            types.append(t.toString());

    double number = std::nan("");
    if (!types.isEmpty()) {
        bool matched = false;
        QString matchedType;
        for (const QString &type : types) {
            if (matchesType(type, value, &number)) {
                matched = true;
                matchedType = type;
                break;
            }
        }
        if (!matched) {
            return err("type_mismatch",
                       QStringLiteral("%1: expected %2, got %3").arg(path, types.join(QStringLiteral(" or ")), describeValue(value)));
        }
        if (matchedType == QLatin1String("number") || matchedType == QLatin1String("integer")) {
            if (prop.contains(QStringLiteral("minimum")) && number < prop.value(QStringLiteral("minimum")).toDouble()) {
                return err("bad_args", QStringLiteral("%1 must be %2..%3")
                                           .arg(path, numberText(prop.value(QStringLiteral("minimum")).toDouble()),
                                                numberText(prop.value(QStringLiteral("maximum")).toDouble())));
            }
            if (prop.contains(QStringLiteral("maximum")) && number > prop.value(QStringLiteral("maximum")).toDouble()) {
                return err("bad_args", QStringLiteral("%1 must be %2..%3")
                                           .arg(path, numberText(prop.value(QStringLiteral("minimum")).toDouble()),
                                                numberText(prop.value(QStringLiteral("maximum")).toDouble())));
            }
        }
    }

    const QJsonArray allowed = prop.value(QStringLiteral("enum")).toArray();
    if (!allowed.isEmpty() && value.isString()) {
        QStringList names;
        bool found = false;
        for (const QJsonValue &option : allowed) {
            const QString name = option.toString();
            names.append(name);
            if (name.compare(value.toString().trimmed(), Qt::CaseInsensitive) == 0) {
                value = name;
                found = true;
                break;
            }
        }
        if (!found)
            return err("bad_args", QStringLiteral("%1 must be one of %2").arg(path, names.join(QStringLiteral(", "))));
    }

    if (value.isObject() && prop.value(QStringLiteral("properties")).isObject()) {
        QJsonObject nested = value.toObject();
        const QJsonObject error = validateObject(path + QLatin1Char('.'), prop, nested, ignored);
        if (!error.isEmpty())
            return error;
        value = nested;
    }

    if (value.isArray() && prop.value(QStringLiteral("items")).isObject()) {
        const QJsonObject items = prop.value(QStringLiteral("items")).toObject();
        QJsonArray array = value.toArray();
        for (int i = 0; i < array.size(); ++i) {
            QJsonValue item = array.at(i);
            const QJsonObject error = validateValue(QStringLiteral("%1[%2]").arg(path).arg(i), items, item, ignored);
            if (!error.isEmpty())
                return error;
            array[i] = item;
        }
        value = array;
    }
    return {};
}

QJsonObject validateObject(const QString &prefix, const QJsonObject &schema, QJsonObject &args,
                           QStringList *ignored)
{
    const QJsonObject props = schema.value(QStringLiteral("properties")).toObject();

    for (const QJsonValue &requiredKey : schema.value(QStringLiteral("required")).toArray()) {
        const QString key = requiredKey.toString();
        if (args.contains(key) && !args.value(key).isNull())
            continue;
        const QStringList got = args.keys();
        return err("bad_args", QStringLiteral("%1%2 required — %3 (got: %4)")
                                   .arg(prefix, key,
                                        props.value(key).toObject().value(QStringLiteral("description")).toString(),
                                        got.isEmpty() ? QStringLiteral("nothing") : got.join(QStringLiteral(", "))));
    }

    for (auto it = args.begin(); it != args.end(); ++it) {
        if (!props.contains(it.key())) {
            if (ignored)
                ignored->append(prefix + it.key());
            continue;
        }
        QJsonValue value = it.value();
        const QJsonObject error = validateValue(prefix + it.key(), props.value(it.key()).toObject(), value, ignored);
        if (!error.isEmpty())
            return error;
        it.value() = value;
    }
    return {};
}

} // namespace

QJsonObject validateArgs(const QString &op, const QJsonObject &schema, QJsonObject &args, QStringList *ignored)
{
    Q_UNUSED(op);
    return validateObject({}, schema, args, ignored);
}

} // namespace drift::mcp
