#pragma once

#include "engine/render3d/scene/ClipTransform3d.h"

#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>
#include <QVector3D>

// The preview's 3D transform gizmo for a clip that is a 3D layer: arrows to move, rings to
// rotate, square-tipped handles to scale. Pure geometry, so the maths is testable without QML.
//
// Everything is in overlay px: the preview's on-screen pixels, `scale` of them per canvas px.
// Handle sizes are fixed in overlay px, so the gizmo reads the same at any zoom.
//
// The clip stores world-axis position (x, y, z; see ClipTransform3d.h) and three Euler angles (X,
// then Y, then the in-plane spin). The gizmo's orientation only picks the axes the handles follow.
// World uses the world's axes (+x right, +y down, +z away from the viewer), through any parent;
// View uses the screen's; Local uses the clip's own. Scale is always along the clip's own edges. A drag solves for whatever stored values produce
// the motion: a local move changes x, y and z together, and any ring changes all three angles.
namespace drift::gizmo {

enum class Tool { Move, Rotate, Scale };
// World: the scene's axes. Local: the object's own. View: the screen's (X right, Y down, Z into it).
enum class Orientation { World, Local, View };

Tool toolFromString(const QString &name);
Orientation orientationFromString(const QString &name);

struct Pose
{
    QRectF rect;          // layout rect, canvas px
    double rotation = 0.0; // in-plane spin, degrees
    ClipPose3d pose3d;
    QSizeF canvas;
    // Transform layers over the clip, canvas px to canvas px: the handles are drawn through it and
    // the pointer is read back through its inverse, so the solving stays in the clip's own space.
    QTransform parent;
};

// A viewpoint other than the clip's own eye: the scene camera, or the preview's free 3D view.
// `worldToCanvas` takes world space (canvas px from the top-left, y down, +z away from the
// viewer) to homogeneous canvas px, with GL's clip-space z, as cameraViewProjection does.
// Without one, the clip is seen through its own `perspective`, with the eye at the canvas centre.
//
// An orthographic view has no w row (it projects with w = 1). `right`, `down` and `forward` are the
// view's screen axes in world space, for the View orientation; without them View acts as World.
struct View
{
    bool valid = false;
    QMatrix4x4 worldToCanvas;
    bool hasAxes = false;
    QVector3D right;
    QVector3D down;
    QVector3D forward;
};

enum class HandleKind {
    Arrow,   // move along an axis
    Dolly,   // move along an axis that points at the viewer: drawn as a ring, dragged vertically
    Ring,    // rotate about an axis
    Scale,   // scale along one of the clip's own edges
    Uniform, // scale both edges together
};

struct Handle
{
    QString id; // "x", "y", "z", or "xy" for uniform scale
    int axis = -1;
    HandleKind kind = HandleKind::Arrow;
    QList<QPolygonF> front; // polylines facing the viewer
    QList<QPolygonF> back;  // polylines on the far side of a ring, drawn faded
    QPolygonF head;         // filled: an arrow's cone, a scale handle's square
};

struct Geometry
{
    bool valid = false;
    QPointF origin;
    QList<Handle> handles;
};

// The same placement with the anchor moved to `local` (rect-local px, z out of the plane): the rect
// and position shift so every point lands where it did, but rotations now turn about `local`. For
// geometry that hangs off the clip's anchor yet should be handled at its bounds centre.
Pose pivotedAt(const Pose &pose, const QVector3D &local);
// A pose edited after pivotedAt, expressed again with `original`'s anchor and the same placement.
Pose anchoredLike(const Pose &pivoted, const Pose &original);

// `size` scales the handles (1 on desktop, larger for touch). Under a `view`, a transform-layer
// parent must be affine (it is folded into the view); a tilted parent gives no gizmo.
Geometry geometry(const Pose &pose, Tool tool, Orientation orientation, double scale,
                  double size = 1.0, const View &view = {});

// The handle within `tolerance` overlay px of `point`, nearest first; empty for none.
QString pick(const Geometry &geometry, QPointF point, double tolerance);

struct DragResult
{
    Pose pose;
    double uniformFactor = 1.0; // Scale "xy" only: how much both edges grew
};

// The pose after dragging `handle` from `press` to `now` (overlay px), starting from `start`.
// `snap` steps rotations by `rotateStep` degrees and, when `moveStep` is above zero, moves by that many px.
DragResult drag(const Pose &start, Tool tool, Orientation orientation, const QString &handle,
                QPointF press, QPointF now, bool snap, double scale, const View &view = {},
                double moveStep = 0.0, double rotateStep = 15.0);

// Where a homogeneous projection's eye sits in the space it projects from: the point it maps to
// w = 0 at the screen's centre lines. For a world -> canvas matrix that is the camera position.
QVector3D eyeOf(const QMatrix4x4 &worldToCanvas);

// X-then-Y-then-Z Euler angles (degrees) for a rotation matrix, choosing among the equivalent
// solutions the one closest to `nearest`, so a drag never jumps by a full turn.
void eulerFromMatrix(const QMatrix4x4 &rotation, double nearest[3], double out[3]);

} // namespace drift::gizmo
