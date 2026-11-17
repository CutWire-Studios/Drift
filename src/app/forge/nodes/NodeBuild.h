#pragma once

#include "app/forge/compiler/EmitCtx.h"
#include "app/forge/nodes/Registry.h"

namespace drift::forge::build {

using Out = QMap<QString, QString>;

inline QString S(const char *s)
{
    return QString::fromUtf8(s);
}

inline QJsonArray rgba(double r, double g, double b, double a)
{
    return QJsonArray{r, g, b, a};
}

inline QJsonArray vec2v(double x, double y)
{
    return QJsonArray{x, y};
}

inline NodeDef node(const char *type, const char *label, const char *category, const char *description)
{
    NodeDef d;
    d.type = S(type);
    d.label = S(label);
    d.category = S(category);
    d.description = S(description);
    return d;
}

inline OutputDef out(const char *id, const char *label, SocketType t)
{
    return {S(id), S(label), t};
}

inline InputDef floatIn(const char *id, const char *label, double def, double min, double max)
{
    InputDef i;
    i.id = S(id);
    i.label = S(label);
    i.type = SocketType::Float;
    i.defaultValue = def;
    i.min = min;
    i.max = max;
    return i;
}

inline InputDef clockIn(const char *id, const char *label)
{
    InputDef i;
    i.id = S(id);
    i.label = S(label);
    i.type = SocketType::Float;
    i.defaultValue = 0;
    i.clock = true;
    return i;
}

inline InputDef vecIn(const char *id, const char *label, double x, double y, double min, double max)
{
    InputDef i;
    i.id = S(id);
    i.label = S(label);
    i.type = SocketType::Vec2;
    i.defaultValue = vec2v(x, y);
    i.min = min;
    i.max = max;
    return i;
}

inline InputDef colorIn(const char *id, const char *label, const QJsonArray &def)
{
    InputDef i;
    i.id = S(id);
    i.label = S(label);
    i.type = SocketType::Color;
    i.defaultValue = def;
    return i;
}

inline InputDef imageIn()
{
    return colorIn("image", "Image", rgba(0, 0, 0, 1));
}

inline InputDef swatch(const char *id, const char *label, const QJsonArray &def)
{
    InputDef i = colorIn(id, label, def);
    i.widget = QStringLiteral("swatch");
    return i;
}

inline InputDef widget(InputDef i, const char *w)
{
    i.widget = S(w);
    return i;
}

inline InputDef unit(InputDef i, const char *u)
{
    i.unit = S(u);
    return i;
}

inline InputDef integer(InputDef i)
{
    i.integer = true;
    return i;
}

inline InputDef hint(InputDef i, const char *h)
{
    i.hint = S(h);
    return i;
}

inline OptionDef selectOpt(const char *id, const char *label, const QList<SelectChoice> &choices, const char *def)
{
    OptionDef o;
    o.id = S(id);
    o.label = S(label);
    o.kind = OptionKind::Select;
    o.choices = choices;
    o.defaultValue = S(def);
    return o;
}

inline OptionDef toggleOpt(const char *id, const char *label, bool def)
{
    OptionDef o;
    o.id = S(id);
    o.label = S(label);
    o.kind = OptionKind::Toggle;
    o.defaultValue = def;
    return o;
}

inline SelectChoice choice(const char *value, const char *label)
{
    return {S(value), S(label)};
}

}
