#include "TextRaster.h"

#include "TextLayout.h"

#include <QEasingCurve>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <cmath>

using namespace drift::text;

namespace {

QMutex g_cacheMutex;
QHash<quint64, QImage> g_cache;
// Least-recently-used first. Entries are caption-box-sized ARGB32 at the render canvas, and both
// the preview-scale and the export-scale copy of a cue live here at once, so a count cap alone let
// the cache reach hundreds of MB on a phone. Budget the bytes and evict one entry at a time —
// the old cap cleared the whole cache when it tripped, re-rastering every visible caption.
QList<quint64> g_cacheLru;
qint64 g_cacheBytes = 0;
// Karaoke re-rasterizes as the spoken word advances, so a single cue can occupy one entry per
// word — the cache has to be roomy enough that scrubbing a caption track does not thrash it.
constexpr int kMaxCacheEntries = 256;
#ifdef Q_OS_ANDROID
constexpr qint64 kMaxCacheBytes = 32LL * 1024 * 1024;
#else
constexpr qint64 kMaxCacheBytes = 256LL * 1024 * 1024;
#endif


// Separable box blur over a premultiplied image. Three passes approximate a gaussian well enough
// for a drop shadow, and premultiplied is what keeps transparent pixels from dragging the glyph
// edges toward black.
void blurRows(const QImage &src, QImage &dst, int radius)
{
    const int w = src.width();
    const int h = src.height();
    const int span = radius * 2 + 1;

    for (int y = 0; y < h; ++y) {
        const QRgb *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        QRgb *d = reinterpret_cast<QRgb *>(dst.scanLine(y));

        int a = 0, r = 0, g = 0, b = 0;
        for (int i = -radius; i <= radius; ++i) {
            const QRgb px = s[qBound(0, i, w - 1)];
            a += qAlpha(px); r += qRed(px); g += qGreen(px); b += qBlue(px);
        }
        for (int x = 0; x < w; ++x) {
            d[x] = qRgba(r / span, g / span, b / span, a / span);
            const QRgb out = s[qBound(0, x - radius, w - 1)];
            const QRgb in = s[qBound(0, x + radius + 1, w - 1)];
            a += qAlpha(in) - qAlpha(out); r += qRed(in) - qRed(out);
            g += qGreen(in) - qGreen(out); b += qBlue(in) - qBlue(out);
        }
    }
}

void blurColumns(const QImage &src, QImage &dst, int radius)
{
    const int w = src.width();
    const int h = src.height();
    const int span = radius * 2 + 1;

    const int stride = src.bytesPerLine() / 4;
    const QRgb *s = reinterpret_cast<const QRgb *>(src.constBits());
    const int dstStride = dst.bytesPerLine() / 4;
    QRgb *d = reinterpret_cast<QRgb *>(dst.bits());

    for (int x = 0; x < w; ++x) {
        int a = 0, r = 0, g = 0, b = 0;
        for (int i = -radius; i <= radius; ++i) {
            const QRgb px = s[qBound(0, i, h - 1) * stride + x];
            a += qAlpha(px); r += qRed(px); g += qGreen(px); b += qBlue(px);
        }
        for (int y = 0; y < h; ++y) {
            d[y * dstStride + x] = qRgba(r / span, g / span, b / span, a / span);
            const QRgb out = s[qBound(0, y - radius, h - 1) * stride + x];
            const QRgb in = s[qBound(0, y + radius + 1, h - 1) * stride + x];
            a += qAlpha(in) - qAlpha(out); r += qRed(in) - qRed(out);
            g += qGreen(in) - qGreen(out); b += qBlue(in) - qBlue(out);
        }
    }
}

void blurPremultiplied(QImage &image, int radius)
{
    if (radius < 1 || image.isNull())
        return;
    QImage scratch(image.size(), QImage::Format_ARGB32_Premultiplied);
    for (int pass = 0; pass < 3; ++pass) {
        blurRows(image, scratch, radius);
        blurColumns(scratch, image, radius);
    }
}

QEasingCurve::Type easingType(drift::TextEase ease)
{
    switch (ease) {
    case drift::TextEase::Linear:
        return QEasingCurve::Linear;
    case drift::TextEase::EaseInOut:
        return QEasingCurve::InOutQuad;
    case drift::TextEase::Back:
        return QEasingCurve::OutBack;
    case drift::TextEase::EaseOut:
        return QEasingCurve::OutCubic;
    }
    return QEasingCurve::OutCubic;
}

// `settled` runs 0 (fully out) to 1 (fully in place). `entering` flips the slide direction: an
// entrance arrives from the opposite side, an exit departs toward the named one.
void applyAnimation(const drift::TextAnimation &anim, double settled, bool entering,
                    const QRectF &layoutRect, double renderScale, TextAnimSample *out)
{
    if (anim.kind == drift::TextAnimKind::None)
        return;

    const double a = QEasingCurve(easingType(anim.ease)).valueForProgress(qBound(0.0, settled, 1.0));
    const double away = 1.0 - a; // how far from settled
    const double travelX = 0.35 * layoutRect.width();
    const double travelY = 0.35 * layoutRect.height();
    const double sign = entering ? 1.0 : -1.0;

    switch (anim.kind) {
    case drift::TextAnimKind::None:
        break;
    case drift::TextAnimKind::Fade:
        out->opacity *= a;
        break;
    case drift::TextAnimKind::SlideUp:
        out->dy += sign * away * travelY;
        out->opacity *= a;
        break;
    case drift::TextAnimKind::SlideDown:
        out->dy -= sign * away * travelY;
        out->opacity *= a;
        break;
    case drift::TextAnimKind::SlideLeft:
        out->dx += sign * away * travelX;
        out->opacity *= a;
        break;
    case drift::TextAnimKind::SlideRight:
        out->dx -= sign * away * travelX;
        out->opacity *= a;
        break;
    case drift::TextAnimKind::Pop:
        out->scale *= 0.6 + 0.4 * a; // Back easing overshoots past 1 here, which is the bounce
        out->opacity *= qBound(0.0, a, 1.0);
        break;
    case drift::TextAnimKind::Blur:
        out->blurPx = qMax(out->blurPx, away * kTextBlurMaxPx * renderScale);
        out->opacity *= qBound(0.0, a, 1.0);
        break;
    case drift::TextAnimKind::Typewriter:
        // Hard binary reveal: a span is off until the playhead reaches its staggered start
        // (settled >= 0), then fully on. Duration is irrelevant, which is what makes it snap.
        out->opacity *= (settled >= 0.0 ? 1.0 : 0.0);
        break;
    case drift::TextAnimKind::Rise:
        out->dy += sign * away * travelY;
        out->scale *= 0.9 + 0.1 * a;
        out->opacity *= a;
        break;
    case drift::TextAnimKind::Bounce: {
        const double b = QEasingCurve(QEasingCurve::OutBounce).valueForProgress(qBound(0.0, settled, 1.0));
        out->dy += sign * (1.0 - b) * travelY;
        out->opacity *= qBound(0.0, settled * 4.0, 1.0);
        break;
    }
    case drift::TextAnimKind::Wave:
        break; // continuous; applied by applyWave in the samplers, not from a settle progress
    }
}

// Continuous vertical oscillation. Unlike the entrance/exit kinds it never settles — it bobs for the
// whole clip, phase-shifted per span so a wave travels across the characters.
void applyWave(drift::TimeUs clipLocalUs, int spanIndex, const QRectF &layoutRect, double renderScale,
               TextAnimSample *out)
{
    const double t = static_cast<double>(clipLocalUs) / 1'000'000.0;
    const double amp = qMin(layoutRect.height() * 0.12, 36.0 * renderScale);
    const double freq = 2.0 * M_PI * 1.1; // ~1.1 Hz
    const double phase = 0.6 * spanIndex;
    out->dy += amp * std::sin(t * freq + phase);
}

// bounds) and the fill, so both agree on the shape's extent.
// block, so a shadow or glow costs the same whether the text is one word or twenty.
QImage blurredShapeLayer(const QList<QPainterPath> &shapes, const QSize &imageSize,
                         const QColor &color, double blurPx)
{
    QImage image(imageSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    for (const QPainterPath &shape : shapes)
        p.fillPath(shape, color);
    p.end();
    blurPremultiplied(image, qRound(blurPx));
    return image;
}

// Draw the highlight pills, shadow, glow, outline, glyph fill and underline for a laid-out block.
// The box background is drawn by the caller (it is per-block, not per-span), so this stays reusable
// for both the whole-layer raster and the per-span reveal rasters.
void paintStyledWords(QPainter &p, const QList<StyledWord> &words, const drift::TextStyle &style,
                      double renderScale, const QSize &imageSize)
{
    for (const StyledWord &word : words) {
        const drift::TextHighlight *highlight = highlightFor(style, word.accent);
        if (!highlight)
            continue;
        const double pad = highlight->padding * renderScale;
        const double radius = highlight->radius * renderScale;
        p.setPen(Qt::NoPen);
        p.setBrush(highlight->color);
        p.drawRoundedRect(word.cellRect.adjusted(-pad, -pad, pad, pad), radius, radius);
    }

    QList<QPainterPath> shapes;
    shapes.reserve(words.size());
    for (const StyledWord &word : words)
        shapes.append(outlineShape(word.path, outlineWidthFor(style, word.accent), renderScale));

    if (style.shadowEnabled && style.shadowOpacity > 0.0) {
        const QImage shadow = blurredShapeLayer(shapes, imageSize, style.shadowColor,
                                                style.shadowBlur * renderScale);
        p.setOpacity(qBound(0.0, style.shadowOpacity, 1.0));
        p.drawImage(QPointF(style.shadowOffsetX * renderScale, style.shadowOffsetY * renderScale), shadow);
        p.setOpacity(1.0);
    }

    if (style.glowEnabled && style.glowOpacity > 0.0) {
        const QImage glow = blurredShapeLayer(shapes, imageSize, style.glowColor,
                                              style.glowRadius * renderScale);
        p.setOpacity(qBound(0.0, style.glowOpacity, 1.0));
        p.drawImage(QPointF(0, 0), glow);
        p.setOpacity(1.0);
    }

    for (int i = 0; i < words.size(); ++i) {
        if (outlineWidthFor(style, words.at(i).accent) > 0.0) // behind the glyphs, never eating into them
            p.fillPath(shapes.at(i), outlineColorFor(style, words.at(i).accent));
        p.fillPath(words.at(i).path, fillColorFor(style, words.at(i).accent));
    }

    // Emoji come from the bitmap face, so they are drawn rather than filled — and they get no
    // outline, shadow or glow, since there is no shape to derive one from. The pen colour is
    // ignored by a colour face but has to be a real pen: the highlight pass above left NoPen.
    for (const StyledWord &word : words) {
        if (word.emojiText.isEmpty())
            continue;
        p.setFont(word.emojiFont);
        p.setPen(fillColorFor(style, word.accent));
        p.drawText(QPointF(word.cellRect.left(), word.baselineY), word.emojiText);
    }

    if (style.underlineEnabled && style.underlineWidth > 0.0) {
        // One rule per line, spanning that line's words.
        QHash<int, QRectF> perLine;
        for (const StyledWord &word : words) {
            const QRectF rule(word.cellRect.left(), word.baselineY + style.underlineOffset * renderScale,
                              word.cellRect.width(), style.underlineWidth * renderScale);
            const auto it = perLine.find(word.line);
            if (it == perLine.end())
                perLine.insert(word.line, rule);
            else
                *it = it->united(rule);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(style.underlineColor);
        const double radius = style.underlineWidth * renderScale * 0.5;
        for (const QRectF &rule : std::as_const(perLine))
            p.drawRoundedRect(rule, radius, radius);
    }
}
} // namespace

TextRasterResult rasterizeText(const drift::Clip &clip, const QString &text, const QRectF &layoutRect,
                               double renderScale, int activeWordIndex)
{
    if (text.isEmpty() || layoutRect.width() < 1.0 || layoutRect.height() < 1.0)
        return {};

    const drift::TextStyle &style = clip.textStyle;

    // The bleed is derived from style constants — never from the current animation — so the image
    // size holds still while an entrance plays and the cached raster stays usable.
    const double bleed = std::ceil(bleedFor(style) * renderScale) + 2.0;
    const int imageW = qMax(1, qRound(layoutRect.width() + bleed * 2.0));
    const int imageH = qMax(1, qRound(layoutRect.height() + bleed * 2.0));

    TextRasterResult result;
    result.rect = QRectF(layoutRect.x() - bleed, layoutRect.y() - bleed, imageW, imageH);

    const quint64 key = rasterKey(text, style, imageW, imageH, renderScale, activeWordIndex);
    {
        QMutexLocker lock(&g_cacheMutex);
        const auto it = g_cache.constFind(key);
        if (it != g_cache.constEnd()) {
            g_cacheLru.removeOne(key);
            g_cacheLru.append(key);
            result.image = it.value();
            return result;
        }
    }

    const StyleFonts fonts = fontsForStyle(style, renderScale);
    const QList<StyledWord> words = translatedWords(
        layoutStyledText(text, style, fonts.base, fonts.accent, layoutRect.width(),
                         layoutRect.height(), activeWordIndex, WordSplit::Whole),
        bleed, bleed);
    if (words.isEmpty())
        return {};

    QImage image(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (style.boxEnabled) {
        const double padding = style.boxPadding * renderScale;
        const QRectF box = paintedBounds(words, style, renderScale)
                               .adjusted(-padding, -padding, padding, padding);
        p.setPen(Qt::NoPen);
        p.setBrush(style.boxColor);
        p.drawRoundedRect(box, style.boxRadius * renderScale, style.boxRadius * renderScale);
    }

    paintStyledWords(p, words, style, renderScale, image.size());
    p.end();

    {
        const qint64 imageBytes = qint64(image.sizeInBytes());
        QMutexLocker lock(&g_cacheMutex);
        // Two threads (preview and export) can raster the same key at once. Drop the older copy
        // rather than letting its bytes stay on the total forever.
        if (g_cacheLru.removeOne(key))
            g_cacheBytes -= qint64(g_cache.take(key).sizeInBytes());
        while (!g_cacheLru.isEmpty()
               && (g_cache.size() >= kMaxCacheEntries || g_cacheBytes + imageBytes > kMaxCacheBytes))
            g_cacheBytes -= qint64(g_cache.take(g_cacheLru.takeFirst()).sizeInBytes());
        g_cache.insert(key, image);
        g_cacheLru.append(key);
        g_cacheBytes += imageBytes;
    }

    result.image = image;
    return result;
}

TextRasterResult rasterizeText(const drift::Clip &clip, const QRectF &layoutRect, double renderScale,
                               int activeWordIndex)
{
    const QString text = clip.textContent.isEmpty() ? clip.name : clip.textContent;
    return rasterizeText(clip, text, layoutRect, renderScale, activeWordIndex);
}

namespace {

// The whole span list for a clip depends only on text + style + layout size + scale + unit, and is
// time-independent (motion rides on the layer), so it is cached wholesale. Rects are stored relative
// to the layout rect's top-left and offset to absolute canvas coords on retrieval, so moving the
// clip does not invalidate the cache.
QMutex g_spanCacheMutex;
QHash<quint64, QList<TextSpanRaster>> g_spanCache;
constexpr int kMaxSpanCacheEntries = 16;


QList<TextSpanRaster> offsetSpans(const QList<TextSpanRaster> &local, const QPointF &origin)
{
    QList<TextSpanRaster> out = local;
    for (TextSpanRaster &s : out)
        s.rect.translate(origin);
    return out;
}

} // namespace

QList<TextSpanRaster> rasterizeTextSpans(const drift::Clip &clip, const QString &text,
                                         const QRectF &layoutRect, double renderScale,
                                         drift::TextAnimUnit unit, int activeWordIndex)
{
    if (text.isEmpty() || layoutRect.width() < 1.0 || layoutRect.height() < 1.0)
        return {};
    if (unit == drift::TextAnimUnit::Block)
        return {}; // whole-layer path — callers use rasterizeText instead

    const drift::TextStyle &style = clip.textStyle;

    const quint64 key = spanRasterKey(text, style, layoutRect, renderScale, unit, activeWordIndex);
    {
        QMutexLocker lock(&g_spanCacheMutex);
        const auto it = g_spanCache.constFind(key);
        if (it != g_spanCache.constEnd())
            return offsetSpans(it.value(), layoutRect.topLeft());
    }

    const double bleed = std::ceil(bleedFor(style) * renderScale) + 2.0;

    const StyleFonts fonts = fontsForStyle(style, renderScale);
    const WordSplit split =
        unit == drift::TextAnimUnit::Character ? WordSplit::Characters : WordSplit::Whole;
    const QList<StyledWord> words =
        layoutStyledText(text, style, fonts.base, fonts.accent, layoutRect.width(),
                         layoutRect.height(), activeWordIndex, split);
    if (words.isEmpty())
        return {};

    const QList<QList<StyledWord>> groups = groupSpans(words, unit);

    // Built with layout-local rects (relative to layoutRect.topLeft()); cached, then offset to canvas.
    QList<TextSpanRaster> local;
    local.reserve(groups.size() + 1);

    // A single static box behind every span, so the background never staggers with the glyphs.
    if (style.boxEnabled) {
        const QRectF blockInk = paintedBounds(words, style, renderScale);
        if (!blockInk.isEmpty()) {
            const double padding = style.boxPadding * renderScale;
            const QRectF boxLocal = blockInk.adjusted(-padding, -padding, padding, padding);
            const int bw = qMax(1, qCeil(boxLocal.width()));
            const int bh = qMax(1, qCeil(boxLocal.height()));
            QImage boxImg(bw, bh, QImage::Format_ARGB32_Premultiplied);
            boxImg.fill(Qt::transparent);
            QPainter bp(&boxImg);
            bp.setRenderHint(QPainter::Antialiasing);
            bp.setPen(Qt::NoPen);
            bp.setBrush(style.boxColor);
            bp.drawRoundedRect(QRectF(0, 0, bw, bh), style.boxRadius * renderScale,
                               style.boxRadius * renderScale);
            bp.end();

            TextSpanRaster box;
            box.image = boxImg;
            box.rect = QRectF(boxLocal.x(), boxLocal.y(), bw, bh);
            box.index = -1;
            box.count = groups.size();
            local.append(box);
        }
    }

    for (int i = 0; i < groups.size(); ++i) {
        const QRectF ink = paintedBounds(groups.at(i), style, renderScale);
        if (ink.isEmpty())
            continue;
        const int iw = qMax(1, qCeil(ink.width() + bleed * 2.0));
        const int ih = qMax(1, qCeil(ink.height() + bleed * 2.0));

        QImage image(iw, ih, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        paintStyledWords(p, translatedWords(groups.at(i), bleed - ink.x(), bleed - ink.y()), style,
                         renderScale, image.size());
        p.end();

        TextSpanRaster span;
        span.image = image;
        span.rect = QRectF(ink.x() - bleed, ink.y() - bleed, iw, ih);
        span.index = i;
        span.count = groups.size();
        local.append(span);
    }

    {
        QMutexLocker lock(&g_spanCacheMutex);
        if (g_spanCache.size() >= kMaxSpanCacheEntries)
            g_spanCache.clear();
        g_spanCache.insert(key, local);
    }

    return offsetSpans(local, layoutRect.topLeft());
}

// Sample entrance/exit motion for a text span occupying [windowStartUs, windowStartUs +
// windowDurationUs). Text clips pass the clip's span; subtitles pass the active cue's span
// so every cue animates in and out on its own.
static TextAnimSample sampleTextAnimationWindow(const drift::TextStyle &style, drift::TimeUs timelineUs,
                                                drift::TimeUs windowStartUs, drift::TimeUs windowDurationUs,
                                                const QRectF &layoutRect, double renderScale)
{
    TextAnimSample sample;

    const drift::TimeUs elapsed = timelineUs - windowStartUs;
    const drift::TimeUs remaining = windowDurationUs - elapsed;

    // Whole-layer (Block) wave: the entire text bobs together. Per-span waves are handled by
    // sampleTextSpanAnimation with a per-span phase.
    if (style.animIn.kind == drift::TextAnimKind::Wave) {
        applyWave(elapsed, 0, layoutRect, renderScale, &sample);
        return sample;
    }

    if (style.animIn.kind != drift::TextAnimKind::None && style.animIn.durationUs > 0) {
        const double settled = static_cast<double>(elapsed) / static_cast<double>(style.animIn.durationUs);
        applyAnimation(style.animIn, settled, true, layoutRect, renderScale, &sample);
    }
    if (style.animOut.kind != drift::TextAnimKind::None && style.animOut.durationUs > 0) {
        const double settled = static_cast<double>(remaining) / static_cast<double>(style.animOut.durationUs);
        applyAnimation(style.animOut, settled, false, layoutRect, renderScale, &sample);
    }

    sample.opacity = qBound(0.0, sample.opacity, 1.0);
    return sample;
}

TextAnimSample sampleTextAnimation(const drift::Clip &clip, drift::TimeUs timelineUs,
                                   const QRectF &layoutRect, double renderScale)
{
    return sampleTextAnimationWindow(clip.textStyle, timelineUs, clip.timelineStart,
                                     clip.timelineDuration, layoutRect, renderScale);
}

TextAnimSample sampleSubtitleCueAnimation(const drift::Clip &clip, const drift::SubtitleCue &cue,
                                          drift::TimeUs timelineUs, const QRectF &layoutRect,
                                          double renderScale)
{
    return sampleTextAnimationWindow(clip.textStyle, timelineUs, clip.timelineStart + cue.startUs,
                                     cue.endUs - cue.startUs, layoutRect, renderScale);
}

namespace {

// The stagger "slot" a span fires in, as a fractional index. Forward = reading order; the others
// remap it without changing the drawn order.
double reindexForOrder(int index, int count, drift::TextAnimOrder order)
{
    if (count <= 1)
        return 0.0;
    switch (order) {
    case drift::TextAnimOrder::Forward:
        return index;
    case drift::TextAnimOrder::Backward:
        return count - 1 - index;
    case drift::TextAnimOrder::CenterOut:
        return std::abs(index - (count - 1) / 2.0);
    case drift::TextAnimOrder::Random: {
        // Stable per-index pseudo-random slot so the shuffle holds still across frames.
        const quint32 h = qHash(static_cast<quint32>(index) * 2654435761u) ^ 0x9e3779b9u;
        return (h & 0xffffu) / 65535.0 * (count - 1);
    }
    }
    return index;
}

// Largest slot any span can occupy for a given order — used to anchor the staggered exit so the last
// span leaves exactly at the clip's end.
double maxReindex(int count, drift::TextAnimOrder order)
{
    if (count <= 1)
        return 0.0;
    if (order == drift::TextAnimOrder::CenterOut)
        return (count - 1) / 2.0;
    return count - 1;
}

} // namespace

TextAnimSample sampleTextSpanAnimation(const drift::Clip &clip, drift::TimeUs timelineUs, int spanIndex,
                                       int spanCount, const QRectF &layoutRect, double renderScale)
{
    TextAnimSample sample;
    const drift::TextStyle &style = clip.textStyle;
    const drift::TimeUs clipStart = clip.timelineStart;
    const drift::TimeUs clipEnd = clip.timelineStart + clip.timelineDuration;

    const drift::TextAnimation &in = style.animIn;
    if (in.kind == drift::TextAnimKind::Wave) {
        applyWave(timelineUs - clipStart, spanIndex, layoutRect, renderScale, &sample);
    } else if (in.kind != drift::TextAnimKind::None && in.durationUs > 0) {
        const double slot = reindexForOrder(spanIndex, spanCount, in.order);
        const drift::TimeUs spanStart = clipStart + static_cast<drift::TimeUs>(slot * in.staggerUs);
        const double settled =
            static_cast<double>(timelineUs - spanStart) / static_cast<double>(in.durationUs);
        applyAnimation(in, settled, true, layoutRect, renderScale, &sample);
    }

    const drift::TextAnimation &out = style.animOut;
    if (out.kind != drift::TextAnimKind::None && out.kind != drift::TextAnimKind::Wave
        && out.durationUs > 0) {
        const double slot = reindexForOrder(spanIndex, spanCount, out.order);
        const double maxSlot = maxReindex(spanCount, out.order);
        // Each span finishes exiting slot-staggered before the clip end; the last-out span lands on
        // clipEnd. settled runs 1 (in place) -> 0 (gone) as the finish time approaches.
        const drift::TimeUs finish = clipEnd - static_cast<drift::TimeUs>((maxSlot - slot) * out.staggerUs);
        const double settled = static_cast<double>(finish - timelineUs) / static_cast<double>(out.durationUs);
        applyAnimation(out, settled, false, layoutRect, renderScale, &sample);
    }

    sample.opacity = qBound(0.0, sample.opacity, 1.0);
    return sample;
}

void clearTextRasterCaches()
{
    {
        QMutexLocker lock(&g_cacheMutex);
        g_cache.clear();
        g_cacheLru.clear();
        g_cacheBytes = 0;
    }
    QMutexLocker lock(&g_spanCacheMutex);
    g_spanCache.clear();
}
