#pragma once

#include "Keyframe.h"
#include "TextShading.h"
#include "Time.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace drift {

enum class ShapeKind {
    // Basic
    Rectangle,
    RoundedRectangle,
    Square,
    Ellipse,
    Triangle,
    RightTriangle,
    Diamond,
    Pentagon,
    Hexagon,
    Octagon,
    Parallelogram,
    Trapezoid,
    // Arrows
    Arrow,
    DoubleArrow,
    BlockArrow,
    CurvedArrow,
    Chevron,
    // Bubbles
    SpeechBubble,
    SpeechBubbleRect,
    ThoughtBubble,
    Callout,
    // Fun
    Star,
    LightningBolt,
    Cloud,
    Heart,
    Cross,
    Burst,
    Banner,
};

QString shapeKindToString(ShapeKind kind);
ShapeKind shapeKindFromString(const QString &kind);

// A fill and a stroke with the catalog's stable ids, so "layer.fill.color.r" and
// "layer.stroke.width" address a fresh shape without a lookup. The fill's gradient is pre-seeded
// with fill → fillDeep so switching the paint to Gradient looks right immediately. A strokeWidth
// of 0 leaves the stroke layer disabled.
QList<TextShadingLayer> defaultShapeLayers(const QColor &fill, const QColor &fillDeep = QColor(),
                                           const QColor &stroke = Qt::white, double strokeWidth = 4.0);

struct ShapeStyle
{
    ShapeKind kind = ShapeKind::Rectangle;

    // Ordered shading stack, layers[0] back-most. The same model TextStyle::layers uses.
    QList<TextShadingLayer> layers = defaultShapeLayers(QColor(0, 180, 255), QColor(122, 0, 255));

    // Geometry knobs. Each is read by a subset of kinds only; see ShapePath.cpp.
    double cornerRadius = 0.0; // project px — native on the rect family, a corner path effect elsewhere
    int points = 5;            // star / burst spikes
    double innerRatio = 0.5;   // star / burst inner radius, fraction of outer
    double headSize = 0.4;     // arrows / chevron / banner: head length, fraction of width
    double thickness = 0.4;    // arrows / chevron / cross: shaft, fraction of height
    double tailX = 0.25;       // bubbles: tail anchor along the bottom edge, 0..1
    double tailSize = 0.2;     // bubbles: tail extent, fraction of height

    // Keyed by shapeKeyframeProperties() names ("cornerRadius", "layer.<id>.<field>", …); times
    // are relative to the clip start.
    QMap<QString, KeyframeTrack<double>> keyframes;

    bool isAnimated() const;
    // The style with every keyframed value baked in at the given clip time.
    ShapeStyle resolvedAt(TimeUs clipTimeUs) const;

    // The front-most enabled fill's colour — what a swatch or a clip thumbnail shows.
    QColor primaryColor() const;
    void setPrimaryColor(const QColor &color);
    // Convenience for tests and importers: replace the stack with one solid fill (+ the stroke).
    void setSolidFill(const QColor &color);
    void setStroke(double width, const QColor &color = Qt::white);
};

QStringList shapeKeyframeProperties(const ShapeStyle &style);
// The stored key for a property name, or empty when the style has no such scalar.
QString shapeKeyframeCanonicalKey(const QString &key, const ShapeStyle &style);
QString shapeKeyframeLabel(const QString &key, const ShapeStyle &style);
bool shapeStyleScalar(const ShapeStyle &style, const QString &key, double *out);
bool setShapeStyleScalar(ShapeStyle &style, const QString &key, double value);
// Everything that changes pixels (render cache keys). Keyframes are not part of it.
quint64 shapeStyleHash(const ShapeStyle &style);
// A paint that moves on its own (gradient offsetSpeed, a time-driven shader effect).
bool shapeTimeDrivenPaint(const ShapeStyle &style);

QJsonObject shapeStyleToJson(const ShapeStyle &style);
// Reads the layered form, or the flat fill/stroke form projects before format 8 wrote.
ShapeStyle shapeStyleFromJson(const QJsonObject &o);

// The shapes offered in the assets panel. Several entries can share a ShapeKind and differ only in
// their default aspect or colour ("circle" vs "ellipse"), so ids — not kinds — are what the UI and
// the drag mime data carry.
struct ShapeCatalogEntry
{
    QString id;
    QString label;
    QString category;
    double aspect; // default layout box width / height
    ShapeStyle style;
};

struct ShapeCategory
{
    QString id;
    QString label;
};

const QList<ShapeCatalogEntry> &shapeCatalog();
const ShapeCatalogEntry *shapeCatalogEntry(const QString &id);
QList<ShapeCategory> shapeCategories();

} // namespace drift
