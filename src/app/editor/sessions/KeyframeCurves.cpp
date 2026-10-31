#include "app/editor/sessions/KeyframeCurves.h"

#include "core/animation/Keyframe.h"
#include "core/base/Bezier.h"
#include "core/base/Time.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace KeyframeCurves {

namespace {

constexpr int kEasingSamples = 48;

QString text(const QVariantMap &map, const char *key)
{
    return map.value(QLatin1String(key)).toString();
}

double number(const QVariantMap &map, const char *key, double fallback = 0)
{
    const QVariant v = map.value(QLatin1String(key));
    return v.isValid() && !v.isNull() ? v.toDouble() : fallback;
}

// Number(v.toFixed(decimals)).toString(): fixed digits, then the trailing zeros dropped.
QString tickText(double v, int decimals)
{
    QString out = QString::number(v, 'f', decimals);
    if (out.contains(QLatin1Char('.'))) {
        while (out.endsWith(QLatin1Char('0')))
            out.chop(1);
        if (out.endsWith(QLatin1Char('.')))
            out.chop(1);
    }
    return out == QLatin1String("-0") ? QStringLiteral("0") : out;
}

drift::EasingSpec fnSpec(const Point &p)
{
    drift::EasingSpec spec;
    spec.kind = drift::EasingSpec::Kind::Function;
    spec.fn = p.fn;
    spec.amplitude = p.fnAmplitude;
    spec.period = p.fnPeriod;
    return spec;
}

// Where a Penner segment from `a` to `b` reaches, for sizing an axis around its overshoot.
void extendByEasing(const Point &a, const Point &b, double *lo, double *hi)
{
    const double span = b.value - a.value;
    for (const QPointF &s : drift::sampleEasing(fnSpec(a), kEasingSamples)) {
        const double w = a.value + span * s.y();
        *lo = std::min(*lo, w);
        *hi = std::max(*hi, w);
    }
}

drift::KeyframeTrack<double> trackFor(const QList<Point> &sorted)
{
    drift::KeyframeTrack<double> track;
    for (const Point &p : sorted) {
        drift::Keyframe<double> key;
        key.value = p.value;
        key.inDx = double(drift::secondsToUs(p.inDx));
        key.inDy = p.inDy;
        key.outDx = double(drift::secondsToUs(p.outDx));
        key.outDy = p.outDy;
        key.corner = p.corner;
        key.hold = p.hold;
        key.fn = p.fn;
        key.fnAmplitude = p.fnAmplitude;
        key.fnPeriod = p.fnPeriod;
        track.setKeyframe(drift::secondsToUs(p.seconds), key);
    }
    return track;
}

} // namespace

Point pointFromMap(const QVariantMap &map)
{
    Point p;
    p.seconds = number(map, "seconds");
    p.value = number(map, "value");
    p.inDx = number(map, "inDx");
    p.inDy = number(map, "inDy");
    p.outDx = number(map, "outDx");
    p.outDy = number(map, "outDy");
    p.corner = map.value(QStringLiteral("corner")).toBool();
    p.hold = map.value(QStringLiteral("hold")).toBool();
    p.fn = drift::easingFnFromString(text(map, "fn"));
    p.fnAmplitude = number(map, "fnAmp", 1.0);
    p.fnPeriod = number(map, "fnPeriod", 0.3);
    return p;
}

QVariantMap toMap(const Point &p)
{
    return {{QStringLiteral("seconds"), p.seconds},
            {QStringLiteral("value"), p.value},
            {QStringLiteral("inDx"), p.inDx},
            {QStringLiteral("inDy"), p.inDy},
            {QStringLiteral("outDx"), p.outDx},
            {QStringLiteral("outDy"), p.outDy},
            {QStringLiteral("corner"), p.corner},
            {QStringLiteral("hold"), p.hold},
            {QStringLiteral("fn"), drift::easingFnToString(p.fn)},
            {QStringLiteral("fnAmp"), p.fnAmplitude},
            {QStringLiteral("fnPeriod"), p.fnPeriod}};
}

Series seriesFromMap(const QVariantMap &map)
{
    Series s;
    s.prop = text(map, "prop");
    s.rowProp = map.contains(QStringLiteral("rowProp")) ? text(map, "rowProp") : s.prop;
    s.label = text(map, "label");
    s.rowIndex = map.value(QStringLiteral("rowIndex"), -1).toInt();
    s.color = map.value(QStringLiteral("color")).value<QColor>();
    for (const QVariant &p : map.value(QStringLiteral("points")).toList())
        s.points.append(pointFromMap(p.toMap()));
    s.baseValue = number(map, "baseValue");
    s.enabled = map.value(QStringLiteral("enabled"), true).toBool();
    s.shown = map.value(QStringLiteral("shown"), true).toBool();
    s.valueMin = number(map, "valueMin", 0);
    s.valueMax = number(map, "valueMax", 1);
    s.ownMin = number(map, "ownMin", s.valueMin);
    s.ownMax = number(map, "ownMax", s.valueMax);
    return s;
}

QVariantMap toMap(const Series &s)
{
    QVariantList points;
    points.reserve(s.points.size());
    for (const Point &p : s.points)
        points.append(toMap(p));
    return {{QStringLiteral("prop"), s.prop},
            {QStringLiteral("rowProp"), s.rowProp},
            {QStringLiteral("rowIndex"), s.rowIndex},
            {QStringLiteral("label"), s.label},
            {QStringLiteral("color"), s.color},
            {QStringLiteral("points"), points},
            {QStringLiteral("baseValue"), s.baseValue},
            {QStringLiteral("enabled"), s.enabled},
            {QStringLiteral("shown"), s.shown},
            {QStringLiteral("ownMin"), s.ownMin},
            {QStringLiteral("ownMax"), s.ownMax},
            {QStringLiteral("valueMin"), s.valueMin},
            {QStringLiteral("valueMax"), s.valueMax}};
}

QList<Series> seriesFromList(const QVariantList &list)
{
    QList<Series> out;
    out.reserve(list.size());
    for (const QVariant &v : list)
        out.append(seriesFromMap(v.toMap()));
    return out;
}

QVariantList toList(const QList<Series> &series)
{
    QVariantList out;
    out.reserve(series.size());
    for (const Series &s : series)
        out.append(toMap(s));
    return out;
}

Range valueRange(const QString &id, const QList<Point> &points, double base, bool hasRange, double min,
                 double max)
{
    if (id == QLatin1String("opacity"))
        return {0, 1};
    if (id == QLatin1String("volume"))
        return {0, 2};
    if (id == QLatin1String("rotation") || id == QLatin1String("mask.rotation"))
        return {-180, 180};
    if (id == QLatin1String("mask.x") || id == QLatin1String("mask.y") || id == QLatin1String("mask.w")
        || id == QLatin1String("mask.h"))
        return {0, 1};
    if (hasRange)
        return {min, max};
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < points.size(); ++i) {
        const Point &p = points.at(i);
        lo = std::min(lo, p.value);
        hi = std::max(hi, p.value);
        if (p.fn != drift::EasingFn::None && i + 1 < points.size())
            extendByEasing(p, points.at(i + 1), &lo, &hi);
    }
    if (!std::isfinite(lo))
        lo = base;
    if (!std::isfinite(hi))
        hi = base;
    return {lo - std::max(1.0, std::abs(lo) * 0.1), hi + std::max(1.0, std::abs(hi) * 0.1)};
}

Range padRange(double lo, double hi)
{
    const double pad = std::max((hi - lo) * 0.1, hi - lo < 1e-9 ? std::max(1.0, std::abs(lo) * 0.1) : 0.0);
    return {lo - pad, hi + pad};
}

bool fitRange(const QList<Series> &series, const std::function<bool(const QString &, double)> &selected,
              bool selectedOnly, Range *out)
{
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const Series &s : series) {
        if (s.points.isEmpty()) {
            if (!selectedOnly) {
                lo = std::min(lo, s.baseValue);
                hi = std::max(hi, s.baseValue);
            }
            continue;
        }
        for (qsizetype i = 0; i < s.points.size(); ++i) {
            const Point &p = s.points.at(i);
            if (selectedOnly && !(selected && selected(s.prop, p.seconds)))
                continue;
            for (double v : {p.value, p.value + p.outDy, p.value + p.inDy}) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            if (p.fn != drift::EasingFn::None && i + 1 < s.points.size())
                extendByEasing(p, s.points.at(i + 1), &lo, &hi);
        }
    }
    if (!std::isfinite(lo))
        return false;
    *out = padRange(lo, hi);
    return true;
}

QList<Tick> gridTicks(Range range, bool labelled)
{
    QList<Tick> out;
    if (!labelled) {
        for (int i = 0; i <= 4; ++i)
            out.append({i / 4.0, QString()});
        return out;
    }
    const double span = std::max(1e-6, range.max - range.min);
    const double raw = span / 5;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double norm = raw / mag;
    const double step = (norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10) * mag;
    const int decimals = std::max(0, std::min(6, int(std::ceil(-std::log10(step) - 1e-9))));
    for (double v = std::ceil(range.min / step) * step; v <= range.max + step * 1e-6; v += step)
        out.append({(v - range.min) / span, tickText(v, decimals)});
    return out;
}

double yForValue(double value, const Series &series, double graphHeight)
{
    const double span = std::max(1e-6, series.valueMax - series.valueMin);
    return graphHeight - ((value - series.valueMin) / span) * graphHeight;
}

double valueForY(double y, const Series &series, double graphHeight)
{
    const double span = std::max(1e-6, series.valueMax - series.valueMin);
    return series.valueMin + (graphHeight - y) / graphHeight * span;
}

QList<Point> sortedPoints(const QList<Point> &points)
{
    QList<Point> out = points;
    std::stable_sort(out.begin(), out.end(), [](const Point &a, const Point &b) { return a.seconds < b.seconds; });
    return out;
}

double valueAt(const QList<Point> &sorted, double seconds)
{
    return trackFor(sorted).evaluateAt(drift::secondsToUs(seconds));
}

QVector<QPointF> sampleCurve(const QList<Point> &sorted, double base, double clipStart, double clipEnd, double s0,
                             double s1, double stepSeconds)
{
    QVector<QPointF> out;
    if (sorted.isEmpty()) {
        out.append({clipStart, base});
        out.append({clipEnd, base});
        return out;
    }
    const drift::KeyframeTrack<double> track = trackFor(sorted);
    const double step = std::max(stepSeconds, 1e-6);
    out.append({clipStart, sorted.first().value});
    out.append({sorted.first().seconds, sorted.first().value});
    for (qsizetype i = 1; i < sorted.size(); ++i) {
        const Point &a = sorted.at(i - 1);
        const Point &b = sorted.at(i);
        if (a.hold) {
            out.append({b.seconds, a.value});
            out.append({b.seconds, b.value});
            continue;
        }
        const double lo = std::max(a.seconds, s0);
        const double hi = std::min(b.seconds, s1);
        if (hi > lo) {
            const int n = std::max(1, int(std::ceil((hi - lo) / step)));
            for (int k = 0; k < n; ++k) {
                const double t = lo + (hi - lo) * k / n;
                out.append({t, track.evaluateAt(drift::secondsToUs(t))});
            }
        }
        out.append({b.seconds, b.value});
    }
    out.append({sorted.last().seconds, sorted.last().value});
    out.append({clipEnd, sorted.last().value});
    return out;
}

QList<Handle> tangentHandles(const QList<Point> &points)
{
    QList<Handle> out;
    for (int i = 0; i < points.size(); ++i) {
        if (i > 0 && !points.at(i - 1).shaped())
            out.append({i, false});
        if (i < points.size() - 1 && !points.at(i).shaped())
            out.append({i, true});
    }
    return out;
}

double handleSeconds(const Point &point, bool outgoing)
{
    const double raw = outgoing ? point.outDx : point.inDx;
    if (std::abs(raw) > 1e-6)
        return raw;
    return outgoing ? kDefaultHandleSeconds : -kDefaultHandleSeconds;
}

double handleValue(const Point &point, bool outgoing)
{
    return point.value + (outgoing ? point.outDy : point.inDy);
}

namespace {

void keepSmooth(Point &p, bool outgoingMoved)
{
    if (p.corner)
        return;
    if (outgoingMoved && std::abs(p.outDx) > 1e-6)
        p.inDy = p.outDy / p.outDx * p.inDx;
    else if (!outgoingMoved && std::abs(p.inDx) > 1e-6)
        p.outDy = p.inDy / p.inDx * p.outDx;
}

} // namespace

Point withHandle(Point p, bool outgoing, double dx, double value)
{
    if (outgoing) {
        p.outDx = std::max(0.0, dx);
        p.outDy = value - p.value;
    } else {
        p.inDx = std::min(0.0, dx);
        p.inDy = value - p.value;
    }
    keepSmooth(p, outgoing);
    return p;
}

namespace {

bool isStraight(const Point &a, const Point &b)
{
    return std::abs(a.outDx) < 1e-9 && std::abs(a.outDy) < 1e-9 && std::abs(b.inDx) < 1e-9
           && std::abs(b.inDy) < 1e-9;
}

} // namespace

double segmentParameter(const Point &a, const Point &b, double seconds)
{
    const double span = b.seconds - a.seconds;
    if (span <= 0)
        return 0;
    if (isStraight(a, b))
        return std::clamp((seconds - a.seconds) / span, 0.0, 1.0);
    const double x1 = std::clamp(a.seconds + a.outDx, a.seconds, b.seconds);
    const double x2 = std::clamp(b.seconds + b.inDx, a.seconds, b.seconds);
    return drift::bezierParameterForX(a.seconds, x1, x2, b.seconds, seconds);
}

double segmentValue(const Point &a, const Point &b, double seconds)
{
    const double s = segmentParameter(a, b, seconds);
    if (isStraight(a, b))
        return a.value + (b.value - a.value) * s;
    return drift::cubicBezier(a.value, a.value + a.outDy, b.value + b.inDy, b.value, s);
}

void deformSegment(Point &a, Point &b, double t, double dSeconds, double dValue)
{
    const double span = b.seconds - a.seconds;
    if (span <= 0)
        return;
    if (isStraight(a, b)) {
        a.outDx = span / 3;
        a.outDy = (b.value - a.value) / 3;
        b.inDx = -span / 3;
        b.inDy = -(b.value - a.value) / 3;
    }

    // Close to a key the weights blow up; past these the drag is all on the nearer handle anyway.
    t = std::clamp(t, 0.1, 0.9);
    double weight = 1;
    if (t <= 1.0 / 6)
        weight = 0;
    else if (t <= 0.5)
        weight = std::pow((6 * t - 1) / 2, 3) / 2;
    else if (t <= 5.0 / 6)
        weight = (1 - std::pow((6 * (1 - t) - 1) / 2, 3)) / 2 + 0.5;
    const double k0 = (1 - weight) / (3 * t * (1 - t) * (1 - t));
    const double k1 = weight / (3 * t * t * (1 - t));

    a.outDx = std::clamp(a.outDx + k0 * dSeconds, 0.0, span);
    a.outDy += k0 * dValue;
    b.inDx = std::clamp(b.inDx + k1 * dSeconds, -span, 0.0);
    b.inDy += k1 * dValue;
    keepSmooth(a, true);
    keepSmooth(b, false);
}

} // namespace KeyframeCurves
