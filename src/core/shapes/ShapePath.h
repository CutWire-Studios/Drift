#pragma once

#include "core/shapes/ShapeStyle.h"

#include <QPainterPath>
#include <QRectF>
#include <QSizeF>
#include <QString>

namespace drift {

// Every shape fills `bounds` exactly, so a "square" or "circle" is a rectangle or ellipse in a
// square box rather than a path that forces its own aspect. Strokes are the painter's business:
// the path is the outline the layer stack paints around.
QPainterPath shapePath(const ShapeStyle &style, const QRectF &bounds);

// "Make editable": the path's subpaths as normalized vertices (lines -> corners, cubics -> tangent
// handles). A subpath that ends where it began is closed and its duplicated last vertex dropped;
// anything else is open. The fill rule is not carried, copy path.fillRule() into the style.
QVector<ShapeContour> contoursFromPainterPath(const QPainterPath &path, const QRectF &bounds);

// The contours as an SVG "d" string, coordinates multiplied by `scale`. Closed contours end in Z and
// open ones do not, so an SVG parser gives the same open/closed split back.
QString contoursToSvgPath(const QVector<ShapeContour> &contours, const QSizeF &scale = QSizeF(1.0, 1.0));

// Any path serialized as an SVG "d" string, for QML's PathSvg; masks serialize drift::maskPath()
// through here for their assets-panel thumbnails.
QString painterPathToSvg(const QPainterPath &path);

// Regular n-gon inscribed in `bounds`, first vertex at the top. Shared with mask rendering.
QPainterPath regularPolygonPath(const QRectF &bounds, int sides, double rotationDeg);

// Five-point star filling `bounds`, top spike up. Shared with mask rendering.
QPainterPath maskStarPath(const QRectF &bounds);

// Heart filling `bounds`. Shared with mask rendering.
QPainterPath heartPath(const QRectF &bounds);

} // namespace drift
