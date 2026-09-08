#pragma once

#include <QJsonArray>
#include <QJsonValue>
#include <QList>
#include <QPointF>
#include <QString>

#include <cmath>

namespace drift {

// Style for edge fades and CapCut-style intro/outro progress (Linear / Smooth /
// Natural / Custom / Bezier). Natural is equal-power — nicer for audio and soft fades.
//
// Custom and Bezier are both hand-drawn but are different shapes, not two spellings of one:
// Custom is a polyline through any number of knots, Bezier is a single cubic with its ends
// pinned and two handles, exactly like CSS cubic-bezier(). They coexist because the polyline
// can hold a shape (a flat hold, a step) that one cubic cannot, and the cubic gives smooth
// acceleration that a polyline only approximates.
enum class FadeCurve { Linear, Smooth, EqualPower, Custom, Bezier };

QString fadeCurveToString(FadeCurve curve);
FadeCurve fadeCurveFromString(const QString &curve);

// Unit gain curve for fades and animation progress: t in [0,1] → gain in [0,1].
//
// Holds both hand-drawn shapes so that `shapedProgress(t, curve, shape)` stays a three-argument
// call at all six of its call sites: the polyline for FadeCurve::Custom, and the two cubic
// handles for FadeCurve::Bezier. Which one is read is decided by the FadeCurve beside it.
class FadeShape
{
public:
    bool isEmpty() const { return m_points.size() < 2; }

    const QList<QPointF> &points() const { return m_points; }
    void setPoints(QList<QPointF> points);
    void clear();

    double gainAt(double t) const;

    // Cubic handles, in curve space. The anchors are always (0,0) and (1,1); these are the two
    // control points between them, so the pair is the same four numbers CSS cubic-bezier() takes.
    QPointF handle1() const { return m_c1; }
    QPointF handle2() const { return m_c2; }
    void setHandles(QPointF c1, QPointF c2);
    double bezierAt(double t) const;
    // True once handles have been set away from the default ease. Keeps projects that never
    // touched bezier serializing exactly as they did before it existed.
    bool hasHandles() const { return m_hasHandles; }

    QJsonValue toJson() const;
    static FadeShape fromJson(const QJsonValue &value);

    // Seed matching Smooth (smoothstep).
    static FadeShape smoothPreset();
    // Straight diagonal — Linear preset.
    static FadeShape linearPreset();
    // Equal-power (Natural) seed.
    static FadeShape equalPowerPreset();
    // Cubic seeds, matching the CSS keywords of the same name.
    static FadeShape bezierPreset(const QString &name);

private:
    QList<QPointF> m_points;
    // Defaults are CSS ease-in-out; a shape that has never been given handles still evaluates
    // sensibly if a project asks for Bezier.
    QPointF m_c1{0.42, 0.0};
    QPointF m_c2{0.58, 1.0};
    bool m_hasHandles = false;
};

inline double shapedProgress(double t, FadeCurve curve, const FadeShape &shape)
{
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    switch (curve) {
    case FadeCurve::Linear:
        return t;
    case FadeCurve::Smooth:
        return t * t * (3.0 - 2.0 * t);
    case FadeCurve::EqualPower:
        return std::sin(t * 1.5707963267948966); // t * pi/2
    case FadeCurve::Custom:
        return shape.isEmpty() ? t : shape.gainAt(t);
    case FadeCurve::Bezier:
        return shape.bezierAt(t);
    }
    return t;
}

} // namespace drift
