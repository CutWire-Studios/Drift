#include "ui/imageproviders/ShapePreviewImageProvider.h"

#include "core/shapes/ShapeStyle.h"
#include "engine/render2d/shapes/UserShapeStore.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/skia/SkiaRuntime.h"
#include "engine/render2d/shapes/SkiaShapePainter.h"
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
    // "user:<stem>?r=<revision>" for a library shape: the suffix only keeps this cache honest.
    const QString shapeId = id.left(id.indexOf(u'?'));
    drift::ShapeStyle style;
    double aspect = 0.0;
    if (drift::UserShapeStore::isUserId(shapeId)) {
        if (const std::optional<drift::UserShape> shape = drift::UserShapeStore::instance().find(shapeId)) {
            style = drift::pathShapeStyle(shape->contours, shape->fillRule);
            aspect = shape->aspect;
        }
    } else if (const drift::ShapeCatalogEntry *entry = drift::shapeCatalogEntry(shapeId)) {
        style = entry->style;
        aspect = entry->aspect;
    }
    if (aspect <= 0.0) {
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
    aspect = aspect > 0.01 ? aspect : 1.0;
    double boxH = cardH;
    double boxW = boxH * aspect;
    if (boxW > cardW) {
        boxW = cardW;
        boxH = boxW / aspect;
    }
    double scale = boxH / kReferenceHeight;
    const double bleed = drift::skia::shapeBleedFor(style) * scale;
    const double fit = qMin((cardW - 2.0 * bleed) / boxW, (cardH - 2.0 * bleed) / boxH);
    if (fit > 0.0 && fit < 1.0) {
        boxW *= fit;
        boxH *= fit;
        scale *= fit;
    }
    drift::skia::ShapePaintRequest request;
    request.style = style;
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
        // Counted per card: a byte cost against a max of 64 held at most one card.
        g_cardCache.insert(key, new QImage(card));
    }
    if (size)
        *size = card.size();
    return card;
}
