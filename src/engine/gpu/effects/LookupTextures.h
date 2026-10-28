#pragma once

#include <QByteArray>
#include <QPointF>
#include <QString>

#include <vector>

namespace drift {

// Gradient and curve params baked to a 256x1 RGBA8 texture, texel i at t = i / 255. Empty when the
// JSON is malformed or has no stops/keys.
QByteArray bakeGradient(const QString &json);
// A periodic curve (a hue curve) wraps: x = 0 and x = 1 are the same point.
QByteArray bakeCurve(const QString &json, bool periodic = false);
// True when the curve maps every x to itself, or for a hue curve, rests flat at y = 0.5.
bool curveIsIdentity(const QString &json, bool hue);

struct CurvePoint
{
    double x = 0.0;
    double y = 0.0;
    QString ease;
};

constexpr int kMaxCurvePoints = 16;
constexpr double kMinCurveGap = 0.01;

std::vector<CurvePoint> identityCurve(bool hue);
// Editable points from stored curve JSON: finite, clamped to [0, 1], sorted, at least kMinCurveGap
// apart and at most kMaxCurvePoints. Anything unusable falls back to the identity curve. A missing
// ease reads as "linear", as bakeCurve does.
std::vector<CurvePoint> parseCurvePoints(const QString &json, bool hue);
// True when the points are identity, as curveIsIdentity(json, hue) decides for the same curve.
bool curveIsIdentity(const std::vector<CurvePoint> &points, bool hue);
// The curve bakeCurve applies, at x: per-key eases and, when periodic, the wrapped key extension.
double evalCurve(const std::vector<CurvePoint> &points, double x, bool periodic);

// Monotone cubic (Fritsch-Carlson) through keys sorted by x, evaluated at x and clamped to [0,1].
double evalMonotoneSpline(const std::vector<QPointF> &keys, double x);

} // namespace drift
