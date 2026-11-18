#include "ui/imageproviders/slipframe/SlipFrameImageProvider.h"

#include "engine/compositor/SourceFrameCrop.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "ui/imageproviders/slipframe/SlipFrameUrl.h"

#include <QColor>
#include <QtMath>

namespace {
// A stream id of its own, so the first and last frame of a slip drag keep separate decode cursors
// and don't fight the timeline's.
constexpr quint64 kSlipStreamSalt = 0x5A17F4A3C9E1B2D7ull;
} // namespace

SlipFrameImageProvider::SlipFrameImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage SlipFrameImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    drift::SlipFrameRequest req;
    if (!drift::parseSlipFrameId(id, &req))
        return {};
    const bool sized = requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0;
    const int w = sized ? requestedSize.width() : 960;
    const int h = sized ? requestedSize.height() : 540;
    const bool framed = req.frame != QRectF(0, 0, 1, 1) || req.angle != 0.0;
    // A crop shows only part of the frame, so decode enough of it for that part to fill the size.
    const int dw = framed ? qCeil(w / req.frame.width()) : w;
    const int dh = framed ? qCeil(h / req.frame.height()) : h;
    QImage image = ClipReaderPool::instance().readVideoFrame(
        req.path, kSlipStreamSalt ^ static_cast<quint64>(req.salt), req.sourceUs, dw, dh, req.rotation);
    if (image.isNull()) {
        // Offline or unreadable media: a dark placeholder, so the Image does not log a failure on
        // every drag step.
        image = QImage(16, 9, QImage::Format_RGB32);
        image.fill(QColor(0x20, 0x20, 0x20));
    } else if (framed) {
        image = drift::cropSourceFrame(image, req.frame, req.angle);
    }
    if (size)
        *size = image.size();
    return image;
}
