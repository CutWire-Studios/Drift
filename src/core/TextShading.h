#pragma once

#include "BlendMode.h"
#include "Keyframe.h"
#include "TextParamSpec.h"
#include "VectorSource.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QString>

// The look of a text block or a shape as an ordered stack of shading layers (Resolve's "shading
// elements"): each layer is a fill, stroke, shadow, glow or extrude painted from a solid colour, a
// gradient, an image texture or a shader effect. layers[0] is drawn first (back-most). Box, pills,
// underline and the word accent stay geometry decorations on TextStyle, not layers.
//
// The `Text` prefix on the type names is historical: the same stack is what ShapeStyle carries.

namespace drift {

struct TextStyle;

enum class TextLayerKind { Fill, Stroke, Shadow, Glow, Extrude };
enum class TextPaintKind { Solid, Gradient, Texture, Effect };
enum class TextGradientKind { Linear, Radial, Sweep };
// The box the gradient is mapped onto. Block = the whole block's painted bounds (the legacy
// behaviour); Line / Word / Glyph re-map per piece; AccentRun = the contiguous run of accented
// words the piece belongs to (a gradient on the highlighted words only).
enum class TextGradientSpace { Block, Line, Word, Glyph, AccentRun };
enum class TextLayerScope { All, Base, Accent };
// Where a stroke sits relative to the outline: centred on it, grown outward only, or kept inside.
enum class StrokeAlign { Center, Outside, Inside };
enum class StrokeDash { Solid, Dash, Dot, DashDot };

QString textLayerKindToString(TextLayerKind kind);
TextLayerKind textLayerKindFromString(const QString &kind);
QString textPaintKindToString(TextPaintKind kind);
TextPaintKind textPaintKindFromString(const QString &kind);
QString textGradientKindToString(TextGradientKind kind);
TextGradientKind textGradientKindFromString(const QString &kind);
QString textGradientSpaceToString(TextGradientSpace space);
TextGradientSpace textGradientSpaceFromString(const QString &space);
QString textLayerScopeToString(TextLayerScope scope);
TextLayerScope textLayerScopeFromString(const QString &scope);
QString strokeAlignToString(StrokeAlign align);
StrokeAlign strokeAlignFromString(const QString &align);
QString strokeDashToString(StrokeDash dash);
StrokeDash strokeDashFromString(const QString &dash);
// On/off run lengths in stroke widths; empty for Solid.
QList<double> strokeDashIntervals(StrokeDash dash);

struct TextGradientStop
{
    double pos = 0.0;
    QColor color = Qt::white;
    bool operator==(const TextGradientStop &o) const { return qFuzzyCompare(pos + 1.0, o.pos + 1.0) && color == o.color; }
};

struct TextGradient
{
    TextGradientKind kind = TextGradientKind::Linear;
    QList<TextGradientStop> stops = {{0.0, Qt::white}, {1.0, QColor(255, 120, 0)}};
    double angle = 90.0;       // degrees; 0 = left → right, 90 = top → bottom
    double offset = 0.0;       // shift along the axis in box widths (keyframable → moving gradient)
    double offsetSpeed = 0.0;  // box widths per second, added from clip time
    double scale = 1.0;        // axis length multiplier
    QPointF center{0.5, 0.5};  // radial / sweep centre, fraction of the space box
    bool repeat = false;       // tile instead of clamp
    bool oklab = false;        // interpolate in OKLab
    TextGradientSpace space = TextGradientSpace::Block;
};

struct TextTexture
{
    QString path;
    double scale = 1.0;
    double angle = 0.0;
    QPointF offset;
    bool tile = true;
};

// A shader effect (SkSL) over the layer's base paint; ids are the renderer's registry
// ("shine", "shimmer", "neon-pulse", "glitch", "chrome", "dissolve").
struct TextShaderEffect
{
    QString id;
    QMap<QString, VectorSlotValue> params;
};

struct TextPaint
{
    TextPaintKind kind = TextPaintKind::Solid;
    QColor color = Qt::white; // Solid; the tint for Texture / Effect
    TextGradient gradient;
    TextTexture texture;
    TextShaderEffect effect;
};

struct TextShadingLayer
{
    QString id; // stable, lowercase, unique in the style
    TextLayerKind kind = TextLayerKind::Fill;
    bool enabled = true;
    TextPaint paint;
    double opacity = 1.0;
    BlendMode blend = BlendMode::Normal;
    double offsetX = 0.0; // project px at pixelSize
    double offsetY = 0.0;
    double blur = 0.0;    // shadow blur / glow radius / soft fill, px
    double width = 2.0;   // stroke width | extrude depth, px
    double spread = 0.0;  // shadow / glow dilation, px
    StrokeAlign strokeAlign = StrokeAlign::Outside; // the legacy outline grew outward only
    StrokeDash dash = StrokeDash::Solid;
    double dashOffset = 0.0;   // dash phase in stroke widths (keyframable → marching ants)
    bool knockout = false;     // a fill that punches through the layers beneath (hollow text)
    double trimStart = 0.0;    // stroke write-on
    double trimEnd = 1.0;
    double sketchLength = 0.0;    // SkDiscretePathEffect segment length, px; 0 = off
    double sketchDeviation = 0.0; // px
    int sketchSeed = 0;
    int extrudeSteps = 8;
    double extrudeAngle = 45.0;
    double extrudeDarken = 0.5;
    TextLayerScope scope = TextLayerScope::All;
};

TextShadingLayer solidFillLayer(const QColor &color, const QString &id = QStringLiteral("fill"));
TextShadingLayer strokeLayer(double width, const QColor &color, const QString &id = QStringLiteral("stroke"));
TextShadingLayer shadowLayer(const QColor &color, double offsetX, double offsetY, double blur, double opacity,
                             const QString &id = QStringLiteral("shadow"));
TextShadingLayer glowLayer(const QColor &color, double radius, double opacity, const QString &id = QStringLiteral("glow"));

QString mintTextLayerId(const QList<TextShadingLayer> &layers);
TextShadingLayer *findTextLayer(QList<TextShadingLayer> &layers, const QString &id);
const TextShadingLayer *findTextLayer(const QList<TextShadingLayer> &layers, const QString &id);
const TextShadingLayer *firstTextLayerOfKind(const QList<TextShadingLayer> &layers, TextLayerKind kind,
                                             bool enabledOnly = true);
TextShadingLayer *firstTextLayerOfKind(QList<TextShadingLayer> &layers, TextLayerKind kind, bool enabledOnly = true);
// Everything about the stack that changes pixels (cache keys).
quint64 textLayersHash(const QList<TextShadingLayer> &layers);

// Keyframe addressing inside a layer stack. Keys are "layer.<id>.<field>" where field is one of
// shadingLayerKeyframeFields(): "opacity", "color.r", "gradient.stop.2.pos", "effect.speed", …
struct LayerKeyPath
{
    QString layerId;
    QString field;
};
bool parseLayerKey(const QString &key, LayerKeyPath *out);
// The fields a layer of this kind / paint exposes to keyframes, in inspector order.
QStringList shadingLayerKeyframeFields(const TextShadingLayer &layer);
bool shadingLayerScalar(const TextShadingLayer &layer, const QString &field, double *out);
bool setShadingLayerScalar(TextShadingLayer &layer, const QString &field, double value);
QString shadingLayerFieldLabel(const QString &field);
QString shadingLayerKindLabel(TextLayerKind kind);
// "Stroke · Width", or "Stroke 2 · Width" when the stack holds several strokes; the raw key when
// the layer is unknown.
QString shadingLayerKeyframeLabel(const QList<TextShadingLayer> &layers, const QString &key);
// Drops every "layer.<id>.*" track a removed layer owned.
void eraseLayerKeyframes(QMap<QString, KeyframeTrack<double>> &keyframes, const QString &layerId);

// The shader effects a paint can pick, with the params the inspector renders. Ids match the
// renderer's SkSL registry.
struct TextEffectSpec
{
    QString id;
    QString label;
    QList<TextAnimParamSpec> params;
    bool timeDriven = true; // redraws every frame while its speed is non-zero
};
const QList<TextEffectSpec> &textShaderEffectSpecs();
const TextEffectSpec *textShaderEffectSpec(const QString &id);

QJsonObject textPaintToJson(const TextPaint &paint);
TextPaint textPaintFromJson(const QJsonObject &o);
QJsonObject textShadingLayerToJson(const TextShadingLayer &layer);
TextShadingLayer textShadingLayerFromJson(const QJsonObject &o);

} // namespace drift
