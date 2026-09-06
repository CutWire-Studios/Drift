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

struct Mask
{
    MaskShape shape = MaskShape::None;
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

    // Matte only, and only from the people-cutout backend: the colour-decontaminated foreground,
    // which is what removes background spill from hair edges. Same size, timebase and offset as
    // mattePath. Empty for SAM2 mattes, which have no such output. Ignored when `invert` is set —
    // the background half of a cutout pair must keep its own colours.
    QString matteFgrPath;
};

} // namespace drift
