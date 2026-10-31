#pragma once

#include "engine/gpu/effects/LookupTextures.h"

#include <QPointF>
#include <QSizeF>
#include <QString>

#include <vector>

// Point editing for a colour curve in item pixels, apart from the QQuickItem so it can be tested
// without a scene. Points are always sorted by x, at least kMinCurveGap apart and at most
// kMaxCurvePoints; the first and last never move in x or get removed.
namespace drift::curveedit {

using Points = std::vector<CurvePoint>;

// The point whose centre is nearest to `pos` within `reach` pixels, or -1.
int hit(const Points &pts, QSizeF size, QPointF pos, double reach);
// Adds a point on the drawn curve at pixel x. Returns its index, or -1 when the curve is full or
// another point is closer than kMinCurveGap.
int insertAt(Points &pts, double px, double width, bool hue);
// Moves point `idx` to `pos`. A point dragged more than 25% outside the plot is removed instead:
// the return is the new index, or -1 when it was removed.
int dragTo(Points &pts, int idx, QPointF pos, QSizeF size, bool hue);
// Nudges by (dx, dy) in curve units, keeping the neighbour gap.
void nudge(Points &pts, int idx, double dx, double dy, bool hue);
// Removes an inner point; false for the end points or an out-of-range index.
bool removeAt(Points &pts, int idx);
// A hue curve wraps, so its end points share one y.
void tieHueEnds(Points &pts, int idx, bool hue);
QString toJson(const Points &pts);

} // namespace drift::curveedit
