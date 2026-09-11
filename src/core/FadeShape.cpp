#include "FadeShape.h"

#include "Bezier.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace drift {

QString fadeCurveToString(FadeCurve curve)
{
    switch (curve) {
    case FadeCurve::Linear:
        return QStringLiteral("linear");
    case FadeCurve::Smooth:
        return QStringLiteral("smooth");
    case FadeCurve::EqualPower:
        return QStringLiteral("equalPower");
    case FadeCurve::Custom:
        return QStringLiteral("custom");
    case FadeCurve::Bezier:
        return QStringLiteral("bezier");
    }
    return QStringLiteral("smooth");
}

FadeCurve fadeCurveFromString(const QString &curve)
{
    if (curve == QStringLiteral("linear"))
        return FadeCurve::Linear;
    if (curve == QStringLiteral("equalPower"))
        return FadeCurve::EqualPower;
    if (curve == QStringLiteral("custom"))
        return FadeCurve::Custom;
    if (curve == QStringLiteral("bezier"))
        return FadeCurve::Bezier;
    return FadeCurve::Smooth;
}

void FadeShape::setPoints(QList<QPointF> points)
{
    if (points.size() < 2) {
        m_points = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
        return;
    }

    std::sort(points.begin(), points.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });

    QList<QPointF> cleaned;
    cleaned.reserve(points.size());
    for (QPointF p : points) {
        p.setX(qBound(0.0, p.x(), 1.0));
        p.setY(qBound(0.0, p.y(), 1.0));
        if (!cleaned.isEmpty() && qAbs(cleaned.last().x() - p.x()) < 1e-9)
            cleaned.last().setY(p.y());
        else
            cleaned.append(p);
    }

    if (cleaned.isEmpty() || cleaned.first().x() > 1e-9)
        cleaned.prepend(QPointF(0.0, 0.0));
    else
        cleaned.first() = QPointF(0.0, cleaned.first().y());

    if (cleaned.last().x() < 1.0 - 1e-9)
        cleaned.append(QPointF(1.0, 1.0));
    else
        cleaned.last() = QPointF(1.0, cleaned.last().y());

    // Ends stay silent → full: pin gain at the rails.
    cleaned.first().setY(0.0);
    cleaned.last().setY(1.0);

    if (cleaned.size() < 2)
        cleaned = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};

    m_points = cleaned;
}

void FadeShape::clear()
{
    m_points.clear();
    m_c1 = QPointF(0.42, 0.0);
    m_c2 = QPointF(0.58, 1.0);
    m_hasHandles = false;
}

void FadeShape::setHandles(QPointF c1, QPointF c2)
{
    // x is clamped to [0,1] and kept ordered so the cubic stays single-valued: bezierParameterForX
    // bisects on x and would not converge on a curve that folds back. y is clamped too, because a
    // handle above 1 would push a fade's gain past unity and a transition's progress past its end.
    c1.setX(qBound(0.0, c1.x(), 1.0));
    c2.setX(qBound(0.0, c2.x(), 1.0));
    c1.setY(qBound(0.0, c1.y(), 1.0));
    c2.setY(qBound(0.0, c2.y(), 1.0));
    m_c1 = c1;
    m_c2 = c2;
    m_hasHandles = true;
}

double FadeShape::bezierAt(double t) const
{
    t = qBound(0.0, t, 1.0);
    const double u = bezierParameterForX(0.0, m_c1.x(), m_c2.x(), 1.0, t);
    return qBound(0.0, cubicBezier(0.0, m_c1.y(), m_c2.y(), 1.0, u), 1.0);
}

double FadeShape::gainAt(double t) const
{
    t = qBound(0.0, t, 1.0);
    if (isEmpty())
        return t; // linear fallback

    if (t <= m_points.first().x())
        return m_points.first().y();
    if (t >= m_points.last().x())
        return m_points.last().y();

    for (int i = 0; i + 1 < m_points.size(); ++i) {
        const QPointF &a = m_points.at(i);
        const QPointF &b = m_points.at(i + 1);
        if (t < a.x() || t > b.x())
            continue;
        const double span = b.x() - a.x();
        if (span < 1e-12)
            return b.y();
        const double u = (t - a.x()) / span;
        return a.y() + (b.y() - a.y()) * u;
    }
    return m_points.last().y();
}

// A shape with no handles still writes the bare point array every older build expects. Only a
// shape that has been given handles is promoted to an object, and then only for that one field.
QJsonValue FadeShape::toJson() const
{
    QJsonArray array;
    for (const QPointF &p : m_points) {
        array.append(QJsonObject{
            {QStringLiteral("t"), p.x()},
            {QStringLiteral("g"), p.y()},
        });
    }
    if (!m_hasHandles)
        return array;

    return QJsonObject{
        {QStringLiteral("points"), array},
        {QStringLiteral("c1"), QJsonArray{m_c1.x(), m_c1.y()}},
        {QStringLiteral("c2"), QJsonArray{m_c2.x(), m_c2.y()}},
    };
}

FadeShape FadeShape::fromJson(const QJsonValue &value)
{
    const QJsonObject obj = value.toObject();
    const QJsonArray array = value.isArray() ? value.toArray()
                                             : obj.value(QStringLiteral("points")).toArray();

    QList<QPointF> points;
    points.reserve(array.size());
    for (const QJsonValue &entry : array) {
        const QJsonObject p = entry.toObject();
        points.append(QPointF(p.value(QStringLiteral("t")).toDouble(),
                              p.value(QStringLiteral("g")).toDouble()));
    }
    FadeShape shape;
    shape.setPoints(points);

    if (!value.isArray() && obj.contains(QStringLiteral("c1"))) {
        const QJsonArray c1 = obj.value(QStringLiteral("c1")).toArray();
        const QJsonArray c2 = obj.value(QStringLiteral("c2")).toArray();
        if (c1.size() == 2 && c2.size() == 2) {
            shape.setHandles(QPointF(c1.at(0).toDouble(), c1.at(1).toDouble()),
                             QPointF(c2.at(0).toDouble(), c2.at(1).toDouble()));
        }
    }
    return shape;
}

// The CSS keywords, which is what people reach for and what the presets in the editor offer.
FadeShape FadeShape::bezierPreset(const QString &name)
{
    FadeShape shape = FadeShape::linearPreset();
    if (name == QStringLiteral("linear"))
        shape.setHandles(QPointF(1.0 / 3.0, 1.0 / 3.0), QPointF(2.0 / 3.0, 2.0 / 3.0));
    else if (name == QStringLiteral("easeIn"))
        shape.setHandles(QPointF(0.42, 0.0), QPointF(1.0, 1.0));
    else if (name == QStringLiteral("easeOut"))
        shape.setHandles(QPointF(0.0, 0.0), QPointF(0.58, 1.0));
    else if (name == QStringLiteral("ease"))
        shape.setHandles(QPointF(0.25, 0.1), QPointF(0.25, 1.0));
    else // easeInOut
        shape.setHandles(QPointF(0.42, 0.0), QPointF(0.58, 1.0));
    return shape;
}

FadeShape FadeShape::linearPreset()
{
    FadeShape shape;
    // Keep a middle handle so the custom editor always has something to drag.
    shape.setPoints({QPointF(0.0, 0.0), QPointF(0.5, 0.5), QPointF(1.0, 1.0)});
    return shape;
}

FadeShape FadeShape::smoothPreset()
{
    QList<QPointF> points;
    constexpr int kSamples = 9;
    for (int i = 0; i < kSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSamples - 1);
        const double g = t * t * (3.0 - 2.0 * t);
        points.append(QPointF(t, g));
    }
    FadeShape shape;
    shape.setPoints(points);
    return shape;
}

FadeShape FadeShape::equalPowerPreset()
{
    QList<QPointF> points;
    constexpr int kSamples = 9;
    constexpr double kHalfPi = 1.5707963267948966;
    for (int i = 0; i < kSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSamples - 1);
        points.append(QPointF(t, std::sin(t * kHalfPi)));
    }
    FadeShape shape;
    shape.setPoints(points);
    return shape;
}

} // namespace drift
