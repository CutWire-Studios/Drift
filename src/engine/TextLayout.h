#pragma once

#include "core/TextStyle.h"

#include <QColor>
#include <QFont>
#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>

// The layout half of text rendering, shared by the QPainter raster (TextRaster) and the Skia
// painter (SkiaTextPainter): word/grapheme splitting, QTextLayout line breaking and placement,
// accent resolution, outline shapes, bleed and cache keys. Both backends draw exactly these
// pieces, so block geometry, karaoke indices and reveal spans cannot drift between them.

namespace drift::text {

// One drawable piece of the block: a whole word, or a single character of one when the caller
// asked for a character split. Everything is in block-local coordinates (0,0 = layout rect
// top-left) and the piece carries the word's accent state, so painting never re-derives it.
struct StyledWord
{
    QPainterPath path;
    QRectF inkRect;
    QRectF cellRect;      // advance width × the font's ascent..descent band: the highlight pill
    double baselineY = 0.0;
    int index = 0;        // reading-order word index; shared by every character of a word
    int line = 0;
    bool accent = false;

    // Set instead of `path` for a colour-emoji cluster, which has no outline to fill and is
    // drawn from the bitmap face at paint time. Its origin is (cellRect.left(), baselineY).
    QString emojiText;
    QFont emojiFont;
};

enum class WordSplit { Whole, Characters };

struct WordRange { int start; int length; };

// Everything about a style that changes pixels; excludes the animation and the time.
quint64 styleHash(const TextStyle &s);
quint64 rasterKey(const QString &text, const TextStyle &s, int imageW, int imageH,
                  double renderScale, int activeWordIndex);
quint64 spanRasterKey(const QString &text, const TextStyle &s, const QRectF &layoutRect,
                      double renderScale, TextAnimUnit unit, int activeWordIndex);

// The grapheme cluster boundaries inside [from, to), ends included.
QList<int> graphemeBoundaries(const QString &source, int from, int to);
QList<WordRange> wordRanges(const QString &source);

// Lay the text out and split it into per-word (or per-character) pieces.
QList<StyledWord> layoutStyledText(const QString &text, const TextStyle &style, const QFont &font,
                                   const QFont &accentFont, double wrapWidth, double blockHeight,
                                   int activeWordIndex, WordSplit split);
QList<StyledWord> translatedWords(const QList<StyledWord> &words, double dx, double dy);

// Margin (project px) the style can paint outside the layout rect: outline, shadow, glow, box,
// pills, underline, scaled accents and the entrance blur. The size authority for both backends.
double bleedFor(const TextStyle &style);

// The glyph path grown outward by the outline width (already includes the glyph).
QPainterPath outlineShape(const QPainterPath &path, double outlineWidth, double renderScale);
double outlineWidthFor(const TextStyle &style, bool accent);
QColor outlineColorFor(const TextStyle &style, bool accent);
QColor fillColorFor(const TextStyle &style, bool accent);
const TextHighlight *highlightFor(const TextStyle &style, bool accent);

// The base and accent fonts a style resolves to at this render scale.
struct StyleFonts
{
    QFont base;
    QFont accent;
};
StyleFonts fontsForStyle(const TextStyle &style, double renderScale);

// Everything the block paints over: outlined glyphs plus any highlight pills.
QRectF paintedBounds(const QList<StyledWord> &words, const TextStyle &style, double renderScale);

// Group laid-out pieces into reveal spans (Word / Character: one piece each; Line: a line's words).
QList<QList<StyledWord>> groupSpans(const QList<StyledWord> &words, TextAnimUnit unit);

} // namespace drift::text
