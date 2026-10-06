#pragma once

#include <QList>
#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>
#include <QVector4D>

#include <optional>

namespace drift::snap {

// A clip as the preview sees it: where its own layout px (an item of the rect's size laid out from
// (0, 0), z = 0) land in homogeneous canvas pixels, and what moving its layout rect by one pixel
// along x or y does to any of those points. Moving the rect is a translation in the clip's parent
// plane before any projection, so the homogeneous image of every point moves by the same `ex`/`ey`
// per pixel, whatever the tilt, parent or camera: the moves below are exact linear solves.
struct PlacedClip
{
    QMatrix4x4 local; // layout px of the rect -> homogeneous canvas px
    QVector4D ex;     // homogeneous change per +1 px of transformX
    QVector4D ey;     // homogeneous change per +1 px of transformY
    QSizeF size;      // the rect's width and height
};

// Screen lines, in canvas px: what a moved or resized clip sticks to.
struct Targets
{
    QList<double> x;
    QList<double> y;
};

// The rect translation that keeps the point grabbed at `press` under `now` (both canvas px).
// Empty when the clip's plane is seen edge-on or the pointer lands behind the eye.
std::optional<QPointF> dragDelta(const PlacedClip &clip, const QPointF &press, const QPointF &now);

struct MoveSnap
{
    QPointF delta;
    double guideX = -1.0; // the engaged target, or -1
    double guideY = -1.0;
};

// `delta` pulled so a near-vertical projected left edge, centre line or right edge lands on an x
// target, and likewise for the horizontal ones on y, within `tolerance` canvas px. An edge tilted
// more than a few degrees from the screen axis has nothing to line up with and does not snap.
MoveSnap snapMove(const PlacedClip &clip, const QPointF &delta, const Targets &targets,
                  double tolerance);

struct EdgeSnap
{
    double size = 0.0;     // the snapped width (or height)
    double guide = -1.0;   // the engaged target, or -1
    double distance = 0.0; // how far the edge was pulled, in canvas px
};

// Resizing: the moving edge sits at `anchor + scale * size` in the start rect's local px along
// the axis (`horizontal` for width), spanning the other axis's start extent. The size whose edge
// lands on a target within `tolerance` canvas px, if any.
std::optional<EdgeSnap> snapEdge(const PlacedClip &clip, bool horizontal, double anchor,
                                 double scale, double size, const Targets &targets,
                                 double tolerance);

} // namespace drift::snap
