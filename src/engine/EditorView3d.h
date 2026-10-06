#pragma once

#include "engine/SceneCamera3d.h"

#include <QMatrix4x4>
#include <QSizeF>
#include <QVector3D>

namespace drift {

// The preview's free viewpoint in 3D mode: an orbit camera around `target`, the way Blender's
// viewport moves. Session state of the editor, never part of the project and never exported.
//
// Same world space as everything else: canvas px about the canvas centre, y down, +z toward the
// default viewer. Yaw turns about the world's vertical axis, pitch tips the view up or down
// (+ looks down from above), and at yaw = pitch = 0 the eye sits on +z looking at the canvas head
// on, the way the scene camera does at rest.
struct EditorView3d
{
    QVector3D target;
    double yaw = 0.0;   // degrees
    double pitch = 0.0; // degrees, clamped short of straight up/down
    double distance = 2000.0;
    double fovY = 32.0; // degrees, vertical
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

// Where the eye is.
QVector3D editorViewEye(const EditorView3d &view);

// World -> homogeneous canvas px with GL clip-space z, y down, top-left origin: the matrix the
// compositor places every clip through in 3D mode. With `lookThrough`, the scene camera's
// cameraViewProjection instead.
QMatrix4x4 editorViewProjection(const EditorView3d &view, const QSizeF &canvas,
                                const SceneCamera3d &sceneCamera = {});

// Navigation, each a pure step from one view to the next. Pixel arguments are canvas px.
EditorView3d orbitEditorView(const EditorView3d &view, double dxDeg, double dyDeg);
// Slides the target in the view plane so the scene follows the pointer.
EditorView3d panEditorView(const EditorView3d &view, double dxPx, double dyPx, const QSizeF &canvas);
// Moves toward (steps > 0) or away from the target, by a fixed ratio per step.
EditorView3d dollyEditorView(const EditorView3d &view, double steps);
// Frames a world-space sphere: centred and filling most of the view.
EditorView3d frameEditorView(const EditorView3d &view, const QVector3D &centre, double radius);

} // namespace drift
