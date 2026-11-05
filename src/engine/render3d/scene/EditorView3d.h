#pragma once

#include "engine/render3d/scene/SceneCamera3d.h"

#include <QMatrix4x4>
#include <QSizeF>
#include <QString>
#include <QVector3D>

namespace drift {

// The preview's free viewpoint in 3D mode: an orbit camera around `target`, the way Blender's
// viewport moves. Session state of the editor, never part of the project and never exported.
//
// Same world space as everything else (see ClipTransform3d.h): canvas px from the canvas top-left,
// y down, +z away from the viewer. Yaw turns about the world's vertical axis, pitch tips the view
// up or down (+ looks down from above), and at yaw = pitch = 0 the eye sits on -z looking at the
// canvas head on, the way the scene camera does at rest.
struct EditorView3d
{
    QVector3D target; // world; overview() puts it at the canvas centre
    double yaw = 0.0;   // degrees
    double pitch = 0.0; // degrees, clamped short of straight up/down
    double distance = 2000.0;
    double fovY = 32.0; // degrees, vertical
    // Parallel projection: distance and fovY still set how much is in view at the target.
    bool ortho = false;
    // Look through the scene camera instead: the view becomes exactly what the camera exports.
    bool lookThrough = false;

    // Pulled back and turned, so a fresh 3D view shows the stage and the camera in front of it.
    static EditorView3d overview(const QSizeF &canvas);
};

// The 3D view fills the preview panel rather than the project's frame. Its frame in project px:
// the panel's aspect at the project's scale, just covering the project canvas, which sits centred
// in it with its top-left at `offset`. An empty viewport gives the project canvas itself.
struct EditorFrame
{
    QSizeF size;
    QPointF offset;
};
EditorFrame editorFrame(const QSizeF &project, const QSizeF &viewport);

// The screen's axes in world space: right, down and into the screen.
struct EditorViewAxes
{
    QVector3D right;
    QVector3D down;
    QVector3D forward;
};
EditorViewAxes editorViewAxes(const EditorView3d &view);

// Where the eye is.
QVector3D editorViewEye(const EditorView3d &view);

// World -> homogeneous px of `frame`, with GL clip-space z, y down, top-left origin: the matrix the
// compositor places every clip through in 3D mode. `project` is the project canvas the world is
// measured on, sitting in the frame at frame.offset. With `lookThrough`, the scene camera's
// cameraViewProjection instead.
QMatrix4x4 editorViewProjection(const EditorView3d &view, const EditorFrame &frame, const QSizeF &project,
                                const SceneCamera3d &sceneCamera);

// The view of a frame that is the project canvas itself.
QMatrix4x4 editorViewProjection(const EditorView3d &view, const QSizeF &canvas,
                                const SceneCamera3d &sceneCamera);

// Navigation, each a pure step from one view to the next. Pixel arguments are canvas px.
EditorView3d orbitEditorView(const EditorView3d &view, double dxDeg, double dyDeg);
// Orbit that turns the whole view about `pivot` instead of the target, so the pivot stays put on screen.
EditorView3d orbitEditorViewAround(const EditorView3d &view, double dxDeg, double dyDeg, const QVector3D &pivot);
// Slides the target in the view plane so the scene follows the pointer.
EditorView3d panEditorView(const EditorView3d &view, double dxPx, double dyPx, const QSizeF &canvas);
// Moves toward (steps > 0) or away from the target, by a fixed ratio per step.
EditorView3d dollyEditorView(const EditorView3d &view, double steps);
// dollyEditorView that keeps the point under the pointer (ndc, y up; `aspect` = width / height) in place.
EditorView3d dollyEditorViewToward(const EditorView3d &view, double steps, double ndcX, double ndcY, double aspect);
// "front", "back", "left", "right", "top" or "bottom": looking exactly along an axis at the current target.
EditorView3d presetEditorView(const EditorView3d &view, const QString &axis);
// A scene camera as a view: its eye looking at a target on its axis, at the point of interest or one lens away.
EditorView3d editorViewOfCamera(const SceneCamera3d &camera);
// Fly mode: the eye turns in place (+right, +up, degrees) and then moves along the view's forward,
// its screen right and the world's up, in world px. The target travels with the eye.
EditorView3d flyEditorView(const EditorView3d &view, double turnRightDeg, double turnUpDeg,
                           double forward, double right, double up);
// Frames a world-space sphere: centred and filling most of the view.
EditorView3d frameEditorView(const EditorView3d &view, const QVector3D &centre, double radius);

} // namespace drift
