#pragma once

#include "core/VectorSource.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// What a Lottie or SVG document declares and what this renderer cannot honour, so an agent can
// see the problem before it composes a scene around it. Skia-free header; the .cpp parses with
// Skottie / SkSVGDOM under DRIFT_WITH_SKIA and reports "unsupported" without it.

namespace drift::vec {

struct VectorLayerInfo
{
    QString name;
    double width = 0;
    double height = 0;
    double inSec = 0;
    double outSec = 0;
};

struct VectorSlotInfo
{
    QString id;
    VectorSlotValue::Type type = VectorSlotValue::Type::Scalar;
};

struct VectorNamedProperty
{
    QString node;
    QString type; // color | opacity | transform | text
};

struct VectorMarker
{
    QString name;
    double t0 = 0;
    double t1 = 0;
};

// An SVG element the user can address by id (see parseSvgOverrideKey). The paint fields are the
// element's own attributes as written (or from its style=""), empty when it has none.
struct VectorSvgElement
{
    QString id;
    QString tag;
    QStringList classes;
    bool inDefs = false;
    QString fill;
    QString stroke;
    QString strokeWidth;
    QString opacity;
};

// What one pass over an SVG's XML yields: the elements carrying an id, and the same document
// with an id minted onto every paintable element that had none, so the renderer can reach all
// of them through SkSVGDOM::findNodeById (Skia keeps no other handle on a node).
struct SvgScan
{
    QList<VectorSvgElement> elements; // user ids only
    QStringList injectedIds;
    QByteArray rewritten;
};
SvgScan scanSvg(const QByteArray &data);

struct InspectReport
{
    bool ok = false;
    QString error;
    VectorKind kind = VectorKind::Lottie;
    QString version;
    QString title;
    double fps = 0;
    TimeUs durationUs = 0;
    int width = 0;
    int height = 0;
    QList<VectorLayerInfo> layers;
    QList<VectorSlotInfo> slotInfos; // `slots` is a Qt macro
    QList<VectorNamedProperty> namedProperties;
    QStringList fonts;
    QList<VectorMarker> markers;
    QStringList expressions; // property paths carrying an expression Skottie will ignore
    QStringList unsupported; // parser warnings, deduplicated
    QStringList hints;
    QList<VectorSvgElement> svgElements; // SVG only: elements with an id the renderer can find

    QJsonObject toJson() const;
};

// `<` opens an SVG, anything else is taken as Lottie JSON.
VectorKind detectVectorKind(const QByteArray &data);

InspectReport inspectVector(const QByteArray &data, VectorKind kind);

// Fills the probed fields of `source` (size, fps, duration, title, hash) from the document it
// names, inline or on disk. False with `error` when the document does not parse.
bool probeVectorSource(VectorSource &source, QString *error = nullptr);

// The document bytes a source names: inline text or the file at `path`.
QByteArray vectorSourceBytes(const VectorSource &source);

} // namespace drift::vec
