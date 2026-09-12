#pragma once

#include "ShapeStyle.h"

#include <QPainterPath>
#include <QRectF>
#include <QString>

namespace drift {

// Every shape fills `bounds` exactly, so a "square" or "circle" is a rectangle or ellipse in a
// square box rather than a path that forces its own aspect. Strokes are the painter's business:
// the path is the outline the layer stack paints around.
QPainterPath shapePath(const ShapeStyle &style, const QRectF &bounds);

// Any path serialized as an SVG "d" string, for QML's PathSvg; masks serialize drift::maskPath()
// through here for their assets-panel thumbnails.
QString painterPathToSvg(const QPainterPath &path);

// Regular n-gon inscribed in `bounds`, first vertex at the top. Shared with mask rendering.
QPainterPath regularPolygonPath(const QRectF &bounds, int sides, double rotationDeg);

// Heart filling `bounds`. Shared with mask rendering.
QPainterPath heartPath(const QRectF &bounds);

} // namespace drift
