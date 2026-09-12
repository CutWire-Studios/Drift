#include "SkiaTextPainter.h"

#include "SkiaPath.h"
#include "SkiaTextEffects.h"
#include "SkiaVectorResources.h"
#include "core/TextAnimationPreset.h"

#include <QCache>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "include/core/SkBlendMode.h"
#include "include/core/SkBlurTypes.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImage.h"
#include "include/core/SkMaskFilter.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathEffect.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkShaderMaskFilter.h"
#include "include/effects/SkTrimPathEffect.h"

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

SkBlendMode toSkBlendMode(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal:
        return SkBlendMode::kSrcOver;
    case BlendMode::Multiply:
        return SkBlendMode::kMultiply;
    case BlendMode::Screen:
        return SkBlendMode::kScreen;
    case BlendMode::Overlay:
        return SkBlendMode::kOverlay;
    case BlendMode::Add:
        return SkBlendMode::kPlus;
    case BlendMode::Darken:
        return SkBlendMode::kDarken;
    case BlendMode::Lighten:
        return SkBlendMode::kLighten;
    }
    return SkBlendMode::kSrcOver;
}

// One laid-out piece with its geometry already in Skia form, so paint() only issues draws.
struct Piece
{
    SkPath glyphs;
    QRectF cellRect;
    QRectF inkRect;
    double baselineY = 0.0;
    int line = 0;
    int wordIndex = 0;
    bool accent = false;
    sk_sp<SkImage> emoji; // bitmap-face cluster, pre-drawn by Qt
    QPointF emojiOrigin;  // where the emoji image's top-left lands
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

struct BlockGeometry
{
    QList<Piece> pieces;
};

QMutex g_geometryMutex;
QCache<quint64, std::shared_ptr<const BlockGeometry>> g_geometryCache(48);

// Glyph paths and emoji bitmaps for a fragment set, converted once per layout. Keyed by the
// set and the emoji padding (the widest stroke), the only style input that touches geometry.
std::shared_ptr<const BlockGeometry> geometryFor(const text::FragmentSet &set, double emojiPadPx)
{
    const quint64 key = qHashMulti(set.key, qRound(emojiPadPx * 4.0));
    {
        QMutexLocker lock(&g_geometryMutex);
        if (const auto *hit = g_geometryCache.object(key))
            return *hit;
    }
    auto geometry = std::make_shared<BlockGeometry>();
    geometry->pieces.reserve(set.frags.size());
    for (const text::StyledWord &word : set.frags) {
        Piece piece;
        piece.cellRect = word.cellRect;
        piece.inkRect = word.inkRect;
        piece.baselineY = word.baselineY;
        piece.line = word.line;
        piece.wordIndex = word.index;
        piece.accent = word.accent;
        if (!word.emojiText.isEmpty())
            piece.emoji = renderEmoji(word, emojiPadPx, &piece.emojiOrigin);
        else
            piece.glyphs = toSkPath(word.path);
        geometry->pieces.append(piece);
    }
    QMutexLocker lock(&g_geometryMutex);
    g_geometryCache.insert(key, new std::shared_ptr<const BlockGeometry>(geometry));
    return geometry;
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

// Whether any enabled layer changes with time on its own (moving gradients, shader effects).
bool timeDrivenPaint(const TextStyle &style)
{
    for (const TextShadingLayer &layer : style.layers) {
        if (!layer.enabled)
            continue;
        if (layer.paint.kind == TextPaintKind::Gradient && !qFuzzyIsNull(layer.paint.gradient.offsetSpeed))
            return true;
        if (layer.paint.kind == TextPaintKind::Effect && textEffectIsAnimated(layer.paint.effect))
            return true;
    }
    return false;
}

double widestStroke(const TextStyle &style)
{
    double w = textStrokeWidth(style, false);
    if (style.accent.rule != WordAccentRule::None)
        w = qMax(w, textStrokeWidth(style, true));
    return w;
}

// A soft-edged reveal across `box`: opaque behind the front, clear ahead of it. `angleDeg` is the
// direction the front travels, `progress` how far it has come (0 = nothing shown).
sk_sp<SkShader> wipeShader(const QRectF &box, double angleDeg, double progress, double softness)
{
    const double rad = qDegreesToRadians(angleDeg);
    const QPointF dir(std::cos(rad), std::sin(rad));
    double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
    for (const QPointF &corner : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()}) {
        const double t = QPointF::dotProduct(corner, dir);
        lo = qMin(lo, t);
        hi = qMax(hi, t);
    }
    const double len = qMax(1.0, hi - lo);
    const double soft = qBound(0.0, softness, 1.0);
    const double front = qBound(0.0, progress, 1.0) * (1.0 + soft);
    const QPointF origin = dir * lo;
    const SkPoint pts[2] = {SkPoint::Make(float(origin.x()), float(origin.y())),
                            SkPoint::Make(float(origin.x() + dir.x() * len), float(origin.y() + dir.y() * len))};
    const SkColor4f colors[2] = {SkColors::kWhite, SkColors::kTransparent};
    const float pos[2] = {float(qBound(0.0, front - soft, 1.0)), float(qBound(0.0, front, 1.0))};
    const SkGradient::Colors stops{SkSpan<const SkColor4f>(colors, 2), SkSpan<const float>(pos, 2), SkTileMode::kClamp};
    return SkShaders::LinearGradient(pts, SkGradient{stops, SkGradient::Interpolation{}});
}

QColor withAlpha(const QColor &c, double factor)
{
    QColor out = c;
    out.setAlphaF(qBound(0.0, c.alphaF() * factor, 1.0));
    return out;
}

class AnimatedTextPainter final : public VectorPainter
{
public:
    AnimatedTextPainter(QSize size, quint64 key, const TextPaintRequest &request,
                        std::shared_ptr<const BlockGeometry> geometry, const Bend &bend, double bleedPx)
        : m_size(size), m_key(key == 0 ? 0 : (key | 1)), m_style(request.style), m_scale(request.renderScale),
          m_set(request.set), m_geometry(std::move(geometry)), m_frame(request.frame), m_bend(bend),
          m_bleed(bleedPx), m_timeSec(request.timeSec), m_grouping(request.anchorGrouping),
          m_alignment(request.anchorAlignment)
    {
        m_style.keyframes.clear();
        prepare();
    }

    QSize size() const override { return m_size; }
    quint64 cacheKey() const override { return m_key; }

    void paint(SkCanvas &canvas) const override
    {
        canvas.translate(float(m_bleed), float(m_bleed));
        SkPaint fill;
        fill.setAntiAlias(true);

        if (m_style.boxEnabled && !m_set->ink.isEmpty()) {
            const double padding = m_style.boxPadding * m_scale;
            const QRectF box = m_set->ink.adjusted(-padding, -padding, padding, padding);
            fill.setColor(toSkColor(m_style.boxColor));
            const float r = float(m_style.boxRadius * m_scale);
            canvas.drawRoundRect(toSkRect(box), r, r, fill);
        }

        // Pills follow the straight cell geometry, which a bent line no longer has.
        if (!m_bend.active) {
            for (int i = 0; i < m_pieces.size(); ++i) {
                if (!visible(i))
                    continue;
                const Piece &piece = m_pieces.at(i);
                const TextHighlight *highlight = text::highlightFor(m_style, piece.accent);
                if (!highlight)
                    continue;
                const double pad = highlight->padding * m_scale;
                const float r = float(highlight->radius * m_scale);
                fill.setColor(toSkColor(withAlpha(highlight->color, props(i).opacity)));
                canvas.save();
                canvas.concat(m_matrices.at(i));
                canvas.drawRoundRect(toSkRect(piece.cellRect.adjusted(-pad, -pad, pad, pad)), r, r, fill);
                canvas.restore();
            }
        }

        // Fully opaque fragments are drawn layer by layer, so every stroke sits under every fill.
        // A fragment mid-fade is drawn on its own translucent layer instead, so the stroke and
        // fill fade as one and the stroke never shows through the glyph.
        QList<int> solid, fading;
        for (int i = 0; i < m_pieces.size(); ++i) {
            if (!visible(i))
                continue;
            (props(i).opacity >= 0.999 ? solid : fading).append(i);
        }
        for (const TextShadingLayer &layer : m_style.layers)
            drawLayer(canvas, layer, solid, 1.0);
        for (int i : fading) {
            SkPaint alpha;
            alpha.setAlphaf(float(props(i).opacity));
            canvas.saveLayer(nullptr, &alpha);
            for (const TextShadingLayer &layer : m_style.layers)
                drawLayer(canvas, layer, {i}, 1.0);
            canvas.restore();
        }

        drawUnderline(canvas);
        drawCaret(canvas);

        if (m_frame.block.wipeProgress >= 0.0) {
            SkPaint mask;
            mask.setBlendMode(SkBlendMode::kDstIn);
            mask.setShader(wipeShader(m_set->ink, m_frame.block.wipeAngle, m_frame.block.wipeProgress,
                                      m_frame.block.wipeSoftness));
            canvas.drawPaint(mask);
        }
    }

private:
    const textanim::FragmentProps &props(int i) const
    {
        static const textanim::FragmentProps kIdentity;
        return i < m_frame.props.size() ? m_frame.props.at(i) : kIdentity;
    }

    bool visible(int i) const
    {
        const textanim::FragmentProps &p = props(i);
        return !p.hidden && p.opacity > 0.001;
    }

    // The point a fragment scales and rotates about, per the anchor grouping.
    QPointF anchorFor(int i) const
    {
        const Piece &piece = m_pieces.at(i);
        QRectF box = piece.cellRect;
        switch (m_grouping) {
        case TextAnchorGrouping::Character:
            break;
        case TextAnchorGrouping::Word:
            for (const Piece &other : m_pieces)
                if (other.wordIndex == piece.wordIndex && other.line == piece.line)
                    box = box.united(other.cellRect);
            break;
        case TextAnchorGrouping::Line:
            for (const Piece &other : m_pieces)
                if (other.line == piece.line)
                    box = box.united(other.cellRect);
            break;
        case TextAnchorGrouping::All:
            box = m_set->ink;
            break;
        }
        return QPointF(box.center().x() + box.width() * 0.5 * m_alignment.x(),
                       piece.baselineY + box.height() * 0.5 * m_alignment.y());
    }

    void prepare()
    {
        m_pieces = m_geometry->pieces;
        m_matrices.reserve(m_pieces.size());
        for (int i = 0; i < m_pieces.size(); ++i) {
            const Piece &piece = m_pieces.at(i);
            const textanim::FragmentProps &p = props(i);
            const QPointF anchor = anchorFor(i);
            SkMatrix m = m_bend.active ? m_bend.matrixFor(piece.cellRect.left(), piece.baselineY) : SkMatrix::I();
            SkMatrix anim = SkMatrix::Translate(float(anchor.x() + p.dx), float(anchor.y() + p.dy));
            anim.preConcat(SkMatrix::RotateDeg(float(p.rotation)));
            anim.preConcat(SkMatrix::Skew(float(std::tan(qDegreesToRadians(p.skew))), 0.0f));
            anim.preConcat(SkMatrix::Scale(float(p.scaleX), float(p.scaleY)));
            anim.preConcat(SkMatrix::Translate(float(-anchor.x()), float(-anchor.y())));
            m.preConcat(anim);
            m_matrices.append(m);
        }
        m_strokeWidthPx = widestStroke(m_style) * m_scale;
    }

    // The front-most enabled fill: the one the legacy accent colour overrides.
    bool isPrimaryFill(const TextShadingLayer &layer) const
    {
        return &layer == firstTextLayerOfKind(m_style.layers, TextLayerKind::Fill, true);
    }

    bool inScope(const TextShadingLayer &layer, const Piece &piece) const
    {
        switch (layer.scope) {
        case TextLayerScope::All:
            return true;
        case TextLayerScope::Base:
            return !piece.accent;
        case TextLayerScope::Accent:
            return piece.accent;
        }
        return true;
    }

    // Where a gradient maps for this piece.
    QRectF paintBoxFor(TextGradientSpace space, int i) const
    {
        const Piece &piece = m_pieces.at(i);
        switch (space) {
        case TextGradientSpace::Block:
            return m_set->ink;
        case TextGradientSpace::Glyph:
            return piece.inkRect;
        case TextGradientSpace::Line: {
            QRectF box;
            for (const Piece &other : m_pieces)
                if (other.line == piece.line)
                    box = box.isNull() ? other.inkRect : box.united(other.inkRect);
            return box;
        }
        case TextGradientSpace::Word: {
            QRectF box;
            for (const Piece &other : m_pieces)
                if (other.wordIndex == piece.wordIndex)
                    box = box.isNull() ? other.inkRect : box.united(other.inkRect);
            return box;
        }
        case TextGradientSpace::AccentRun: {
            // The contiguous run of accented words this piece belongs to; a plain piece maps to
            // the whole block.
            if (!piece.accent)
                return m_set->ink;
            int lo = i, hi = i;
            while (lo > 0 && m_pieces.at(lo - 1).accent && m_pieces.at(lo - 1).line == piece.line)
                --lo;
            while (hi + 1 < m_pieces.size() && m_pieces.at(hi + 1).accent && m_pieces.at(hi + 1).line == piece.line)
                ++hi;
            QRectF box;
            for (int k = lo; k <= hi; ++k)
                box = box.isNull() ? m_pieces.at(k).inkRect : box.united(m_pieces.at(k).inkRect);
            return box;
        }
        }
        return m_set->ink;
    }

    // The paint for one piece of one layer: the layer's own paint, or the animator's / accent's
    // colour override.
    void layerPaint(SkPaint &paint, const TextShadingLayer &layer, int i, double alpha) const
    {
        const Piece &piece = m_pieces.at(i);
        const textanim::FragmentProps &p = props(i);
        paint.setShader(nullptr);
        paint.setColorFilter(nullptr);
        std::optional<QColor> override;
        if (layer.kind == TextLayerKind::Fill) {
            if (p.fillColor)
                override = *p.fillColor;
            else if (piece.accent && m_style.accent.colorEnabled && isPrimaryFill(layer))
                override = m_style.accent.color;
            alpha *= p.fillOpacity;
        } else if (layer.kind == TextLayerKind::Stroke) {
            if (p.strokeColor)
                override = *p.strokeColor;
            else if (piece.accent && m_style.accent.outlineEnabled)
                override = m_style.accent.outlineColor;
            alpha *= p.strokeOpacity;
        }
        if (override) {
            paint.setColor(toSkColor(withAlpha(*override, alpha)));
            return;
        }
        const TextPaint &tp = layer.paint;
        switch (tp.kind) {
        case TextPaintKind::Solid:
            paint.setColor(toSkColor(withAlpha(tp.color, alpha)));
            break;
        case TextPaintKind::Gradient:
            paint.setColor(SkColorSetARGB(qRound(alpha * 255.0), 0, 0, 0));
            paint.setShader(gradientShaderFor(tp.gradient, paintBoxFor(tp.gradient.space, i), m_timeSec));
            break;
        case TextPaintKind::Texture:
            paint.setColor(SkColorSetARGB(qRound(alpha * 255.0), 0, 0, 0));
            paint.setShader(textureShaderFor(tp.texture, paintBoxFor(TextGradientSpace::Block, i)));
            if (!paint.getShader())
                paint.setColor(toSkColor(withAlpha(tp.color, alpha)));
            break;
        case TextPaintKind::Effect: {
            paint.setColor(SkColorSetARGB(qRound(alpha * 255.0), 0, 0, 0));
            sk_sp<SkShader> base = SkShaders::Color(toSkColor(tp.color));
            paint.setShader(effectShaderFor(tp.effect, std::move(base), paintBoxFor(TextGradientSpace::Block, i),
                                            m_timeSec, m_frame.block.wipeProgress));
            if (!paint.getShader())
                paint.setColor(toSkColor(withAlpha(tp.color, alpha)));
            break;
        }
        }
    }

    void applyFragmentWipe(SkPaint &paint, int i) const
    {
        const textanim::FragmentProps &p = props(i);
        if (p.wipeProgress < 0.0)
            return;
        paint.setMaskFilter(SkShaderMaskFilter::Make(
            wipeShader(m_pieces.at(i).cellRect, p.wipeAngle, p.wipeProgress, p.wipeSoftness)));
    }

    // One layer of the stack over the given pieces. Pieces are bucketed by their own blur so
    // a handful of transitional fragments do not force a filter over the whole block.
    void drawLayer(SkCanvas &canvas, const TextShadingLayer &layer, const QList<int> &pieces, double alpha) const
    {
        if (!layer.enabled || layer.opacity <= 0.0 || pieces.isEmpty())
            return;
        QList<int> scoped;
        for (int i : pieces)
            if (inScope(layer, m_pieces.at(i)))
                scoped.append(i);
        if (scoped.isEmpty())
            return;

        const bool blurred = layer.blur > 0.0 && layer.kind != TextLayerKind::Stroke && layer.kind != TextLayerKind::Extrude;
        const bool needsLayer = blurred || layer.opacity < 1.0 || layer.blend != BlendMode::Normal || layer.spread > 0.0
                                || layer.knockout;
        if (needsLayer) {
            SkPaint lp;
            lp.setAlphaf(float(layer.opacity));
            lp.setBlendMode(layer.knockout ? SkBlendMode::kDstOut : toSkBlendMode(layer.blend));
            sk_sp<SkImageFilter> filter;
            if (layer.spread > 0.0) {
                const float r = float(layer.spread * m_scale);
                filter = SkImageFilters::Dilate(r, r, nullptr);
            }
            if (blurred) {
                // The legacy raster ran a three-pass box blur of radius round(blur·scale); three
                // boxes of radius r have the variance of a gaussian with sigma = r + 0.5.
                const float sigma = float(qRound(layer.blur * m_scale)) + 0.5f;
                filter = SkImageFilters::Blur(sigma, sigma, std::move(filter));
            }
            lp.setImageFilter(std::move(filter));
            canvas.saveLayer(nullptr, &lp);
        }
        canvas.translate(float(layer.offsetX * m_scale), float(layer.offsetY * m_scale));

        // Bucket by the animator's per-fragment blur (quantised to half a pixel).
        QMap<int, QList<int>> buckets;
        for (int i : scoped)
            buckets[qRound(props(i).blurPx * 2.0)].append(i);
        for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
            const double sigma = it.key() / 2.0;
            if (sigma > 0.0) {
                SkPaint bp;
                bp.setImageFilter(SkImageFilters::Blur(float(sigma), float(sigma), nullptr));
                canvas.saveLayer(nullptr, &bp);
            }
            for (int i : it.value())
                drawPiece(canvas, layer, i, alpha);
            if (sigma > 0.0)
                canvas.restore();
        }

        canvas.translate(float(-layer.offsetX * m_scale), float(-layer.offsetY * m_scale));
        if (needsLayer)
            canvas.restore();
    }

    void drawPiece(SkCanvas &canvas, const TextShadingLayer &layer, int i, double alpha) const
    {
        const Piece &piece = m_pieces.at(i);
        const textanim::FragmentProps &p = props(i);
        canvas.save();
        canvas.concat(m_matrices.at(i));
        SkPaint paint;
        paint.setAntiAlias(true);
        switch (layer.kind) {
        case TextLayerKind::Fill:
            layerPaint(paint, layer, i, alpha);
            applyFragmentWipe(paint, i);
            if (piece.emoji)
                drawEmoji(canvas, piece, paint.getMaskFilter() ? &paint : nullptr);
            else
                canvas.drawPath(piece.glyphs, paint);
            break;
        case TextLayerKind::Stroke: {
            double width = (piece.accent && m_style.accent.outlineEnabled ? m_style.accent.outlineWidth : layer.width)
                           + p.strokeWidthDelta / m_scale;
            width = qMax(0.0, width) * m_scale;
            if (width <= 0.0)
                break;
            layerPaint(paint, layer, i, alpha);
            applyFragmentWipe(paint, i);
            if (piece.emoji) {
                // The bitmap's alpha, dilated by the stroke width and tinted: the same ring an
                // outlined glyph gets.
                SkPaint ring;
                ring.setAlphaf(paint.getAlphaf());
                ring.setImageFilter(SkImageFilters::ColorFilter(
                    SkColorFilters::Blend(paint.getColor() | 0xFF000000, SkBlendMode::kSrcIn),
                    SkImageFilters::Dilate(float(width), float(width), nullptr)));
                drawEmoji(canvas, piece, &ring);
                break;
            }
            paint.setStyle(SkPaint::kStroke_Style);
            // A centred stroke of twice the width grows entirely outward when the fill is drawn
            // over it, which is what the legacy outline did.
            paint.setStrokeWidth(float(layer.strokeOutside ? width * 2.0 : width));
            paint.setStrokeJoin(SkPaint::kRound_Join);
            paint.setStrokeCap(SkPaint::kRound_Cap);
            if (!qFuzzyIsNull(layer.trimStart) || !qFuzzyCompare(layer.trimEnd, 1.0))
                paint.setPathEffect(SkTrimPathEffect::Make(float(layer.trimStart), float(layer.trimEnd)));
            canvas.drawPath(piece.glyphs, paint);
            break;
        }
        case TextLayerKind::Shadow:
        case TextLayerKind::Glow: {
            // Shadows and glows are silhouettes: the glyph grown by the stroke, in the layer's colour.
            paint.setColor(toSkColor(withAlpha(layer.paint.color, alpha)));
            applyFragmentWipe(paint, i);
            if (piece.emoji) {
                SkPaint silhouette;
                sk_sp<SkImageFilter> dilate =
                    m_strokeWidthPx > 0.0 ? SkImageFilters::Dilate(float(m_strokeWidthPx), float(m_strokeWidthPx), nullptr)
                                          : nullptr;
                silhouette.setImageFilter(SkImageFilters::ColorFilter(
                    SkColorFilters::Blend(toSkColor(layer.paint.color), SkBlendMode::kSrcIn), std::move(dilate)));
                silhouette.setAlphaf(float(alpha));
                drawEmoji(canvas, piece, &silhouette);
                break;
            }
            canvas.drawPath(piece.glyphs, paint);
            if (m_strokeWidthPx > 0.0) {
                paint.setStyle(SkPaint::kStroke_Style);
                paint.setStrokeWidth(float(m_strokeWidthPx * 2.0));
                paint.setStrokeJoin(SkPaint::kRound_Join);
                canvas.drawPath(piece.glyphs, paint);
            }
            break;
        }
        case TextLayerKind::Extrude: {
            if (piece.emoji)
                break;
            const int steps = qMax(1, layer.extrudeSteps);
            const double rad = qDegreesToRadians(layer.extrudeAngle);
            const double depth = layer.width * m_scale;
            for (int k = steps; k >= 1; --k) {
                const double f = double(k) / steps;
                QColor c = layer.paint.color.darker(100 + int(layer.extrudeDarken * 100.0 * f));
                paint.setColor(toSkColor(withAlpha(c, alpha)));
                canvas.save();
                canvas.translate(float(std::cos(rad) * depth * f), float(std::sin(rad) * depth * f));
                canvas.drawPath(piece.glyphs, paint);
                canvas.restore();
            }
            break;
        }
        }
        canvas.restore();
    }

    void drawEmoji(SkCanvas &canvas, const Piece &piece, const SkPaint *paint) const
    {
        canvas.drawImage(piece.emoji.get(), float(piece.emojiOrigin.x()), float(piece.emojiOrigin.y()),
                         SkSamplingOptions(SkFilterMode::kLinear), paint);
    }

    void drawUnderline(SkCanvas &canvas) const
    {
        if (m_bend.active || !m_style.underlineEnabled || m_style.underlineWidth <= 0.0)
            return;
        QHash<int, QRectF> perLine;
        for (int i = 0; i < m_pieces.size(); ++i) {
            if (!visible(i))
                continue;
            const Piece &piece = m_pieces.at(i);
            const textanim::FragmentProps &p = props(i);
            const QRectF rule(piece.cellRect.left() + p.dx, piece.baselineY + p.dy + m_style.underlineOffset * m_scale,
                              piece.cellRect.width(), m_style.underlineWidth * m_scale);
            const auto it = perLine.find(piece.line);
            if (it == perLine.end())
                perLine.insert(piece.line, rule);
            else
                *it = it->united(rule);
        }
        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(toSkColor(m_style.underlineColor));
        const float r = float(m_style.underlineWidth * m_scale * 0.5);
        for (const QRectF &rule : std::as_const(perLine))
            canvas.drawRoundRect(toSkRect(rule), r, r, fill);
    }

    void drawCaret(SkCanvas &canvas) const
    {
        const textanim::CaretState &caret = m_frame.caret;
        if (!caret.visible || m_pieces.isEmpty())
            return;
        double x, baseline;
        if (caret.afterFragment >= 0 && caret.afterFragment < m_pieces.size()) {
            const Piece &piece = m_pieces.at(caret.afterFragment);
            x = piece.cellRect.right() + props(caret.afterFragment).dx;
            baseline = piece.baselineY + props(caret.afterFragment).dy;
        } else {
            const Piece &piece = m_pieces.first();
            x = piece.cellRect.left() + props(0).dx;
            baseline = piece.baselineY + props(0).dy;
        }
        QRectF rect;
        switch (m_style.animation.caret.shape) {
        case TextCaret::Shape::Bar:
            rect = QRectF(x, baseline - caret.heightPx * 0.8, qMax(1.0, caret.widthPx), caret.heightPx);
            break;
        case TextCaret::Shape::Underscore:
            rect = QRectF(x, baseline - caret.heightPx * 0.08, caret.heightPx * 0.6, qMax(1.0, caret.heightPx * 0.08));
            break;
        case TextCaret::Shape::Block:
            rect = QRectF(x, baseline - caret.heightPx * 0.8, caret.heightPx * 0.6, caret.heightPx);
            break;
        }
        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(toSkColor(withAlpha(caret.color, m_style.animation.caret.shape == TextCaret::Shape::Block ? 0.5 : 1.0)));
        canvas.drawRect(toSkRect(rect), fill);
    }

    QSize m_size;
    quint64 m_key;
    TextStyle m_style;
    double m_scale;
    std::shared_ptr<const text::FragmentSet> m_set;
    std::shared_ptr<const BlockGeometry> m_geometry;
    textanim::Frame m_frame;
    Bend m_bend;
    double m_bleed;
    double m_timeSec;
    TextAnchorGrouping m_grouping;
    QPointF m_alignment;
    QList<Piece> m_pieces;
    QList<SkMatrix> m_matrices;
    double m_strokeWidthPx = 0.0;
};

} // namespace

TextPainterResult makeTextPainter(const TextPaintRequest &request)
{
    if (!request.set || request.set->frags.isEmpty() || request.layoutRect.width() < 1.0
        || request.layoutRect.height() < 1.0)
        return {};
    const TextStyle &style = request.style;
    const double scale = request.renderScale;

    // The static bleed plus how far the animators can throw a fragment: the image size must not
    // depend on time, or the hold-phase cache key would.
    double maxCell = 0.0;
    int lines = 1;
    for (const text::StyledWord &w : request.set->frags) {
        maxCell = qMax(maxCell, qMax(w.cellRect.width(), w.cellRect.height()));
        lines = qMax(lines, w.line + 1);
    }
    const textanim::Bounds &env = request.envelope;
    const double rotationPad = env.maxRotationDeg > 0.0 ? maxCell * 0.5 : 0.0;
    const double envelope = qMax(env.maxDx, env.maxDy) + env.maxBlurPx * 2.0 + (env.maxScale - 1.0) * maxCell * 0.5
                            + rotationPad + env.maxTrackingPx * lines * 0.5;
    const double bleed = std::ceil(text::bleedFor(style) * scale + envelope) + 2.0;
    const int imageW = qMax(1, qRound(request.layoutRect.width() + bleed * 2.0));
    const int imageH = qMax(1, qRound(request.layoutRect.height() + bleed * 2.0));

    TextPainterResult result;
    result.rect = QRectF(request.layoutRect.x() - bleed, request.layoutRect.y() - bleed, imageW, imageH);
    result.block = request.frame.block;

    std::shared_ptr<const BlockGeometry> geometry = geometryFor(*request.set, widestStroke(style) * scale);
    const Bend bend = bendFor(request.set->frags, style, scale);

    const bool cacheable = request.frame.isStatic && !style.isAnimated() && !timeDrivenPaint(style);
    const quint64 key = cacheable ? qHashMulti(request.set->key, text::styleHash(style), imageW, imageH,
                                               qRound(scale * 1000.0), request.frame.poseHash)
                                  : 0;
    result.painter = std::make_shared<AnimatedTextPainter>(QSize(imageW, imageH), key, request, std::move(geometry),
                                                           bend, bleed);
    return result;
}

TextPainterResult makeTextPainter(const Clip &clip, const QString &text, const QRectF &layoutRect,
                                  double renderScale, int activeWordIndex, TimeUs clipTimeUs)
{
    if (text.isEmpty())
        return {};
    const TimeUs window = clip.timelineDuration > 0 ? clip.timelineDuration : secondsToUs(3.0);
    const TimeUs t = clipTimeUs < 0 ? window / 2 : clipTimeUs;
    TextPaintRequest request;
    request.style = clip.textStyle.isAnimated() ? clip.textStyle.resolvedAt(t) : clip.textStyle;
    const ResolvedTextAnimation anim = resolveTextAnimation(request.style.animation);
    request.set = text::fragmentsFor(text, request.style, layoutRect.width(), layoutRect.height(), renderScale,
                                     activeWordIndex, text::splitFor(anim.resolved, request.style));
    if (!request.set || request.set->frags.isEmpty())
        return {};
    const textanim::EvalContext ctx =
        text::evalContextFor(request.style, layoutRect, renderScale, 0, window, t, activeWordIndex);
    request.frame = textanim::evaluateTextAnimation(anim.set, anim.resolved, request.set->infos, request.set->domains, ctx);
    request.envelope = textanim::animationBounds(anim.resolved, ctx);
    request.layoutRect = layoutRect;
    request.renderScale = renderScale;
    request.timeSec = usToSeconds(t);
    request.anchorGrouping = anim.set.anchorGrouping;
    request.anchorAlignment = anim.set.anchorAlignment;
    return makeTextPainter(request);
}

void clearTextGeometryCache()
{
    QMutexLocker lock(&g_geometryMutex);
    g_geometryCache.clear();
    clearTextEffectCaches();
}

} // namespace drift::skia
