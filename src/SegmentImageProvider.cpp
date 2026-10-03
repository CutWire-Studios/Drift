#include "SegmentImageProvider.h"

#include "SegmentImageStore.h"

SegmentImageProvider::SegmentImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage SegmentImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Callers append a revision (image://segment/mask?rev=7) to defeat QML's URL-keyed cache; the
    // query itself carries no meaning here.
    const QString kind = id.section(QLatin1Char('?'), 0, 0);

    QImage image;
    if (kind == QLatin1String("frame"))
        image = SegmentImageStore::frame();
    else if (kind == QLatin1String("mask"))
        image = SegmentImageStore::mask();
    else if (kind == QLatin1String("restore-original"))
        image = SegmentImageStore::restoreOriginal();
    else if (kind == QLatin1String("restore-enhanced"))
        image = SegmentImageStore::restoreEnhanced();

    if (image.isNull()) {
        if (size)
            *size = QSize();
        return {};
    }

    if (requestedSize.isValid() && requestedSize.width() > 0 && requestedSize.height() > 0)
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size)
        *size = image.size();
    return image;
}
