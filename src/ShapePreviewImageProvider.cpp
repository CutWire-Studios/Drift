#include "ShapePreviewImageProvider.h"

#include "core/ShapeStyle.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/SkiaRuntime.h"
#include "engine/SkiaShapePainter.h"
#endif

#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>

namespace {

constexpr int kDefaultSide = 112;
// A fresh shape is 30% of a 1080p canvas tall, so the catalog's 4 px stroke and 32 px corner
// radius read at the same proportion on the card as on the timeline.
constexpr double kReferenceHeight = 1080.0 * 0.30;

QMutex g_cardMutex;
QCache<QString, QImage> g_cardCache(64);

} // namespace

ShapePreviewImageProvider::ShapePreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage ShapePreviewImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(id);
    if (!entry) {
        if (size)
            *size = QSize();
        return {};
    }
    const int cardW = requestedSize.width() > 0 ? requestedSize.width() : kDefaultSide;
    const int cardH = requestedSize.height() > 0 ? requestedSize.height() : kDefaultSide;
    const QString key = QStringLiteral("%1|%2x%3").arg(id).arg(cardW).arg(cardH);
    {
        QMutexLocker lock(&g_cardMutex);
        if (const QImage *hit = g_cardCache.object(key)) {
            if (size)
                *size = hit->size();
            return *hit;
        }
    }

    QImage card(cardW, cardH, QImage::Format_ARGB32_Premultiplied);
    card.fill(Qt::transparent);
#ifdef DRIFT_WITH_SKIA
    // The shape box is fitted inside the card with room for the bleed (stroke, shadow) around it.
    const double aspect = entry->aspect > 0.01 ? entry->aspect : 1.0;
    double boxH = cardH;
    double boxW = boxH * aspect;
    if (boxW > cardW) {
        boxW = cardW;
        boxH = boxW / aspect;
    }
    double scale = boxH / kReferenceHeight;
    const double bleed = drift::skia::shapeBleedFor(entry->style) * scale;
    const double fit = qMin((cardW - 2.0 * bleed) / boxW, (cardH - 2.0 * bleed) / boxH);
    if (fit > 0.0 && fit < 1.0) {
        boxW *= fit;
        boxH *= fit;
        scale *= fit;
    }
    drift::skia::ShapePaintRequest request;
    request.style = entry->style;
    request.layoutRect = QRectF((cardW - boxW) / 2.0, (cardH - boxH) / 2.0, boxW, boxH);
    request.renderScale = scale;
    const drift::skia::ShapePainterResult painted = drift::skia::makeShapePainter(request);
    const QImage raster = drift::skia::SkiaRuntime::rasterize(*painted.painter);
    QPainter p(&card);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(painted.rect.topLeft(), raster);
#endif
    {
        QMutexLocker lock(&g_cardMutex);
        g_cardCache.insert(key, new QImage(card), qMax(1, int(card.sizeInBytes() / 1024)));
    }
    if (size)
        *size = card.size();
    return card;
}
