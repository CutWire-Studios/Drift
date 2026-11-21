#pragma once

#include "core/shapes/ShapeStyle.h"

#include <QByteArray>
#include <QString>

namespace drift {

// Reads an SVG's geometry as editable contours: <path>, <rect>, <circle>, <ellipse>, <line>,
// <polyline> and <polygon>, through nested <g> and `transform`. <defs>, <clipPath>, <mask>,
// <symbol> and the like are skipped. Colours and strokes are ignored: the result is an outline.
// `out` is normalized to the union bounds, which `aspect` (width / height) reports; a line gets a
// thin box rather than a zero-height one. `rule` is even-odd when any element asks for it.
// Without Skia (needed to parse path data) this fails with an error.
bool importSvgShapeData(const QByteArray &svg, QVector<ShapeContour> *out, Qt::FillRule *rule, double *aspect,
                        QString *error);
bool importSvgShape(const QString &path, QVector<ShapeContour> *out, Qt::FillRule *rule, double *aspect,
                    QString *error);

// One SVG path "d" string whose coordinates already are the 0..1 box, as the MCP `path` field
// carries. Needs Skia like the above.
bool contoursFromSvgPath(const QString &d, QVector<ShapeContour> *out, Qt::FillRule *rule, QString *error);

} // namespace drift
