#include "TextStylePreviewImageProvider.h"

#include "core/Clip.h"
#include "core/TextStyle.h"
#include "engine/RenderBackend.h"
#include "engine/TextRaster.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/SkiaRuntime.h"
#include "engine/SkiaTextPainter.h"
#endif

#include <QPainter>

#include <optional>

namespace {

// Fallback when a pack has no sampleText (should not happen for built-in packs).
const QString kFallbackSample = QStringLiteral("Your text here");

// Preview cards are sized in project pixels against a 1080-wide canvas, which is what maps a pack's
// pixelSize onto the card at the same relative scale the compositor uses.
constexpr double kReferenceWidth = 1080.0;
constexpr int kDefaultWidth = 240;
constexpr int kDefaultHeight = 108;

} // namespace

TextStylePreviewImageProvider::TextStylePreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage TextStylePreviewImageProvider::requestImage(const QString &id, QSize *size,
                                                   const QSize &requestedSize)
{
    const std::optional<drift::TextPreset> preset = drift::textPresetForId(id);
    if (!preset) {
        if (size)
            *size = QSize();
        return {};
    }

    const int width = requestedSize.width() > 0 ? requestedSize.width() : kDefaultWidth;
    const int height = requestedSize.height() > 0 ? requestedSize.height() : kDefaultHeight;

    drift::Clip clip;
    clip.type = drift::ClipType::Text;
    clip.textStyle = preset->style;

    const QString sample = preset->sampleText.isEmpty() ? kFallbackSample : preset->sampleText;

    const QRectF layoutRect(0, 0, width, height);
    // A karaoke pack accents nothing at all without a playhead, so the card borrows the second word.
    const int activeWord = preset->style.accent.rule == drift::WordAccentRule::Karaoke ? 1 : -1;
    const double scale = width / kReferenceWidth;
    QImage raster;
    QPointF origin;
#ifdef DRIFT_WITH_SKIA
    if (drift::vectorBackend() == drift::VectorBackend::Skia) {
        // Same painter the compositor draws, rasterised on the CPU: no GL on the image provider.
        const drift::skia::TextPainterResult painted =
            drift::skia::makeTextPainter(clip, sample, layoutRect, scale, activeWord);
        if (painted.painter)
            raster = drift::skia::SkiaRuntime::rasterize(*painted.painter);
        origin = painted.rect.topLeft();
    } else
#endif
    {
        const TextRasterResult result = rasterizeText(clip, sample, layoutRect, scale, activeWord);
        raster = result.image;
        origin = result.rect.topLeft();
    }

    QImage card(width, height, QImage::Format_ARGB32_Premultiplied);
    card.fill(Qt::transparent);
    if (!raster.isNull()) {
        QPainter p(&card);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(origin, raster);
    }

    if (size)
        *size = card.size();
    return card;
}
