#pragma once

#include "Keyframe.h"
#include "Time.h"

#include <QColor>
#include <QMap>
#include <QPointF>
#include <QString>
#include <QVector>

namespace drift {

// Media is a raster mask backed by an image or video file rather than a parametric shape: its
// pixels are the coverage map. Segmentation produces one (a grayscale video written by
// MatteWriter); the user can also drop in any image or video. Spelled "matte" in older projects.
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
// entry has nothing to combine with, so its op is ignored and it simply seeds the accumulator.
enum class MaskOp { Add, Subtract, Intersect };

QString maskOpToString(MaskOp op);
MaskOp maskOpFromString(const QString &op);

// A vertex of a Freeform mask, normalized to the clip's frame. The tangents are carried from the
// start so curved masks are additive rather than a second migration: both zero means a straight
// edge into this point, which is all the editor writes today. They mirror Keyframe's handle
// convention — relative to `pos`, `in` pointing back toward the previous vertex.
struct MaskPoint
{
    QPointF pos;
    QPointF inTan;
    QPointF outTan;
    bool corner = true; // when false the editor holds the two tangents collinear

    bool isStraight() const
    {
        return inTan.isNull() && outTan.isNull();
    }
    bool operator==(const MaskPoint &other) const
    {
        return pos == other.pos && inTan == other.inTan && outTan == other.outTan
               && corner == other.corner;
    }
};

// One timed entry on a track's mask lane. It occupies a span of the timeline like a clip does,
// and masks whichever of that track's clips it overlaps — outside its span the clips are
// untouched. It is not owned by any clip: splitting or deleting a clip leaves the mask in place.
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

    // Position on the timeline, absolute. `lane` is the row within the track's mask lane, so
    // overlapping masks stack visually instead of colliding; it also orders the composite, low
    // lane first, which is what makes Subtract and Intersect predictable.
    TimeUs timelineStart = 0;
    TimeUs timelineDuration = 0;
    int lane = 0;
    double x = 0.5; // center, normalized
    double y = 0.5;
    double w = 0.6; // size, normalized
    double h = 0.6;
    double rotation = 0.0;
    double feather = 0.0; // px blur on the alpha edge
    bool invert = false;
    QVector<MaskPoint> points; // for Freeform

    // Media only: an image or video whose pixels are the coverage map. A segmentation matte
    // covers the segmented source range, so video is indexed at (sourceUs - mediaOffsetUs).
    QString mediaPath;
    TimeUs mediaOffsetUs = 0;
    MaskMediaFit mediaFit = MaskMediaFit::Stretch;
    MaskMediaChannel mediaChannel = MaskMediaChannel::Luma;
    // Wrap video coverage back to the start once it runs out, instead of holding the last frame.
    bool mediaLoop = false;

    // Animated scalars, keyed by the same names the inspector and the keyframe graph use:
    // "x", "y", "w", "h", "rotation", "feather". A non-empty track wins over the scalar above at
    // render time; the scalar still holds the last static value, so clearing a track returns the
    // property to a constant rather than to the struct default. Mirrors Effect::paramKeyframes.
    //
    // Key times are relative to `timelineStart`, not to any clip: a mask outlives the clip it
    // was drawn over, so there is no clip for clip-relative times to anchor to.
    QMap<QString, KeyframeTrack<double>> keyframes;

    // Polygon shape over time. The whole vertex list is one key, because a polygon is edited as
    // a shape: separate tracks per coordinate would make "the same shape at time t" impossible
    // to express. Between two keys with matching vertex counts the positions interpolate
    // linearly; a mismatch holds the earlier key rather than inventing a correspondence.
    QMap<TimeUs, QVector<MaskPoint>> pathKeys;

    // Media coverage is decoded per frame by the compositor, so it has no rasterizable path.
    bool isMedia() const { return shape == MaskShape::Media && !mediaPath.isEmpty(); }
    bool contributes() const { return enabled && shape != MaskShape::None; }
    bool isAnimated() const;

    TimeUs timelineEnd() const { return timelineStart + timelineDuration; }
    // End-exclusive, matching Clip::containsTime, so a mask ending where another begins does not
    // briefly apply both.
    bool containsTime(TimeUs timelineUs) const
    {
        return timelineUs >= timelineStart && timelineUs < timelineEnd();
    }

    // scalar, overridden by keyframes[key] evaluated at clipTimeUs.
    double valueAt(const QString &key, TimeUs clipTimeUs) const;
    // A copy with every animated property baked down to its value at clipTimeUs. The compositor
    // calls this once per frame so the rasterizer only ever sees plain numbers.
    Mask resolvedAt(TimeUs clipTimeUs) const;
    void detachSharedData();
};

// True when no entry would change the layer's alpha, so the compositor can skip masking whole.
bool masksAreInert(const QList<Mask> &masks);

// The track's masks covering `timelineUs`, in lane order (low lane composites first). Masks
// outside their span simply do not contribute, which is what makes a mask's bar on the lane mean
// exactly the stretch of time it covers.
QList<Mask> masksActiveAt(const QList<Mask> &masks, TimeUs timelineUs);

// How many rows the mask lane needs: one more than the highest lane in use, or 0 for none.
int maskLaneCount(const QList<Mask> &masks);

// The lowest lane index on which `candidate` would not overlap any existing mask in time.
int firstFreeMaskLane(const QList<Mask> &masks, TimeUs start, TimeUs duration, int ignoreIndex = -1);

// A media mask covering the whole clip frame. This is the right default everywhere media enters
// the stack: the parametric defaults (w = h = 0.6) would shrink a segmentation matte to 60% and
// silently crop the subject.
Mask fullFrameMediaMask(const QString &path, TimeUs offsetUs = 0);

} // namespace drift
