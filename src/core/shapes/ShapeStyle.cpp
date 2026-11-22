#include "core/shapes/ShapeStyle.h"

#include "core/effects/Effect.h"

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
    {ShapeKind::Path, "path"},
    {ShapeKind::Ring, "ring"},
    {ShapeKind::Pie, "pie"},
    {ShapeKind::Arc, "arc"},
    {ShapeKind::Crescent, "crescent"},
    {ShapeKind::Pill, "pill"},
    {ShapeKind::Frame, "frame"},
    {ShapeKind::Polygon, "polygon"},
    {ShapeKind::Gear, "gear"},
    {ShapeKind::Seal, "seal"},
    {ShapeKind::Shield, "shield"},
    {ShapeKind::Teardrop, "teardrop"},
    {ShapeKind::LocationPin, "location-pin"},
    {ShapeKind::Tag, "tag"},
    {ShapeKind::Bookmark, "bookmark"},
    {ShapeKind::Checkmark, "checkmark"},
    {ShapeKind::XMark, "x-mark"},
};

ShapeStyle makeStyle(ShapeKind kind, const QColor &fill, const QColor &fillSecondary)
{
    ShapeStyle style;
    style.kind = kind;
    style.layers = defaultShapeLayers(fill, fillSecondary);
    return style;
}

ShapeVertex corner(double x, double y)
{
    return {QPointF(x, y), {}, {}, TangentMode::Corner};
}

// A smooth vertex with its tangent handles mirrored.
ShapeVertex smoothVertex(double x, double y, double dx, double dy)
{
    return {QPointF(x, y), QPointF(-dx, -dy), QPointF(dx, dy), TangentMode::Mirrored};
}

// Open stroke-only path: fill off, round caps, centred so the width reads the same on both sides.
ShapeStyle makeLineStyle(const QVector<ShapeVertex> &vertices)
{
    ShapeStyle style;
    style.kind = ShapeKind::Path;
    style.layers = defaultShapeLayers(QColor(0, 180, 255), QColor(), Qt::white, 8.0);
    style.layers[0].enabled = false;
    style.layers[1].strokeAlign = StrokeAlign::Center;
    style.layers[1].cap = StrokeCap::Round;
    style.contours = {ShapeContour{vertices, false}};
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
        {QStringLiteral("pill"), QCoreApplication::translate("ShapeStyle", "Pill"), QStringLiteral("basic"), 2.4,
         makeStyle(ShapeKind::Pill, green, greenDeep)},
        {QStringLiteral("polygon"), QCoreApplication::translate("ShapeStyle", "Polygon"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Polygon, purple, purpleDeep)},
        {QStringLiteral("ring"), QCoreApplication::translate("ShapeStyle", "Ring"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Ring, orange, orangeDeep)},
        {QStringLiteral("frame"), QCoreApplication::translate("ShapeStyle", "Frame"), QStringLiteral("basic"), 1.6,
         makeStyle(ShapeKind::Frame, blue, blueDeep)},
        {QStringLiteral("pie"), QCoreApplication::translate("ShapeStyle", "Pie"), QStringLiteral("basic"), 1.0,
         makeStyle(ShapeKind::Pie, yellow, yellowDeep)},
        {QStringLiteral("arc"), QCoreApplication::translate("ShapeStyle", "Arc"), QStringLiteral("basic"), 1.6,
         makeStyle(ShapeKind::Arc, orange, orangeDeep)},
        {QStringLiteral("crescent"), QCoreApplication::translate("ShapeStyle", "Crescent"), QStringLiteral("basic"), 0.9,
         makeStyle(ShapeKind::Crescent, yellow, yellowDeep)},

        {QStringLiteral("line"), QCoreApplication::translate("ShapeStyle", "Line"), QStringLiteral("lines"), 12.0,
         makeLineStyle({corner(0.0, 0.5), corner(1.0, 0.5)})},
        {QStringLiteral("curve"), QCoreApplication::translate("ShapeStyle", "Curve"), QStringLiteral("lines"), 1.6,
         makeLineStyle({{QPointF(0.0, 1.0), {}, QPointF(0.5, 0.0), TangentMode::Mirrored}, {QPointF(1.0, 0.0), QPointF(-0.5, 0.0), {}, TangentMode::Mirrored}})},
        {QStringLiteral("wave"), QCoreApplication::translate("ShapeStyle", "Wave"), QStringLiteral("lines"), 4.0,
         makeLineStyle({{QPointF(0.0, 0.5), {}, QPointF(0.0833, -0.5), TangentMode::Mirrored},
                        smoothVertex(0.25, 0.0, 0.0833, 0.0),
                        smoothVertex(0.5, 1.0, 0.0833, 0.0),
                        smoothVertex(0.75, 0.0, 0.0833, 0.0),
                        {QPointF(1.0, 0.5), QPointF(-0.0833, 0.5), {}, TangentMode::Mirrored}})},
        {QStringLiteral("zigzag"), QCoreApplication::translate("ShapeStyle", "Zigzag"), QStringLiteral("lines"), 4.0,
         makeLineStyle({corner(0.0, 1.0), corner(0.2, 0.0), corner(0.4, 1.0), corner(0.6, 0.0), corner(0.8, 1.0),
                        corner(1.0, 0.0)})},

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
        {QStringLiteral("gear"), QCoreApplication::translate("ShapeStyle", "Gear"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Gear, QColor(170, 180, 195), QColor(100, 110, 130))},
        {QStringLiteral("seal"), QCoreApplication::translate("ShapeStyle", "Badge"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::Seal, yellow, yellowDeep)},
        {QStringLiteral("shield"), QCoreApplication::translate("ShapeStyle", "Shield"), QStringLiteral("fun"), 0.85,
         makeStyle(ShapeKind::Shield, blue, blueDeep)},
        {QStringLiteral("teardrop"), QCoreApplication::translate("ShapeStyle", "Teardrop"), QStringLiteral("fun"), 0.7,
         makeStyle(ShapeKind::Teardrop, blue, blueDeep)},
        {QStringLiteral("location-pin"), QCoreApplication::translate("ShapeStyle", "Location pin"), QStringLiteral("fun"),
         0.72, makeStyle(ShapeKind::LocationPin, orange, orangeDeep)},
        {QStringLiteral("tag"), QCoreApplication::translate("ShapeStyle", "Tag"), QStringLiteral("fun"), 1.4,
         makeStyle(ShapeKind::Tag, green, greenDeep)},
        {QStringLiteral("bookmark"), QCoreApplication::translate("ShapeStyle", "Bookmark"), QStringLiteral("fun"), 0.7,
         makeStyle(ShapeKind::Bookmark, pink, pinkDeep)},
        {QStringLiteral("checkmark"), QCoreApplication::translate("ShapeStyle", "Checkmark"), QStringLiteral("fun"), 1.2,
         makeStyle(ShapeKind::Checkmark, green, greenDeep)},
        {QStringLiteral("x-mark"), QCoreApplication::translate("ShapeStyle", "X mark"), QStringLiteral("fun"), 1.0,
         makeStyle(ShapeKind::XMark, pink, pinkDeep)},
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
        case ShapeKind::Polygon:
            entry.style.points = 7;
            break;
        case ShapeKind::Ring:
            entry.style.thickness = 0.3;
            break;
        case ShapeKind::Frame:
            entry.style.thickness = 0.2;
            break;
        case ShapeKind::Pie:
            entry.style.sweep = 270.0;
            break;
        case ShapeKind::Arc:
            entry.style.sweep = 200.0;
            entry.style.thickness = 0.25;
            break;
        case ShapeKind::Gear:
            entry.style.points = 8;
            entry.style.innerRatio = 0.78;
            break;
        case ShapeKind::Seal:
            entry.style.points = 14;
            entry.style.innerRatio = 0.9;
            break;
        case ShapeKind::Checkmark:
            entry.style.thickness = 0.3;
            break;
        case ShapeKind::XMark:
            entry.style.thickness = 0.28;
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

ShapeStyle pathShapeStyle(const QVector<ShapeContour> &contours, Qt::FillRule rule)
{
    const bool allOpen = !contours.isEmpty()
        && std::all_of(contours.cbegin(), contours.cend(), [](const ShapeContour &c) { return !c.closed; });
    ShapeStyle style;
    if (allOpen) {
        if (const ShapeCatalogEntry *line = shapeCatalogEntry(QStringLiteral("line")))
            style.layers = line->style.layers;
    }
    style.kind = ShapeKind::Path;
    style.contours = contours;
    style.fillRule = rule;
    return style;
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
        {QStringLiteral("lines"), QCoreApplication::translate("ShapeStyle", "Lines")},
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
    {"sweep", QT_TRANSLATE_NOOP("ShapeStyle", "Sweep"), &ShapeStyle::sweep, 1.0, 359.0},
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
    if (!pathKeys.isEmpty())
        return true;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            return true;
    }
    return false;
}

namespace {

bool sameStructure(const QVector<ShapeContour> &a, const QVector<ShapeContour> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i).closed != b.at(i).closed || a.at(i).vertices.size() != b.at(i).vertices.size())
            return false;
    }
    return true;
}

// The contours between the two keys around `timeUs`. A structure mismatch means the path was
// reshaped rather than moved, so the earlier key holds until the later one arrives (as Mask does).
QVector<ShapeContour> contoursAt(const QMap<TimeUs, QVector<ShapeContour>> &keys, TimeUs timeUs)
{
    if (keys.size() == 1)
        return keys.first();
    auto after = keys.lowerBound(timeUs);
    if (after == keys.constEnd())
        return std::prev(after).value();
    if (after.key() == timeUs || after == keys.constBegin())
        return after.value();

    const auto before = std::prev(after);
    const QVector<ShapeContour> &a = before.value();
    const QVector<ShapeContour> &b = after.value();
    if (!sameStructure(a, b))
        return a;

    const double span = double(after.key() - before.key());
    const double t = span > 0.0 ? double(timeUs - before.key()) / span : 0.0;
    QVector<ShapeContour> out = a;
    for (int c = 0; c < out.size(); ++c) {
        for (int v = 0; v < out[c].vertices.size(); ++v) {
            ShapeVertex &r = out[c].vertices[v];
            const ShapeVertex &vb = b.at(c).vertices.at(v);
            r.p += (vb.p - r.p) * t;
            r.in += (vb.in - r.in) * t;
            r.out += (vb.out - r.out) * t;
        }
    }
    return out;
}

} // namespace

ShapeStyle ShapeStyle::resolvedAt(TimeUs clipTimeUs) const
{
    ShapeStyle out = *this;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            setShapeStyleScalar(out, it.key(), it->evaluateAt(clipTimeUs));
    }
    if (!pathKeys.isEmpty()) {
        out.contours = contoursAt(pathKeys, clipTimeUs);
        out.pathKeys.clear();
    }
    return out;
}

quint64 shapeStyleHash(const ShapeStyle &style)
{
    quint64 h = qHashMulti(0, static_cast<int>(style.kind), textLayersHash(style.layers), style.cornerRadius,
                           style.points, style.innerRatio, style.headSize, style.thickness, style.tailX,
                           style.tailSize, style.sweep, static_cast<int>(style.fillRule));
    for (const ShapeContour &c : style.contours) {
        h = qHashMulti(h, c.closed, c.vertices.size());
        for (const ShapeVertex &v : c.vertices)
            h = qHashMulti(h, v.p.x(), v.p.y(), v.in.x(), v.in.y(), v.out.x(), v.out.y());
    }
    return h;
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

namespace {

QJsonArray contoursToJson(const QVector<ShapeContour> &contours)
{
    QJsonArray out;
    for (const ShapeContour &c : contours) {
        QJsonArray vertices;
        for (const ShapeVertex &v : c.vertices) {
            QJsonObject o{{QStringLiteral("x"), v.p.x()}, {QStringLiteral("y"), v.p.y()}};
            if (!v.in.isNull()) {
                o.insert(QStringLiteral("inX"), v.in.x());
                o.insert(QStringLiteral("inY"), v.in.y());
            }
            if (!v.out.isNull()) {
                o.insert(QStringLiteral("outX"), v.out.x());
                o.insert(QStringLiteral("outY"), v.out.y());
            }
            // `smooth` stays for builds that predate the mode field; they read an Aligned vertex as
            // mirrored and a Free one as a plain corner with handles, and neither changes the outline.
            if (v.smooth())
                o.insert(QStringLiteral("smooth"), true);
            if (v.mode == TangentMode::Aligned || v.mode == TangentMode::Free)
                o.insert(QStringLiteral("mode"), tangentModeToString(v.mode));
            vertices.append(o);
        }
        out.append(QJsonObject{{QStringLiteral("closed"), c.closed}, {QStringLiteral("vertices"), vertices}});
    }
    return out;
}

QVector<ShapeContour> contoursFromJson(const QJsonArray &array)
{
    QVector<ShapeContour> out;
    for (const QJsonValue &cv : array) {
        const QJsonObject co = cv.toObject();
        ShapeContour c;
        c.closed = co.value(QStringLiteral("closed")).toBool(true);
        for (const QJsonValue &vv : co.value(QStringLiteral("vertices")).toArray()) {
            const QJsonObject o = vv.toObject();
            ShapeVertex v;
            v.p = QPointF(o.value(QStringLiteral("x")).toDouble(), o.value(QStringLiteral("y")).toDouble());
            v.in = QPointF(o.value(QStringLiteral("inX")).toDouble(), o.value(QStringLiteral("inY")).toDouble());
            v.out = QPointF(o.value(QStringLiteral("outX")).toDouble(), o.value(QStringLiteral("outY")).toDouble());
            v.mode = o.contains(QStringLiteral("mode"))
                         ? tangentModeFromString(o.value(QStringLiteral("mode")).toString())
                         : (o.value(QStringLiteral("smooth")).toBool(false) ? TangentMode::Mirrored
                                                                           : TangentMode::Corner);
            c.vertices.append(v);
        }
        out.append(c);
    }
    return out;
}

} // namespace

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
        {QStringLiteral("sweep"), s.sweep},
    };
    if (!s.contours.isEmpty())
        o.insert(QStringLiteral("contours"), contoursToJson(s.contours));
    if (s.fillRule != Qt::WindingFill)
        o.insert(QStringLiteral("fillRule"), QStringLiteral("evenodd"));
    if (!s.pathKeys.isEmpty()) {
        QJsonArray keys;
        for (auto it = s.pathKeys.constBegin(); it != s.pathKeys.constEnd(); ++it)
            keys.append(QJsonObject{{QStringLiteral("t"), qint64(it.key())},
                                    {QStringLiteral("contours"), contoursToJson(it.value())}});
        o.insert(QStringLiteral("pathKeys"), keys);
    }
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
    s.sweep = o.value(QStringLiteral("sweep")).toDouble(s.sweep);
    s.contours = contoursFromJson(o.value(QStringLiteral("contours")).toArray());
    s.fillRule = o.value(QStringLiteral("fillRule")).toString() == QLatin1String("evenodd") ? Qt::OddEvenFill
                                                                                          : Qt::WindingFill;
    for (const QJsonValue &v : o.value(QStringLiteral("pathKeys")).toArray()) {
        const QJsonObject key = v.toObject();
        s.pathKeys.insert(TimeUs(key.value(QStringLiteral("t")).toInteger()),
                          contoursFromJson(key.value(QStringLiteral("contours")).toArray()));
    }
    const QJsonObject keyframesJson = o.value(QStringLiteral("keyframes")).toObject();
    for (auto it = keyframesJson.constBegin(); it != keyframesJson.constEnd(); ++it) {
        const QString canonical = shapeKeyframeCanonicalKey(it.key(), s);
        if (!canonical.isEmpty())
            s.keyframes.insert(canonical, keyframesFromJson(it.value().toObject()));
    }
    return s;
}

} // namespace drift
