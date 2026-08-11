#pragma once

#include "Time.h"

#include <QColor>
#include <QPointF>
#include <QString>
#include <QVector>

namespace drift {

// Matte is a per-frame raster mask backed by a grayscale video, unlike the parametric shapes:
// only `mattePath`, `matteSrcOffsetUs` and `invert` apply to it.
enum class MaskShape { None, Rectangle, Ellipse, Star, Heart, Bars, Freeform, Matte };

QString maskShapeToString(MaskShape shape);
MaskShape maskShapeFromString(const QString &shape);

// How an entry folds into the coverage accumulated by the entries before it. The first enabled
// entry has nothing to combine with, so its op is ignored and it simply seeds the accumulator.
enum class MaskOp { Add, Subtract, Intersect };

QString maskOpToString(MaskOp op);
MaskOp maskOpFromString(const QString &op);

// One entry in a clip's mask stack. Coordinates are normalized to the clip's own frame rather
// than the canvas — the coverage map is rasterized at the layer's size and sampled at the
// layer's UV, so a mask travels with the clip's transform.
struct Mask
{
    MaskShape shape = MaskShape::None;
    MaskOp op = MaskOp::Add;
    bool enabled = true;
    QString name; // user label in the stack list; empty means "derive from the shape"
    double x = 0.5; // center, normalized
    double y = 0.5;
    double w = 0.6; // size, normalized
    double h = 0.6;
    double rotation = 0.0;
    double feather = 0.0; // px blur on the alpha edge
    bool invert = false;
    QVector<QPointF> points; // normalized, for Freeform

    // Matte only: a grayscale video whose frames are the coverage map. It covers the segmented
    // source range, so it is indexed at (sourceUs - matteSrcOffsetUs).
    QString mattePath;
    TimeUs matteSrcOffsetUs = 0;

    // Matte coverage is decoded per frame by the compositor, so it has no rasterizable path.
    bool isMatte() const { return shape == MaskShape::Matte && !mattePath.isEmpty(); }
    bool contributes() const { return enabled && shape != MaskShape::None; }
};

// True when no entry would change the layer's alpha, so the compositor can skip masking whole.
bool masksAreInert(const QList<Mask> &masks);

} // namespace drift
