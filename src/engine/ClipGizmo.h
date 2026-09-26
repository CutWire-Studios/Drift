#pragma once

#include "engine/ClipTransform3d.h"

#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QString>

// The preview's 3D transform gizmo for a clip that is a 3D layer: arrows to move, rings to
// rotate, square-tipped handles to scale. Pure geometry, so the maths is testable without QML.
//
// Everything is in overlay px: the preview's on-screen pixels, `scale` of them per canvas px.
// Handle sizes are fixed in overlay px, so the gizmo reads the same at any zoom.
//
// The clip stores canvas-axis position (x, y, z) and three Euler angles (X, then Y, then the
// in-plane spin). The gizmo's orientation only picks the axes the handles follow. Global uses the
// camera's axes. Local uses the clip's own axes. A drag solves for whatever stored values produce
// the motion: a local move changes x, y and z together, and any ring changes all three angles.
namespace drift::gizmo {

enum class Tool { Move, Rotate, Scale };
enum class Orientation { Global, Local };

Tool toolFromString(const QString &name);
Orientation orientationFromString(const QString &name);

struct Pose
{
    QRectF rect;          // layout rect, canvas px
    double rotation = 0.0; // in-plane spin, degrees
    ClipPose3d pose3d;
    QSizeF canvas;
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

// `size` scales the handles (1 on desktop, larger for touch).
Geometry geometry(const Pose &pose, Tool tool, Orientation orientation, double scale,
                  double size = 1.0);

// The handle within `tolerance` overlay px of `point`, nearest first; empty for none.
QString pick(const Geometry &geometry, QPointF point, double tolerance);

struct DragResult
{
    Pose pose;
    double uniformFactor = 1.0; // Scale "xy" only: how much both edges grew
};

// The pose after dragging `handle` from `press` to `now` (overlay px), starting from `start`.
// `snap` steps rotations by 15°.
DragResult drag(const Pose &start, Tool tool, Orientation orientation, const QString &handle,
                QPointF press, QPointF now, bool snap, double scale);

// X-then-Y-then-Z Euler angles (degrees) for a rotation matrix, choosing among the equivalent
// solutions the one closest to `nearest`, so a drag never jumps by a full turn.
void eulerFromMatrix(const QMatrix4x4 &rotation, double nearest[3], double out[3]);

} // namespace drift::gizmo
