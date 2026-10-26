#include "engine/compositor/SourceFrameCrop.h"

#include <QPainter>

namespace drift {

QImage cropSourceFrame(const QImage &image, const QRectF &frame, double angle)
{
    if (image.isNull())
        return image;
    const int w = image.width();
    const int h = image.height();
    if (angle == 0.0) {
        if (frame == QRectF(0, 0, 1, 1))
            return image;
        const int left = qBound(0, qRound(frame.x() * w), w - 1);
        const int top = qBound(0, qRound(frame.y() * h), h - 1);
        return image.copy(left, top, qBound(1, qRound(frame.width() * w), w - left),
                          qBound(1, qRound(frame.height() * h), h - top));
    }

    QImage out(qMax(1, qRound(frame.width() * w)), qMax(1, qRound(frame.height() * h)),
               QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter painter(&out);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.translate(-frame.x() * w, -frame.y() * h);
        painter.translate(w / 2.0, h / 2.0);
        painter.rotate(angle);
        painter.translate(-w / 2.0, -h / 2.0);
        painter.drawImage(0, 0, image);
    }
    // Keep the caller's pixel layout; one without alpha could not hold the empty corners.
    return out.convertToFormat(image.hasAlphaChannel() ? image.format() : QImage::Format_RGBA8888);
}

} // namespace drift
