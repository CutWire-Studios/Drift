#include "engine/gpu/effects/LookupTextures.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace drift {
namespace {

constexpr int kTexels = 256;

using Rgba = std::array<double, 4>;

bool parseHexRgba(const QString &hex, Rgba *out)
{
    if (!hex.startsWith(QLatin1Char('#')) || (hex.size() != 7 && hex.size() != 9))
        return false;
    *out = {0.0, 0.0, 0.0, 1.0};
    bool ok = false;
    for (int channel = 0; channel < 3; ++channel) {
        const unsigned value = hex.mid(1 + 2 * channel, 2).toUInt(&ok, 16);
        if (!ok)
            return false;
        (*out)[size_t(channel)] = value / 255.0;
    }
    if (hex.size() == 9) {
        const unsigned alpha = hex.mid(7, 2).toUInt(&ok, 16);
        if (!ok)
            return false;
        (*out)[3] = alpha / 255.0;
    }
    return true;
}

// The easing applied to a segment's 0..1 fraction. Keyed by the left key, as Forge does.
double applyEase(const QString &ease, double k)
{
    if (ease == QLatin1String("smooth"))
        return k * k * (3.0 - 2.0 * k);
    if (ease == QLatin1String("in"))
        return k * k * k;
    if (ease == QLatin1String("out"))
        return 1.0 - (1.0 - k) * (1.0 - k) * (1.0 - k);
    if (ease == QLatin1String("hold"))
        return 0.0;
    return k;
}

uint8_t toByte(double v)
{
    return uint8_t(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}

} // namespace

double evalMonotoneSpline(const std::vector<QPointF> &keys, double x)
{
    const size_t n = keys.size();
    if (n == 0)
        return 0.0;
    if (x <= keys.front().x())
        return keys.front().y();
    if (x >= keys.back().x())
        return keys.back().y();

    const size_t segments = n - 1;
    std::vector<double> d(segments);
    for (size_t j = 0; j < segments; ++j) {
        const double dx = keys[j + 1].x() - keys[j].x();
        d[j] = dx > 0.0 ? (keys[j + 1].y() - keys[j].y()) / dx : 0.0;
    }
    std::vector<double> m(n);
    m.front() = d.front();
    m.back() = d.back();
    for (size_t j = 1; j < segments; ++j)
        m[j] = d[j - 1] * d[j] <= 0.0 ? 0.0 : (d[j - 1] + d[j]) / 2.0;
    for (size_t j = 0; j < segments; ++j) {
        if (d[j] == 0.0) {
            m[j] = 0.0;
            m[j + 1] = 0.0;
            continue;
        }
        const double a = m[j] / d[j];
        const double b = m[j + 1] / d[j];
        const double s = a * a + b * b;
        if (s > 9.0) {
            const double t = 3.0 / std::sqrt(s);
            m[j] = t * a * d[j];
            m[j + 1] = t * b * d[j];
        }
    }

    size_t k = 0;
    while (k + 2 < n && keys[k + 1].x() <= x)
        ++k;
    const double h = keys[k + 1].x() - keys[k].x();
    const double t = (x - keys[k].x()) / h;
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2.0 * t3 - 3.0 * t2 + 1.0;
    const double h10 = t3 - 2.0 * t2 + t;
    const double h01 = -2.0 * t3 + 3.0 * t2;
    const double h11 = t3 - t2;
    const double y = h00 * keys[k].y() + h10 * h * m[k] + h01 * keys[k + 1].y() + h11 * h * m[k + 1];
    return std::clamp(y, 0.0, 1.0);
}

QByteArray bakeGradient(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray())
        return {};

    struct Stop
    {
        double position;
        Rgba rgba;
    };
    std::vector<Stop> stops;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject stop = value.toObject();
        Rgba rgba{};
        if (!parseHexRgba(stop.value(QStringLiteral("color")).toString(), &rgba))
            return {};
        stops.push_back({stop.value(QStringLiteral("position")).toDouble(), rgba});
    }
    if (stops.empty())
        return {};
    std::stable_sort(stops.begin(), stops.end(),
                     [](const Stop &a, const Stop &b) { return a.position < b.position; });

    QByteArray out(kTexels * 4, Qt::Uninitialized);
    for (int i = 0; i < kTexels; ++i) {
        const double t = i / 255.0;
        Rgba c{};
        if (t <= stops.front().position) {
            c = stops.front().rgba;
        } else if (t >= stops.back().position) {
            c = stops.back().rgba;
        } else {
            const auto right = std::lower_bound(stops.begin(), stops.end(), t,
                                                [](const Stop &s, double v) { return s.position < v; });
            const Stop &left = *(right - 1);
            const double k = (t - left.position) / (right->position - left.position);
            for (size_t channel = 0; channel < 4; ++channel)
                c[channel] = left.rgba[channel] + (right->rgba[channel] - left.rgba[channel]) * k;
        }
        for (size_t channel = 0; channel < 4; ++channel)
            out[i * 4 + int(channel)] = char(toByte(c[channel]));
    }
    return out;
}

namespace {

struct CurveKey
{
    double x;
    double y;
    QString ease;
};

std::vector<CurveKey> parseCurveKeys(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray())
        return {};
    std::vector<CurveKey> keys;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject key = value.toObject();
        keys.push_back({key.value(QStringLiteral("x")).toDouble(),
                        key.value(QStringLiteral("y")).toDouble(),
                        key.value(QStringLiteral("ease")).toString(QStringLiteral("linear"))});
    }
    std::stable_sort(keys.begin(), keys.end(), [](const CurveKey &a, const CurveKey &b) { return a.x < b.x; });
    return keys;
}

} // namespace

namespace {

std::vector<CurveKey> toKeys(const std::vector<CurvePoint> &points)
{
    std::vector<CurveKey> keys;
    keys.reserve(points.size());
    for (const CurvePoint &p : points)
        keys.push_back({p.x, p.y, p.ease});
    return keys;
}

// A hue curve is evaluated over copies of its keys one period either side, so the spline's
// tangents at red see the keys across the wrap. A key at 1 tied to the one at 0 is the same
// key, and keeping both would flatten the tangent there.
std::vector<CurveKey> periodicKeys(std::vector<CurveKey> keys)
{
    if (keys.size() > 1 && keys.front().x <= 0.0 && keys.back().x >= 1.0
        && std::abs(keys.back().y - keys.front().y) < 1e-9) {
        keys.pop_back();
    }
    std::vector<CurveKey> extended;
    extended.reserve(keys.size() * 3);
    for (const double shift : {-1.0, 0.0, 1.0}) {
        for (const CurveKey &key : keys)
            extended.push_back({key.x + shift, key.y, key.ease});
    }
    std::stable_sort(extended.begin(), extended.end(),
                     [](const CurveKey &a, const CurveKey &b) { return a.x < b.x; });
    return extended;
}

struct PreparedCurve
{
    std::vector<CurveKey> keys;
    std::vector<QPointF> points;
};

PreparedCurve prepareCurve(std::vector<CurveKey> keys, bool periodic)
{
    PreparedCurve out;
    out.keys = periodic ? periodicKeys(std::move(keys)) : std::move(keys);
    out.points.reserve(out.keys.size());
    for (const CurveKey &key : out.keys)
        out.points.emplace_back(key.x, key.y);
    return out;
}

double evalPrepared(const PreparedCurve &curve, double t)
{
    const std::vector<CurveKey> &keys = curve.keys;
    if (t <= keys.front().x)
        return keys.front().y;
    if (t >= keys.back().x)
        return keys.back().y;
    const auto right = std::lower_bound(keys.begin(), keys.end(), t,
                                        [](const CurveKey &k, double v) { return k.x < v; });
    const CurveKey &left = *(right - 1);
    if (left.ease == QLatin1String("spline"))
        return evalMonotoneSpline(curve.points, t);
    if (right->x > left.x) {
        const double k = (t - left.x) / (right->x - left.x);
        return left.y + (right->y - left.y) * applyEase(left.ease, k);
    }
    return right->y;
}

} // namespace

QByteArray bakeCurve(const QString &json, bool periodic)
{
    std::vector<CurveKey> keys = parseCurveKeys(json);
    if (keys.empty())
        return {};
    const PreparedCurve curve = prepareCurve(std::move(keys), periodic);

    QByteArray out(kTexels * 4, Qt::Uninitialized);
    for (int i = 0; i < kTexels; ++i) {
        const char value = char(toByte(evalPrepared(curve, i / 255.0)));
        out[i * 4 + 0] = value;
        out[i * 4 + 1] = value;
        out[i * 4 + 2] = value;
        out[i * 4 + 3] = char(255);
    }
    return out;
}

std::vector<CurvePoint> identityCurve(bool hue)
{
    const double y0 = hue ? 0.5 : 0.0;
    const double y1 = hue ? 0.5 : 1.0;
    return {{0.0, y0, QStringLiteral("spline")}, {1.0, y1, QStringLiteral("spline")}};
}

std::vector<CurvePoint> parseCurvePoints(const QString &json, bool hue)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray())
        return identityCurve(hue);

    std::vector<CurvePoint> pts;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject key = value.toObject();
        const QJsonValue x = key.value(QStringLiteral("x"));
        const QJsonValue y = key.value(QStringLiteral("y"));
        if (!x.isDouble() || !y.isDouble() || !std::isfinite(x.toDouble()) || !std::isfinite(y.toDouble()))
            continue;
        pts.push_back({std::clamp(x.toDouble(), 0.0, 1.0), std::clamp(y.toDouble(), 0.0, 1.0),
                       key.value(QStringLiteral("ease")).toString(QStringLiteral("linear"))});
    }
    std::stable_sort(pts.begin(), pts.end(), [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });

    std::vector<CurvePoint> out;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (!out.empty() && pts[i].x - out.back().x < kMinCurveGap) {
            if (i + 1 == pts.size() && out.size() > 1)
                out.back() = pts[i];
            continue;
        }
        out.push_back(pts[i]);
    }
    if (out.size() > size_t(kMaxCurvePoints)) {
        const CurvePoint last = out.back();
        out.resize(size_t(kMaxCurvePoints) - 1);
        out.push_back(last);
    }
    return out.size() >= 2 ? out : identityCurve(hue);
}

double evalCurve(const std::vector<CurvePoint> &points, double x, bool periodic)
{
    if (points.empty())
        return std::clamp(x, 0.0, 1.0);
    return evalPrepared(prepareCurve(toKeys(points), periodic), x);
}

bool curveIsIdentity(const std::vector<CurvePoint> &points, bool hue)
{
    if (points.empty())
        return false;
    for (size_t i = 0; i < points.size(); ++i) {
        const CurvePoint &key = points[i];
        if (std::abs(key.y - (hue ? 0.5 : key.x)) > 1e-9)
            return false;
        if (hue)
            continue;
        if (i + 1 < points.size() && key.ease != QLatin1String("linear") && key.ease != QLatin1String("spline"))
            return false;
    }
    return hue || (points.front().x <= 0.0 && points.back().x >= 1.0);
}

bool curveIsIdentity(const QString &json, bool hue)
{
    std::vector<CurvePoint> points;
    for (const CurveKey &key : parseCurveKeys(json))
        points.push_back({key.x, key.y, key.ease});
    return curveIsIdentity(points, hue);
}

} // namespace drift
