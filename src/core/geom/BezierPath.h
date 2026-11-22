#pragma once

#include "core/shapes/ShapeStyle.h"

#include <QPointF>
#include <QVector>

namespace drift::geom {

using drift::TangentMode;
enum class TangentHandle { In, Out };

inline TangentMode tangentModeOf(const ShapeVertex &v)
{
    return v.mode;
}

int segmentCount(const ShapeContour &c);
// Segment `seg` runs from vertex seg to vertex (seg + 1) % n.
QPointF cubicPoint(const ShapeVertex &a, const ShapeVertex &b, double t);
QPointF cubicTangent(const ShapeVertex &a, const ShapeVertex &b, double t);

// de Casteljau split: inserts a vertex at t of segment `seg` without changing the outline.
// Returns the new vertex' index.
int splitSegment(ShapeContour &c, int seg, double t);

struct NearestHit
{
    int segment = -1;
    double t = 0.0;
    double distance = 0.0;
    QPointF point;
    bool valid() const { return segment >= 0; }
};
NearestHit nearestPoint(const ShapeContour &c, const QPointF &p);

// Polyline approximating the outline, `stepsPerSegment` samples per cubic. Closed contours do not
// repeat the first point.
QVector<QPointF> flatten(const ShapeContour &c, int stepsPerSegment = 16);

// Removes vertex i and refits the neighbours' facing handles so the outline keeps its shape as
// closely as one cubic can. Ends of an open contour, and spans that were straight, are plain
// removals.
bool removeVertexHeal(ShapeContour &c, int i);
bool removeVertex(ShapeContour &c, int i);

// The vertex after dragging `which` handle to `newPos` (same space as v.p). Constrain45 snaps the
// handle's angle to 45 degree steps around the vertex.
ShapeVertex applyTangentDrag(ShapeVertex v, TangentHandle which, const QPointF &newPos, TangentMode mode,
                             bool constrain45);

// A change of vertex count, recorded so it can be replayed on every shape key of an animated path:
// keys with the same count stay in step with the outline being edited.
struct PathOp
{
    enum class Kind { Insert, Remove };
    Kind kind = Kind::Insert;
    int segment = 0; // Insert: the segment split; Remove: the vertex index
    double t = 0.5;
    bool heal = false;
};
// Replays `op` on one contour; false when it does not fit (the vertex count is off).
bool applyPathOp(ShapeContour &c, const PathOp &op);

// The handles a smooth point gets when it has none: along the line through its neighbours.
void makeSmooth(ShapeContour &c, int i, TangentMode mode = TangentMode::Mirrored);
// Turns the vertex into a corner: both handles go.
void makeCorner(ShapeContour &c, int i);
// Reverses vertex order, swapping each vertex' in and out handle so the outline is unchanged.
void reverseContour(ShapeContour &c);

// Keyframe-style handles are time/value offsets. In-handles have dx <= 0, out-handles dx >= 0.
struct GraphTangents
{
    double inDx = 0.0;
    double inDy = 0.0;
    double outDx = 0.0;
    double outDy = 0.0;
};

// `dx`/`dy` is the dragged handle's new offset. `horizontalOnly` flattens the handle (dy = 0).
// dx is clamped to the span toward the neighbouring key on that side (`spanIn` before the key,
// `spanOut` after); when it clamps, dy scales with it so the slope is kept.
GraphTangents applyGraphTangentDrag(GraphTangents t, TangentHandle which, double dx, double dy,
                                    TangentMode mode, bool horizontalOnly, double spanIn, double spanOut);

} // namespace drift::geom
