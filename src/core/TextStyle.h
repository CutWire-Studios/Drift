#pragma once

#include "Keyframe.h"
#include "TextAnimator.h"
#include "TextShading.h"
#include "Time.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <optional>

namespace drift {

enum class TextAlign { Left, Center, Right };
enum class TextVAlign { Top, Middle, Bottom };

// Which words a style pack accents. The positional rules are time-independent, so the layout
// stays cacheable for the whole cue; Karaoke follows the word being spoken and re-lays-out as
// the playhead crosses each word.
enum class WordAccentRule { None, FirstWord, LastWord, EveryOther, EveryNth, LongestWord,
                            RandomStable, Karaoke };

QString textAlignToString(TextAlign align);
TextAlign textAlignFromString(const QString &align);

QString textVAlignToString(TextVAlign valign);
TextVAlign textVAlignFromString(const QString &valign);

QString wordAccentRuleToString(WordAccentRule rule);
WordAccentRule wordAccentRuleFromString(const QString &rule);

// Rounded pill drawn behind a word. Used both for "every word" backgrounds and for the
// accent-only highlight a pack paints under its chosen words.
struct TextHighlight
{
    bool enabled = false;
    QColor color = QColor(230, 40, 40);
    double padding = 6.0; // px at pixelSize
    double radius = 4.0;
};

// The per-word override a style pack applies to the words its rule picks out. Everything left
// disabled falls through to the block style, so a pack only states what it changes.
struct WordAccent
{
    WordAccentRule rule = WordAccentRule::None;
    int n = 2;    // EveryNth stride
    int phase = 0; // index of the first accented word

    bool colorEnabled = false;
    QColor color = QColor(255, 214, 64);
    double sizeScale = 1.0; // relative to the block's pixelSize; changes the layout

    bool outlineEnabled = false;
    double outlineWidth = 0.0;
    QColor outlineColor = Qt::black;

    TextHighlight highlight;
};

struct TextStyle
{
    QString packId; // last applied style pack; cleared once the style is hand-edited
    QString fontFamily = QStringLiteral("Inter");
    int pixelSize = 64; // at project height
    int fontWeight = 700; // 100..900
    bool italic = false;

    // The look: an ordered shading stack, layers[0] drawn first. A fresh style is one solid
    // white fill. `lookId` names the Looks-gallery recipe the stack came from (cleared on a hand
    // edit) and `lookParams` its slider values, so the look stays regenerable.
    QList<TextShadingLayer> layers = {solidFillLayer(Qt::white)};
    QString lookId;
    QMap<QString, VectorSlotValue> lookParams;

    // Bends a single-line block along an arc: -100..100, the rise at the middle as a fraction of
    // two em (positive arches upward). Multi-line blocks ignore it.
    double pathBend = 0.0;

    TextAlign align = TextAlign::Center;
    TextVAlign valign = TextVAlign::Middle;
    bool wordWrap = true;
    double lineHeight = 1.2; // multiple of the font's natural line spacing
    double letterSpacing = 0.0; // px at pixelSize

    bool boxEnabled = false; // filled background behind the whole block
    QColor boxColor = QColor(0, 0, 0, 128);
    double boxPadding = 8.0;
    double boxRadius = 0.0;

    TextHighlight wordHighlight; // pill behind every word

    bool underlineEnabled = false;
    QColor underlineColor = QColor(230, 40, 40);
    double underlineWidth = 6.0;
    double underlineOffset = 4.0; // below the baseline

    WordAccent accent;

    // In / Out / Loop motion (presets or inline animators) evaluated per fragment.
    TextAnimationSet animation;

    // Animated scalars keyed by textKeyframeProperties() names ("pixelSize", "layer.shadow.blur",
    // …), key times relative to the clip's start. A non-empty, enabled track wins over the scalar
    // at render time; the scalar keeps the last static value, so clearing a track returns the
    // property to a constant. Mirrors Mask::keyframes.
    QMap<QString, KeyframeTrack<double>> keyframes;

    bool isAnimated() const; // has enabled keyframe tracks
    bool hasFragmentAnimation() const { return animation.isActive(); }
    // A copy with every keyframed property baked down to its value at clipTimeUs. The renderers
    // only ever see plain numbers; `keyframes` is kept on the copy so callers can still tell an
    // animated style from a static one (cache keys).
    TextStyle resolvedAt(TimeUs clipTimeUs) const;

    // The colour of the text as a user thinks of it: the front-most enabled fill's solid colour
    // (or first gradient stop). Setting it edits that fill.
    QColor primaryColor() const;
    void setPrimaryColor(const QColor &color);
};

// Convenience accessors the layout and painters use so nobody spells out layer lookups.
QColor textFillColor(const TextStyle &style, bool accent);
double textStrokeWidth(const TextStyle &style, bool accent);
QColor textStrokeColor(const TextStyle &style, bool accent);
// Sets the front-most fill to a solid colour (importers).
void setSolidFill(TextStyle &style, const QColor &color);

// The scalars a text style can animate, in inspector order: the flat ones then one group per
// layer. Keys are canonical ("layer.<id>.<field>").
QStringList textKeyframeProperties(const TextStyle &style);
// Canonical spelling of a keyframe key: the key itself when valid on this style, the layer path
// for a legacy alias ("outlineWidth" → "layer.stroke.width"), or empty when unknown.
QString textKeyframeCanonicalKey(const QString &key, const TextStyle &style);
// Human label for a key ("Shadow · Blur").
QString textKeyframeLabel(const QString &key, const TextStyle &style);
// The scalar a keyframe property reads/writes on the style; colour channels are 0..1. False for
// an unknown key. Legacy aliases are accepted.
bool textStyleScalar(const TextStyle &style, const QString &key, double *out);
bool setTextStyleScalar(TextStyle &style, const QString &key, double value);

struct TextPreset
{
    QString id;
    QString label;
    TextStyle style;
    // Short phrase shown on picker thumbnails — chosen to demo the pack's look
    // (accents, wrap, weight) rather than a meaningless filler line.
    QString sampleText;
};

// Built-in style packs only. User-saved presets live in TextPresetStore; textPresetForId()
// resolves both, which is what keeps the preview provider and applyTextPreset kind-agnostic.
const QList<TextPreset> &textPresets();
// By value: a user preset lives in a mutable library that the GUI thread edits while the
// preview provider resolves ids on the image-loading thread.
std::optional<TextPreset> textPresetForId(const QString &id);
std::optional<TextStyle> textStyleForPresetId(const QString &id);

// Shared with the project file format, which is why these live here rather than in Project.cpp:
// the user preset store writes the same style objects and must inherit the same key migrations.
QJsonObject textHighlightToJson(const TextHighlight &h);
TextHighlight textHighlightFromJson(const QJsonObject &o, const TextHighlight &fallback);
QJsonObject wordAccentToJson(const WordAccent &a);
WordAccent wordAccentFromJson(const QJsonObject &o);
QJsonObject textStyleToJson(const TextStyle &s);
// Reads both the v7 layered form and the flat v6 form (migrated into layers and preset slots).
TextStyle textStyleFromJson(const QJsonObject &o);

} // namespace drift
