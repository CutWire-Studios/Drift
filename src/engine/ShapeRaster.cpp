#include "ShapeRaster.h"

#include "core/ShapePath.h"

#include <QPainter>
#include <QtMath>

namespace drift {

namespace {

Qt::PenStyle penStyleFor(ShapeStrokeStyle style)
{
    switch (style) {
    case ShapeStrokeStyle::None:
        return Qt::NoPen;
    case ShapeStrokeStyle::Solid:
        return Qt::SolidLine;
    case ShapeStrokeStyle::Dash:
        return Qt::DashLine;
    case ShapeStrokeStyle::Dot:
        return Qt::DotLine;
    case ShapeStrokeStyle::DashDot:
        return Qt::DashDotLine;
    }
    return Qt::SolidLine;
}

} // namespace

ShapeLayout shapeLayout(const ShapeStyle &style, int width, int height, double renderScale)
{
    ShapeLayout layout;
    layout.strokeWidth =
        style.strokeStyle == ShapeStrokeStyle::None ? 0.0 : style.strokeWidth * renderScale;
    const double inset = layout.strokeWidth / 2.0;
    layout.bounds = QRectF(0, 0, qMax(1, width), qMax(1, height))
                        .adjusted(inset, inset, -inset, -inset)
                        .normalized();
    layout.style = style;
    layout.style.cornerRadius = style.cornerRadius * renderScale;
    return layout;
}

QBrush shapeBrush(const ShapeStyle &style, const QRectF &bounds)
{
    switch (style.fillKind) {
    case ShapeFillKind::None:
        return Qt::NoBrush;
    case ShapeFillKind::Solid:
        return style.fill;
    case ShapeFillKind::LinearGradient: {
        // Angle sweeps the gradient axis across the shape's own bounding box, so the same angle
        // reads the same whatever the clip is scaled to.
        const double radians = qDegreesToRadians(style.gradientAngle);
        const QPointF centre = bounds.center();
        const QPointF half(qCos(radians) * bounds.width() / 2.0,
                           qSin(radians) * bounds.height() / 2.0);
        QLinearGradient gradient(centre - half, centre + half);
        gradient.setColorAt(0.0, style.fill);
        gradient.setColorAt(1.0, style.fillSecondary);
        return gradient;
    }
    case ShapeFillKind::RadialGradient: {
        QRadialGradient gradient(bounds.center(),
                                 qMax(bounds.width(), bounds.height()) / 2.0);
        gradient.setColorAt(0.0, style.fill);
        gradient.setColorAt(1.0, style.fillSecondary);
        return gradient;
    }
    }
    return style.fill;
}

QPen shapePen(const ShapeStyle &style, double strokeWidth)
{
    if (strokeWidth <= 0.0)
        return QPen(Qt::NoPen);
    return QPen(style.stroke, strokeWidth, penStyleFor(style.strokeStyle), Qt::RoundCap,
                Qt::RoundJoin);
}

QImage rasterizeShape(const ShapeStyle &style, int width, int height, double renderScale)
{
    const ShapeLayout layout = shapeLayout(style, width, height, renderScale);

    QImage image(qMax(1, width), qMax(1, height), QImage::Format_RGBA8888);
    image.fill(Qt::transparent);

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(shapeBrush(layout.style, layout.bounds));
    p.setPen(shapePen(style, layout.strokeWidth));
    p.drawPath(shapePath(layout.style, layout.bounds));
    p.end();
    return image;
}

quint64 shapeRasterKey(const ShapeStyle &style, int width, int height, double renderScale)
{
    const quint64 key = qHashMulti(
        0x5ca9e, style.kind, style.fillKind, style.fill.rgba(), style.fillSecondary.rgba(),
        style.gradientAngle, style.stroke.rgba(), style.strokeWidth, style.strokeStyle,
        style.cornerRadius, style.points, style.innerRatio, style.headSize, style.thickness,
        style.tailX, style.tailSize, width, height, renderScale);
    return key ? key : 1;
}

} // namespace drift
