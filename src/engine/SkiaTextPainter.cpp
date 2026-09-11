#include "SkiaTextPainter.h"

#include "SkiaPath.h"
#include "SkiaVectorResources.h"
#include "TextLayout.h"

#include <QHash>
#include <QPainter>
#include <QtMath>

#include <cmath>

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/effects/SkImageFilters.h"

namespace drift::skia {

namespace {

SkRect toSkRect(const QRectF &r)
{
    return SkRect::MakeXYWH(float(r.x()), float(r.y()), float(r.width()), float(r.height()));
}

// One laid-out piece with its geometry already in Skia form, so paint() only issues draws.
struct Piece
{
    SkPath glyphs;
    SkPath shape;        // glyphs grown by the outline; == glyphs when there is no outline
    bool outlined = false;
    QRectF cellRect;
    double baselineY = 0.0;
    int line = 0;
    bool accent = false;
    sk_sp<SkImage> emoji; // bitmap-face cluster, pre-drawn by Qt
    QPointF emojiOrigin;  // where the emoji image's top-left lands
};

// Colour emoji have no outlines: the CBDT face is drawn by Qt into a small premultiplied image
// here on the scene thread, and blitted 1:1 at paint time. The cell is padded because a bitmap
// glyph can overhang its advance.
sk_sp<SkImage> renderEmoji(const text::StyledWord &word, QPointF *origin)
{
    const double pad = std::ceil(word.cellRect.height() * 0.25) + 1.0;
    const int w = qMax(1, qCeil(word.cellRect.width() + pad * 2.0));
    const int h = qMax(1, qCeil(word.cellRect.height() + pad * 2.0));
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(word.emojiFont);
    p.setPen(Qt::white);
    p.drawText(QPointF(pad, pad + (word.baselineY - word.cellRect.top())), word.emojiText);
    p.end();
    *origin = QPointF(word.cellRect.left() - pad, word.cellRect.top() - pad);
    return imageFromQImage(image);
}

QList<Piece> piecesFor(const QList<text::StyledWord> &words, const TextStyle &style, double renderScale)
{
    QList<Piece> pieces;
    pieces.reserve(words.size());
    for (const text::StyledWord &word : words) {
        Piece piece;
        piece.cellRect = word.cellRect;
        piece.baselineY = word.baselineY;
        piece.line = word.line;
        piece.accent = word.accent;
        if (!word.emojiText.isEmpty()) {
            piece.emoji = renderEmoji(word, &piece.emojiOrigin);
        } else {
            piece.glyphs = toSkPath(word.path);
            const double outline = text::outlineWidthFor(style, word.accent);
            piece.outlined = outline > 0.0;
            piece.shape = piece.outlined ? toSkPath(text::outlineShape(word.path, outline, renderScale))
                                         : piece.glyphs;
        }
        pieces.append(piece);
    }
    return pieces;
}

class TextBlockPainter final : public VectorPainter
{
public:
    TextBlockPainter(QSize size, quint64 key, const TextStyle &style, double renderScale,
                     QList<Piece> pieces, const QRectF &box)
        : m_size(size), m_key(key | 1), m_style(style), m_scale(renderScale),
          m_pieces(std::move(pieces)), m_box(box)
    {
    }

    QSize size() const override { return m_size; }
    quint64 cacheKey() const override { return m_key; }

    void paint(SkCanvas &canvas) const override
    {
        SkPaint fill;
        fill.setAntiAlias(true);

        if (m_style.boxEnabled && !m_box.isEmpty()) {
            fill.setColor(toSkColor(m_style.boxColor));
            const float r = float(m_style.boxRadius * m_scale);
            canvas.drawRoundRect(toSkRect(m_box), r, r, fill);
        }

        for (const Piece &piece : m_pieces) {
            const TextHighlight *highlight = text::highlightFor(m_style, piece.accent);
            if (!highlight)
                continue;
            const double pad = highlight->padding * m_scale;
            const float r = float(highlight->radius * m_scale);
            fill.setColor(toSkColor(highlight->color));
            canvas.drawRoundRect(toSkRect(piece.cellRect.adjusted(-pad, -pad, pad, pad)), r, r, fill);
        }

        if (m_style.shadowEnabled && m_style.shadowOpacity > 0.0) {
            blurredShapes(canvas, m_style.shadowColor, m_style.shadowBlur, m_style.shadowOpacity,
                          QPointF(m_style.shadowOffsetX * m_scale, m_style.shadowOffsetY * m_scale));
        }
        if (m_style.glowEnabled && m_style.glowOpacity > 0.0)
            blurredShapes(canvas, m_style.glowColor, m_style.glowRadius, m_style.glowOpacity, QPointF());

        for (const Piece &piece : m_pieces) {
            if (piece.emoji)
                continue;
            if (piece.outlined) { // behind the glyphs, never eating into them
                fill.setColor(toSkColor(text::outlineColorFor(m_style, piece.accent)));
                canvas.drawPath(piece.shape, fill);
            }
            fill.setColor(toSkColor(text::fillColorFor(m_style, piece.accent)));
            canvas.drawPath(piece.glyphs, fill);
        }

        for (const Piece &piece : m_pieces) {
            if (!piece.emoji)
                continue;
            canvas.drawImage(piece.emoji.get(), float(piece.emojiOrigin.x()), float(piece.emojiOrigin.y()),
                             SkSamplingOptions(SkFilterMode::kLinear));
        }

        if (m_style.underlineEnabled && m_style.underlineWidth > 0.0) {
            QHash<int, QRectF> perLine;
            for (const Piece &piece : m_pieces) {
                const QRectF rule(piece.cellRect.left(), piece.baselineY + m_style.underlineOffset * m_scale,
                                  piece.cellRect.width(), m_style.underlineWidth * m_scale);
                const auto it = perLine.find(piece.line);
                if (it == perLine.end())
                    perLine.insert(piece.line, rule);
                else
                    *it = it->united(rule);
            }
            fill.setColor(toSkColor(m_style.underlineColor));
            const float r = float(m_style.underlineWidth * m_scale * 0.5);
            for (const QRectF &rule : std::as_const(perLine))
                canvas.drawRoundRect(toSkRect(rule), r, r, fill);
        }
    }

private:
    // Every piece's outline shape, filled in one colour and blurred as a layer. The QPainter
    // raster runs a three-pass box blur of radius round(blur·scale); three boxes of radius r
    // have the variance of a gaussian with sigma = r + 0.5.
    void blurredShapes(SkCanvas &canvas, const QColor &color, double blurPx, double opacity,
                       const QPointF &offset) const
    {
        const int radius = qRound(blurPx * m_scale);
        SkPaint layer;
        layer.setAlphaf(float(qBound(0.0, opacity, 1.0)));
        if (radius >= 1) {
            const float sigma = float(radius) + 0.5f;
            layer.setImageFilter(SkImageFilters::Blur(sigma, sigma, nullptr));
        }
        canvas.saveLayer(nullptr, &layer);
        canvas.translate(float(offset.x()), float(offset.y()));
        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(toSkColor(color));
        for (const Piece &piece : m_pieces) {
            if (!piece.emoji)
                canvas.drawPath(piece.shape, fill);
        }
        canvas.restore();
    }

    QSize m_size;
    quint64 m_key;
    TextStyle m_style;
    double m_scale;
    QList<Piece> m_pieces;
    QRectF m_box;
};

} // namespace

TextPainterResult makeTextPainter(const Clip &clip, const QString &textContent, const QRectF &layoutRect,
                                  double renderScale, int activeWordIndex)
{
    if (textContent.isEmpty() || layoutRect.width() < 1.0 || layoutRect.height() < 1.0)
        return {};
    const TextStyle &style = clip.textStyle;

    // Same bleed and image size as the raster, so the two backends place the block identically.
    const double bleed = std::ceil(text::bleedFor(style) * renderScale) + 2.0;
    const int imageW = qMax(1, qRound(layoutRect.width() + bleed * 2.0));
    const int imageH = qMax(1, qRound(layoutRect.height() + bleed * 2.0));

    TextPainterResult result;
    result.rect = QRectF(layoutRect.x() - bleed, layoutRect.y() - bleed, imageW, imageH);

    const text::StyleFonts fonts = text::fontsForStyle(style, renderScale);
    const QList<text::StyledWord> words = text::translatedWords(
        text::layoutStyledText(textContent, style, fonts.base, fonts.accent, layoutRect.width(),
                               layoutRect.height(), activeWordIndex, text::WordSplit::Whole),
        bleed, bleed);
    if (words.isEmpty())
        return {};

    QRectF box;
    if (style.boxEnabled) {
        const double padding = style.boxPadding * renderScale;
        box = text::paintedBounds(words, style, renderScale).adjusted(-padding, -padding, padding, padding);
    }
    result.painter = std::make_shared<TextBlockPainter>(
        QSize(imageW, imageH), text::rasterKey(textContent, style, imageW, imageH, renderScale, activeWordIndex),
        style, renderScale, piecesFor(words, style, renderScale), box);
    return result;
}

QList<TextSpanPainter> makeTextSpanPainters(const Clip &clip, const QString &textContent,
                                            const QRectF &layoutRect, double renderScale,
                                            TextAnimUnit unit, int activeWordIndex)
{
    if (textContent.isEmpty() || layoutRect.width() < 1.0 || layoutRect.height() < 1.0)
        return {};
    if (unit == TextAnimUnit::Block)
        return {};
    const TextStyle &style = clip.textStyle;
    const double bleed = std::ceil(text::bleedFor(style) * renderScale) + 2.0;
    const quint64 key = text::spanRasterKey(textContent, style, layoutRect, renderScale, unit, activeWordIndex);

    const text::StyleFonts fonts = text::fontsForStyle(style, renderScale);
    const text::WordSplit split =
        unit == TextAnimUnit::Character ? text::WordSplit::Characters : text::WordSplit::Whole;
    const QList<text::StyledWord> words =
        text::layoutStyledText(textContent, style, fonts.base, fonts.accent, layoutRect.width(),
                               layoutRect.height(), activeWordIndex, split);
    if (words.isEmpty())
        return {};
    const QList<QList<text::StyledWord>> groups = text::groupSpans(words, unit);
    const QPointF origin = layoutRect.topLeft();

    QList<TextSpanPainter> out;
    out.reserve(groups.size() + 1);

    // A single static box behind every span, so the background never staggers with the glyphs.
    if (style.boxEnabled) {
        const QRectF blockInk = text::paintedBounds(words, style, renderScale);
        if (!blockInk.isEmpty()) {
            const double padding = style.boxPadding * renderScale;
            const QRectF boxLocal = blockInk.adjusted(-padding, -padding, padding, padding);
            const int bw = qMax(1, qCeil(boxLocal.width()));
            const int bh = qMax(1, qCeil(boxLocal.height()));
            TextStyle boxOnly = style;
            TextSpanPainter box;
            box.painter = std::make_shared<TextBlockPainter>(QSize(bw, bh), qHashMulti(key, -1), boxOnly,
                                                             renderScale, QList<Piece>(), QRectF(0, 0, bw, bh));
            box.rect = QRectF(boxLocal.x(), boxLocal.y(), bw, bh).translated(origin);
            box.index = -1;
            box.count = groups.size();
            out.append(box);
        }
    }

    // Spans carry no box of their own: the block-level one above is the only background.
    TextStyle spanStyle = style;
    spanStyle.boxEnabled = false;
    for (int i = 0; i < groups.size(); ++i) {
        const QRectF ink = text::paintedBounds(groups.at(i), style, renderScale);
        if (ink.isEmpty())
            continue;
        const int iw = qMax(1, qCeil(ink.width() + bleed * 2.0));
        const int ih = qMax(1, qCeil(ink.height() + bleed * 2.0));
        const QList<text::StyledWord> local = text::translatedWords(groups.at(i), bleed - ink.x(), bleed - ink.y());
        TextSpanPainter span;
        span.painter = std::make_shared<TextBlockPainter>(QSize(iw, ih), qHashMulti(key, i), spanStyle, renderScale,
                                                          piecesFor(local, style, renderScale), QRectF());
        span.rect = QRectF(ink.x() - bleed, ink.y() - bleed, iw, ih).translated(origin);
        span.index = i;
        span.count = groups.size();
        out.append(span);
    }
    return out;
}

} // namespace drift::skia
