#pragma once

#include "engine/render3d/scene/EditorView3d.h"
#include "engine/render3d/scene/SceneCamera3d.h"

#include <QMatrix4x4>
#include <QObject>
#include <functional>
#include <QPointF>
#include <QVariantList>
#include <QVariantMap>

class AppController;
class Outliner3dModel;

namespace drift::snap {
struct PlacedClip;
}
namespace drift::gizmo {
struct View;
}

// The preview area's editor state and the maths its overlays run on: the free 3D viewpoint of the
// editor viewport (the Camera viewport always shows the scene camera and has none of its own), the gizmo, snapping, drops, and which interaction tool owns the pointer. Session
// state, never saved with the project (guidesEnabled aside, which AppController saves for it).
// Reached from QML as EditorState.preview. Clip edits still go through AppController's
// previewSet* / beginPreviewDrag / commitPreviewDrag transaction.
class PreviewController : public QObject
{
    Q_OBJECT

    // The cameras, models and 3D layers under the playhead, for the 3D Outliner.
    Q_PROPERTY(Outliner3dModel *outliner READ outliner CONSTANT)

    // Whether a 3D viewport is showing. The panel sets it while it is visible; MCP can ask for it.
    Q_PROPERTY(bool viewport3dOpen READ viewport3dOpen WRITE setViewport3dOpen NOTIFY viewport3dOpenChanged)
    // In the editor viewport: looking through the scene camera rather than from the free viewpoint.
    Q_PROPERTY(bool lookThrough READ lookThrough NOTIFY viewChanged)
    // In the editor viewport: Blender-style fly navigation owns the pointer and the keyboard.
    Q_PROPERTY(bool flying READ flying WRITE setFlying NOTIFY flyingChanged)
    // Bumped whenever the 3D viewpoint moves, so overlays drawn through it re-read their geometry.
    Q_PROPERTY(int viewRevision READ viewRevision NOTIFY viewChanged)
    // The 3D gizmo on a clip that is a 3D layer: which tool ("move", "rotate", "scale") and which axes
    // its handles follow: "world" (the scene's), "local" (the object's own) or "view" (the screen's).
    // Scale always follows the object's own edges whatever this says.
    Q_PROPERTY(QString gizmoTool READ gizmoTool WRITE setGizmoTool NOTIFY gizmoChanged)
    Q_PROPERTY(QString gizmoOrientation READ gizmoOrientation WRITE setGizmoOrientation NOTIFY gizmoChanged)
    // Editor viewport projection and lens: orthographic (Numpad 5) and the vertical field of view in
    // degrees, 10 to 120 (the default is 32).
    Q_PROPERTY(bool orthographic READ orthographic WRITE setOrthographic NOTIFY viewChanged)
    Q_PROPERTY(double editorFov READ editorFov WRITE setEditorFov NOTIFY viewChanged)
    // The editor viewport's floor grid (its spacing in px, 0 for automatic) and the gizmo's snap steps:
    // moves by translateSnap px (0 for none) and turns by rotationSnap degrees. A drag snaps unless
    // Ctrl is held.
    Q_PROPERTY(bool showGrid READ showGrid WRITE setShowGrid NOTIFY viewChanged)
    Q_PROPERTY(double gridSize READ gridSize WRITE setGridSize NOTIFY viewChanged)
    Q_PROPERTY(double translateSnap READ translateSnap WRITE setTranslateSnap NOTIFY gizmoChanged)
    Q_PROPERTY(double rotationSnap READ rotationSnap WRITE setRotationSnap NOTIFY gizmoChanged)
    // Navigation: "blender" (middle-drag orbits, Shift pans, Ctrl dollies) or "maya" (Alt with the
    // left, middle or right button orbits, pans or dollies); the wheel zooms toward the pointer in both.
    Q_PROPERTY(QString navigationScheme READ navigationScheme WRITE setNavigationScheme NOTIFY navigationChanged)
    // Orbit about the selection's centre when something is selected, else about the view's target.
    Q_PROPERTY(bool orbitAroundSelection READ orbitAroundSelection WRITE setOrbitAroundSelection NOTIFY navigationChanged)
    // While looking through the scene camera, orbit/pan/dolly move the camera clip rather than
    // dropping out of look-through.
    Q_PROPERTY(bool lockCameraToView READ lockCameraToView WRITE setLockCameraToView NOTIFY navigationChanged)
    Q_PROPERTY(bool guidesEnabled READ guidesEnabled WRITE setGuidesEnabled NOTIFY guidesEnabledChanged)
    // Library set whose guides are being dragged on the preview; empty when not editing.
    Q_PROPERTY(QString guideEditSetId READ guideEditSetId WRITE setGuideEditSetId NOTIFY guideEditSetIdChanged)
    Q_PROPERTY(bool canvasCropMode READ canvasCropMode WRITE setCanvasCropMode NOTIFY canvasCropModeChanged)
    // Phone crop page: the selected clip's source frame is edited on the preview. Exclusive with canvasCropMode.
    Q_PROPERTY(bool clipCropMode READ clipCropMode WRITE setClipCropMode NOTIFY clipCropModeChanged)
    Q_PROPERTY(QString maskViewMode READ maskViewMode WRITE setMaskViewMode NOTIFY maskViewModeChanged)
    // The masking workspace's drawing tool: "" (none), "rect", "ellipse", "polygon", "brush", "ai"
    // or "tracker". Session state, like the rest of this class.
    Q_PROPERTY(QString maskingTool READ maskingTool WRITE setMaskingTool NOTIFY maskingToolChanged)
    Q_PROPERTY(bool maskEditMode READ maskEditMode WRITE setMaskEditMode NOTIFY maskEditModeChanged)
    // Whether the preview should be showing mask handles right now. Selecting a mask clip is
    // itself a request to edit it, so the toggle is only needed to keep the handles up while some
    // *other* clip is selected.
    Q_PROPERTY(bool maskEditActive READ maskEditActive NOTIFY maskEditActiveChanged)
    // The one tool that owns the preview's pointer: "crop", "clipCrop", "guideEdit", "maskDraw", "maskBrush",
    // "maskAi", "tracker", "mask" or "transform", in that order of precedence. The masking tools
    // and the mask editor are 2D tools, so the editor viewport falls through to "transform".
    Q_PROPERTY(QString activeTool READ activeTool NOTIFY activeToolChanged)
    // Grips are drawn only while the picture holds still and there is a canvas to put them on.
    Q_PROPERTY(bool handlesVisible READ handlesVisible NOTIFY handlesVisibleChanged)

public:
    explicit PreviewController(AppController &app, QObject *parent = nullptr);
    ~PreviewController() override;

    // Viewport ids, the same numbers as PreviewStream: 0 is the Camera viewport, 1 the Editor's.
    static constexpr int kCameraViewport = 0;
    static constexpr int kEditorViewport = 1;

    Outliner3dModel *outliner() const { return m_outliner; }
    bool viewport3dOpen() const { return m_viewport3dOpen; }
    void setViewport3dOpen(bool open);
    // Marks it open and asks the shell to show a workspace that has one (viewport3dRequested).
    Q_INVOKABLE void requestViewport3d();
    bool lookThrough() const { return m_editor.view.lookThrough; }
    bool flying() const { return m_editor.flying; }
    void setFlying(bool flying);
    // The editor viewport's viewpoint, or the overview it starts from when it was never placed.
    drift::EditorView3d currentView() const;
    // Settles the never-placed viewpoint onto the overview, so navigation starts from it.
    void placeView();
    int viewRevision() const { return int(m_editor.serial); }
    QString gizmoTool() const { return m_gizmoTool; }
    void setGizmoTool(const QString &tool);
    QString gizmoOrientation() const { return m_gizmoOrientation; }
    void setGizmoOrientation(const QString &orientation);
    bool orthographic() const { return m_editor.view.ortho; }
    void setOrthographic(bool ortho);
    double editorFov() const { return m_editor.view.fovY; }
    void setEditorFov(double degrees);
    bool showGrid() const { return m_showGrid; }
    void setShowGrid(bool on);
    double gridSize() const { return m_gridSize; }
    void setGridSize(double px);
    double translateSnap() const { return m_translateSnap; }
    void setTranslateSnap(double px);
    double rotationSnap() const { return m_rotationSnap; }
    void setRotationSnap(double degrees);
    QString navigationScheme() const { return m_navigationScheme; }
    void setNavigationScheme(const QString &scheme);
    bool orbitAroundSelection() const { return m_orbitAroundSelection; }
    void setOrbitAroundSelection(bool on);
    bool lockCameraToView() const { return m_lockCameraToView; }
    void setLockCameraToView(bool on);
    bool guidesEnabled() const { return m_guidesEnabled; }
    void setGuidesEnabled(bool enabled);
    // A loaded project's saved value: neither dirties the project nor becomes the app default.
    void restoreGuidesEnabled(bool enabled);
    QString guideEditSetId() const { return m_guideEditSetId; }
    void setGuideEditSetId(const QString &id);
    bool canvasCropMode() const { return m_canvasCropMode; }
    void setCanvasCropMode(bool active);
    bool clipCropMode() const { return m_clipCropMode; }
    void setClipCropMode(bool active);
    QString maskViewMode() const { return m_maskViewMode; }
    void setMaskViewMode(const QString &mode);
    bool maskEditMode() const { return m_maskEditMode; }
    void setMaskEditMode(bool active);
    QString maskingTool() const { return m_maskingTool; }
    void setMaskingTool(const QString &tool);
    bool maskEditActive() const;
    QString activeTool() const { return m_activeTool; }
    bool handlesVisible() const { return m_handlesVisible; }

    // Editor viewport navigation. Orbit is in degrees; pan in canvas px of pointer travel; dolly in wheel
    // steps (+ toward the target). Each re-renders the preview.
    Q_INVOKABLE void orbit(double dxDeg, double dyDeg);
    Q_INVOKABLE void pan(double dxCanvas, double dyCanvas);
    Q_INVOKABLE void dolly(double steps);
    // dolly that keeps the point under the pointer in place; (ndcX, ndcY) is the pointer in the
    // viewport, -1..1 with y up, and aspect its width / height.
    Q_INVOKABLE void zoomAt(double steps, double ndcX, double ndcY, double aspect);
    Q_INVOKABLE void toggleOrthographic() { setOrthographic(!orthographic()); }
    // Writes the editor view's eye and aim into the scene camera at the playhead (roll 0), keyed as
    // other camera writes are. False when no camera clip covers the playhead; one undo step.
    Q_INVOKABLE bool alignCameraToView();
    // Frames every clip at the playhead, the stage and the camera.
    Q_INVOKABLE void frameAll();
    // Fly mode step: turn in degrees (+right, +up), then move along the view's forward, its screen
    // right and the world's up, each in multiples of the view's distance to its target. Looking
    // through an active scene camera, the camera clip flies instead (in multiples of its lens),
    // keyed like a gizmo drag; one flight is one undo step.
    Q_INVOKABLE void fly(double turnRightDeg, double turnUpDeg, double forward, double right, double up);
    // Frames the selected clip (or the whole stage when nothing is selected).
    Q_INVOKABLE void frameSelection();
    // "front", "back", "left", "right", "top" or "bottom", looking exactly along that axis at the
    // current target.
    Q_INVOKABLE void setAxisView(const QString &axis);
    Q_INVOKABLE void toggleLookThrough();
    Q_INVOKABLE void resetView();
    // True while the pointer is orbiting, panning or dollying: the preview renders lighter.
    Q_INVOKABLE void setNavigating(bool navigating);
    // A viewport changed size: the editor view's frame, which fills it, changed with it.
    Q_INVOKABLE void notifyResized(int viewport);
    // For the corner axis widget: [{axis: "x"|"y"|"z", x, y, depth}] — each world axis's direction
    // on screen (y down) and how far it points toward the viewer, as unit-ish values.
    Q_INVOKABLE QVariantList axes() const;
    // In the editor viewport, selects the camera clip when (canvasX, canvasY) is on its drawn body, within
    // `tolerance` canvas px of its eye. False when it is not there.
    Q_INVOKABLE bool pickCamera(double canvasX, double canvasY, double tolerance);
    // In the editor viewport, the scene camera as a gizmo box (kind "camera"): a point at the eye
    // turned as the camera is, which gizmoGeometry/Pick/applyGizmoDrag move and turn. Empty for the
    // Camera viewport.
    Q_INVOKABLE QVariantMap cameraBox(int viewport) const;
    // True while a camera clip covers the playhead: the grips have to be drawn through the camera
    // rather than straight onto the canvas.
    Q_INVOKABLE bool cameraActive() const;
    // Whether any camera clip exists in the project, at any time: with one the scene has one eye.
    Q_INVOKABLE bool projectHasCamera() const;
    // The selected 3D layer's anchor point at the playhead: {localX/Y/Z} in its own frame, {worldX/Y/Z}
    // through the transform layers over it. Empty when it is not under the playhead.
    Q_INVOKABLE QVariantMap selectionPositions() const;
    // Every camera clip, in track order: {track, clip, name, start (s), active (under the playhead)}.
    Q_INVOKABLE QVariantList sceneCameras() const;
    // Puts a camera clip back at rest: no pan, dolly or turn, so it frames the canvas head on again.
    // The lens is kept. Without indexes, the camera covering the playhead. One undo step; false
    // when there is no such camera.
    Q_INVOKABLE bool resetSceneCamera(int trackIndex = -1, int clipIndex = -1);

    // The 3D gizmo for a clipsAtPlayhead box (or a live pose of the same shape), in overlay px at
    // `scale` overlay px per canvas px. `size` enlarges the handles for touch. Every call below
    // that takes `viewport` measures through that viewport's view (0 camera, 1 editor).
    // Geometry: {valid, origin:{x,y}, handles:[{id, kind, front:[[{x,y}…]…], back:[…], head:[…]}]}.
    Q_INVOKABLE QVariantMap gizmoGeometry(const QVariantMap &box, double scale, double size, int viewport = 0) const;
    Q_INVOKABLE QString gizmoPick(const QVariantMap &box, double scale, double size, double x,
                                  double y, double tolerance, int viewport = 0) const;
    // Drags `handle` from press to now (overlay px) starting at `start`, writes the result to the
    // clip the way the other preview setters do, and returns the new pose in the box's shape.
    Q_INVOKABLE QVariantMap applyGizmoDrag(const QVariantMap &start, const QString &handle,
                                           double pressX, double pressY, double nowX, double nowY,
                                           bool snap, double scale, int viewport = 0);
    // The 3D gizmo's frame for several 3D layers: a box (kind "group") at the centroid of their
    // centres in world space, unturned. Empty when none of the boxes can take part.
    Q_INVOKABLE QVariantMap groupGizmoPivot(const QVariantList &boxes, int viewport = 0) const;
    // applyGizmoDrag for the frame from groupGizmoPivot: the drag moves, turns or scales the
    // members as one body about the pivot, each from its box at the grab (startBoxes), and the
    // frame's new pose is returned. Scale handles all grow the group uniformly.
    Q_INVOKABLE QVariantMap applyGroupGizmoDrag(const QVariantMap &pivot, const QVariantList &startBoxes,
                                                const QString &handle, double pressX, double pressY,
                                                double nowX, double nowY, bool snap, double scale, int viewport = 0);
    // For a clipsAtPlayhead box with a 3D pose: the QtQuick transform of an item laid out at
    // (x, y, w, h) * scale in overlay px, placing its content where the clip renders.
    Q_INVOKABLE QMatrix4x4 clipPoseMatrix(const QVariantMap &box, double x, double y, double w,
                                          double h, double rotation, double scaleX,
                                          double scaleY, int viewport = 0) const;

    // A body drag of a clipsAtPlayhead box from press to now (canvas px), through its pose,
    // parents and the camera, so the grabbed point stays under the pointer. With `snap`, the
    // projected edges and centre pull to the canvas edges, centre lines and visible guides within
    // `tolerance` canvas px. Returns {valid, x, y, guideX, guideY} with the box's new rect origin;
    // guides are canvas px, -1 when not engaged.
    Q_INVOKABLE QVariantMap snapMove(const QVariantMap &box, double pressX, double pressY,
                                     double nowX, double nowY, double tolerance, bool snap,
                                     int viewport = 0) const;
    // A resize of the box (its rect as it stood at the grab) to `width` x `height`, with the moving
    // edges along `dxSign`/`dySign` (-1 left/top, +1 right/bottom, 0 fixed). `centrePivot` says the
    // rect grows about its centre rather than the opposite edge. Returns {width, height, guideX,
    // guideY, distX, distY}: each axis snapped on its own, with how far it was pulled (-1 if not).
    Q_INVOKABLE QVariantMap snapResize(const QVariantMap &box, double width, double height,
                                       int dxSign, int dySign, bool centrePivot,
                                       double tolerance, int viewport = 0) const;
    // Moves several clips as one body from where they stood at the grab (clipsAtPlayhead boxes), all
    // in canvas px: each clip's centre goes to pivot + R(rotationDeg) * S(scaleX, scaleY) * (centre -
    // pivot) + (dx, dy), its size follows the scale and its angle the rotation. Written through the
    // single-clip preview setters, so keyframing and blocked-transform notices behave the same.
    Q_INVOKABLE void applyGroupTransform(const QVariantList &startBoxes, double pivotX, double pivotY,
                                         double scaleX, double scaleY, double rotationDeg, double dx,
                                         double dy);
    Q_INVOKABLE QVariantList clipsAtPlayhead(int viewport = 0) const;
    // Topmost visible clip under a canvas point at the playhead, rotation-aware; empty if none.
    Q_INVOKABLE QVariantMap clipAtCanvasPoint(double canvasX, double canvasY, int viewport = 0) const;
    // Canvas px ↔ the box's own (child) canvas px, through its parent.
    Q_INVOKABLE QPointF mapToClipSpace(const QVariantMap &box, double x, double y) const;
    Q_INVOKABLE QPointF mapFromClipSpace(const QVariantMap &box, double x, double y) const;
    // The parent as an overlay-px to overlay-px matrix at `scale` overlay px per canvas px.
    Q_INVOKABLE QMatrix4x4 parentOverlayMatrix(const QVariantMap &box, double scale) const;

    // Asset drops on the preview, the counterpart of AppController's planAssetDrop/dropAsset:
    // plan only says what a drop would do, drop does exactly that.
    Q_INVOKABLE QVariantMap planDrop(const QString &kind, const QString &payload, double canvasX,
                                     double canvasY) const;
    Q_INVOKABLE QVariantMap dropAsset(const QString &kind, const QString &payload,
                                      const QString &label, double canvasX, double canvasY);

    // Everything the mask editor needs, resolved in one call so QML cannot get the three lookups
    // out of step: the host clip's rect at the playhead, and the mask layers on that track
    // covering it. Empty when the selection names no maskable track.
    Q_INVOKABLE QVariantMap maskEditorState() const;
    // The frame the masking overlays place against: the selection's mask target, with `inside`
    // telling whether that target is under the playhead (hasFrame follows it).
    Q_INVOKABLE QVariantMap maskingFrame() const;
    // maskingFrame for a given media clip rather than the selection: the frame trackers work in when
    // an adjustment layer is selected. Empty (hasFrame false) for an invalid clip.
    Q_INVOKABLE QVariantMap trackerFrameOf(int trackIndex, int clipIndex) const;
    // What the depth handles need, as one snapshot: the selected media clip's frame on the canvas
    // and every enabled depth effect with a handle (relight, depth of field) on its stack, with
    // parameters resolved at the playhead. Empty when there is nothing to show.
    Q_INVOKABLE QVariantMap depthEffectEditorState() const;

    // The selected clip's lights as the 3D view stands them, in canvas px: a model clip's own
    // lights, or 3D Relight's over a clip's relief. One {effect, light, prefix, x, y, z, zMin, zMax,
    // spot, aimX, aimY, color} per enabled light: `effect` is the Relight's index on the stack (-1
    // for a model's lights), `prefix` the keyframe key its parameters sit under ("fx.2.light1_",
    // "model3d.light1_"), and z the light's own near/far parameter within [zMin, zMax]. Empty
    // outside the editor viewport or when the clip has nothing to light.
    Q_INVOKABLE QVariantList depthLights3d() const;
    // A light dragged to canvas (x, y), kept at the distance from the view it was at, as the
    // light's {x, y, z} parameters. Empty when the light cannot be found.
    Q_INVOKABLE QVariantMap moveDepthLight3d(int effectIndex, int light, double canvasX, double canvasY) const;
    // Where a spot aimed at canvas (x, y) points, as its {aimX, aimY}: on the relief under the
    // pointer, or for a model on the plane through its centre. Empty when the pointer is off it.
    Q_INVOKABLE QVariantMap aimDepthLight3d(int effectIndex, double canvasX, double canvasY) const;

    // The viewpoint every preview overlay is drawn through, world -> homogeneous canvas px at
    // project scale: the free view for the editor viewport, else the scene camera when one covers
    // the playhead. False for the plain per-clip eye.
    bool viewProjection(QMatrix4x4 *view, int viewport) const;

signals:
    void viewport3dOpenChanged();
    // MCP or another non-UI caller wants a 3D viewport on screen.
    void viewport3dRequested();
    void flyingChanged();
    void viewChanged();
    void gizmoChanged();
    void navigationChanged();
    void guidesEnabledChanged();
    void guideEditSetIdChanged();
    void canvasCropModeChanged();
    void clipCropModeChanged();
    void maskEditModeChanged();
    void maskViewModeChanged();
    void maskingToolChanged();
    void maskEditActiveChanged();
    void activeToolChanged();
    void handlesVisibleChanged();

private:
    // The scene camera at the playhead in canvas pixels (renderScale 1, which is the space the
    // overlay measures in). `active` reports whether a camera clip covers the playhead at all;
    // when it does not, the camera comes back at rest and every caller keeps its old path.
    drift::SceneCamera3d sceneCamera(bool *active = nullptr) const;
    QSizeF projectSize() const;
    // A clipsAtPlayhead box as the snapping maths needs it, in canvas px.
    drift::snap::PlacedClip placedClip(const QVariantMap &box, int viewport) const;
    // The viewpoint the gizmo is drawn and solved from: the scene camera when one covers the
    // playhead, otherwise none (each clip's own eye).
    drift::gizmo::View gizmoView(int viewport) const;
    // A clip's frame at the playhead: rect, rotation and pose with keyframes evaluated, and its parent transform.
    QVariantMap clipFrameMap(int trackIndex, int clipIndex) const;
    // The selected clip's relief in 3D mode, as the compositor lays it out. False when there is none.
    struct SelectedRelief;
    bool selectedRelief(SelectedRelief *out, int viewport = kEditorViewport) const;
    // The selected model clip's lights in 3D mode, as the compositor places them.
    struct SelectedModelLights;
    bool selectedModelLights(SelectedModelLights *out, int viewport = kEditorViewport) const;
    // Hands the editor viewport's viewpoint to the Editor stream's renderer.
    void pushView();
    void pushMaskView();
    // Looking through the camera with it locked to the view: runs `step` on the camera as a view and
    // writes the result into the camera clip. False when that is not the case (or nothing to write).
    bool navigateCamera(const std::function<drift::EditorView3d(const drift::EditorView3d &)> &step);
    bool writeSceneCamera(int trackIndex, int clipIndex, const drift::SceneCamera3d &next, const QString &undoText);
    // The selection's centre and a radius around it, in world px. False with nothing selected.
    bool selectionSphere(QVector3D *centre, double *radius) const;
    drift::EditorView3d overviewView() const;
    void flyCamera(int trackIndex, int clipIndex, double turnRightDeg, double turnUpDeg, double forward,
                   double right, double up);
    void setView(const drift::EditorView3d &view);
    void updateActiveTool();
    void updateHandlesVisible();
    void refreshOutliner();

    AppController &m_app;
    // The editor viewport's own state; the Camera viewport has none, it is the scene camera.
    struct ViewportState {
        drift::EditorView3d view;
        bool placed = false;
        quint64 serial = 0;
        bool navigating = false;
        bool flying = false;
    };
    ViewportState m_editor;
    Outliner3dModel *m_outliner = nullptr;
    bool m_viewport3dOpen = false;
    // This flight has written the camera clip (so landing commits it), or was refused a write
    // (so the "turn on auto keyframes" notice shows once, not every frame).
    bool m_flyEdited = false;
    bool m_flyBlocked = false;
    QString m_navigationScheme = QStringLiteral("blender");
    bool m_orbitAroundSelection = true;
    bool m_lockCameraToView = false;
    double m_editorFov = 32.0;
    bool m_showGrid = true;
    double m_gridSize = 0.0;
    double m_translateSnap = 0.0;
    double m_rotationSnap = 15.0;
    QString m_gizmoTool = QStringLiteral("move");
    QString m_gizmoOrientation = QStringLiteral("world");
    bool m_guidesEnabled = false;
    QString m_guideEditSetId;
    bool m_canvasCropMode = false;
    bool m_clipCropMode = false;
    bool m_maskEditMode = false;
    QString m_maskViewMode = QStringLiteral("off");
    QString m_maskingTool;
    QString m_activeTool = QStringLiteral("transform");
    bool m_handlesVisible = false;
};
