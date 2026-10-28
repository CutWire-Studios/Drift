#pragma once

#include "core/animation/Easing.h"

#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <functional>

// The Keyframes curve view's maths: what a property's curve is, how tall its axis is, where the
// grid sits and what a click lands on. Curves are evaluated by the engine's own KeyframeTrack, so a
// drawn curve (easing overshoot and handles included) is the one playback follows. Pure data in,
// plain values out, so it needs no project.
namespace KeyframeCurves {

struct Point
{
    double seconds = 0;
    double value = 0;
    double inDx = 0;
    double inDy = 0;
    double outDx = 0;
    double outDy = 0;
    bool corner = false;
    bool hold = false;
    drift::EasingFn fn = drift::EasingFn::None;
    double fnAmplitude = 1.0;
    double fnPeriod = 0.3;

    // A step or a Penner function ignores its handles, so neither end of its segment has one.
    bool shaped() const { return hold || fn != drift::EasingFn::None; }
};

struct Series
{
    QString prop;
    QString rowProp;
    QString label;
    int rowIndex = -1;
    QColor color;
    QList<Point> points;
    double baseValue = 0;
    bool enabled = true;
    bool shown = true;
    double ownMin = 0;
    double ownMax = 1;
    double valueMin = 0;
    double valueMax = 1;
};

struct Range
{
    double min = 0;
    double max = 1;
};

struct Tick
{
    double frac = 0;
    QString text;
};

// A handle with no length still has to be grabbable, so an unset one reaches this far.
constexpr double kDefaultHandleSeconds = 0.12;

Point pointFromMap(const QVariantMap &map);
QVariantMap toMap(const Point &point);
Series seriesFromMap(const QVariantMap &map);
QVariantMap toMap(const Series &series);
QList<Series> seriesFromList(const QVariantList &list);
QVariantList toList(const QList<Series> &series);

// The axis a property's curve is fitted to, from its fixed range when it has one, else from its
// keys and the overshoot of its easing. `hasRange` makes `min`..`max` the range (effect params).
Range valueRange(const QString &id, const QList<Point> &points, double base, bool hasRange, double min,
                 double max);
// The extent of the series over all keys (or the selected ones), handles and easing included;
// false when there is nothing to fit. An unkeyed series counts only for a fit of everything.
bool fitRange(const QList<Series> &series, const std::function<bool(const QString &, double)> &selected,
              bool selectedOnly, Range *out);
// `lo`..`hi` plus 10%, and a floor so a flat curve still has an axis.
Range padRange(double lo, double hi);

QList<Tick> gridTicks(Range range, bool labelled);

double yForValue(double value, const Series &series, double graphHeight);
double valueForY(double y, const Series &series, double graphHeight);

// The engine's track for the points, handles in microseconds as stored.
double valueAt(const QList<Point> &sorted, double seconds);
// The curve as (seconds, value) samples at most `stepSeconds` apart inside [s0, s1], flat from
// `clipStart` to the first key and from the last key to `clipEnd`. A step is drawn as a corner.
QVector<QPointF> sampleCurve(const QList<Point> &sorted, double base, double clipStart, double clipEnd, double s0,
                             double s1, double stepSeconds);
QList<Point> sortedPoints(const QList<Point> &points);

// The grips of a series: one per segment end that has handles. `outgoing` marks the right handle.
struct Handle
{
    int pointIndex = 0;
    bool outgoing = false;
};
QList<Handle> tangentHandles(const QList<Point> &points);
double handleSeconds(const Point &point, bool outgoing);
double handleValue(const Point &point, bool outgoing);

// The key with one handle moved to `dx` seconds / `value` (absolute). dx keeps its side of the key,
// and unless the key is a corner its other handle turns to the same slope.
Point withHandle(Point point, bool outgoing, double dx, double value);
// Where a handle-shaped segment `a`..`b` is at `seconds`: its bezier parameter and its value,
// as the engine evaluates it.
double segmentParameter(const Point &a, const Point &b, double seconds);
double segmentValue(const Point &a, const Point &b, double seconds);
// Inkscape's curve drag: moves the inner handles of the segment `a`..`b` so its point at bezier
// parameter `t` follows a drag of `dSeconds`/`dValue`, the nearer key's handle taking more of it.
// A straight segment first gets handles on its chord so the press does not change its shape.
void deformSegment(Point &a, Point &b, double t, double dSeconds, double dValue);

} // namespace KeyframeCurves
