#pragma once

#include "ShapeStyle.h"

#include <QPainterPath>
#include <QRectF>
#include <QString>

namespace drift {

// Every shape fills `bounds` exactly, so a "square" or "circle" is a rectangle or ellipse in a
// square box rather than a path that forces its own aspect. Callers inset `bounds` by half the
// stroke width to keep the outline inside the layer.
QPainterPath shapePath(const ShapeStyle &style, const QRectF &bounds);

// Any path serialized as an SVG "d" string, for QML's PathSvg. Sharing the paths the compositor
// rasterizes is what keeps the assets-panel thumbnails and the composited frame from drifting
// apart — masks serialize drift::maskPath() through here for the same reason.
QString painterPathToSvg(const QPainterPath &path);

// The same path serialized as an SVG "d" string, for QML's PathSvg.
QString shapeSvgPath(const ShapeStyle &style, const QRectF &bounds);

// Regular n-gon inscribed in `bounds`, first vertex at the top. Shared with mask rendering.
QPainterPath regularPolygonPath(const QRectF &bounds, int sides, double rotationDeg);

// Heart filling `bounds`. Shared with mask rendering.
QPainterPath heartPath(const QRectF &bounds);

} // namespace drift
