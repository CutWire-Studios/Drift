#include "TextLayout.h"

#include "EmojiCatalog.h"
#include "FontCatalog.h"
#include "TextRaster.h"

#include <QFontMetricsF>
#include <QPainterPathStroker>
#include <QRawFont>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QTextOption>
#include <QtMath>

#include <cmath>
#include <limits>

namespace drift::text {

static quint64 highlightHash(const drift::TextHighlight &h)
{
    return qHashMulti(0, h.enabled, h.color.rgba(), h.padding, h.radius);
}

// Everything about a style that changes pixels. Shared by the block and span cache keys, which
// would otherwise duplicate a 30-argument hash between them.
quint64 styleHash(const drift::TextStyle &s)
{
    // Deliberately excludes the animation and the time: motion is applied to the layer, not the
    // raster, so one texture serves every frame of an entrance or exit.
    const drift::WordAccent &a = s.accent;
    return qHashMulti(0, s.fontFamily, s.pixelSize, s.fontWeight, s.italic, s.color.rgba(),
                      static_cast<int>(s.align), static_cast<int>(s.valign), s.wordWrap, s.lineHeight,
                      s.letterSpacing, s.outlineEnabled, s.outlineWidth, s.outlineColor.rgba(),
                      s.shadowEnabled,
                      s.shadowOffsetX, s.shadowOffsetY, s.shadowBlur, s.shadowOpacity,
                      s.shadowColor.rgba(), s.glowEnabled, s.glowColor.rgba(), s.glowRadius,
                      s.glowOpacity, s.boxEnabled, s.boxColor.rgba(), s.boxPadding, s.boxRadius,
                      highlightHash(s.wordHighlight), s.underlineEnabled, s.underlineColor.rgba(),
                      s.underlineWidth, s.underlineOffset,
                      qHashMulti(0, static_cast<int>(a.rule), a.n, a.phase, a.colorEnabled,
                                 a.color.rgba(), a.sizeScale, a.outlineEnabled, a.outlineWidth,
                                 a.outlineColor.rgba(), highlightHash(a.highlight)));
}

quint64 rasterKey(const QString &text, const drift::TextStyle &s, int imageW, int imageH,
                  double renderScale, int activeWordIndex)
{
    return qHashMulti(0, text, styleHash(s), imageW, imageH, qRound(renderScale * 1000.0),
                      activeWordIndex);
}



// Colour emoji ship as a CBDT/CBLC bitmap face, which carries no glyph outlines for
// QPainterPath::addText to take — laid out as a path they come out empty and vanish. They have
// to be drawn with QPainter::drawText instead, so the layout has to pick them out first.
//
// The codepoint ranges come first so ordinary text never probes the font, and VS-16 is in them
// because it forces emoji presentation on characters that are otherwise textual. The face's own
// cmap then has the final say: a dingbat the emoji font does not carry stays with the styled
// font rather than falling back to whatever the system offers.
static bool isEmojiCluster(const QString &cluster, const QRawFont &emojiFace)
{
    const QList<uint> points = cluster.toUcs4();

    bool candidate = false;
    for (const uint point : points) {
        if (point == 0xFE0F || (point >= 0x1F000 && point <= 0x1FAFF)
            || (point >= 0x2600 && point <= 0x27BF) || (point >= 0x2B00 && point <= 0x2BFF)) {
            candidate = true;
            break;
        }
    }
    if (!candidate)
        return false;

    for (const uint point : points) {
        // Joiners, selectors and tag characters glue a sequence together and are in no cmap;
        // only the visible characters say anything about coverage.
        if (point == 0x200D || point == 0xFE0E || point == 0xFE0F
            || (point >= 0xE0020 && point <= 0xE007F))
            continue;
        if (!emojiFace.supportsCharacter(point))
            return false;
    }
    return true;
}

// The grapheme cluster boundaries inside [from, to), ends included. Qt's are extended clusters,
// so a ZWJ sequence, a flag or a skin-tone modifier stays in one piece.
QList<int> graphemeBoundaries(const QString &source, int from, int to)
{
    QList<int> stops{from};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, source);
    finder.setPosition(from);
    while (finder.toNextBoundary() > 0 && finder.position() < to)
        stops.append(finder.position());
    stops.append(to);
    return stops;
}

QList<WordRange> wordRanges(const QString &source)
{
    QList<WordRange> ranges;
    int i = 0;
    while (i < source.size()) {
        while (i < source.size() && source.at(i).isSpace())
            ++i;
        if (i >= source.size())
            break;
        int end = i;
        while (end < source.size() && !source.at(end).isSpace())
            ++end;
        ranges.append({i, end - i});
        i = end;
    }
    return ranges;
}

// Whether a pack's rule picks out this word. Positional rules are pure functions of the index, so
// the raster stays valid for the whole cue; Karaoke is the one rule that moves with the playhead.
static bool accentedWord(const drift::WordAccent &accent, int index, int count, int longestIndex,
                  int activeWordIndex)
{
    switch (accent.rule) {
    case drift::WordAccentRule::None:
        return false;
    case drift::WordAccentRule::FirstWord:
        return index == 0;
    case drift::WordAccentRule::LastWord:
        return index == count - 1;
    case drift::WordAccentRule::EveryOther:
        return index >= accent.phase && (index - accent.phase) % 2 == 0;
    case drift::WordAccentRule::EveryNth:
        return index >= accent.phase && (index - accent.phase) % qMax(1, accent.n) == 0;
    case drift::WordAccentRule::LongestWord:
        return index == longestIndex;
    case drift::WordAccentRule::RandomStable: {
        // Stable per-index draw so the same words stay accented across frames. ~1 word in 3.
        const quint32 h = qHash(static_cast<quint32>(index) * 2654435761u) ^ 0x9e3779b9u;
        return (h & 0xffffu) < 0x5555u;
    }
    case drift::WordAccentRule::Karaoke:
        return index == activeWordIndex;
    }
    return false;
}

// Lay the text out and split it into per-word (or per-character) pieces. `accentFont` differs from
// `font` only when the pack scales its accent words, which is also the only case that needs
// QTextLayout formats — without them the layout, and so the output, is byte-identical to the
// single-font path this replaced.
QList<StyledWord> layoutStyledText(const QString &text, const drift::TextStyle &style,
                                   const QFont &font, const QFont &accentFont, double wrapWidth,
                                   double blockHeight, int activeWordIndex, WordSplit split)
{
    QString source = text;
    source.replace(QLatin1Char('\n'), QChar::LineSeparator); // QTextLayout breaks on the separator

    const QList<WordRange> ranges = wordRanges(source);
    if (ranges.isEmpty())
        return {};

    int longestIndex = 0;
    for (int i = 1; i < ranges.size(); ++i) {
        if (ranges.at(i).length > ranges.at(longestIndex).length)
            longestIndex = i;
    }

    QList<bool> accentFlags;
    accentFlags.reserve(ranges.size());
    for (int i = 0; i < ranges.size(); ++i)
        accentFlags.append(accentedWord(style.accent, i, ranges.size(), longestIndex, activeWordIndex));

    QTextOption option;
    option.setWrapMode(style.wordWrap ? QTextOption::WordWrap : QTextOption::NoWrap);

    QTextLayout layout(source, font);
    layout.setTextOption(option);

    bool mixedSizes = false;
    if (accentFont.pixelSize() != font.pixelSize()) {
        QList<QTextLayout::FormatRange> formats;
        QTextCharFormat format;
        format.setFont(accentFont);
        for (int i = 0; i < ranges.size(); ++i) {
            if (accentFlags.at(i))
                formats.append({ranges.at(i).start, ranges.at(i).length, format});
        }
        if (!formats.isEmpty()) {
            layout.setFormats(formats);
            mixedSizes = true;
        }
    }

    const QFontMetricsF metrics(font);
    const double effectiveWrap = style.wordWrap ? qMax(1.0, wrapWidth) : std::numeric_limits<double>::max();

    // The colour face arrives with the emoji-font addon; without it the family is empty and emoji
    // keep falling through to the outline path and disappearing, exactly as before.
    const QString emojiFamily = emojiFontFamily();
    const bool emojiCapable = !emojiFamily.isEmpty();
    QFont emojiBaseFont;
    QFont emojiAccentFont;
    QRawFont emojiFace;
    if (emojiCapable) {
        emojiBaseFont = QFont(emojiFamily);
        emojiBaseFont.setPixelSize(qMax(1, font.pixelSize()));
        emojiAccentFont = QFont(emojiFamily);
        emojiAccentFont.setPixelSize(qMax(1, accentFont.pixelSize()));
        emojiFace = QRawFont::fromFont(emojiBaseFont);
    }

    layout.beginLayout();
    forever {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(effectiveWrap);
    }
    layout.endLayout();

    const int lineCount = layout.lineCount();
    if (lineCount == 0)
        return {};

    // With mixed sizes the line's own box is what keeps a scaled word from colliding with the line
    // above; with one font it stays on the font's line spacing, exactly as before.
    QList<double> steps;
    steps.reserve(lineCount);
    double totalH = 0.0;
    for (int i = 0; i < lineCount; ++i) {
        const double natural = mixedSizes ? layout.lineAt(i).height() : metrics.lineSpacing();
        const double step = natural * qMax(0.1, style.lineHeight);
        steps.append(step);
        totalH += step;
    }

    double blockTop = 0.0;
    if (style.valign == drift::TextVAlign::Middle)
        blockTop = (blockHeight - totalH) * 0.5;
    else if (style.valign == drift::TextVAlign::Bottom)
        blockTop = blockHeight - totalH;

    QList<StyledWord> words;
    double y = 0.0;
    for (int i = 0; i < lineCount; ++i) {
        const QTextLine line = layout.lineAt(i);
        const int start = line.textStart();
        const int len = line.textLength();
        const double lineTop = y;
        y += steps.at(i);
        if (len <= 0)
            continue;
        const int end = start + len;

        // Alignment is applied here rather than through QTextOption so it stays correct with
        // NoWrap, where the natural text can be wider than the box.
        const double natural = line.naturalTextWidth();
        double lineX = 0.0;
        if (style.align == drift::TextAlign::Center)
            lineX = (wrapWidth - natural) * 0.5;
        else if (style.align == drift::TextAlign::Right)
            lineX = wrapWidth - natural;

        const double baseline = blockTop + lineTop + (mixedSizes ? line.ascent() : metrics.ascent());

        for (int wi = 0; wi < ranges.size(); ++wi) {
            // A word wider than the box is broken across lines by Qt; each fragment is clamped to
            // the line and keeps the whole word's index and accent.
            const int ws = qMax(ranges.at(wi).start, start);
            const int we = qMin(ranges.at(wi).start + ranges.at(wi).length, end);
            if (we <= ws)
                continue;

            const QFont &wordFont = accentFlags.at(wi) ? accentFont : font;
            const QFontMetricsF wordMetrics(wordFont);
            const QFont &emojiFont = accentFlags.at(wi) ? emojiAccentFont : emojiBaseFont;

            auto appendRun = [&](int from, int to, bool emoji) {
                QString slice = source.mid(from, to - from);
                slice.remove(QChar::LineSeparator);
                if (slice.trimmed().isEmpty())
                    return;
                const double x0 = lineX + line.cursorToX(from);
                const double x1 = lineX + line.cursorToX(to);
                StyledWord word;
                word.cellRect = QRectF(qMin(x0, x1), baseline - wordMetrics.ascent(),
                                       std::abs(x1 - x0), wordMetrics.height());
                if (emoji) {
                    word.emojiText = slice;
                    word.emojiFont = emojiFont;
                    word.inkRect = word.cellRect;
                } else {
                    QPainterPath path;
                    // Winding, not the odd-even default: at heavy weights adjacent glyph contours
                    // overlap, and odd-even punches those overlaps out as holes.
                    path.setFillRule(Qt::WindingFill);
                    path.addText(x0, baseline, wordFont, slice);
                    const QRectF ink = path.boundingRect();
                    if (ink.isEmpty())
                        return;
                    word.path = path;
                    word.inkRect = ink;
                }
                word.baselineY = baseline;
                word.index = wi;
                word.line = i;
                word.accent = accentFlags.at(wi);
                words.append(word);
            };

            // Break the piece into runs of ordinary text and single emoji clusters. Run edges are
            // layout positions, so advances, wrapping and alignment are what they were; with no
            // emoji in the piece this is the one whole-piece run it has always been.
            auto emitPiece = [&](int from, int to) {
                const QList<int> stops = graphemeBoundaries(source, from, to);
                int runStart = -1;
                for (int s = 0; s + 1 < stops.size(); ++s) {
                    const int cs = stops.at(s);
                    const int ce = stops.at(s + 1);
                    if (emojiCapable && isEmojiCluster(source.mid(cs, ce - cs), emojiFace)) {
                        if (runStart >= 0)
                            appendRun(runStart, cs, false);
                        runStart = -1;
                        appendRun(cs, ce, true);
                    } else if (runStart < 0) {
                        runStart = cs;
                    }
                }
                if (runStart >= 0)
                    appendRun(runStart, to, false);
            };

            if (split == WordSplit::Characters) {
                // Cluster by cluster, not QChar by QChar: a flag or a ZWJ sequence is one
                // character to the reader and must animate as one.
                const QList<int> stops = graphemeBoundaries(source, ws, we);
                for (int s = 0; s + 1 < stops.size(); ++s)
                    emitPiece(stops.at(s), stops.at(s + 1));
            } else {
                emitPiece(ws, we);
            }
        }
    }
    return words;
}

QList<StyledWord> translatedWords(const QList<StyledWord> &words, double dx, double dy)
{
    QList<StyledWord> out = words;
    for (StyledWord &word : out) {
        word.path.translate(dx, dy);
        word.inkRect.translate(dx, dy);
        word.cellRect.translate(dx, dy);
        word.baselineY += dy;
    }
    return out;
}

double highlightBleed(const drift::TextHighlight &highlight)
{
    return highlight.enabled ? highlight.padding + highlight.radius : 0.0;
}

double bleedFor(const drift::TextStyle &style)
{
    const drift::WordAccent &accent = style.accent;
    const bool accented = accent.rule != drift::WordAccentRule::None;

    double bleed = qMax(style.outlineEnabled ? style.outlineWidth : 0.0,
                        accented && accent.outlineEnabled ? accent.outlineWidth : 0.0);
    if (style.shadowEnabled)
        bleed += style.shadowBlur * 2.0 + qMax(std::abs(style.shadowOffsetX), std::abs(style.shadowOffsetY));
    if (style.glowEnabled)
        bleed += style.glowRadius * 2.0;
    if (style.boxEnabled)
        bleed += style.boxPadding + style.boxRadius;
    bleed += qMax(highlightBleed(style.wordHighlight),
                  accented ? highlightBleed(accent.highlight) : 0.0);
    if (style.underlineEnabled)
        bleed += style.underlineOffset + style.underlineWidth;
    // A scaled accent word overshoots the block's line box on both sides.
    if (accented && accent.sizeScale > 1.0)
        bleed += style.pixelSize * (accent.sizeScale - 1.0);
    if (style.animIn.kind == drift::TextAnimKind::Blur || style.animOut.kind == drift::TextAnimKind::Blur)
        bleed += kTextBlurMaxPx;
    return bleed;
}

// Grow the glyph path outward by the outline width. Shared by the box background (which sizes to its

QPainterPath outlineShape(const QPainterPath &path, double outlineWidth, double renderScale)
{
    if (outlineWidth <= 0.0)
        return path;
    QPainterPathStroker stroker;
    // The stroker is centred on the path, so doubling the width yields an outline that grows entirely
    // outward and leaves the glyph shape intact.
    stroker.setWidth(outlineWidth * renderScale * 2.0);
    stroker.setJoinStyle(Qt::RoundJoin);
    stroker.setCapStyle(Qt::RoundCap);
    QPainterPath shape = stroker.createStroke(path).united(path);
    shape.setFillRule(Qt::WindingFill);
    return shape;
}

// The style a word is drawn with: the block's, with the pack's accent overrides folded in.
double outlineWidthFor(const drift::TextStyle &style, bool accent)
{
    if (accent && style.accent.outlineEnabled)
        return style.accent.outlineWidth;
    if (!style.outlineEnabled)
        return 0.0;
    return style.outlineWidth;
}

QColor outlineColorFor(const drift::TextStyle &style, bool accent)
{
    return accent && style.accent.outlineEnabled ? style.accent.outlineColor : style.outlineColor;
}

QColor fillColorFor(const drift::TextStyle &style, bool accent)
{
    return accent && style.accent.colorEnabled ? style.accent.color : style.color;
}

const drift::TextHighlight *highlightFor(const drift::TextStyle &style, bool accent)
{
    if (accent && style.accent.highlight.enabled)
        return &style.accent.highlight;
    return style.wordHighlight.enabled ? &style.wordHighlight : nullptr;
}

// Fill every word's outline shape into a scratch image and blur it once. One pass serves the whole


// The fonts differ only when the pack scales its accent words — which is exactly when the layout
// needs QTextLayout formats.
StyleFonts fontsForStyle(const drift::TextStyle &style, double renderScale)
{
    StyleFonts fonts;
    fonts.base = fontForStyle(style, qRound(style.pixelSize * renderScale));
    if (!qFuzzyIsNull(style.letterSpacing))
        fonts.base.setLetterSpacing(QFont::AbsoluteSpacing, style.letterSpacing * renderScale);

    fonts.accent = fonts.base;
    const double scale = style.accent.sizeScale;
    if (style.accent.rule != drift::WordAccentRule::None && scale > 0.0 && !qFuzzyCompare(scale, 1.0))
        fonts.accent.setPixelSize(qMax(1, qRound(style.pixelSize * scale * renderScale)));
    return fonts;
}

// Everything the block actually paints over: outlined glyphs plus any highlight pills. The box
// background sizes to this.
QRectF paintedBounds(const QList<StyledWord> &words, const drift::TextStyle &style, double renderScale)
{
    QRectF bounds;
    for (const StyledWord &word : words) {
        // An emoji has no path, so its cell is what the box background and the per-span ink test
        // have to size to — otherwise an emoji-only span is treated as empty and dropped.
        QRectF piece =
            word.emojiText.isEmpty()
                ? outlineShape(word.path, outlineWidthFor(style, word.accent), renderScale).boundingRect()
                : word.cellRect;
        if (const drift::TextHighlight *highlight = highlightFor(style, word.accent)) {
            const double pad = highlight->padding * renderScale;
            piece = piece.united(word.cellRect.adjusted(-pad, -pad, pad, pad));
        }
        bounds = bounds.isNull() ? piece : bounds.united(piece);
    }
    return bounds;
}

// Group the laid-out pieces into the reveal spans the caller asked for. Word and Character spans
// are one piece each (the layout already split them); Line spans gather a line's words so a mixed
// accent line still animates as one unit.
QList<QList<StyledWord>> groupSpans(const QList<StyledWord> &words, drift::TextAnimUnit unit)
{
    QList<QList<StyledWord>> groups;
    for (const StyledWord &word : words) {
        if (unit == drift::TextAnimUnit::Line && !groups.isEmpty()
            && groups.last().first().line == word.line)
            groups.last().append(word);
        else
            groups.append(QList<StyledWord>{word});
    }
    return groups;
}


quint64 spanRasterKey(const QString &text, const drift::TextStyle &s, const QRectF &layoutRect,
                      double renderScale, drift::TextAnimUnit unit, int activeWordIndex)
{
    return qHashMulti(0, text, styleHash(s), qRound(layoutRect.width()), qRound(layoutRect.height()),
                      qRound(renderScale * 1000.0), static_cast<int>(unit), activeWordIndex);
}

} // namespace drift::text
