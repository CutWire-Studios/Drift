#include "ui/color/CurveEdit.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace drift::curveedit {

int hit(const Points &pts, QSizeF size, QPointF pos, double reach)
{
    int best = -1;
    double bestD = reach * reach;
    for (size_t i = 0; i < pts.size(); ++i) {
        const double dx = pts[i].x * size.width() - pos.x();
        const double dy = (1.0 - pts[i].y) * size.height() - pos.y();
        const double d = dx * dx + dy * dy;
        if (d <= bestD) {
            bestD = d;
            best = int(i);
        }
    }
    return best;
}

int insertAt(Points &pts, double px, double width, bool hue)
{
    if (pts.size() >= size_t(kMaxCurvePoints) || width <= 0.0)
        return -1;
    const double x = std::clamp(px / width, 0.0, 1.0);
    for (const CurvePoint &p : pts) {
        if (p.x > x - kMinCurveGap && p.x < x + kMinCurveGap)
            return -1;
    }
    const double y = evalCurve(pts, x, hue);
    const auto at = std::upper_bound(pts.begin(), pts.end(), x, [](double v, const CurvePoint &p) { return v < p.x; });
    const auto it = pts.insert(at, {x, y, QStringLiteral("spline")});
    return int(it - pts.begin());
}

void tieHueEnds(Points &pts, int idx, bool hue)
{
    const int last = int(pts.size()) - 1;
    if (!hue || last < 1 || (idx != 0 && idx != last))
        return;
    pts[size_t(idx == 0 ? last : 0)].y = pts[size_t(idx)].y;
}

int dragTo(Points &pts, int idx, QPointF pos, QSizeF size, bool hue)
{
    const int last = int(pts.size()) - 1;
    if (idx < 0 || idx > last || size.width() <= 0.0 || size.height() <= 0.0)
        return idx;
    const bool inner = idx > 0 && idx < last;
    const bool outside = pos.x() < -size.width() * 0.25 || pos.x() > size.width() * 1.25
                         || pos.y() < -size.height() * 0.25 || pos.y() > size.height() * 1.25;
    if (inner && outside) {
        pts.erase(pts.begin() + idx);
        return -1;
    }
    double x = pts[size_t(idx)].x;
    if (inner) {
        x = std::min(pts[size_t(idx) + 1].x - kMinCurveGap,
                     std::max(pts[size_t(idx) - 1].x + kMinCurveGap, pos.x() / size.width()));
    }
    pts[size_t(idx)].x = x;
    pts[size_t(idx)].y = std::clamp(1.0 - pos.y() / size.height(), 0.0, 1.0);
    tieHueEnds(pts, idx, hue);
    return idx;
}

void nudge(Points &pts, int idx, double dx, double dy, bool hue)
{
    const int last = int(pts.size()) - 1;
    if (idx < 0 || idx > last)
        return;
    CurvePoint &p = pts[size_t(idx)];
    if (idx > 0 && idx < last) {
        p.x = std::min(pts[size_t(idx) + 1].x - kMinCurveGap,
                       std::max(pts[size_t(idx) - 1].x + kMinCurveGap, std::clamp(p.x + dx, 0.0, 1.0)));
    }
    p.y = std::clamp(p.y + dy, 0.0, 1.0);
    tieHueEnds(pts, idx, hue);
}

bool removeAt(Points &pts, int idx)
{
    if (idx <= 0 || idx >= int(pts.size()) - 1)
        return false;
    pts.erase(pts.begin() + idx);
    return true;
}

QString toJson(const Points &pts)
{
    QJsonArray arr;
    for (const CurvePoint &p : pts) {
        arr.append(QJsonObject{{QStringLiteral("x"), p.x},
                               {QStringLiteral("y"), p.y},
                               {QStringLiteral("ease"), p.ease.isEmpty() ? QStringLiteral("spline") : p.ease}});
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

} // namespace drift::curveedit
