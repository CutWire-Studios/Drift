#pragma once

#include "engine/EditorView3d.h"
#include "engine/SceneCamera3d.h"

#include <QMatrix4x4>
#include <QObject>
#include <QPointF>
#include <QVariantList>
#include <QVariantMap>

class AppController;

namespace drift::snap {
struct PlacedClip;
}
namespace drift::gizmo {
struct View;
}

// The preview area's editor state and the maths its overlays run on: the 2D/3D mode and the free
// 3D viewpoint, the gizmo, snapping, drops, and which interaction tool owns the pointer. Session
// state, never saved with the project (guidesEnabled aside, which AppController saves for it).
// Reached from QML as EditorState.preview. Clip edits still go through AppController's
// previewSet* / beginPreviewDrag / commitPreviewDrag transaction.
class PreviewController : public QObject
{
    Q_OBJECT

    // "2d" shows the camera's picture, "3d" the world from a free viewpoint with the camera drawn
    // in it.
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    // In 3D mode: looking through the scene camera rather than from the free viewpoint.
    Q_PROPERTY(bool lookThrough READ lookThrough NOTIFY viewChanged)
    // Bumped whenever the 3D viewpoint moves, so overlays drawn through it re-read their geometry.
    Q_PROPERTY(int viewRevision READ viewRevision NOTIFY viewChanged)
    // The 3D gizmo on a clip that is a 3D layer: which tool ("move", "rotate", "scale") and whether
    // its handles follow the camera's axes ("global") or the clip's own ("local").
    Q_PROPERTY(QString gizmoTool READ gizmoTool WRITE setGizmoTool NOTIFY gizmoChanged)
    Q_PROPERTY(QString gizmoOrientation READ gizmoOrientation WRITE setGizmoOrientation NOTIFY gizmoChanged)
    Q_PROPERTY(bool guidesEnabled READ guidesEnabled WRITE setGuidesEnabled NOTIFY guidesEnabledChanged)
    // Library set whose guides are being dragged on the preview; empty when not editing.
    Q_PROPERTY(QString guideEditSetId READ guideEditSetId WRITE setGuideEditSetId NOTIFY guideEditSetIdChanged)
    Q_PROPERTY(bool canvasCropMode READ canvasCropMode WRITE setCanvasCropMode NOTIFY canvasCropModeChanged)
    Q_PROPERTY(bool maskEditMode READ maskEditMode WRITE setMaskEditMode NOTIFY maskEditModeChanged)
    // Whether the preview should be showing mask handles right now. Selecting a mask clip is
    // itself a request to edit it, so the toggle is only needed to keep the handles up while some
    // *other* clip is selected.
    Q_PROPERTY(bool maskEditActive READ maskEditActive NOTIFY maskEditActiveChanged)
    // The one tool that owns the preview's pointer: "crop", "guideEdit", "mask" or "transform", in
    // that order of precedence. Masks are a 2D tool, so 3D falls through to "transform".
    Q_PROPERTY(QString activeTool READ activeTool NOTIFY activeToolChanged)
    // Grips are drawn only while the picture holds still and there is a canvas to put them on.
    Q_PROPERTY(bool handlesVisible READ handlesVisible NOTIFY handlesVisibleChanged)

public:
    explicit PreviewController(AppController &app, QObject *parent = nullptr);
    ~PreviewController() override;

    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    bool lookThrough() const { return m_view.lookThrough; }
    int viewRevision() const { return int(m_viewSerial); }
    QString gizmoTool() const { return m_gizmoTool; }
    void setGizmoTool(const QString &tool);
    QString gizmoOrientation() const { return m_gizmoOrientation; }
    void setGizmoOrientation(const QString &orientation);
    bool guidesEnabled() const { return m_guidesEnabled; }
    void setGuidesEnabled(bool enabled);
    // A loaded project's saved value: neither dirties the project nor becomes the app default.
    void restoreGuidesEnabled(bool enabled);
    QString guideEditSetId() const { return m_guideEditSetId; }
    void setGuideEditSetId(const QString &id);
    bool canvasCropMode() const { return m_canvasCropMode; }
    void setCanvasCropMode(bool active);
    bool maskEditMode() const { return m_maskEditMode; }
    void setMaskEditMode(bool active);
    bool maskEditActive() const;
    QString activeTool() const { return m_activeTool; }
    bool handlesVisible() const { return m_handlesVisible; }

    // 3D mode navigation. Orbit is in degrees; pan in canvas px of pointer travel; dolly in wheel
    // steps (+ toward the target). Each re-renders the preview.
    Q_INVOKABLE void orbit(double dxDeg, double dyDeg);
    Q_INVOKABLE void pan(double dxCanvas, double dyCanvas);
    Q_INVOKABLE void dolly(double steps);
    // Frames the selected clip (or the whole stage when nothing is selected).
    Q_INVOKABLE void frameSelection();
    // "front", "back", "left", "right", "top" or "bottom", looking at the current target.
    Q_INVOKABLE void setAxisView(const QString &axis);
    Q_INVOKABLE void toggleLookThrough();
    Q_INVOKABLE void resetView();
    // True while the pointer is orbiting, panning or dollying: the preview renders lighter.
    Q_INVOKABLE void setNavigating(bool navigating);
    // The preview panel changed size: the 3D view's frame, which fills it, changed with it.
    Q_INVOKABLE void notifyResized();
    // For the corner axis widget: [{axis: "x"|"y"|"z", x, y, depth}] — each world axis's direction
    // on screen (y down) and how far it points toward the viewer, as unit-ish values.
    Q_INVOKABLE QVariantList axes() const;
    // In 3D mode, selects the camera clip when (canvasX, canvasY) is on its drawn body, within
    // `tolerance` canvas px of its eye. False when it is not there.
    Q_INVOKABLE bool pickCamera(double canvasX, double canvasY, double tolerance);
    // In 3D mode, the scene camera as a gizmo box (kind "camera"): a point at the eye turned as the
    // camera is, which gizmoGeometry/Pick/applyGizmoDrag move and turn. Empty otherwise.
    Q_INVOKABLE QVariantMap cameraBox() const;
    // True while a camera clip covers the playhead: the grips have to be drawn through the camera
    // rather than straight onto the canvas.
    Q_INVOKABLE bool cameraActive() const;
    // Puts a camera clip back at rest: no pan, dolly or turn, so it frames the canvas head on again.
    // The lens is kept. Without indexes, the camera covering the playhead. One undo step; false
    // when there is no such camera.
    Q_INVOKABLE bool resetSceneCamera(int trackIndex = -1, int clipIndex = -1);

    // The 3D gizmo for a clipsAtPlayhead box (or a live pose of the same shape), in overlay px at
    // `scale` overlay px per canvas px. `size` enlarges the handles for touch.
    // Geometry: {valid, origin:{x,y}, handles:[{id, kind, front:[[{x,y}…]…], back:[…], head:[…]}]}.
    Q_INVOKABLE QVariantMap gizmoGeometry(const QVariantMap &box, double scale, double size) const;
    Q_INVOKABLE QString gizmoPick(const QVariantMap &box, double scale, double size, double x,
                                  double y, double tolerance) const;
    // Drags `handle` from press to now (overlay px) starting at `start`, writes the result to the
    // clip the way the other preview setters do, and returns the new pose in the box's shape.
    Q_INVOKABLE QVariantMap applyGizmoDrag(const QVariantMap &start, const QString &handle,
                                           double pressX, double pressY, double nowX, double nowY,
                                           bool snap, double scale);
    // For a clipsAtPlayhead box with a 3D pose: the QtQuick transform of an item laid out at
    // (x, y, w, h) * scale in overlay px, placing its content where the clip renders.
    Q_INVOKABLE QMatrix4x4 clipPoseMatrix(const QVariantMap &box, double x, double y, double w,
                                          double h, double rotation, double scaleX,
                                          double scaleY) const;

    // A body drag of a clipsAtPlayhead box from press to now (canvas px), through its pose,
    // parents and the camera, so the grabbed point stays under the pointer. With `snap`, the
    // projected edges and centre pull to the canvas edges, centre lines and visible guides within
    // `tolerance` canvas px. Returns {valid, x, y, guideX, guideY} with the box's new rect origin;
    // guides are canvas px, -1 when not engaged.
    Q_INVOKABLE QVariantMap snapMove(const QVariantMap &box, double pressX, double pressY,
                                     double nowX, double nowY, double tolerance, bool snap) const;
    // A resize of the box (its rect as it stood at the grab) to `width` x `height`, with the moving
    // edges along `dxSign`/`dySign` (-1 left/top, +1 right/bottom, 0 fixed). `centrePivot` says the
    // rect grows about its centre rather than the opposite edge. Returns {width, height, guideX,
    // guideY, distX, distY}: each axis snapped on its own, with how far it was pulled (-1 if not).
    Q_INVOKABLE QVariantMap snapResize(const QVariantMap &box, double width, double height,
                                       int dxSign, int dySign, bool centrePivot,
                                       double tolerance) const;
    Q_INVOKABLE QVariantList clipsAtPlayhead() const;
    // Topmost visible clip under a canvas point at the playhead, rotation-aware; empty if none.
    Q_INVOKABLE QVariantMap clipAtCanvasPoint(double canvasX, double canvasY) const;
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
    // What the depth handles need, as one snapshot: the selected media clip's frame on the canvas
    // and every enabled depth effect with a handle (relight, depth of field) on its stack, with
    // parameters resolved at the playhead. Empty when there is nothing to show.
    Q_INVOKABLE QVariantMap depthEffectEditorState() const;

    // The viewpoint every preview overlay is drawn through, world -> homogeneous canvas px at
    // project scale: the free view in 3D mode, else the scene camera when one covers the playhead.
    // False for the plain per-clip eye.
    bool viewProjection(QMatrix4x4 *view) const;

signals:
    void modeChanged();
    void viewChanged();
    void gizmoChanged();
    void guidesEnabledChanged();
    void guideEditSetIdChanged();
    void canvasCropModeChanged();
    void maskEditModeChanged();
    void maskEditActiveChanged();
    void activeToolChanged();
    void handlesVisibleChanged();

private:
    // The scene camera at the playhead in canvas pixels (renderScale 1, which is the space the
    // overlay measures in). `active` reports whether a camera clip covers the playhead at all;
    // when it does not, the camera comes back at rest and every caller keeps its old path.
    drift::SceneCamera3d sceneCamera(bool *active = nullptr) const;
    // A clipsAtPlayhead box as the snapping maths needs it, in canvas px.
    drift::snap::PlacedClip placedClip(const QVariantMap &box) const;
    // The viewpoint the gizmo is drawn and solved from: the scene camera when one covers the
    // playhead, otherwise none (each clip's own eye).
    drift::gizmo::View gizmoView() const;
    // Hands the current mode and viewpoint to the preview renderer.
    void pushView();
    void setView(const drift::EditorView3d &view);
    void updateActiveTool();
    void updateHandlesVisible();

    AppController &m_app;
    QString m_mode = QStringLiteral("2d");
    drift::EditorView3d m_view;
    bool m_viewPlaced = false;
    quint64 m_viewSerial = 0;
    bool m_navigating = false;
    QString m_gizmoTool = QStringLiteral("move");
    QString m_gizmoOrientation = QStringLiteral("global");
    bool m_guidesEnabled = false;
    QString m_guideEditSetId;
    bool m_canvasCropMode = false;
    bool m_maskEditMode = false;
    QString m_activeTool = QStringLiteral("transform");
    bool m_handlesVisible = false;
};
