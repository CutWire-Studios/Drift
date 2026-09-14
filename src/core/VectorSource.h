#pragma once

#include "Keyframe.h"
#include "Time.h"

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QMap>
#include <QPointF>
#include <QString>

namespace drift {

// A Lottie animation or SVG document placed on a Graphic track (ClipType::Vector). The document
// is either inline (`source`, how agents hand one over MCP) or a file the bin imported (`path`);
// `hash` identifies the bytes either way and keys the renderer's parsed-document cache.

enum class VectorKind { Lottie, Svg };
enum class VectorFit { Contain, Cover, Stretch };
// What plays once the animation has run past its own duration (or before its start, when the
// offset pushes it there).
enum class VectorLoop { Hold, Loop, PingPong, Hide };

QString vectorKindToString(VectorKind kind);
VectorKind vectorKindFromString(const QString &kind);
QString vectorFitToString(VectorFit fit);
VectorFit vectorFitFromString(const QString &fit);
QString vectorLoopToString(VectorLoop loop);
VectorLoop vectorLoopFromString(const QString &loop);

// One override for a Lottie slot (the animation's declared template inputs). The type must match
// the slot's declared type; the renderer ignores a mismatch and inspect reports it.
struct VectorSlotValue
{
    enum class Type { Color, Scalar, Vec2, Text, Image };

    Type type = Type::Scalar;
    QColor color;
    double scalar = 0.0;
    QPointF vec2;
    QString text;
    QString image; // file path

    static VectorSlotValue fromColor(const QColor &c);
    static VectorSlotValue fromScalar(double v);
    static VectorSlotValue fromVec2(const QPointF &p);
    static VectorSlotValue fromText(const QString &t);
    static VectorSlotValue fromImage(const QString &path);

    QJsonObject toJson() const;
    static VectorSlotValue fromJson(const QJsonObject &o);
    bool operator==(const VectorSlotValue &other) const;
    bool operator!=(const VectorSlotValue &other) const { return !(*this == other); }
};

QString vectorSlotTypeToString(VectorSlotValue::Type type);
VectorSlotValue::Type vectorSlotTypeFromString(const QString &type);

struct VectorSource
{
    VectorKind kind = VectorKind::Lottie;
    QString source; // inline document text; empty when `path` is used
    QString path;   // document file; empty when inline
    QString hash;   // vectorSourceHash() of the document bytes

    // Probed from the document when it was attached; the renderer treats them as hints.
    int width = 0;
    int height = 0;
    double fps = 0.0;
    TimeUs durationUs = 0; // 0 for a still (SVG)
    QString title;

    VectorFit fit = VectorFit::Contain;
    VectorLoop loop = VectorLoop::Hold;
    // Added to the clip's source time before folding, so an animation can start mid-way.
    TimeUs startOffsetUs = 0;
    // Keyed by slot id; not `slots`, which Qt macros away. Lottie slots are the animation's
    // declared inputs; an SVG has none, and instead takes the reserved svg.* override keys (see
    // parseSvgOverrideKey).
    QMap<QString, VectorSlotValue> slotValues;
    // Keyed by slot key plus an optional colour channel ("svg.logo.fill.r", "svg.strokeWidth");
    // times are relative to the clip start. Baked into slotValues by resolvedAt().
    QMap<QString, KeyframeTrack<double>> keyframes;

    bool isInline() const { return !source.isEmpty(); }
    bool isEmpty() const { return source.isEmpty() && path.isEmpty(); }
    bool isAnimated() const;
    VectorSource resolvedAt(TimeUs clipTimeUs) const;

    QJsonObject toJson() const;
    static VectorSource fromJson(const QJsonObject &o);
};

// An SVG clip is restyled through reserved slot keys:
//   svg.fill / svg.stroke (Color), svg.strokeWidth / svg.opacity (Scalar)   — the whole document
//   svg.<id>.fill / .stroke (Color), svg.<id>.strokeWidth / .opacity / .visible (Scalar) — one element
// where <id> is an element id from the document (ids may contain dots: the property is the last
// segment). visible takes 0 or 1 and is not keyframable.
struct SvgOverrideKey
{
    QString elementId; // empty for the whole document
    QString prop;      // fill | stroke | strokeWidth | opacity | visible
};
bool parseSvgOverrideKey(const QString &slot, SvgOverrideKey *out);
VectorSlotValue::Type svgOverrideType(const QString &prop);
// "Fill", "#logo · Stroke width"
QString svgOverrideLabel(const SvgOverrideKey &key);

// A slot scalar by keyframe key: the slot itself for a Scalar, or one of the .r/.g/.b/.a channels
// (0..1) of a Color. Setting creates the slot when it is missing so a keyed value always has a
// static home. False for an unknown key, a Text/Image/Vec2 slot, or visible.
bool vectorSlotScalar(const VectorSource &source, const QString &key, double *out);
bool setVectorSlotScalar(VectorSource &source, const QString &key, double value);

// Hex SHA-256 of the document bytes.
QString vectorSourceHash(const QByteArray &data);

// Maps a raw animation time (clip source time plus offset) onto the animation's own timeline
// according to the loop mode. Returns false when nothing should be drawn (Hide outside
// 0..duration). A still (duration <= 0) always folds to 0.
bool foldVectorTime(TimeUs animUs, TimeUs durationUs, VectorLoop loop, TimeUs *out);

} // namespace drift
