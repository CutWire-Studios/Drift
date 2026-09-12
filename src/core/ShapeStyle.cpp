#include "ShapeStyle.h"

#include "Effect.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <algorithm>

namespace drift {
namespace {

struct KindName
{
    ShapeKind kind;
    const char *name;
};

// The first five names are what projects saved before the roster grew still contain, so they must
// not change.
constexpr KindName kKindNames[] = {
    {ShapeKind::Rectangle, "rectangle"},
    {ShapeKind::Square, "square"},
    {ShapeKind::Triangle, "triangle"},
    {ShapeKind::Pentagon, "pentagon"},
    {ShapeKind::Hexagon, "hexagon"},
    {ShapeKind::RoundedRectangle, "rounded-rectangle"},
    {ShapeKind::Ellipse, "ellipse"},
    {ShapeKind::RightTriangle, "right-triangle"},
    {ShapeKind::Diamond, "diamond"},
    {ShapeKind::Octagon, "octagon"},
    {ShapeKind::Parallelogram, "parallelogram"},
    {ShapeKind::Trapezoid, "trapezoid"},
    {ShapeKind::Arrow, "arrow"},
    {ShapeKind::DoubleArrow, "double-arrow"},
    {ShapeKind::BlockArrow, "block-arrow"},
    {ShapeKind::CurvedArrow, "curved-arrow"},
    {ShapeKind::Chevron, "chevron"},
    {ShapeKind::SpeechBubble, "speech-bubble"},
    {ShapeKind::SpeechBubbleRect, "speech-bubble-rect"},
    {ShapeKind::ThoughtBubble, "thought-bubble"},
    {ShapeKind::Callout, "callout"},
    {ShapeKind::Star, "star"},
    {ShapeKind::LightningBolt, "lightning-bolt"},
    {ShapeKind::Cloud, "cloud"},
    {ShapeKind::Heart, "heart"},
    {ShapeKind::Cross, "cross"},
    {ShapeKind::Burst, "burst"},
    {ShapeKind::Banner, "banner"},
};

ShapeStyle makeStyle(ShapeKind kind, const QColor &fill, const QColor &fillSecondary)
{
    ShapeStyle style;
    style.kind = kind;
    style.layers = defaultShapeLayers(fill, fillSecondary);
    return style;
}

QList<ShapeCatalogEntry> buildCatalog()
{
    const QColor blue(0, 180, 255);
    const QColor blueDeep(0, 92, 220);
    const QColor orange(255, 120, 64);
    const QColor orangeDeep(220, 60, 40);
    const QColor yellow(255, 214, 10);
    const QColor yellowDeep(240, 140, 0);
    const QColor purple(160, 96, 255);
    const QColor purpleDeep(96, 40, 200);
    const QColor green(80, 220, 140);
    const QColor greenDeep(20, 150, 110);
    const QColor pink(255, 96, 150);
    const QColor pinkDeep(200, 30, 110);

    QList<ShapeCatalogEntry> catalog{
        {QStringLiteral("rectangle"), QCoreApplication::translate("ShapeStyle", "Rectangle"), QStringLiteral("basic"), 1.6,
         makeStyle(ShapeKind::Rectangle, blue, blueDeep)},
        {QStringLiteral("rounded-rectangle"), QCoreApplication::translate("ShapeStyle", "Rounded rectangle"),
         QStringLiteral("basic"), 1.6, makeStyle(ShapeKind::RoundedRectangle, blue, blueDeep)},
        {QStringLiteral("square"), QCoreApplication::translate("ShapeStyle", "Square"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Square, orange, orangeDeep)},
        // "ellipse" leads "circle" because the inspector's kind list keeps the first entry of each
        // ShapeKind, and "Ellipse" is the honest label for both.
        {QStringLiteral("ellipse"), QCoreApplication::translate("ShapeStyle", "Ellipse"), QStringLiteral("basic"), 1.6,
         makeStyle(ShapeKind::Ellipse, yellow, yellowDeep)},
        {QStringLiteral("circle"), QCoreApplication::translate("ShapeStyle", "Circle"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Ellipse, yellow, yellowDeep)},
        {QStringLiteral("triangle"), QCoreApplication::translate("ShapeStyle", "Triangle"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Triangle, yellow, yellowDeep)},
        {QStringLiteral("right-triangle"), QCoreApplication::translate("ShapeStyle", "Right triangle"), QStringLiteral("basic"),
         1.0, makeStyle(ShapeKind::RightTriangle, yellow, yellowDeep)},
        {QStringLiteral("diamond"), QCoreApplication::translate("ShapeStyle", "Diamond"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Diamond, blue, blueDeep)},
        {QStringLiteral("pentagon"), QCoreApplication::translate("ShapeStyle", "Pentagon"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Pentagon, purple, purpleDeep)},
        {QStringLiteral("hexagon"), QCoreApplication::translate("ShapeStyle", "Hexagon"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Hexagon, green, greenDeep)},
        {QStringLiteral("octagon"), QCoreApplication::translate("ShapeStyle", "Octagon"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Octagon, green, greenDeep)},
        {QStringLiteral("parallelogram"), QCoreApplication::translate("ShapeStyle", "Parallelogram"), QStringLiteral("basic"),
         1.6, makeStyle(ShapeKind::Parallelogram, blue, blueDeep)},
        {QStringLiteral("trapezoid"), QCoreApplication::translate("ShapeStyle", "Trapezoid"), QStringLiteral("basic"), 1.6,
         makeStyle(ShapeKind::Trapezoid, blue, blueDeep)},

        {QStringLiteral("arrow"), QCoreApplication::translate("ShapeStyle", "Arrow"), QStringLiteral("arrows"), 2.0,
         makeStyle(ShapeKind::Arrow, orange, orangeDeep)},
        {QStringLiteral("double-arrow"), QCoreApplication::translate("ShapeStyle", "Double arrow"), QStringLiteral("arrows"),
         2.0, makeStyle(ShapeKind::DoubleArrow, orange, orangeDeep)},
        {QStringLiteral("block-arrow"), QCoreApplication::translate("ShapeStyle", "Block arrow"), QStringLiteral("arrows"), 1.6,
         makeStyle(ShapeKind::BlockArrow, orange, orangeDeep)},
        {QStringLiteral("curved-arrow"), QCoreApplication::translate("ShapeStyle", "Curved arrow"), QStringLiteral("arrows"),
         1.3, makeStyle(ShapeKind::CurvedArrow, orange, orangeDeep)},
        {QStringLiteral("chevron"), QCoreApplication::translate("ShapeStyle", "Chevron"), QStringLiteral("arrows"), 1.4,
         makeStyle(ShapeKind::Chevron, orange, orangeDeep)},

        {QStringLiteral("speech-bubble"), QCoreApplication::translate("ShapeStyle", "Speech bubble"), QStringLiteral("bubbles"),
         1.4, makeStyle(ShapeKind::SpeechBubble, blue, blueDeep)},
        {QStringLiteral("speech-bubble-rect"), QCoreApplication::translate("ShapeStyle", "Rounded bubble"),
         QStringLiteral("bubbles"), 1.5, makeStyle(ShapeKind::SpeechBubbleRect, blue, blueDeep)},
        {QStringLiteral("thought-bubble"), QCoreApplication::translate("ShapeStyle", "Thought bubble"),
         QStringLiteral("bubbles"), 1.4, makeStyle(ShapeKind::ThoughtBubble, blue, blueDeep)},
        {QStringLiteral("callout"), QCoreApplication::translate("ShapeStyle", "Callout"), QStringLiteral("bubbles"), 1.5,
         makeStyle(ShapeKind::Callout, purple, purpleDeep)},

        {QStringLiteral("star"), QCoreApplication::translate("ShapeStyle", "Star"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Star, yellow, yellowDeep)},
        {QStringLiteral("burst"), QCoreApplication::translate("ShapeStyle", "Burst"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Burst, yellow, orangeDeep)},
        {QStringLiteral("lightning-bolt"), QCoreApplication::translate("ShapeStyle", "Lightning bolt"), QStringLiteral("fun"),
         0.6, makeStyle(ShapeKind::LightningBolt, yellow, orangeDeep)},
        {QStringLiteral("cloud"), QCoreApplication::translate("ShapeStyle", "Cloud"), QStringLiteral("fun"), 1.6,
         makeStyle(ShapeKind::Cloud, QColor(235, 245, 255), QColor(150, 190, 235))},
        {QStringLiteral("heart"), QCoreApplication::translate("ShapeStyle", "Heart"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Heart, pink, pinkDeep)},
        {QStringLiteral("cross"), QCoreApplication::translate("ShapeStyle", "Cross"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Cross, pink, pinkDeep)},
        {QStringLiteral("banner"), QCoreApplication::translate("ShapeStyle", "Banner"), QStringLiteral("fun"), 2.2,
         makeStyle(ShapeKind::Banner, purple, purpleDeep)},
    };

    // Per-kind geometry defaults. Everything not listed keeps the ShapeStyle defaults.
    for (ShapeCatalogEntry &entry : catalog) {
        switch (entry.style.kind) {
        case ShapeKind::RoundedRectangle:
        case ShapeKind::SpeechBubbleRect:
        case ShapeKind::Callout:
            entry.style.cornerRadius = 32.0;
            break;
        case ShapeKind::Burst:
            entry.style.points = 12;
            entry.style.innerRatio = 0.62;
            break;
        case ShapeKind::Cross:
            entry.style.thickness = 0.36;
            break;
        case ShapeKind::Chevron:
            entry.style.headSize = 0.3;
            break;
        case ShapeKind::CurvedArrow:
            entry.style.thickness = 0.35;
            break;
        default:
            break;
        }
    }

    return catalog;
}

} // namespace

QString shapeKindToString(ShapeKind kind)
{
    for (const KindName &entry : kKindNames) {
        if (entry.kind == kind)
            return QString::fromLatin1(entry.name);
    }
    return QStringLiteral("rectangle");
}

ShapeKind shapeKindFromString(const QString &kind)
{
    for (const KindName &entry : kKindNames) {
        if (kind == QLatin1String(entry.name))
            return entry.kind;
    }
    // Catalog ids are not all kind names ("circle" is an ellipse), and the UI addresses shapes by
    // id, so fall back to a catalog lookup before giving up.
    if (const ShapeCatalogEntry *entry = shapeCatalogEntry(kind))
        return entry->style.kind;
    return ShapeKind::Rectangle;
}

const QList<ShapeCatalogEntry> &shapeCatalog()
{
    static const QList<ShapeCatalogEntry> catalog = buildCatalog();
    return catalog;
}

const ShapeCatalogEntry *shapeCatalogEntry(const QString &id)
{
    const QList<ShapeCatalogEntry> &catalog = shapeCatalog();
    const auto it = std::find_if(catalog.cbegin(), catalog.cend(),
                                 [&](const ShapeCatalogEntry &e) { return e.id == id; });
    return it == catalog.cend() ? nullptr : &(*it);
}

QList<ShapeCategory> shapeCategories()
{
    return {
        {QStringLiteral("basic"), QCoreApplication::translate("ShapeStyle", "Basic")},
        {QStringLiteral("arrows"), QCoreApplication::translate("ShapeStyle", "Arrows")},
        {QStringLiteral("bubbles"), QCoreApplication::translate("ShapeStyle", "Bubbles")},
        {QStringLiteral("fun"), QCoreApplication::translate("ShapeStyle", "Fun")},
    };
}

// ---------------------------------------------------------------------------------------------
// Layers

QList<TextShadingLayer> defaultShapeLayers(const QColor &fill, const QColor &fillDeep, const QColor &stroke,
                                           double strokeWidth)
{
    TextShadingLayer fillLayer = solidFillLayer(fill, QStringLiteral("fill"));
    fillLayer.paint.gradient.stops = {{0.0, fill}, {1.0, fillDeep.isValid() ? fillDeep : fill.darker(160)}};
    TextShadingLayer strokeLayer_ = strokeLayer(qMax(0.0, strokeWidth), stroke, QStringLiteral("stroke"));
    strokeLayer_.strokeAlign = StrokeAlign::Inside;
    strokeLayer_.enabled = strokeWidth > 0.0;
    return {fillLayer, strokeLayer_};
}

QColor ShapeStyle::primaryColor() const
{
    if (const TextShadingLayer *fill = firstTextLayerOfKind(layers, TextLayerKind::Fill))
        return fill->paint.kind == TextPaintKind::Gradient && !fill->paint.gradient.stops.isEmpty()
                   ? fill->paint.gradient.stops.first().color
                   : fill->paint.color;
    return QColor(0, 180, 255);
}

void ShapeStyle::setPrimaryColor(const QColor &color)
{
    if (TextShadingLayer *fill = firstTextLayerOfKind(layers, TextLayerKind::Fill, false)) {
        fill->paint.color = color;
        if (!fill->paint.gradient.stops.isEmpty())
            fill->paint.gradient.stops.first().color = color;
        return;
    }
    layers.prepend(solidFillLayer(color, QStringLiteral("fill")));
}

void ShapeStyle::setSolidFill(const QColor &color)
{
    const TextShadingLayer *stroke = firstTextLayerOfKind(layers, TextLayerKind::Stroke, false);
    layers = defaultShapeLayers(color, QColor(), stroke ? stroke->paint.color : QColor(Qt::white),
                                stroke && stroke->enabled ? stroke->width : 0.0);
}

void ShapeStyle::setStroke(double width, const QColor &color)
{
    TextShadingLayer *stroke = firstTextLayerOfKind(layers, TextLayerKind::Stroke, false);
    if (!stroke) {
        TextShadingLayer layer = strokeLayer(width, color, QStringLiteral("stroke"));
        layer.strokeAlign = StrokeAlign::Inside;
        layers.append(layer);
        stroke = &layers.last();
    }
    stroke->width = qMax(0.0, width);
    stroke->paint.color = color;
    stroke->enabled = width > 0.0;
}

// ---------------------------------------------------------------------------------------------
// Keyframes

namespace {

struct KnobField
{
    const char *key;
    const char *label;
    double ShapeStyle::*field;
    double min;
    double max;
};

// points is an int and handled apart from the doubles.
constexpr KnobField kKnobs[] = {
    {"cornerRadius", QT_TRANSLATE_NOOP("ShapeStyle", "Corner radius"), &ShapeStyle::cornerRadius, 0.0, 2000.0},
    {"innerRatio", QT_TRANSLATE_NOOP("ShapeStyle", "Inner radius"), &ShapeStyle::innerRatio, 0.05, 0.95},
    {"headSize", QT_TRANSLATE_NOOP("ShapeStyle", "Head size"), &ShapeStyle::headSize, 0.05, 0.9},
    {"thickness", QT_TRANSLATE_NOOP("ShapeStyle", "Thickness"), &ShapeStyle::thickness, 0.05, 1.0},
    {"tailX", QT_TRANSLATE_NOOP("ShapeStyle", "Tail position"), &ShapeStyle::tailX, 0.08, 0.92},
    {"tailSize", QT_TRANSLATE_NOOP("ShapeStyle", "Tail size"), &ShapeStyle::tailSize, 0.05, 0.5},
};

const KnobField *knobFor(const QString &key)
{
    for (const KnobField &knob : kKnobs)
        if (key == QLatin1String(knob.key))
            return &knob;
    return nullptr;
}

} // namespace

QStringList shapeKeyframeProperties(const ShapeStyle &style)
{
    QStringList out;
    for (const KnobField &knob : kKnobs)
        out.append(QLatin1String(knob.key));
    out.append(QStringLiteral("points"));
    for (const TextShadingLayer &layer : style.layers) {
        for (const QString &field : shadingLayerKeyframeFields(layer))
            out.append(QStringLiteral("layer.%1.%2").arg(layer.id, field));
    }
    return out;
}

QString shapeKeyframeCanonicalKey(const QString &key, const ShapeStyle &style)
{
    if (knobFor(key) || key == QLatin1String("points"))
        return key;
    LayerKeyPath path;
    if (!parseLayerKey(key, &path))
        return {};
    const TextShadingLayer *layer = findTextLayer(style.layers, path.layerId);
    if (!layer)
        return {};
    double probe = 0.0;
    return shadingLayerScalar(*layer, path.field, &probe) ? key : QString();
}

QString shapeKeyframeLabel(const QString &key, const ShapeStyle &style)
{
    if (const KnobField *knob = knobFor(key))
        return QCoreApplication::translate("ShapeStyle", knob->label);
    if (key == QLatin1String("points"))
        return QCoreApplication::translate("ShapeStyle", "Points");
    return shadingLayerKeyframeLabel(style.layers, key);
}

bool shapeStyleScalar(const ShapeStyle &style, const QString &key, double *out)
{
    if (const KnobField *knob = knobFor(key)) {
        *out = style.*(knob->field);
        return true;
    }
    if (key == QLatin1String("points")) {
        *out = style.points;
        return true;
    }
    LayerKeyPath path;
    if (!parseLayerKey(key, &path))
        return false;
    const TextShadingLayer *layer = findTextLayer(style.layers, path.layerId);
    return layer && shadingLayerScalar(*layer, path.field, out);
}

bool setShapeStyleScalar(ShapeStyle &style, const QString &key, double value)
{
    if (const KnobField *knob = knobFor(key)) {
        style.*(knob->field) = qBound(knob->min, value, knob->max);
        return true;
    }
    if (key == QLatin1String("points")) {
        style.points = qBound(3, qRound(value), 60);
        return true;
    }
    LayerKeyPath path;
    if (!parseLayerKey(key, &path))
        return false;
    TextShadingLayer *layer = findTextLayer(style.layers, path.layerId);
    return layer && setShadingLayerScalar(*layer, path.field, value);
}

bool ShapeStyle::isAnimated() const
{
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            return true;
    }
    return false;
}

ShapeStyle ShapeStyle::resolvedAt(TimeUs clipTimeUs) const
{
    ShapeStyle out = *this;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            setShapeStyleScalar(out, it.key(), it->evaluateAt(clipTimeUs));
    }
    return out;
}

quint64 shapeStyleHash(const ShapeStyle &style)
{
    return qHashMulti(0, static_cast<int>(style.kind), textLayersHash(style.layers), style.cornerRadius, style.points,
                      style.innerRatio, style.headSize, style.thickness, style.tailX, style.tailSize);
}

bool shapeTimeDrivenPaint(const ShapeStyle &style)
{
    for (const TextShadingLayer &layer : style.layers) {
        if (!layer.enabled)
            continue;
        if (layer.paint.kind == TextPaintKind::Gradient && !qFuzzyIsNull(layer.paint.gradient.offsetSpeed))
            return true;
        if (layer.paint.kind == TextPaintKind::Effect) {
            const TextEffectSpec *spec = textShaderEffectSpec(layer.paint.effect.id);
            if (spec && spec->timeDriven)
                return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// JSON

QJsonObject shapeStyleToJson(const ShapeStyle &s)
{
    QJsonArray layers;
    for (const TextShadingLayer &layer : s.layers)
        layers.append(textShadingLayerToJson(layer));
    QJsonObject o{
        {QStringLiteral("kind"), shapeKindToString(s.kind)},
        {QStringLiteral("layers"), layers},
        {QStringLiteral("cornerRadius"), s.cornerRadius},
        {QStringLiteral("points"), s.points},
        {QStringLiteral("innerRatio"), s.innerRatio},
        {QStringLiteral("headSize"), s.headSize},
        {QStringLiteral("thickness"), s.thickness},
        {QStringLiteral("tailX"), s.tailX},
        {QStringLiteral("tailSize"), s.tailSize},
    };
    QJsonObject keyframesJson;
    for (auto it = s.keyframes.constBegin(); it != s.keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            keyframesJson.insert(it.key(), keyframesToJson(it.value()));
    }
    if (!keyframesJson.isEmpty())
        o.insert(QStringLiteral("keyframes"), keyframesJson);
    return o;
}

namespace {

// The flat look projects before format 8 wrote: one fill (none / solid / two-stop linear or
// radial gradient) under one stroke. Disabled layers are still created so toggling one back on
// restores the old colour or width. The old stroke was inset by half its width so it stayed
// inside the layout rect, which is what Inside reproduces.
QList<TextShadingLayer> legacyShapeLayersFromJson(const QJsonObject &o)
{
    const auto color = [&](const char *key, const QColor &fallback) {
        return QColor(o.value(QLatin1String(key)).toString(fallback.name(QColor::HexArgb)));
    };
    const QColor fill = color("fill", QColor(0, 180, 255));
    const QColor secondary = color("fillSecondary", QColor(122, 0, 255));
    const QString fillKind = o.value(QStringLiteral("fillKind")).toString(QStringLiteral("solid"));
    const QString strokeStyle = o.value(QStringLiteral("strokeStyle")).toString(QStringLiteral("solid"));

    QList<TextShadingLayer> layers = defaultShapeLayers(fill, secondary, color("stroke", Qt::white),
                                                        o.value(QStringLiteral("strokeWidth")).toDouble(4.0));
    TextShadingLayer &fillLayer = layers[0];
    TextShadingLayer &strokeLayer_ = layers[1];
    fillLayer.paint.gradient.angle = o.value(QStringLiteral("gradientAngle")).toDouble(90.0);
    if (fillKind == QLatin1String("none")) {
        fillLayer.enabled = false;
    } else if (fillKind == QLatin1String("linear") || fillKind == QLatin1String("radial")) {
        fillLayer.paint.kind = TextPaintKind::Gradient;
        fillLayer.paint.gradient.kind = fillKind == QLatin1String("radial") ? TextGradientKind::Radial
                                                                            : TextGradientKind::Linear;
    }
    if (strokeStyle == QLatin1String("none"))
        strokeLayer_.enabled = false;
    else
        strokeLayer_.dash = strokeDashFromString(strokeStyle);
    return layers;
}

} // namespace

ShapeStyle shapeStyleFromJson(const QJsonObject &o)
{
    ShapeStyle s;
    if (o.isEmpty())
        return s;
    s.kind = shapeKindFromString(o.value(QStringLiteral("kind")).toString());
    if (o.contains(QStringLiteral("layers"))) {
        s.layers.clear();
        for (const QJsonValue &v : o.value(QStringLiteral("layers")).toArray())
            s.layers.append(textShadingLayerFromJson(v.toObject()));
        for (TextShadingLayer &layer : s.layers) {
            if (layer.id.isEmpty())
                layer.id = mintTextLayerId(s.layers);
        }
    } else {
        s.layers = legacyShapeLayersFromJson(o);
    }
    // Geometry knobs default to the struct value, so a project saved before shapes gained them
    // still loads.
    s.cornerRadius = o.value(QStringLiteral("cornerRadius")).toDouble(s.cornerRadius);
    s.points = o.value(QStringLiteral("points")).toInt(s.points);
    s.innerRatio = o.value(QStringLiteral("innerRatio")).toDouble(s.innerRatio);
    s.headSize = o.value(QStringLiteral("headSize")).toDouble(s.headSize);
    s.thickness = o.value(QStringLiteral("thickness")).toDouble(s.thickness);
    s.tailX = o.value(QStringLiteral("tailX")).toDouble(s.tailX);
    s.tailSize = o.value(QStringLiteral("tailSize")).toDouble(s.tailSize);
    const QJsonObject keyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = keyframesJson.constBegin(); it != keyframesJson.constEnd(); ++it) {
        const QString canonical = shapeKeyframeCanonicalKey(it.key(), s);
        if (!canonical.isEmpty())
            s.keyframes.insert(canonical, keyframesFromJson(it.value().toObject()));
    }
    return s;
}

} // namespace drift
