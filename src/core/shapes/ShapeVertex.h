#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

namespace drift {

// How a vertex keeps its two handles related while it is edited. Corner and Free leave them
// independent; Mirrored keeps them opposite and equal in length; Aligned keeps them collinear.
// The outline only ever reads in/out.
enum class TangentMode { Corner, Mirrored, Aligned, Free };

inline bool tangentModeIsSmooth(TangentMode mode)
{
    return mode == TangentMode::Mirrored || mode == TangentMode::Aligned;
}

inline QString tangentModeToString(TangentMode mode)
{
    switch (mode) {
    case TangentMode::Mirrored:
        return QStringLiteral("mirrored");
    case TangentMode::Aligned:
        return QStringLiteral("aligned");
    case TangentMode::Free:
        return QStringLiteral("free");
    case TangentMode::Corner:
        break;
    }
    return QStringLiteral("corner");
}

inline TangentMode tangentModeFromString(const QString &s)
{
    if (s == QLatin1String("mirrored"))
        return TangentMode::Mirrored;
    if (s == QLatin1String("aligned"))
        return TangentMode::Aligned;
    if (s == QLatin1String("free"))
        return TangentMode::Free;
    return TangentMode::Corner;
}

// in / out are tangent handles as offsets from p; both zero = a corner.
struct ShapeVertex
{
    QPointF p;
    QPointF in;
    QPointF out;
    TangentMode mode = TangentMode::Corner;

    // Files before the mode field stored a bool that meant "mirrored".
    bool smooth() const { return tangentModeIsSmooth(mode); }

    bool operator==(const ShapeVertex &o) const = default;
};

struct ShapeContour
{
    QVector<ShapeVertex> vertices;
    bool closed = true;

    bool operator==(const ShapeContour &o) const = default;
};

// The vertex after `map` moves every point of the plane: the anchor and both handle tips go through
// it, so a nonlinear map (a homography) bends the handles with the curve instead of only offsetting them.
template<typename Map>
ShapeVertex mapVertex(const ShapeVertex &v, Map &&map)
{
    ShapeVertex out = v;
    out.p = map(v.p);
    out.in = map(v.p + v.in) - out.p;
    out.out = map(v.p + v.out) - out.p;
    return out;
}

inline QVector<ShapeVertex> cornerVertices(const QVector<QPointF> &points)
{
    QVector<ShapeVertex> out;
    out.reserve(points.size());
    for (const QPointF &p : points)
        out.append(ShapeVertex{p, {}, {}, TangentMode::Corner});
    return out;
}

inline QVector<QPointF> vertexPositions(const QVector<ShapeVertex> &vertices)
{
    QVector<QPointF> out;
    out.reserve(vertices.size());
    for (const ShapeVertex &v : vertices)
        out.append(v.p);
    return out;
}

} // namespace drift
