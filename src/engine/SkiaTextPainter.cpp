#include "SkiaTextPainter.h"

#include "SkiaPath.h"
#include "SkiaVectorResources.h"
#include "TextLayout.h"

#include <QBrush>
#include <QHash>
#include <QPainter>
#include <QtMath>

#include <cmath>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImage.h"
#include "include/core/SkMatrix.h"
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

QRectF fromSkRect(const SkRect &r)
{
    return QRectF(r.left(), r.top(), r.width(), r.height());
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
    SkMatrix emojiMatrix = SkMatrix::I(); // the bend, for the image draw
    double outlinePx = 0.0;
};

// Colour emoji have no outlines: the CBDT face is drawn by Qt into a small premultiplied image
// here on the scene thread, and blitted 1:1 at paint time. The cell is padded because a bitmap
// glyph can overhang its advance and because the outline dilation grows it.
sk_sp<SkImage> renderEmoji(const text::StyledWord &word, double extra, QPointF *origin)
{
    const double pad = std::ceil(word.cellRect.height() * 0.25 + extra) + 1.0;
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

// The arc a bent single line sits on: chord = the line's extent, rise = textBendRise. Glyphs keep
// their advances along the arc and the slack the longer arc leaves is split between the ends.
struct Bend
{
    bool active = false;
    double left = 0, width = 0, baseline = 0;
    double radius = 0, sweep = 0, arcLen = 0;
    bool up = true;
    QPointF centre;

    SkMatrix matrixFor(double x0, double baselineY) const
    {
        const double d = (x0 - left) + (arcLen - width) / 2.0;
        const double phi = -sweep / 2.0 + d / radius;
        QPointF pos;
        double rot;
        if (up) {
            pos = centre + QPointF(radius * std::sin(phi), -radius * std::cos(phi));
            rot = phi;
        } else {
            pos = centre + QPointF(radius * std::sin(phi), radius * std::cos(phi));
            rot = -phi;
        }
        // The piece's own baseline offset from the line baseline (accent words sit on the same
        // baseline, so this is normally zero).
        SkMatrix m = SkMatrix::Translate(float(pos.x()), float(pos.y()));
        m.preConcat(SkMatrix::RotateRad(float(rot)));
        m.preConcat(SkMatrix::Translate(float(-x0), float(-baselineY)));
        return m;
    }
};

Bend bendFor(const QList<text::StyledWord> &words, const TextStyle &style, double renderScale)
{
    Bend bend;
    const double rise = text::textBendRise(style) * renderScale;
    if (rise < 0.5 || words.isEmpty())
        return bend;
    for (const text::StyledWord &word : words) {
        if (word.line != words.first().line)
            return bend; // multi-line blocks stay straight
    }
    double left = words.first().cellRect.left();
    double right = words.first().cellRect.right();
    for (const text::StyledWord &word : words) {
        left = qMin(left, word.cellRect.left());
        right = qMax(right, word.cellRect.right());
    }
    const double width = right - left;
    if (width < 1.0)
        return bend;
    bend.active = true;
    bend.up = style.pathBend > 0.0;
    bend.left = left;
    bend.width = width;
    bend.baseline = words.first().baselineY;
    bend.radius = (width * width / 4.0 + rise * rise) / (2.0 * rise);
    bend.sweep = 2.0 * std::asin(qMin(1.0, width / (2.0 * bend.radius)));
    bend.arcLen = bend.radius * bend.sweep;
    const double mid = left + width / 2.0;
    bend.centre = bend.up ? QPointF(mid, bend.baseline + (bend.radius - rise))
                          : QPointF(mid, bend.baseline - (bend.radius - rise));
    return bend;
}

QList<Piece> piecesFor(const QList<text::StyledWord> &words, const TextStyle &style, double renderScale,
                       const Bend &bend)
{
    QList<Piece> pieces;
    pieces.reserve(words.size());
    for (const text::StyledWord &word : words) {
        Piece piece;
        piece.cellRect = word.cellRect;
        piece.baselineY = word.baselineY;
        piece.line = word.line;
        piece.accent = word.accent;
        piece.outlinePx = text::outlineWidthFor(style, word.accent) * renderScale;
        const SkMatrix m = bend.active ? bend.matrixFor(word.cellRect.left(), word.baselineY) : SkMatrix::I();
        if (!word.emojiText.isEmpty()) {
            piece.emoji = renderEmoji(word, piece.outlinePx, &piece.emojiOrigin);
            piece.emojiMatrix = m;
        } else {
            piece.glyphs = toSkPath(word.path);
            const double outline = text::outlineWidthFor(style, word.accent);
            piece.outlined = outline > 0.0;
            piece.shape = piece.outlined ? toSkPath(text::outlineShape(word.path, outline, renderScale))
                                         : piece.glyphs;
            if (bend.active) {
                piece.glyphs = piece.glyphs.makeTransform(m);
                piece.shape = piece.outlined ? piece.shape.makeTransform(m) : piece.glyphs;
            }
        }
        pieces.append(piece);
    }
    return pieces;
}

// The brush a gradient fill sweeps across the block's ink, built with the same angle maths as a
// shape's so the two read alike.
QBrush fillBrush(const TextStyle &style, const QRectF &bounds)
{
    switch (style.fillKind) {
    case TextFillKind::Solid:
        return style.color;
    case TextFillKind::LinearGradient: {
        const double radians = qDegreesToRadians(style.gradientAngle);
        const QPointF centre = bounds.center();
        const QPointF half(qCos(radians) * bounds.width() / 2.0, qSin(radians) * bounds.height() / 2.0);
        QLinearGradient gradient(centre - half, centre + half);
        gradient.setColorAt(0.0, style.color);
        gradient.setColorAt(1.0, style.colorSecondary);
        return gradient;
    }
    case TextFillKind::RadialGradient: {
        QRadialGradient gradient(bounds.center(), qMax(bounds.width(), bounds.height()) / 2.0);
        gradient.setColorAt(0.0, style.color);
        gradient.setColorAt(1.0, style.colorSecondary);
        return gradient;
    }
    }
    return style.color;
}

class TextBlockPainter final : public VectorPainter
{
public:
    TextBlockPainter(QSize size, quint64 key, const TextStyle &style, double renderScale,
                     QList<Piece> pieces, const QRectF &box, const QRectF &ink, bool bent)
        : m_size(size), m_key(key == 0 ? 0 : (key | 1)), m_style(style), m_scale(renderScale),
          m_pieces(std::move(pieces)), m_box(box), m_fill(fillBrush(style, ink)), m_bent(bent)
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

        // Pills and rules follow the straight cell geometry, which a bent line no longer has.
        if (!m_bent) {
            for (const Piece &piece : m_pieces) {
                const TextHighlight *highlight = text::highlightFor(m_style, piece.accent);
                if (!highlight)
                    continue;
                const double pad = highlight->padding * m_scale;
                const float r = float(highlight->radius * m_scale);
                fill.setColor(toSkColor(highlight->color));
                canvas.drawRoundRect(toSkRect(piece.cellRect.adjusted(-pad, -pad, pad, pad)), r, r, fill);
            }
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
                fill.setShader(nullptr);
                canvas.drawPath(piece.shape, fill);
            }
            glyphPaint(fill, piece.accent);
            canvas.drawPath(piece.glyphs, fill);
        }

        for (const Piece &piece : m_pieces) {
            if (!piece.emoji)
                continue;
            canvas.save();
            canvas.concat(piece.emojiMatrix);
            if (piece.outlinePx > 0.0) {
                // The bitmap's alpha, dilated by the outline width and tinted: the same ring an
                // outlined glyph gets, drawn underneath the emoji itself.
                SkPaint ring;
                ring.setImageFilter(SkImageFilters::ColorFilter(
                    SkColorFilters::Blend(toSkColor(text::outlineColorFor(m_style, piece.accent)), SkBlendMode::kSrcIn),
                    SkImageFilters::Dilate(float(piece.outlinePx), float(piece.outlinePx), nullptr)));
                canvas.drawImage(piece.emoji.get(), float(piece.emojiOrigin.x()), float(piece.emojiOrigin.y()),
                                 SkSamplingOptions(SkFilterMode::kLinear), &ring);
            }
            canvas.drawImage(piece.emoji.get(), float(piece.emojiOrigin.x()), float(piece.emojiOrigin.y()),
                             SkSamplingOptions(SkFilterMode::kLinear));
            canvas.restore();
        }

        if (!m_bent && m_style.underlineEnabled && m_style.underlineWidth > 0.0) {
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
            fill.setShader(nullptr);
            const float r = float(m_style.underlineWidth * m_scale * 0.5);
            for (const QRectF &rule : std::as_const(perLine))
                canvas.drawRoundRect(toSkRect(rule), r, r, fill);
        }
    }

private:
    // Solid accent colour when the pack overrides it, else the block's fill (solid or gradient).
    void glyphPaint(SkPaint &paint, bool accent) const
    {
        if (accent && m_style.accent.colorEnabled) {
            paint.setShader(nullptr);
            paint.setColor(toSkColor(m_style.accent.color));
            return;
        }
        applyBrush(paint, m_fill);
    }

    // Every piece's outline shape (and every emoji's silhouette), filled in one colour and blurred
    // as a layer. The QPainter raster runs a three-pass box blur of radius round(blur·scale);
    // three boxes of radius r have the variance of a gaussian with sigma = r + 0.5.
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
            if (!piece.emoji) {
                canvas.drawPath(piece.shape, fill);
                continue;
            }
            SkPaint silhouette;
            sk_sp<SkImageFilter> dilate =
                piece.outlinePx > 0.0 ? SkImageFilters::Dilate(float(piece.outlinePx), float(piece.outlinePx), nullptr)
                                      : nullptr;
            silhouette.setImageFilter(SkImageFilters::ColorFilter(
                SkColorFilters::Blend(toSkColor(color), SkBlendMode::kSrcIn), std::move(dilate)));
            canvas.save();
            canvas.concat(piece.emojiMatrix);
            canvas.drawImage(piece.emoji.get(), float(piece.emojiOrigin.x()), float(piece.emojiOrigin.y()),
                             SkSamplingOptions(SkFilterMode::kLinear), &silhouette);
            canvas.restore();
        }
        canvas.restore();
    }

    QSize m_size;
    quint64 m_key;
    TextStyle m_style;
    double m_scale;
    QList<Piece> m_pieces;
    QRectF m_box;
    QBrush m_fill;
    bool m_bent;
};

// What the pieces paint over. Straight blocks use the layout's own answer so the box matches the
// QPainter raster; bent ones take the transformed shapes.
QRectF inkBounds(const QList<Piece> &pieces, const QList<text::StyledWord> &words, const TextStyle &style,
                 double renderScale, bool bent)
{
    if (!bent)
        return text::paintedBounds(words, style, renderScale);
    QRectF bounds;
    for (const Piece &piece : pieces) {
        QRectF r;
        if (piece.emoji) {
            const SkRect cell = piece.emojiMatrix.mapRect(toSkRect(piece.cellRect));
            r = fromSkRect(cell);
        } else {
            r = fromSkRect(piece.shape.computeTightBounds());
        }
        bounds = bounds.isNull() ? r : bounds.united(r);
    }
    return bounds;
}

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
    // A bend places glyphs one at a time, so the layout has to hand them over one at a time.
    const bool wantBend = text::textBendRise(style) * renderScale >= 0.5;
    const QList<text::StyledWord> words = text::translatedWords(
        text::layoutStyledText(textContent, style, fonts.base, fonts.accent, layoutRect.width(),
                               layoutRect.height(), activeWordIndex,
                               wantBend ? text::WordSplit::Characters : text::WordSplit::Whole),
        bleed, bleed);
    if (words.isEmpty())
        return {};

    const Bend bend = bendFor(words, style, renderScale);
    QList<Piece> pieces = piecesFor(words, style, renderScale, bend);
    const QRectF ink = inkBounds(pieces, words, style, renderScale, bend.active);
    QRectF box;
    if (style.boxEnabled) {
        const double padding = style.boxPadding * renderScale;
        box = ink.adjusted(-padding, -padding, padding, padding);
    }
    // An animated style changes every frame; caching those would only churn the GPU LRU.
    const quint64 key = style.isAnimated()
                            ? 0
                            : text::rasterKey(textContent, style, imageW, imageH, renderScale, activeWordIndex);
    result.painter = std::make_shared<TextBlockPainter>(QSize(imageW, imageH), key, style, renderScale,
                                                        std::move(pieces), box, ink, bend.active);
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
    const quint64 key = style.isAnimated()
                            ? 0
                            : text::spanRasterKey(textContent, style, layoutRect, renderScale, unit, activeWordIndex);

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
    // Spans are placed one by one, each in its own layer, so the gradient sweeps the whole
    // block's ink and the bend is left to the whole-block path.
    const QRectF blockInk = text::paintedBounds(words, style, renderScale);
    const Bend straight;

    QList<TextSpanPainter> out;
    out.reserve(groups.size() + 1);

    // A single static box behind every span, so the background never staggers with the glyphs.
    if (style.boxEnabled) {
        if (!blockInk.isEmpty()) {
            const double padding = style.boxPadding * renderScale;
            const QRectF boxLocal = blockInk.adjusted(-padding, -padding, padding, padding);
            const int bw = qMax(1, qCeil(boxLocal.width()));
            const int bh = qMax(1, qCeil(boxLocal.height()));
            TextSpanPainter box;
            box.painter = std::make_shared<TextBlockPainter>(QSize(bw, bh), key ? qHashMulti(key, -1) : 0, style,
                                                             renderScale, QList<Piece>(), QRectF(0, 0, bw, bh),
                                                             QRectF(0, 0, bw, bh), false);
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
        const QPointF shift(bleed - ink.x(), bleed - ink.y());
        const QList<text::StyledWord> local = text::translatedWords(groups.at(i), shift.x(), shift.y());
        TextSpanPainter span;
        span.painter = std::make_shared<TextBlockPainter>(QSize(iw, ih), key ? qHashMulti(key, i) : 0, spanStyle,
                                                          renderScale, piecesFor(local, style, renderScale, straight),
                                                          QRectF(), blockInk.translated(shift), false);
        span.rect = QRectF(ink.x() - bleed, ink.y() - bleed, iw, ih).translated(origin);
        span.index = i;
        span.count = groups.size();
        out.append(span);
    }
    return out;
}

} // namespace drift::skia
