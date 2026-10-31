#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>

#include <algorithm>
#include <cmath>

// The colour wheel's maths apart from the QQuickItem, so it can be tested without a scene. Offsets
// live in the unit disc with y up, as the grade's lift/gamma/gain vec2 params do.
namespace drift::colorwheel {

inline QPointF clampToUnitDisc(QPointF p)
{
    const double len = std::hypot(p.x(), p.y());
    return len > 1.0 ? QPointF(p.x() / len, p.y() / len) : p;
}

// A drag by `deltaPx` (item pixels, y down) moves the offset relative to where it was, so a
// press off the puck never jumps it. Fine control is a tenth of the speed.
inline QPointF dragged(QPointF offset, QPointF deltaPx, double radiusPx, bool fine)
{
    const double scale = fine ? 0.1 : 1.0;
    return clampToUnitDisc({offset.x() + deltaPx.x() / radiusPx * scale,
                            offset.y() - deltaPx.y() / radiusPx * scale});
}

inline QPointF nudged(QPointF offset, double dx, double dy)
{
    return clampToUnitDisc({offset.x() + dx, offset.y() + dy});
}

// The wheel shows the YIQ shift the grade shader applies for an offset, laid over the theme's
// neutral: the vector at a pixel is that pixel's own offset.
inline QRgb discColor(double x, double y, const QColor &neutral, double rimStrength)
{
    const double i = 0.5957 * x;
    const double q = 0.5226 * y;
    const auto channel = [](double v) { return std::clamp(v, 0.0, 1.0); };
    const double r = channel(neutral.redF() + rimStrength * (0.956 * i + 0.619 * q));
    const double g = channel(neutral.greenF() + rimStrength * (-0.272 * i - 0.647 * q));
    const double b = channel(neutral.blueF() + rimStrength * (-1.106 * i + 1.703 * q));
    return QColor::fromRgbF(float(r), float(g), float(b)).rgb();
}

// A square image of `px` pixels with the disc inscribed; the rim is antialiased by coverage.
inline QImage buildDisc(int px, const QColor &neutral, double rimStrength)
{
    QImage image(px, px, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const double rad = px / 2.0;
    for (int py = 0; py < px; ++py) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(py));
        for (int pxl = 0; pxl < px; ++pxl) {
            const double x = (pxl + 0.5 - rad) / rad;
            const double y = -(py + 0.5 - rad) / rad;
            const double coverage = std::clamp(rad - std::hypot(x, y) * rad + 0.5, 0.0, 1.0);
            if (coverage <= 0.0)
                continue;
            const QColor c = QColor::fromRgb(discColor(x, y, neutral, rimStrength));
            row[pxl] = qPremultiply(qRgba(c.red(), c.green(), c.blue(), int(std::lround(coverage * 255.0))));
        }
    }
    return image;
}

} // namespace drift::colorwheel
