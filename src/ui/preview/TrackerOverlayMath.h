#pragma once

#include <QPointF>
#include <QRectF>
#include <QVector>

#include <algorithm>

namespace drift::trackerview {

enum class Tint { Bad, Warn, Good };

inline Tint tintFor(double confidence, double minConfidence)
{
    if (confidence < minConfidence)
        return Tint::Bad;
    return confidence >= 0.8 ? Tint::Good : Tint::Warn;
}

// A confidence-strip bucket: -1 holds no data, else the lowest confidence in it.
enum class StripTint { Empty, Bad, Warn, Good };

inline StripTint stripTintFor(double value, double minConfidence)
{
    if (value < 0)
        return StripTint::Empty;
    switch (tintFor(value, minConfidence)) {
    case Tint::Bad:
        return StripTint::Bad;
    case Tint::Good:
        return StripTint::Good;
    default:
        return StripTint::Warn;
    }
}

struct StripRun
{
    int first = 0;
    int count = 0;
    StripTint tint = StripTint::Empty;
};

inline QVector<StripRun> stripRuns(const QVector<double> &values, double minConfidence)
{
    QVector<StripRun> runs;
    for (int i = 0; i < values.size(); ++i) {
        const StripTint tint = stripTintFor(values.at(i), minConfidence);
        if (!runs.isEmpty() && runs.last().tint == tint)
            ++runs.last().count;
        else
            runs.append({i, 1, tint});
    }
    return runs;
}

// A point's hit area is at least minSide wide and tall, centred on the marker.
inline bool pointHit(QPointF at, QPointF centre, double patternW, double patternH, double minSide = 24.0)
{
    const double w = std::max(minSide, patternW);
    const double h = std::max(minSide, patternH);
    return QRectF(centre.x() - w / 2, centre.y() - h / 2, w, h).contains(at);
}

// A corner handle is a square of `side`, grabbable `margin` beyond it.
inline bool cornerHit(QPointF at, QPointF centre, double side, double margin)
{
    const double half = side / 2 + margin;
    return QRectF(centre.x() - half, centre.y() - half, half * 2, half * 2).contains(at);
}

inline double signedArea(const QVector<QPointF> &poly)
{
    double a = 0;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF p = poly.at(i);
        const QPointF q = poly.at((i + 1) % poly.size());
        a += p.x() * q.y() - q.x() * p.y();
    }
    return a / 2;
}

// Ear clipping, so a concave outline fills correctly. Triangles come back as index triples.
inline QVector<int> triangulate(const QVector<QPointF> &poly)
{
    QVector<int> out;
    const int n = poly.size();
    if (n < 3)
        return out;
    QVector<int> idx(n);
    for (int i = 0; i < n; ++i)
        idx[i] = i;
    const double sign = signedArea(poly) >= 0 ? 1.0 : -1.0;
    const auto cross = [&](int a, int b, int c) {
        const QPointF pa = poly.at(a);
        const QPointF pb = poly.at(b);
        const QPointF pc = poly.at(c);
        return ((pb.x() - pa.x()) * (pc.y() - pa.y()) - (pb.y() - pa.y()) * (pc.x() - pa.x())) * sign;
    };
    const auto inside = [&](int p, int a, int b, int c) {
        const QPointF pp = poly.at(p);
        const auto side = [&](int u, int v) {
            const QPointF pu = poly.at(u);
            const QPointF pv = poly.at(v);
            return ((pv.x() - pu.x()) * (pp.y() - pu.y()) - (pv.y() - pu.y()) * (pp.x() - pu.x())) * sign;
        };
        return side(a, b) >= 0 && side(b, c) >= 0 && side(c, a) >= 0;
    };
    int guard = n * n;
    while (idx.size() > 3 && guard-- > 0) {
        bool clipped = false;
        for (int i = 0; i < idx.size(); ++i) {
            const int a = idx.at((i + idx.size() - 1) % idx.size());
            const int b = idx.at(i);
            const int c = idx.at((i + 1) % idx.size());
            if (cross(a, b, c) <= 0)
                continue;
            bool empty = true;
            for (const int p : idx) {
                if (p != a && p != b && p != c && inside(p, a, b, c)) {
                    empty = false;
                    break;
                }
            }
            if (!empty)
                continue;
            out << a << b << c;
            idx.removeAt(i);
            clipped = true;
            break;
        }
        if (!clipped)
            break;
    }
    if (idx.size() == 3)
        out << idx.at(0) << idx.at(1) << idx.at(2);
    return out;
}

} // namespace drift::trackerview
