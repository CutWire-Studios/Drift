#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>

#include <cmath>

namespace drift::mcp {

inline QJsonObject ok(QJsonObject extra = {})
{
    extra.insert(QStringLiteral("ok"), true);
    return extra;
}

inline QJsonObject err(const char *code, const QString &detail = {})
{
    QJsonObject o{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QString::fromUtf8(code)}};
    if (!detail.isEmpty())
        o.insert(QStringLiteral("detail"), detail);
    return o;
}

// One error shape for every clip-ref op, so the agent learns what to send rather than
// just that the lookup failed.
inline QJsonObject clipRefError(const QJsonObject &args)
{
    const QString id = args.value(QStringLiteral("clip")).toString().trimmed();
    if (!id.isEmpty()) {
        return err("not_found",
                   QStringLiteral("clip %1 not found — re-read inspect({clips:true}); ids change after "
                                  "set_speed_curve/undo")
                       .arg(id));
    }
    if (args.contains(QStringLiteral("track")) && args.contains(QStringLiteral("index"))) {
        return err("not_found",
                   QStringLiteral("no clip at track %1 index %2")
                       .arg(args.value(QStringLiteral("track")).toVariant().toString(),
                            args.value(QStringLiteral("index")).toVariant().toString()));
    }
    return err("bad_args", QStringLiteral("clip (uuid from inspect({clips:true})) or track+index required"));
}

// fps must survive a set_project_setup round-trip (29.97002997); speed-curve pos is normalised
// over the trimmed source, where 3 dp is a whole frame off on a long clip.
inline int compactPrecision(const QString &key, int dp)
{
    if (key == QLatin1String("fps") || key == QLatin1String("pos"))
        return 6;
    return dp;
}

inline QJsonValue compactJson(const QJsonValue &value, int dp = 3, const QString &key = {})
{
    switch (value.type()) {
    case QJsonValue::Double: {
        const double v = value.toDouble();
        if (!std::isfinite(v) || v == std::floor(v) || std::fabs(v) >= 1e12)
            return value;
        const double scale = std::pow(10.0, compactPrecision(key, dp));
        return std::round(v * scale) / scale;
    }
    case QJsonValue::Array: {
        QJsonArray out;
        for (const QJsonValue &item : value.toArray())
            out.append(compactJson(item, dp, key));
        return out;
    }
    case QJsonValue::Object: {
        QJsonObject out;
        const QJsonObject in = value.toObject();
        for (auto it = in.begin(); it != in.end(); ++it)
            out.insert(it.key(), compactJson(it.value(), dp, it.key()));
        return out;
    }
    default:
        return value;
    }
}

inline QJsonObject compactJson(const QJsonObject &object, int dp = 3)
{
    return compactJson(QJsonValue(object), dp).toObject();
}

inline QJsonObject objectSchema(const QJsonObject &properties, const QStringList &required = {})
{
    QJsonObject s{{QStringLiteral("type"), QStringLiteral("object")},
                  {QStringLiteral("properties"), properties}};
    if (!required.isEmpty()) {
        QJsonArray req;
        for (const QString &key : required)
            req.append(key);
        s.insert(QStringLiteral("required"), req);
    }
    return s;
}

inline QJsonObject stringProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject numberProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("number")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject numberProp(const QString &description, double minimum, double maximum)
{
    QJsonObject p = numberProp(description);
    p.insert(QStringLiteral("minimum"), minimum);
    p.insert(QStringLiteral("maximum"), maximum);
    return p;
}

inline QJsonObject integerProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject integerProp(const QString &description, int minimum, int maximum)
{
    QJsonObject p = integerProp(description);
    p.insert(QStringLiteral("minimum"), minimum);
    p.insert(QStringLiteral("maximum"), maximum);
    return p;
}

inline QJsonObject boolProp(const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), description}};
}

inline QJsonObject arrayProp(const QJsonObject &items, const QString &description)
{
    return {{QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("items"), items},
            {QStringLiteral("description"), description}};
}

inline QJsonObject enumProp(const QString &description, const QStringList &values)
{
    QJsonArray enumValues;
    for (const QString &value : values)
        enumValues.append(value);
    return {{QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), enumValues},
            {QStringLiteral("description"), description}};
}

inline QJsonObject propWithDefault(QJsonObject prop, const QJsonValue &defaultValue)
{
    prop.insert(QStringLiteral("default"), defaultValue);
    return prop;
}

inline QJsonObject toolAnnotations(bool readOnly = false, bool destructive = false, bool idempotent = false)
{
    QJsonObject annotations;
    if (readOnly)
        annotations.insert(QStringLiteral("readOnlyHint"), true);
    if (destructive)
        annotations.insert(QStringLiteral("destructiveHint"), true);
    if (idempotent)
        annotations.insert(QStringLiteral("idempotentHint"), true);
    return annotations;
}

inline QJsonObject toolDef(const QString &name, const QString &description, const QJsonObject &inputSchema,
                           const QJsonObject &annotations = {})
{
    QJsonObject tool{{QStringLiteral("name"), name},
                     {QStringLiteral("description"), description},
                     {QStringLiteral("inputSchema"), inputSchema}};
    if (!annotations.isEmpty())
        tool.insert(QStringLiteral("annotations"), annotations);
    return tool;
}

inline QJsonObject textResult(const QJsonObject &payload, bool isError = false)
{
    const QByteArray json = QJsonDocument(compactJson(payload)).toJson(QJsonDocument::Compact);
    QJsonObject contentItem{{QStringLiteral("type"), QStringLiteral("text")},
                            {QStringLiteral("text"), QString::fromUtf8(json)}};
    return {{QStringLiteral("content"), QJsonArray{contentItem}},
            {QStringLiteral("isError"), isError || payload.value(QStringLiteral("ok")).toBool(true) == false}};
}

// Already an MCP tool result (text/image content) rather than an op payload to wrap.
inline bool isRawResult(const QJsonObject &payload)
{
    return payload.value(QStringLiteral("content")).isArray() && payload.contains(QStringLiteral("isError"));
}

} // namespace drift::mcp
