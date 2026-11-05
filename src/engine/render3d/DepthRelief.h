#pragma once

#include "core/effects/Effect.h"
#include "engine/render3d/editor/EditorOverlay.h"

#include <QColor>
#include <QList>
#include <QMatrix4x4>
#include <QPointF>
#include <QVector3D>

// The preview's 3D view draws a clip with a depth effect as a relief: its picture pushed back by
// its depth map, with 3D Relight's lights standing where they act. Everything here works in
// "relief space", the space effects/depth_relight/main.frag lights in: x across the frame scaled by
// its aspect, y down it, z away from the viewer, all in clip heights about the frame's centre, with
// z = 0 at the nearest thing in the clip and depthRange at the farthest.
namespace drift {

struct DepthFrame;

// Where a clip's relief sits in the world. Relief space maps onto the clip's unit quad
// (clipQuadToWorld's local space, whose +z also points away) by one uniform scale, so the whole chain is a similarity as long
// as the quad's own matrix is.
struct ReliefPlacement
{
    QMatrix4x4 quadToWorld;
    double aspect = 1.0;   // frame width / height
    double heightPx = 1.0; // the clip's height in world px

    QVector3D toQuad(const QVector3D &relief) const;
    QVector3D toWorld(const QVector3D &relief) const;
    QVector3D fromWorld(const QVector3D &world) const;
};

struct ReliefLight
{
    int effectIndex = -1; // in the effect list it was read from
    int light = 0;        // 1..4
    QVector3D position;   // relief space
    QColor color;
    bool spot = false;
    double cone = 180.0; // full angle, degrees
    QVector3D aim;       // relief space, on the surface
};

// The relief's depth in clip heights: the first enabled 3D Relight's "Scene depth", so the lights
// stand where its shader puts them, otherwise that parameter's default.
double reliefDepthRange(const QList<Effect> &effects);

// The frame's depth at uv, 1 nearest and 0 farthest, filtered the way the GPU samples it. 0 for a
// null frame.
double sampleReliefDepth(const DepthFrame *frame, const QPointF &uv);

// The point of the relief surface at uv.
QVector3D reliefSurfacePoint(const DepthFrame *frame, const QPointF &uv, double aspect, double depthRange);

// Every enabled light of every enabled 3D Relight in `effects`, which are resolved at the frame.
QList<ReliefLight> reliefLights(const QList<Effect> &effects, const DepthFrame *frame, double aspect);

// Converts a relief-space light position back to the light's x, y, z parameters, clamped to their
// ranges.
QVector3D reliefLightParams(const QVector3D &relief, double aspect, double depthRange);

// Where a world-space ray first meets the relief, as a frame uv. False when it misses the frame.
bool reliefRayHit(const ReliefPlacement &placement, const DepthFrame *frame, double depthRange,
                  const QVector3D &origin, const QVector3D &through, QPointF *uv);

// Blender-style guides for the lights: a ring facing `eye` at each light with a dashed drop to the
// surface beneath it, and for a spot its cone out to where it is aimed.
void addReliefLightGuides(QList<EditorLine> &out, const QList<ReliefLight> &lights,
                          const ReliefPlacement &placement, const DepthFrame *frame, double depthRange,
                          const QVector3D &eye, bool selected);

} // namespace drift
