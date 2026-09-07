#pragma once

#include "Time.h"

#include <QList>
#include <QPointF>
#include <QString>
#include <QVector>

namespace drift {

// Media is a raster mask backed by an image or video file rather than a parametric shape: its
// pixels are the coverage map. Segmentation produces one (a grayscale matte written by
// MatteWriter); the user can also point it at any image or video. Spelled "matte" in projects
// written before v5.
enum class MaskShape { None, Rectangle, Ellipse, Star, Heart, Bars, Freeform, Media };

QString maskShapeToString(MaskShape shape);
MaskShape maskShapeFromString(const QString &shape);

// How the media is fitted into the mask's rect when their aspects differ.
enum class MaskMediaFit { Stretch, Fit, Fill };

QString maskMediaFitToString(MaskMediaFit fit);
MaskMediaFit maskMediaFitFromString(const QString &fit);

// Which channel of the media carries the coverage. Luma suits the grayscale mattes segmentation
// writes and any black-and-white artwork; Alpha suits a cutout PNG.
enum class MaskMediaChannel { Luma, Alpha };

QString maskMediaChannelToString(MaskMediaChannel channel);
MaskMediaChannel maskMediaChannelFromString(const QString &channel);

// How an entry folds into the coverage accumulated by the entries before it. The first enabled
// entry has nothing to combine with, so its op is ignored and it simply seeds the accumulator —
// otherwise a lone Subtract or Intersect would blank the clip.
enum class MaskOp { Add, Subtract, Intersect };

QString maskOpToString(MaskOp op);
MaskOp maskOpFromString(const QString &op);

// One mask. It is carried by a Mask-kind adjustment clip, which supplies the timing: the
// adjustment's span is the stretch of timeline the mask covers, and its lane position orders the
// stack. That is why there are no timelineStart/lane members here.
//
// Coordinates are normalized to the *host clip's* frame rather than the canvas, because the
// coverage map is rasterized at the layer's size and sampled at the layer's UV. A mask spanning
// a cut therefore masks each clip in that clip's own frame space.
struct Mask
{
    MaskShape shape = MaskShape::None;
    MaskOp op = MaskOp::Add;
    bool enabled = true;
    QString name; // user label on the lane; empty means "derive from the shape"

    double x = 0.5; // center, normalized
    double y = 0.5;
    double w = 0.6; // size, normalized
    double h = 0.6;
    double rotation = 0.0;
    double feather = 0.0; // px blur on the alpha edge
    bool invert = false;
    QVector<QPointF> points; // normalized, for Freeform

    // Media only: an image or video whose pixels are the coverage map.
    QString mediaPath;
    // The source time of the host clip that the media's first frame corresponds to. Media is
    // therefore indexed at (clip.timelineToSourceUs(t) - mediaSrcOffsetUs), NOT relative to the
    // adjustment's own start: a segmentation matte is traced from one clip's source range, so a
    // later head-trim (which moves srcIn but not this) or a speed change would otherwise slide
    // the matte off the picture.
    TimeUs mediaSrcOffsetUs = 0;
    MaskMediaFit mediaFit = MaskMediaFit::Stretch;
    MaskMediaChannel mediaChannel = MaskMediaChannel::Luma;
    // Wrap video coverage back to the start once it runs out, instead of holding the last frame.
    bool mediaLoop = false;

    // Media only, and only from the people-cutout backend: the colour-decontaminated foreground,
    // which is what removes background spill from hair edges. Same size, timebase and offset as
    // mediaPath. Empty for SAM2 mattes, which have no such output. Ignored when `invert` is set —
    // inverting a cutout keeps the background, which must keep its own colours.
    QString mediaFgrPath;

    // Media coverage is decoded per frame by the compositor, so it has no rasterizable path.
    bool isMedia() const { return shape == MaskShape::Media && !mediaPath.isEmpty(); }
    bool contributes() const { return enabled && shape != MaskShape::None; }
};

// True when no entry would change the layer's alpha, so the compositor can skip masking whole.
bool masksAreInert(const QList<Mask> &masks);

// A full-frame media mask. This is the right default everywhere media enters the stack: the
// parametric defaults (w = h = 0.6) would shrink a segmentation matte to 60% of the frame and
// silently crop the subject.
Mask fullFrameMediaMask(const QString &path, TimeUs srcOffsetUs = 0);

} // namespace drift
