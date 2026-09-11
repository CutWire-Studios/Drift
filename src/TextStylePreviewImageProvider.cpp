#include "TextStylePreviewImageProvider.h"

#include "core/Clip.h"
#include "core/TextAnimationPreset.h"
#include "core/TextLook.h"
#include "core/TextStyle.h"
#ifdef DRIFT_WITH_SKIA
#include "engine/SkiaRuntime.h"
#include "engine/SkiaTextPainter.h"
#endif

#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QUrl>
#include <QUrlQuery>

#include <optional>

namespace {

// Fallback when a pack has no sampleText (should not happen for built-in packs).
const QString kFallbackSample = QStringLiteral("Your text here");

// Preview cards are sized in project pixels against a 1080-wide canvas, which is what maps a pack's
// pixelSize onto the card at the same relative scale the compositor uses.
constexpr double kReferenceWidth = 1080.0;
constexpr int kDefaultWidth = 240;
constexpr int kDefaultHeight = 108;

// Animation and look tiles are small; their canned text is sized against a 150 px tall card so
// the preset's px values (blur, rise) read at thumbnail scale.
constexpr double kTileReferenceHeight = 150.0;
constexpr int kTilePixelSize = 58;

QMutex g_cardMutex;
QCache<QString, QImage> g_cardCache(96);

QImage cachedCard(const QString &key)
{
    QMutexLocker lock(&g_cardMutex);
    const QImage *hit = g_cardCache.object(key);
    return hit ? *hit : QImage();
}

void storeCard(const QString &key, const QImage &image)
{
    QMutexLocker lock(&g_cardMutex);
    g_cardCache.insert(key, new QImage(image), qMax(1, int(image.sizeInBytes() / 1024)));
}

drift::Clip tileClip(const QString &fontFamily, int weight, bool italic, drift::TimeUs durationUs)
{
    drift::Clip clip;
    clip.type = drift::ClipType::Text;
    clip.timelineDuration = durationUs;
    clip.textStyle.fontFamily = fontFamily.isEmpty() ? QStringLiteral("Inter") : fontFamily;
    clip.textStyle.fontWeight = weight > 0 ? weight : 700;
    clip.textStyle.italic = italic;
    clip.textStyle.pixelSize = kTilePixelSize;
    clip.textStyle.wordWrap = true;
    return clip;
}

} // namespace

QImage renderTextCard(const drift::Clip &clip, const QString &text, const QSize &size, double renderScale,
                      drift::TimeUs clipTimeUs, int activeWordIndex)
{
    QImage card(size, QImage::Format_ARGB32_Premultiplied);
    card.fill(Qt::transparent);
#ifdef DRIFT_WITH_SKIA
    const QRectF layoutRect(0, 0, size.width(), size.height());
    // Same painter the compositor draws, rasterised on the CPU: no GL on the image provider.
    const drift::skia::TextPainterResult painted =
        drift::skia::makeTextPainter(clip, text, layoutRect, renderScale, activeWordIndex, clipTimeUs);
    if (painted.painter) {
        const QImage raster = drift::skia::SkiaRuntime::rasterize(*painted.painter);
        QPainter p(&card);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawImage(painted.rect.topLeft(), raster);
    }
#else
    Q_UNUSED(clip); Q_UNUSED(text); Q_UNUSED(renderScale); Q_UNUSED(clipTimeUs); Q_UNUSED(activeWordIndex);
#endif
    return card;
}

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
    // A karaoke pack accents nothing at all without a playhead, so the card borrows the second word.
    const int activeWord = preset->style.accent.rule == drift::WordAccentRule::Karaoke ? 1 : -1;
    const QImage card = renderTextCard(clip, sample, QSize(width, height), width / kReferenceWidth, -1, activeWord);
    if (size)
        *size = card.size();
    return card;
}

// ---------------------------------------------------------------------------------------------

TextAnimPreviewImageProvider::TextAnimPreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage TextAnimPreviewImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize);
    const QUrl url(QStringLiteral("image://textanim/") + id);
    const QUrlQuery query(url);
    const QStringList path = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (path.size() < 2) {
        if (size)
            *size = QSize();
        return {};
    }
    const QString slot = path.at(0);
    const QString presetId = path.at(1);
    const int frameW = qBound(16, query.queryItemValue(QStringLiteral("w")).toInt(), 512);
    const int frameH = qBound(16, query.queryItemValue(QStringLiteral("h")).toInt(), 512);
    const int frames = qBound(1, query.queryItemValue(QStringLiteral("frames")).toInt(), 60);
    const int columns = qMin(6, frames);
    const int rows = (frames + columns - 1) / columns;

    const QString key = QStringLiteral("anim|") + id;
    if (const QImage hit = cachedCard(key); !hit.isNull()) {
        if (size)
            *size = hit.size();
        return hit;
    }

    const std::optional<drift::TextAnimationPreset> preset =
        drift::TextAnimationPresetCatalog::instance().presetForId(presetId);
    if (!preset) {
        if (size)
            *size = QSize();
        return {};
    }

    // The window the frames span: an entrance plus a short hold, a hold then the exit, or one
    // loop period. Reveals are timed from the preset's own defaults over the sample's units.
    const QMap<QString, drift::VectorSlotValue> defaults = preset->defaultParams();
    const double duration = drift::scalarParam(defaults, QStringLiteral("duration"), 0.4);
    const double stagger = drift::scalarParam(defaults, QStringLiteral("stagger"), 0.0);
    const QString sample = preset->sampleText.isEmpty() ? QStringLiteral("Your text") : preset->sampleText;
    const int units = qMax(1, sample.size());
    const double reveal = duration + stagger * qMin(units, 12);
    double windowS = 2.0;
    double t0 = 0.0, t1 = 2.0;
    if (slot == QLatin1String("loop")) {
        const double period = drift::scalarParam(defaults, QStringLiteral("period"), 0.0);
        windowS = period > 0.0 ? period : 2.0;
        t0 = 0.0;
        t1 = windowS;
    } else if (slot == QLatin1String("out")) {
        windowS = reveal + 0.6;
        t0 = 0.0;
        t1 = windowS;
    } else {
        windowS = reveal + 0.6;
        t0 = 0.0;
        t1 = windowS;
    }

    drift::Clip clip = tileClip(QString(), 700, false, drift::secondsToUs(windowS));
    drift::TextAnimationSlot animation;
    animation.presetId = presetId;
    if (slot == QLatin1String("out"))
        clip.textStyle.animation.out = animation;
    else if (slot == QLatin1String("loop"))
        clip.textStyle.animation.loop = animation;
    else
        clip.textStyle.animation.in = animation;

    QImage sheet(frameW * columns, frameH * rows, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(Qt::transparent);
    QPainter p(&sheet);
    const double scale = frameH / kTileReferenceHeight;
    for (int i = 0; i < frames; ++i) {
        const double t = frames > 1 ? t0 + (t1 - t0) * i / (frames - 1) : t0;
        const QImage frame = renderTextCard(clip, sample, QSize(frameW, frameH), scale, drift::secondsToUs(t));
        p.drawImage((i % columns) * frameW, (i / columns) * frameH, frame);
    }
    p.end();
    storeCard(key, sheet);
    if (size)
        *size = sheet.size();
    return sheet;
}

// ---------------------------------------------------------------------------------------------

TextLookPreviewImageProvider::TextLookPreviewImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage TextLookPreviewImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QUrl url(QStringLiteral("image://textlook/") + id);
    const QUrlQuery query(url);
    const QString lookId = url.path().split(QLatin1Char('/'), Qt::SkipEmptyParts).value(0);
    if (!drift::textLookForId(lookId)) {
        if (size)
            *size = QSize();
        return {};
    }
    const int width = requestedSize.width() > 0 ? requestedSize.width() : 144;
    const int height = requestedSize.height() > 0 ? requestedSize.height() : 80;
    const QString key = QStringLiteral("look|%1|%2x%3").arg(id).arg(width).arg(height);
    if (const QImage hit = cachedCard(key); !hit.isNull()) {
        if (size)
            *size = hit.size();
        return hit;
    }

    QString text = query.queryItemValue(QStringLiteral("text"), QUrl::FullyDecoded).trimmed();
    if (text.isEmpty())
        text = QStringLiteral("Aa");
    drift::Clip clip = tileClip(query.queryItemValue(QStringLiteral("font"), QUrl::FullyDecoded),
                                query.queryItemValue(QStringLiteral("weight")).toInt(),
                                query.queryItemValue(QStringLiteral("italic")).toInt() != 0, drift::secondsToUs(1.0));
    clip.textStyle.wordWrap = false;
    drift::applyTextLook(clip.textStyle, lookId, {});
    const QImage card = renderTextCard(clip, text, QSize(width, height), height / kTileReferenceHeight);
    storeCard(key, card);
    if (size)
        *size = card.size();
    return card;
}
