#include "TextShading.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QRandomGenerator>

namespace drift {

QString textLayerKindToString(TextLayerKind kind)
{
    switch (kind) {
    case TextLayerKind::Fill:
        return QStringLiteral("fill");
    case TextLayerKind::Stroke:
        return QStringLiteral("stroke");
    case TextLayerKind::Shadow:
        return QStringLiteral("shadow");
    case TextLayerKind::Glow:
        return QStringLiteral("glow");
    case TextLayerKind::Extrude:
        return QStringLiteral("extrude");
    }
    return QStringLiteral("fill");
}

TextLayerKind textLayerKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("stroke"))
        return TextLayerKind::Stroke;
    if (kind == QStringLiteral("shadow"))
        return TextLayerKind::Shadow;
    if (kind == QStringLiteral("glow"))
        return TextLayerKind::Glow;
    if (kind == QStringLiteral("extrude"))
        return TextLayerKind::Extrude;
    return TextLayerKind::Fill;
}

QString textPaintKindToString(TextPaintKind kind)
{
    switch (kind) {
    case TextPaintKind::Solid:
        return QStringLiteral("solid");
    case TextPaintKind::Gradient:
        return QStringLiteral("gradient");
    case TextPaintKind::Texture:
        return QStringLiteral("texture");
    case TextPaintKind::Effect:
        return QStringLiteral("effect");
    }
    return QStringLiteral("solid");
}

TextPaintKind textPaintKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("gradient"))
        return TextPaintKind::Gradient;
    if (kind == QStringLiteral("texture"))
        return TextPaintKind::Texture;
    if (kind == QStringLiteral("effect"))
        return TextPaintKind::Effect;
    return TextPaintKind::Solid;
}

QString textGradientKindToString(TextGradientKind kind)
{
    switch (kind) {
    case TextGradientKind::Linear:
        return QStringLiteral("linear");
    case TextGradientKind::Radial:
        return QStringLiteral("radial");
    case TextGradientKind::Sweep:
        return QStringLiteral("sweep");
    }
    return QStringLiteral("linear");
}

TextGradientKind textGradientKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("radial"))
        return TextGradientKind::Radial;
    if (kind == QStringLiteral("sweep"))
        return TextGradientKind::Sweep;
    return TextGradientKind::Linear;
}

QString textGradientSpaceToString(TextGradientSpace space)
{
    switch (space) {
    case TextGradientSpace::Block:
        return QStringLiteral("block");
    case TextGradientSpace::Line:
        return QStringLiteral("line");
    case TextGradientSpace::Word:
        return QStringLiteral("word");
    case TextGradientSpace::Glyph:
        return QStringLiteral("glyph");
    case TextGradientSpace::AccentRun:
        return QStringLiteral("accentRun");
    }
    return QStringLiteral("block");
}

TextGradientSpace textGradientSpaceFromString(const QString &space)
{
    if (space == QStringLiteral("line"))
        return TextGradientSpace::Line;
    if (space == QStringLiteral("word"))
        return TextGradientSpace::Word;
    if (space == QStringLiteral("glyph") || space == QStringLiteral("character"))
        return TextGradientSpace::Glyph;
    if (space == QStringLiteral("accentRun"))
        return TextGradientSpace::AccentRun;
    return TextGradientSpace::Block;
}

QString textLayerScopeToString(TextLayerScope scope)
{
    switch (scope) {
    case TextLayerScope::All:
        return QStringLiteral("all");
    case TextLayerScope::Base:
        return QStringLiteral("base");
    case TextLayerScope::Accent:
        return QStringLiteral("accent");
    }
    return QStringLiteral("all");
}

TextLayerScope textLayerScopeFromString(const QString &scope)
{
    if (scope == QStringLiteral("base"))
        return TextLayerScope::Base;
    if (scope == QStringLiteral("accent"))
        return TextLayerScope::Accent;
    return TextLayerScope::All;
}

QString strokeAlignToString(StrokeAlign align)
{
    switch (align) {
    case StrokeAlign::Center:
        return QStringLiteral("center");
    case StrokeAlign::Outside:
        return QStringLiteral("outside");
    case StrokeAlign::Inside:
        return QStringLiteral("inside");
    }
    return QStringLiteral("outside");
}

StrokeAlign strokeAlignFromString(const QString &align)
{
    if (align == QStringLiteral("center") || align == QStringLiteral("centre"))
        return StrokeAlign::Center;
    if (align == QStringLiteral("inside"))
        return StrokeAlign::Inside;
    return StrokeAlign::Outside;
}

QString strokeDashToString(StrokeDash dash)
{
    switch (dash) {
    case StrokeDash::Solid:
        return QStringLiteral("solid");
    case StrokeDash::Dash:
        return QStringLiteral("dash");
    case StrokeDash::Dot:
        return QStringLiteral("dot");
    case StrokeDash::DashDot:
        return QStringLiteral("dashdot");
    }
    return QStringLiteral("solid");
}

StrokeDash strokeDashFromString(const QString &dash)
{
    if (dash == QStringLiteral("dash"))
        return StrokeDash::Dash;
    if (dash == QStringLiteral("dot"))
        return StrokeDash::Dot;
    if (dash == QStringLiteral("dashdot") || dash == QStringLiteral("dashDot"))
        return StrokeDash::DashDot;
    return StrokeDash::Solid;
}

QList<double> strokeDashIntervals(StrokeDash dash)
{
    // Qt's pen patterns, so a migrated shape stroke keeps its look.
    switch (dash) {
    case StrokeDash::Solid:
        return {};
    case StrokeDash::Dash:
        return {4.0, 2.0};
    case StrokeDash::Dot:
        return {1.0, 2.0};
    case StrokeDash::DashDot:
        return {4.0, 2.0, 1.0, 2.0};
    }
    return {};
}

TextShadingLayer solidFillLayer(const QColor &color, const QString &id)
{
    TextShadingLayer layer;
    layer.id = id;
    layer.kind = TextLayerKind::Fill;
    layer.paint.color = color;
    return layer;
}

TextShadingLayer strokeLayer(double width, const QColor &color, const QString &id)
{
    TextShadingLayer layer;
    layer.id = id;
    layer.kind = TextLayerKind::Stroke;
    layer.width = width;
    layer.paint.color = color;
    return layer;
}

TextShadingLayer shadowLayer(const QColor &color, double offsetX, double offsetY, double blur, double opacity,
                             const QString &id)
{
    TextShadingLayer layer;
    layer.id = id;
    layer.kind = TextLayerKind::Shadow;
    layer.paint.color = color;
    layer.offsetX = offsetX;
    layer.offsetY = offsetY;
    layer.blur = blur;
    layer.opacity = opacity;
    return layer;
}

TextShadingLayer glowLayer(const QColor &color, double radius, double opacity, const QString &id)
{
    TextShadingLayer layer;
    layer.id = id;
    layer.kind = TextLayerKind::Glow;
    layer.paint.color = color;
    layer.blur = radius;
    layer.opacity = opacity;
    return layer;
}

QString mintTextLayerId(const QList<TextShadingLayer> &layers)
{
    for (;;) {
        const QString id = QStringLiteral("l%1").arg(QRandomGenerator::global()->bounded(0x1000000u), 6, 16, QLatin1Char('0'));
        if (!findTextLayer(layers, id))
            return id;
    }
}

TextShadingLayer *findTextLayer(QList<TextShadingLayer> &layers, const QString &id)
{
    for (TextShadingLayer &layer : layers)
        if (layer.id == id)
            return &layer;
    return nullptr;
}

const TextShadingLayer *findTextLayer(const QList<TextShadingLayer> &layers, const QString &id)
{
    for (const TextShadingLayer &layer : layers)
        if (layer.id == id)
            return &layer;
    return nullptr;
}

const TextShadingLayer *firstTextLayerOfKind(const QList<TextShadingLayer> &layers, TextLayerKind kind,
                                             bool enabledOnly)
{
    // Front-most first: the topmost fill is "the" colour of the text.
    for (int i = layers.size() - 1; i >= 0; --i) {
        const TextShadingLayer &layer = layers.at(i);
        if (layer.kind == kind && (!enabledOnly || layer.enabled))
            return &layer;
    }
    return nullptr;
}

TextShadingLayer *firstTextLayerOfKind(QList<TextShadingLayer> &layers, TextLayerKind kind, bool enabledOnly)
{
    for (int i = layers.size() - 1; i >= 0; --i) {
        TextShadingLayer &layer = layers[i];
        if (layer.kind == kind && (!enabledOnly || layer.enabled))
            return &layer;
    }
    return nullptr;
}

namespace {

quint64 paintHash(const TextPaint &p)
{
    quint64 h = qHashMulti(0, static_cast<int>(p.kind), p.color.rgba());
    if (p.kind == TextPaintKind::Gradient) {
        const TextGradient &g = p.gradient;
        h = qHashMulti(h, static_cast<int>(g.kind), g.angle, g.offset, g.offsetSpeed, g.scale, g.center.x(),
                       g.center.y(), g.repeat, g.oklab, static_cast<int>(g.space));
        for (const TextGradientStop &stop : g.stops)
            h = qHashMulti(h, stop.pos, stop.color.rgba());
    } else if (p.kind == TextPaintKind::Texture) {
        h = qHashMulti(h, p.texture.path, p.texture.scale, p.texture.angle, p.texture.offset.x(),
                       p.texture.offset.y(), p.texture.tile);
    } else if (p.kind == TextPaintKind::Effect) {
        h = qHashMulti(h, p.effect.id);
        for (auto it = p.effect.params.constBegin(); it != p.effect.params.constEnd(); ++it) {
            h = qHashMulti(h, it.key(), static_cast<int>(it->type), it->color.rgba(), it->scalar, it->vec2.x(),
                           it->vec2.y(), it->text, it->image);
        }
    }
    return h;
}

QJsonObject gradientToJson(const TextGradient &g)
{
    QJsonArray stops;
    for (const TextGradientStop &stop : g.stops)
        stops.append(QJsonObject{{QStringLiteral("pos"), stop.pos}, {QStringLiteral("color"), stop.color.name(QColor::HexArgb)}});
    return QJsonObject{
        {QStringLiteral("kind"), textGradientKindToString(g.kind)},
        {QStringLiteral("stops"), stops},
        {QStringLiteral("angle"), g.angle},
        {QStringLiteral("offset"), g.offset},
        {QStringLiteral("offsetSpeed"), g.offsetSpeed},
        {QStringLiteral("scale"), g.scale},
        {QStringLiteral("center"), QJsonArray{g.center.x(), g.center.y()}},
        {QStringLiteral("repeat"), g.repeat},
        {QStringLiteral("oklab"), g.oklab},
        {QStringLiteral("space"), textGradientSpaceToString(g.space)},
    };
}

TextGradient gradientFromJson(const QJsonObject &o)
{
    TextGradient g;
    if (o.isEmpty())
        return g;
    g.kind = textGradientKindFromString(o.value(QStringLiteral("kind")).toString());
    const QJsonArray stops = o.value(QStringLiteral("stops")).toArray();
    if (!stops.isEmpty()) {
        g.stops.clear();
        for (const QJsonValue &v : stops) {
            const QJsonObject s = v.toObject();
            g.stops.append({s.value(QStringLiteral("pos")).toDouble(), QColor(s.value(QStringLiteral("color")).toString())});
        }
    }
    g.angle = o.value(QStringLiteral("angle")).toDouble(g.angle);
    g.offset = o.value(QStringLiteral("offset")).toDouble(g.offset);
    g.offsetSpeed = o.value(QStringLiteral("offsetSpeed")).toDouble(g.offsetSpeed);
    g.scale = o.value(QStringLiteral("scale")).toDouble(g.scale);
    const QJsonArray c = o.value(QStringLiteral("center")).toArray();
    if (c.size() == 2)
        g.center = QPointF(c.at(0).toDouble(), c.at(1).toDouble());
    g.repeat = o.value(QStringLiteral("repeat")).toBool(g.repeat);
    g.oklab = o.value(QStringLiteral("oklab")).toBool(g.oklab);
    g.space = textGradientSpaceFromString(o.value(QStringLiteral("space")).toString());
    return g;
}

} // namespace

quint64 textLayersHash(const QList<TextShadingLayer> &layers)
{
    quint64 h = qHash(layers.size());
    for (const TextShadingLayer &l : layers) {
        if (!l.enabled)
            continue;
        h = qHashMulti(h, l.id, static_cast<int>(l.kind), paintHash(l.paint), l.opacity, static_cast<int>(l.blend),
                       l.offsetX, l.offsetY, l.blur, l.width, l.spread, static_cast<int>(l.strokeAlign),
                       static_cast<int>(l.dash), l.dashOffset, l.knockout, l.trimStart, l.trimEnd, l.sketchLength,
                       l.sketchDeviation, l.sketchSeed, l.extrudeSteps, l.extrudeAngle, l.extrudeDarken,
                       static_cast<int>(l.scope));
    }
    return h;
}

namespace {

TextAnimParamSpec scalarSpec(const char *id, const char *label, const char *unit, double min, double max, double step,
                             double def)
{
    TextAnimParamSpec s;
    s.id = QLatin1String(id);
    s.label = QCoreApplication::translate("TextEffects", label);
    s.unit = QLatin1String(unit);
    s.min = min;
    s.max = max;
    s.step = step;
    s.defaultValue = VectorSlotValue::fromScalar(def);
    return s;
}

TextAnimParamSpec colorSpec(const char *id, const char *label, const QColor &def)
{
    TextAnimParamSpec s;
    s.id = QLatin1String(id);
    s.label = QCoreApplication::translate("TextEffects", label);
    s.type = TextAnimParamSpec::Type::Color;
    s.defaultValue = VectorSlotValue::fromColor(def);
    return s;
}

} // namespace

const QList<TextEffectSpec> &textShaderEffectSpecs()
{
    static const QList<TextEffectSpec> specs{
        {QStringLiteral("shine"), QCoreApplication::translate("TextEffects", "Shine sweep"),
         {scalarSpec("speed", "Speed", "cyc/s", 0.0, 4.0, 0.05, 0.5), scalarSpec("width", "Width", "", 0.02, 1.0, 0.01, 0.25),
          scalarSpec("angle", "Angle", "°", -180.0, 180.0, 1.0, 20.0), scalarSpec("intensity", "Brightness", "", 0.0, 2.0, 0.05, 0.9),
          colorSpec("colorA", "Colour", Qt::white)}},
        {QStringLiteral("shimmer"), QCoreApplication::translate("TextEffects", "Holographic shimmer"),
         {scalarSpec("speed", "Speed", "cyc/s", 0.0, 4.0, 0.05, 0.3), scalarSpec("scale", "Bands", "", 0.5, 12.0, 0.5, 3.0),
          scalarSpec("intensity", "Saturation", "", 0.0, 1.0, 0.05, 0.6)}},
        {QStringLiteral("neon-pulse"), QCoreApplication::translate("TextEffects", "Neon pulse"),
         {scalarSpec("speed", "Speed", "Hz", 0.0, 8.0, 0.1, 1.0), scalarSpec("intensity", "Depth", "", 0.0, 1.0, 0.05, 0.5),
          scalarSpec("flicker", "Flicker", "", 0.0, 1.0, 0.05, 0.3), colorSpec("colorA", "Glow tint", QColor(255, 60, 220))}},
        {QStringLiteral("glitch"), QCoreApplication::translate("TextEffects", "Glitch"),
         {scalarSpec("speed", "Rate", "Hz", 0.0, 30.0, 1.0, 8.0), scalarSpec("amount", "Offset", "px", 0.0, 60.0, 1.0, 12.0),
          scalarSpec("block", "Row height", "px", 1.0, 64.0, 1.0, 8.0)}},
        {QStringLiteral("chrome"), QCoreApplication::translate("TextEffects", "Chrome"),
         {scalarSpec("speed", "Speed", "cyc/s", 0.0, 2.0, 0.05, 0.0), scalarSpec("bands", "Bands", "", 0.5, 6.0, 0.5, 2.0),
          colorSpec("colorA", "Light", QColor(240, 245, 255)), colorSpec("colorB", "Dark", QColor(40, 50, 70))}},
        {QStringLiteral("dissolve"), QCoreApplication::translate("TextEffects", "Dissolve"),
         {scalarSpec("scale", "Grain", "", 0.002, 0.2, 0.002, 0.03), scalarSpec("edge", "Edge", "", 0.0, 0.5, 0.01, 0.1),
          scalarSpec("progress", "Progress", "", 0.0, 1.0, 0.01, 1.0), scalarSpec("seed", "Seed", "", 0.0, 100.0, 1.0, 1.0)},
         false},
    };
    return specs;
}

const TextEffectSpec *textShaderEffectSpec(const QString &id)
{
    for (const TextEffectSpec &spec : textShaderEffectSpecs())
        if (spec.id == id)
            return &spec;
    return nullptr;
}

QJsonObject textPaintToJson(const TextPaint &paint)
{
    QJsonObject o{
        {QStringLiteral("kind"), textPaintKindToString(paint.kind)},
        {QStringLiteral("color"), paint.color.name(QColor::HexArgb)},
    };
    switch (paint.kind) {
    case TextPaintKind::Solid:
        break;
    case TextPaintKind::Gradient:
        o.insert(QStringLiteral("gradient"), gradientToJson(paint.gradient));
        break;
    case TextPaintKind::Texture:
        o.insert(QStringLiteral("texture"),
                 QJsonObject{{QStringLiteral("path"), paint.texture.path},
                             {QStringLiteral("scale"), paint.texture.scale},
                             {QStringLiteral("angle"), paint.texture.angle},
                             {QStringLiteral("offset"), QJsonArray{paint.texture.offset.x(), paint.texture.offset.y()}},
                             {QStringLiteral("tile"), paint.texture.tile}});
        break;
    case TextPaintKind::Effect: {
        QJsonObject params;
        for (auto it = paint.effect.params.constBegin(); it != paint.effect.params.constEnd(); ++it)
            params.insert(it.key(), it->toJson());
        o.insert(QStringLiteral("effect"), QJsonObject{{QStringLiteral("id"), paint.effect.id}, {QStringLiteral("params"), params}});
        break;
    }
    }
    return o;
}

TextPaint textPaintFromJson(const QJsonObject &o)
{
    TextPaint p;
    if (o.isEmpty())
        return p;
    p.kind = textPaintKindFromString(o.value(QStringLiteral("kind")).toString());
    p.color = QColor(o.value(QStringLiteral("color")).toString(p.color.name(QColor::HexArgb)));
    p.gradient = gradientFromJson(o.value(QStringLiteral("gradient")).toObject());
    const QJsonObject t = o.value(QStringLiteral("texture")).toObject();
    if (!t.isEmpty()) {
        p.texture.path = t.value(QStringLiteral("path")).toString();
        p.texture.scale = t.value(QStringLiteral("scale")).toDouble(1.0);
        p.texture.angle = t.value(QStringLiteral("angle")).toDouble(0.0);
        const QJsonArray off = t.value(QStringLiteral("offset")).toArray();
        if (off.size() == 2)
            p.texture.offset = QPointF(off.at(0).toDouble(), off.at(1).toDouble());
        p.texture.tile = t.value(QStringLiteral("tile")).toBool(true);
    }
    const QJsonObject e = o.value(QStringLiteral("effect")).toObject();
    if (!e.isEmpty()) {
        p.effect.id = e.value(QStringLiteral("id")).toString();
        const QJsonObject params = e.value(QStringLiteral("params")).toObject();
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            p.effect.params.insert(it.key(), VectorSlotValue::fromJson(it->toObject()));
    }
    return p;
}

QJsonObject textShadingLayerToJson(const TextShadingLayer &l)
{
    QJsonObject o{
        {QStringLiteral("id"), l.id},
        {QStringLiteral("kind"), textLayerKindToString(l.kind)},
        {QStringLiteral("enabled"), l.enabled},
        {QStringLiteral("paint"), textPaintToJson(l.paint)},
        {QStringLiteral("opacity"), l.opacity},
        {QStringLiteral("blend"), blendModeToString(l.blend)},
        {QStringLiteral("offsetX"), l.offsetX},
        {QStringLiteral("offsetY"), l.offsetY},
        {QStringLiteral("blur"), l.blur},
        {QStringLiteral("width"), l.width},
        {QStringLiteral("spread"), l.spread},
        {QStringLiteral("scope"), textLayerScopeToString(l.scope)},
    };
    if (l.kind == TextLayerKind::Stroke) {
        o.insert(QStringLiteral("strokeAlign"), strokeAlignToString(l.strokeAlign));
        o.insert(QStringLiteral("dash"), strokeDashToString(l.dash));
        o.insert(QStringLiteral("dashOffset"), l.dashOffset);
    }
    if (l.sketchLength > 0.0 || l.sketchDeviation > 0.0) {
        o.insert(QStringLiteral("sketchLength"), l.sketchLength);
        o.insert(QStringLiteral("sketchDeviation"), l.sketchDeviation);
        o.insert(QStringLiteral("sketchSeed"), l.sketchSeed);
    }
    if (l.knockout)
        o.insert(QStringLiteral("knockout"), true);
    if (!qFuzzyIsNull(l.trimStart) || !qFuzzyCompare(l.trimEnd, 1.0)) {
        o.insert(QStringLiteral("trimStart"), l.trimStart);
        o.insert(QStringLiteral("trimEnd"), l.trimEnd);
    }
    if (l.kind == TextLayerKind::Extrude) {
        o.insert(QStringLiteral("extrudeSteps"), l.extrudeSteps);
        o.insert(QStringLiteral("extrudeAngle"), l.extrudeAngle);
        o.insert(QStringLiteral("extrudeDarken"), l.extrudeDarken);
    }
    return o;
}

TextShadingLayer textShadingLayerFromJson(const QJsonObject &o)
{
    TextShadingLayer l;
    l.id = o.value(QStringLiteral("id")).toString().toLower();
    l.kind = textLayerKindFromString(o.value(QStringLiteral("kind")).toString());
    l.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    l.paint = textPaintFromJson(o.value(QStringLiteral("paint")).toObject());
    l.opacity = o.value(QStringLiteral("opacity")).toDouble(1.0);
    l.blend = blendModeFromString(o.value(QStringLiteral("blend")).toString());
    l.offsetX = o.value(QStringLiteral("offsetX")).toDouble(0.0);
    l.offsetY = o.value(QStringLiteral("offsetY")).toDouble(0.0);
    l.blur = o.value(QStringLiteral("blur")).toDouble(0.0);
    l.width = o.value(QStringLiteral("width")).toDouble(2.0);
    l.spread = o.value(QStringLiteral("spread")).toDouble(0.0);
    if (o.contains(QStringLiteral("strokeAlign")))
        l.strokeAlign = strokeAlignFromString(o.value(QStringLiteral("strokeAlign")).toString());
    else
        l.strokeAlign = o.value(QStringLiteral("strokeOutside")).toBool(true) ? StrokeAlign::Outside : StrokeAlign::Center;
    l.dash = strokeDashFromString(o.value(QStringLiteral("dash")).toString());
    l.dashOffset = o.value(QStringLiteral("dashOffset")).toDouble(0.0);
    l.sketchLength = o.value(QStringLiteral("sketchLength")).toDouble(0.0);
    l.sketchDeviation = o.value(QStringLiteral("sketchDeviation")).toDouble(0.0);
    l.sketchSeed = o.value(QStringLiteral("sketchSeed")).toInt(0);
    l.knockout = o.value(QStringLiteral("knockout")).toBool(false);
    l.trimStart = o.value(QStringLiteral("trimStart")).toDouble(0.0);
    l.trimEnd = o.value(QStringLiteral("trimEnd")).toDouble(1.0);
    l.extrudeSteps = o.value(QStringLiteral("extrudeSteps")).toInt(8);
    l.extrudeAngle = o.value(QStringLiteral("extrudeAngle")).toDouble(45.0);
    l.extrudeDarken = o.value(QStringLiteral("extrudeDarken")).toDouble(0.5);
    l.scope = textLayerScopeFromString(o.value(QStringLiteral("scope")).toString());
    return l;
}

// ---------------------------------------------------------------------------------------------
// Keyframe fields

bool parseLayerKey(const QString &key, LayerKeyPath *out)
{
    if (!key.startsWith(QLatin1String("layer.")))
        return false;
    const int dot = key.indexOf(QLatin1Char('.'), 6);
    if (dot < 0)
        return false;
    out->layerId = key.mid(6, dot - 6);
    out->field = key.mid(dot + 1);
    return !out->layerId.isEmpty() && !out->field.isEmpty();
}

QStringList shadingLayerKeyframeFields(const TextShadingLayer &layer)
{
    QStringList fields{QStringLiteral("opacity"), QStringLiteral("offsetX"), QStringLiteral("offsetY"),
                       QStringLiteral("blur")};
    if (layer.kind == TextLayerKind::Stroke || layer.kind == TextLayerKind::Extrude)
        fields.append(QStringLiteral("width"));
    if (layer.kind == TextLayerKind::Shadow || layer.kind == TextLayerKind::Glow)
        fields.append(QStringLiteral("spread"));
    if (layer.kind == TextLayerKind::Stroke)
        fields << QStringLiteral("trimStart") << QStringLiteral("trimEnd") << QStringLiteral("dashOffset");
    if (layer.kind == TextLayerKind::Stroke || layer.kind == TextLayerKind::Fill)
        fields << QStringLiteral("sketchLength") << QStringLiteral("sketchDeviation");
    switch (layer.paint.kind) {
    case TextPaintKind::Solid:
    case TextPaintKind::Texture:
        fields << QStringLiteral("color.r") << QStringLiteral("color.g") << QStringLiteral("color.b") << QStringLiteral("color.a");
        break;
    case TextPaintKind::Gradient:
        fields << QStringLiteral("gradient.angle") << QStringLiteral("gradient.offset") << QStringLiteral("gradient.scale")
               << QStringLiteral("gradient.center.x") << QStringLiteral("gradient.center.y");
        for (int i = 0; i < layer.paint.gradient.stops.size(); ++i)
            fields.append(QStringLiteral("gradient.stop.%1.pos").arg(i));
        break;
    case TextPaintKind::Effect:
        fields << QStringLiteral("color.r") << QStringLiteral("color.g") << QStringLiteral("color.b") << QStringLiteral("color.a");
        for (auto it = layer.paint.effect.params.constBegin(); it != layer.paint.effect.params.constEnd(); ++it) {
            if (it->type == VectorSlotValue::Type::Scalar)
                fields.append(QStringLiteral("effect.%1").arg(it.key()));
        }
        break;
    }
    return fields;
}

namespace {

// Paint-specific fields only exist for the paint kind that owns them, so a stray key on the
// wrong layer is unknown rather than silently absorbed.
bool fieldMatchesPaint(const TextShadingLayer &l, const QString &field)
{
    if (field.startsWith(QLatin1String("gradient.")))
        return l.paint.kind == TextPaintKind::Gradient;
    if (field.startsWith(QLatin1String("effect.")))
        return l.paint.kind == TextPaintKind::Effect;
    if (field.startsWith(QLatin1String("color.")))
        return l.paint.kind != TextPaintKind::Gradient;
    return true;
}

} // namespace

bool shadingLayerScalar(const TextShadingLayer &l, const QString &field, double *out)
{
    if (!fieldMatchesPaint(l, field))
        return false;
    if (field == QLatin1String("opacity"))
        *out = l.opacity;
    else if (field == QLatin1String("offsetX"))
        *out = l.offsetX;
    else if (field == QLatin1String("offsetY"))
        *out = l.offsetY;
    else if (field == QLatin1String("blur"))
        *out = l.blur;
    else if (field == QLatin1String("width"))
        *out = l.width;
    else if (field == QLatin1String("spread"))
        *out = l.spread;
    else if (field == QLatin1String("trimStart"))
        *out = l.trimStart;
    else if (field == QLatin1String("trimEnd"))
        *out = l.trimEnd;
    else if (field == QLatin1String("dashOffset"))
        *out = l.dashOffset;
    else if (field == QLatin1String("sketchLength"))
        *out = l.sketchLength;
    else if (field == QLatin1String("sketchDeviation"))
        *out = l.sketchDeviation;
    else if (field == QLatin1String("color.r"))
        *out = l.paint.color.redF();
    else if (field == QLatin1String("color.g"))
        *out = l.paint.color.greenF();
    else if (field == QLatin1String("color.b"))
        *out = l.paint.color.blueF();
    else if (field == QLatin1String("color.a"))
        *out = l.paint.color.alphaF();
    else if (field == QLatin1String("gradient.angle"))
        *out = l.paint.gradient.angle;
    else if (field == QLatin1String("gradient.offset"))
        *out = l.paint.gradient.offset;
    else if (field == QLatin1String("gradient.scale"))
        *out = l.paint.gradient.scale;
    else if (field == QLatin1String("gradient.center.x"))
        *out = l.paint.gradient.center.x();
    else if (field == QLatin1String("gradient.center.y"))
        *out = l.paint.gradient.center.y();
    else if (field.startsWith(QLatin1String("gradient.stop."))) {
        const QStringList parts = field.split(QLatin1Char('.'));
        const int i = parts.size() == 4 ? parts.at(2).toInt() : -1;
        if (i < 0 || i >= l.paint.gradient.stops.size() || parts.at(3) != QLatin1String("pos"))
            return false;
        *out = l.paint.gradient.stops.at(i).pos;
    } else if (field.startsWith(QLatin1String("effect."))) {
        const auto it = l.paint.effect.params.constFind(field.mid(7));
        if (it == l.paint.effect.params.constEnd() || it->type != VectorSlotValue::Type::Scalar)
            return false;
        *out = it->scalar;
    } else
        return false;
    return true;
}

bool setShadingLayerScalar(TextShadingLayer &l, const QString &field, double value)
{
    if (!fieldMatchesPaint(l, field))
        return false;
    if (field == QLatin1String("opacity"))
        l.opacity = qBound(0.0, value, 1.0);
    else if (field == QLatin1String("offsetX"))
        l.offsetX = value;
    else if (field == QLatin1String("offsetY"))
        l.offsetY = value;
    else if (field == QLatin1String("blur"))
        l.blur = qMax(0.0, value);
    else if (field == QLatin1String("width"))
        l.width = qMax(0.0, value);
    else if (field == QLatin1String("spread"))
        l.spread = value;
    else if (field == QLatin1String("trimStart"))
        l.trimStart = qBound(0.0, value, 1.0);
    else if (field == QLatin1String("trimEnd"))
        l.trimEnd = qBound(0.0, value, 1.0);
    else if (field == QLatin1String("dashOffset"))
        l.dashOffset = value;
    else if (field == QLatin1String("sketchLength"))
        l.sketchLength = qMax(0.0, value);
    else if (field == QLatin1String("sketchDeviation"))
        l.sketchDeviation = qMax(0.0, value);
    else if (field == QLatin1String("color.r"))
        l.paint.color.setRedF(qBound(0.0, value, 1.0));
    else if (field == QLatin1String("color.g"))
        l.paint.color.setGreenF(qBound(0.0, value, 1.0));
    else if (field == QLatin1String("color.b"))
        l.paint.color.setBlueF(qBound(0.0, value, 1.0));
    else if (field == QLatin1String("color.a"))
        l.paint.color.setAlphaF(qBound(0.0, value, 1.0));
    else if (field == QLatin1String("gradient.angle"))
        l.paint.gradient.angle = value;
    else if (field == QLatin1String("gradient.offset"))
        l.paint.gradient.offset = value;
    else if (field == QLatin1String("gradient.scale"))
        l.paint.gradient.scale = qMax(0.01, value);
    else if (field == QLatin1String("gradient.center.x"))
        l.paint.gradient.center.setX(value);
    else if (field == QLatin1String("gradient.center.y"))
        l.paint.gradient.center.setY(value);
    else if (field.startsWith(QLatin1String("gradient.stop."))) {
        const QStringList parts = field.split(QLatin1Char('.'));
        const int i = parts.size() == 4 ? parts.at(2).toInt() : -1;
        if (i < 0 || i >= l.paint.gradient.stops.size() || parts.at(3) != QLatin1String("pos"))
            return false;
        l.paint.gradient.stops[i].pos = qBound(0.0, value, 1.0);
    } else if (field.startsWith(QLatin1String("effect."))) {
        const auto it = l.paint.effect.params.find(field.mid(7));
        if (it == l.paint.effect.params.end() || it->type != VectorSlotValue::Type::Scalar)
            return false;
        it->scalar = value;
    } else
        return false;
    return true;
}

QString shadingLayerFieldLabel(const QString &field)
{
    static const QMap<QString, QString> labels{
        {QStringLiteral("opacity"), QCoreApplication::translate("TextStyle", "Opacity")},
        {QStringLiteral("offsetX"), QCoreApplication::translate("TextStyle", "Offset X")},
        {QStringLiteral("offsetY"), QCoreApplication::translate("TextStyle", "Offset Y")},
        {QStringLiteral("blur"), QCoreApplication::translate("TextStyle", "Blur")},
        {QStringLiteral("width"), QCoreApplication::translate("TextStyle", "Width")},
        {QStringLiteral("spread"), QCoreApplication::translate("TextStyle", "Spread")},
        {QStringLiteral("trimStart"), QCoreApplication::translate("TextStyle", "Trim start")},
        {QStringLiteral("trimEnd"), QCoreApplication::translate("TextStyle", "Trim end")},
        {QStringLiteral("dashOffset"), QCoreApplication::translate("TextStyle", "Dash offset")},
        {QStringLiteral("sketchLength"), QCoreApplication::translate("TextStyle", "Sketch length")},
        {QStringLiteral("sketchDeviation"), QCoreApplication::translate("TextStyle", "Sketch deviation")},
        {QStringLiteral("color.r"), QCoreApplication::translate("TextStyle", "Red")},
        {QStringLiteral("color.g"), QCoreApplication::translate("TextStyle", "Green")},
        {QStringLiteral("color.b"), QCoreApplication::translate("TextStyle", "Blue")},
        {QStringLiteral("color.a"), QCoreApplication::translate("TextStyle", "Alpha")},
        {QStringLiteral("gradient.angle"), QCoreApplication::translate("TextStyle", "Gradient angle")},
        {QStringLiteral("gradient.offset"), QCoreApplication::translate("TextStyle", "Gradient offset")},
        {QStringLiteral("gradient.scale"), QCoreApplication::translate("TextStyle", "Gradient scale")},
        {QStringLiteral("gradient.center.x"), QCoreApplication::translate("TextStyle", "Centre X")},
        {QStringLiteral("gradient.center.y"), QCoreApplication::translate("TextStyle", "Centre Y")},
    };
    const auto it = labels.constFind(field);
    if (it != labels.constEnd())
        return *it;
    if (field.startsWith(QLatin1String("gradient.stop.")))
        return QCoreApplication::translate("TextStyle", "Stop %1").arg(field.section(QLatin1Char('.'), 2, 2).toInt() + 1);
    if (field.startsWith(QLatin1String("effect.")))
        return field.mid(7);
    return field;
}

QString shadingLayerKindLabel(TextLayerKind kind)
{
    switch (kind) {
    case TextLayerKind::Fill:
        return QCoreApplication::translate("TextStyle", "Fill");
    case TextLayerKind::Stroke:
        return QCoreApplication::translate("TextStyle", "Stroke");
    case TextLayerKind::Shadow:
        return QCoreApplication::translate("TextStyle", "Shadow");
    case TextLayerKind::Glow:
        return QCoreApplication::translate("TextStyle", "Glow");
    case TextLayerKind::Extrude:
        return QCoreApplication::translate("TextStyle", "Extrude");
    }
    return {};
}

QString shadingLayerKeyframeLabel(const QList<TextShadingLayer> &layers, const QString &key)
{
    LayerKeyPath path;
    if (!parseLayerKey(key, &path))
        return key;
    const TextShadingLayer *layer = findTextLayer(layers, path.layerId);
    if (!layer)
        return key;
    int ordinal = 0, sameKind = 0;
    for (const TextShadingLayer &other : layers) {
        if (other.kind != layer->kind)
            continue;
        ++sameKind;
        if (&other == layer)
            ordinal = sameKind;
    }
    QString name = shadingLayerKindLabel(layer->kind);
    if (sameKind > 1)
        name += QStringLiteral(" %1").arg(ordinal);
    return name + QStringLiteral(" · ") + shadingLayerFieldLabel(path.field);
}

void eraseLayerKeyframes(QMap<QString, KeyframeTrack<double>> &keyframes, const QString &layerId)
{
    const QString prefix = QStringLiteral("layer.") + layerId + QLatin1Char('.');
    for (auto it = keyframes.begin(); it != keyframes.end();) {
        if (it.key().startsWith(prefix))
            it = keyframes.erase(it);
        else
            ++it;
    }
}

} // namespace drift
