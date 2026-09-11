#pragma once

#include "core/TextAnimator.h"
#include "core/TextStyle.h"

#include <QColor>
#include <QFont>
#include <QList>
#include <QPainterPath>
#include <QRectF>
#include <QString>

#include <memory>

// The layout half of text rendering: word/grapheme splitting, QTextLayout line breaking and
// placement, accent resolution, bleed and cache keys. The Skia painter (SkiaTextPainter) draws
// exactly these pieces; the animator engine (core/TextAnimator) gets their metadata as fragments.

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
    int charIndex = 0;    // grapheme ordinal in the source text, spaces included
    int nonSpaceIndex = 0;// ordinal among the drawn pieces
    int textStart = 0;    // [textStart, textStart + textLength) in the source string
    int textLength = 0;

    // Set instead of `path` for a colour-emoji cluster, which has no outline to fill and is
    // drawn from the bitmap face at paint time. Its origin is (cellRect.left(), baselineY).
    QString emojiText;
    QFont emojiFont;
};

enum class WordSplit { Whole, Characters };

struct WordRange { int start; int length; };

// Everything about a style that changes pixels; excludes the animation and the time.
quint64 styleHash(const TextStyle &s);
// Everything that determines the laid-out pieces.
quint64 layoutKey(const QString &text, const TextStyle &s, double wrapWidth, double blockHeight,
                  double renderScale, int activeWordIndex, WordSplit split);

// The grapheme cluster boundaries inside [from, to), ends included.
QList<int> graphemeBoundaries(const QString &source, int from, int to);
QList<WordRange> wordRanges(const QString &source);

// Lay the text out and split it into per-word (or per-character) pieces.
QList<StyledWord> layoutStyledText(const QString &text, const TextStyle &style, const QFont &font,
                                   const QFont &accentFont, double wrapWidth, double blockHeight,
                                   int activeWordIndex, WordSplit split);

// One layout run, shared by every consumer that needs the same text at the same size: the
// pieces plus the per-fragment metadata the animator engine works from.
struct FragmentSet
{
    QList<StyledWord> frags;
    QList<textanim::FragmentInfo> infos; // index-parallel with frags
    textanim::Domains domains;
    QRectF ink;                          // paintedBounds() at rest
    quint64 key = 0;
    WordSplit split = WordSplit::Whole;
};
// Cached (mutex-guarded LRU) so the preview workers, the export thread and the thumbnail
// provider never repeat a QTextLayout for the same request.
std::shared_ptr<const FragmentSet> fragmentsFor(const QString &text, const TextStyle &style, double wrapWidth,
                                                double blockHeight, double renderScale, int activeWordIndex,
                                                WordSplit split);
void clearLayoutCache();

// The split the animators need: characters when any selector works per character (or the line
// is bent, which places glyphs one at a time), else whole words.
WordSplit splitFor(const textanim::ResolvedSlots &resolved, const TextStyle &style);
// The evaluation context for one text window (a clip, or a subtitle cue) at `timelineUs`.
textanim::EvalContext evalContextFor(const TextStyle &style, const QRectF &layoutRect, double renderScale,
                                     TimeUs windowStartUs, TimeUs windowDurationUs, TimeUs timelineUs,
                                     int activeWordIndex);

// Margin (project px) the style can paint outside the layout rect: strokes, shadows, glows,
// extrusions, box, pills, underline, scaled accents and the bend rise. The animation envelope is
// added by the painter.
double bleedFor(const TextStyle &style);
// How far (project px) pathBend lifts the middle of the line: |bend|/100 × 2 em.
double textBendRise(const TextStyle &style);

const TextHighlight *highlightFor(const TextStyle &style, bool accent);

// The base and accent fonts a style resolves to at this render scale.
struct StyleFonts
{
    QFont base;
    QFont accent;
};
StyleFonts fontsForStyle(const TextStyle &style, double renderScale);

// Everything the block paints over: stroked glyphs plus any highlight pills.
QRectF paintedBounds(const QList<StyledWord> &words, const TextStyle &style, double renderScale);

} // namespace drift::text
