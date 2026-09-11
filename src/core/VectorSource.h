#pragma once

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
    QMap<QString, VectorSlotValue> slotValues; // keyed by slot id; not `slots`, which Qt macros away

    bool isInline() const { return !source.isEmpty(); }
    bool isEmpty() const { return source.isEmpty() && path.isEmpty(); }

    QJsonObject toJson() const;
    static VectorSource fromJson(const QJsonObject &o);
};

// Hex SHA-256 of the document bytes.
QString vectorSourceHash(const QByteArray &data);

// Maps a raw animation time (clip source time plus offset) onto the animation's own timeline
// according to the loop mode. Returns false when nothing should be drawn (Hide outside
// 0..duration). A still (duration <= 0) always folds to 0.
bool foldVectorTime(TimeUs animUs, TimeUs durationUs, VectorLoop loop, TimeUs *out);

} // namespace drift
