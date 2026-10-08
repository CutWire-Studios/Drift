#include "PreviewController.h"

#include "core/TimelineOps.h"
#include "engine/ClipGizmo.h"
#include "engine/ClipTransform3d.h"
#include "engine/EffectCatalog.h"
#include "engine/PreviewSnap.h"
#include "engine/TransformLayer.h"
#include "models/AppController.h"
#include "models/AppControllerDetail.h"

#include <QLineF>
#include <QPolygonF>
#include <QSettings>
#include <QVector4D>

#include <cmath>

using namespace drift::appdetail;

namespace {

drift::ClipPose3d previewBoxPose(const QVariantMap &box)
{
    drift::ClipPose3d pose;
    pose.rotationX = box.value(QStringLiteral("rotationX")).toDouble();
    pose.rotationY = box.value(QStringLiteral("rotationY")).toDouble();
    pose.positionZ = box.value(QStringLiteral("z")).toDouble();
    pose.perspective =
        box.value(QStringLiteral("perspective"), drift::kDefaultClipPerspective).toDouble();
    return pose;
}

drift::gizmo::Pose gizmoPoseFromBox(const QVariantMap &box)
{
    drift::gizmo::Pose pose;
    pose.rect = QRectF(box.value(QStringLiteral("x")).toDouble(), box.value(QStringLiteral("y")).toDouble(),
                       box.value(QStringLiteral("width")).toDouble(),
                       box.value(QStringLiteral("height")).toDouble());
    pose.rotation = box.value(QStringLiteral("rotation")).toDouble();
    pose.pose3d = previewBoxPose(box);
    pose.canvas = QSizeF(box.value(QStringLiteral("canvasWidth")).toDouble(),
                         box.value(QStringLiteral("canvasHeight")).toDouble());
    pose.parent = previewBoxParent(box);
    return pose;
}

QVariantList polylineToVariant(const QPolygonF &line)
{
    QVariantList out;
    out.reserve(line.size());
    for (const QPointF &p : line)
        out.append(p);
    return out;
}

drift::snap::Targets canvasSnapTargets(double width, double height, const QVariantMap &guides)
{
    drift::snap::Targets targets;
    targets.x = {0.0, width / 2.0, width};
    targets.y = {0.0, height / 2.0, height};
    for (const QVariant &v : guides.value(QStringLiteral("x")).toList())
        targets.x << v.toDouble();
    for (const QVariant &v : guides.value(QStringLiteral("y")).toList())
        targets.y << v.toDouble();
    return targets;
}

} // namespace

PreviewController::PreviewController(AppController &app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    QSettings settings;
    m_guidesEnabled = settings.value(QStringLiteral("preview/guidesEnabled"), false).toBool();
    m_gizmoTool = settings.value(QStringLiteral("preview/gizmoTool"), m_gizmoTool).toString();
    m_gizmoOrientation =
        settings.value(QStringLiteral("preview/gizmoOrientation"), m_gizmoOrientation).toString();

    // Selecting a mask clip turns the handles on, so anything that can change what is selected —
    // or move a mask out from under the playhead — has to re-ask.
    connect(&m_app, &AppController::selectionChanged, this, &PreviewController::maskEditActiveChanged);
    connect(&m_app, &AppController::tracksChanged, this, &PreviewController::maskEditActiveChanged);
    connect(this, &PreviewController::maskEditModeChanged, this, &PreviewController::maskEditActiveChanged);
    // The 3D view draws the selection's box and the camera's frame on it.
    connect(&m_app, &AppController::selectionChanged, this, [this] {
        if (m_mode != QLatin1String("3d"))
            return;
        ++m_viewSerial;
        pushView();
    });

    connect(this, &PreviewController::modeChanged, this, &PreviewController::updateActiveTool);
    connect(this, &PreviewController::canvasCropModeChanged, this, &PreviewController::updateActiveTool);
    connect(this, &PreviewController::guideEditSetIdChanged, this, &PreviewController::updateActiveTool);
    connect(this, &PreviewController::maskEditActiveChanged, this, &PreviewController::updateActiveTool);

    connect(&m_app, &AppController::playingChanged, this, &PreviewController::updateHandlesVisible);
    connect(&m_app, &AppController::scrubbingChanged, this, &PreviewController::updateHandlesVisible);
    connect(&m_app, &AppController::tracksChanged, this, &PreviewController::updateHandlesVisible);
    updateHandlesVisible();
}

PreviewController::~PreviewController() = default;

void PreviewController::restoreGuidesEnabled(bool enabled)
{
    if (m_guidesEnabled == enabled)
        return;
    m_guidesEnabled = enabled;
    emit guidesEnabledChanged();
}

void PreviewController::updateActiveTool()
{
    QString tool = QStringLiteral("transform");
    if (m_canvasCropMode)
        tool = QStringLiteral("crop");
    else if (!m_guideEditSetId.isEmpty())
        tool = QStringLiteral("guideEdit");
    else if (maskEditActive() && m_mode != QLatin1String("3d"))
        tool = QStringLiteral("mask");
    if (tool == m_activeTool)
        return;
    m_activeTool = tool;
    emit activeToolChanged();
}

void PreviewController::updateHandlesVisible()
{
    const bool visible = !m_app.playing() && !m_app.scrubbing() && m_app.projectWidth() > 0;
    if (visible == m_handlesVisible)
        return;
    m_handlesVisible = visible;
    emit handlesVisibleChanged();
}

void PreviewController::setGuidesEnabled(bool enabled)
{
    if (m_guidesEnabled == enabled)
        return;
    m_guidesEnabled = enabled;
    if (!enabled)
        setGuideEditSetId(QString());
    QSettings settings;
    settings.setValue(QStringLiteral("preview/guidesEnabled"), m_guidesEnabled);
    m_app.setDirty(true);
    emit guidesEnabledChanged();
}

void PreviewController::setGizmoTool(const QString &tool)
{
    if (m_gizmoTool == tool
        || (tool != QLatin1String("move") && tool != QLatin1String("rotate")
            && tool != QLatin1String("scale")))
        return;
    m_gizmoTool = tool;
    QSettings().setValue(QStringLiteral("preview/gizmoTool"), m_gizmoTool);
    emit gizmoChanged();
}

void PreviewController::setGizmoOrientation(const QString &orientation)
{
    if (m_gizmoOrientation == orientation
        || (orientation != QLatin1String("global") && orientation != QLatin1String("local")))
        return;
    m_gizmoOrientation = orientation;
    QSettings().setValue(QStringLiteral("preview/gizmoOrientation"), m_gizmoOrientation);
    emit gizmoChanged();
}

void PreviewController::setGuideEditSetId(const QString &id)
{
    if (m_guideEditSetId == id || (!id.isEmpty() && !m_app.libraryGuideSet(id)))
        return;
    m_guideEditSetId = id;
    // Guide editing claims the preview's pointer like crop and mask editing, and the set
    // being edited has to be on screen.
    if (!id.isEmpty()) {
        setCanvasCropMode(false);
        setMaskEditMode(false);
        setGuidesEnabled(true);
        m_app.setGuideSetActive(id, true);
    }
    emit guideEditSetIdChanged();
}

QVariantMap PreviewController::gizmoGeometry(const QVariantMap &box, double scale, double size) const
{
    using namespace drift::gizmo;
    // A camera has no size to scale; the lens is the inspector's.
    if (box.value(QStringLiteral("kind")).toString() == QLatin1String("camera")
        && toolFromString(m_gizmoTool) == Tool::Scale)
        return {{QStringLiteral("valid"), false}};
    const Geometry g = geometry(gizmoPoseFromBox(box), toolFromString(m_gizmoTool),
                                orientationFromString(m_gizmoOrientation), scale, size,
                                gizmoView());
    QVariantList handles;
    for (const Handle &h : g.handles) {
        QVariantList front;
        for (const QPolygonF &line : h.front)
            front.append(QVariant(polylineToVariant(line)));
        QVariantList back;
        for (const QPolygonF &line : h.back)
            back.append(QVariant(polylineToVariant(line)));
        static const char *kinds[] = {"arrow", "dolly", "ring", "scale", "uniform"};
        handles.append(QVariantMap{
            {QStringLiteral("id"), h.id},
            {QStringLiteral("kind"), QString::fromLatin1(kinds[int(h.kind)])},
            {QStringLiteral("front"), front},
            {QStringLiteral("back"), back},
            {QStringLiteral("head"), polylineToVariant(h.head)},
        });
    }
    return {
        {QStringLiteral("valid"), g.valid},
        {QStringLiteral("origin"), g.origin},
        {QStringLiteral("handles"), handles},
    };
}

QString PreviewController::gizmoPick(const QVariantMap &box, double scale, double size, double x,
                                        double y, double tolerance) const
{
    using namespace drift::gizmo;
    if (box.value(QStringLiteral("kind")).toString() == QLatin1String("camera")
        && toolFromString(m_gizmoTool) == Tool::Scale)
        return {};
    const Geometry g = geometry(gizmoPoseFromBox(box), toolFromString(m_gizmoTool),
                                orientationFromString(m_gizmoOrientation), scale, size,
                                gizmoView());
    return pick(g, QPointF(x, y), tolerance);
}

QVariantMap PreviewController::applyGizmoDrag(const QVariantMap &start, const QString &handle,
                                                 double pressX, double pressY, double nowX,
                                                 double nowY, bool snap, double scale)
{
    using namespace drift::gizmo;
    const int trackIndex = start.value(QStringLiteral("track")).toInt();
    const int clipIndex = start.value(QStringLiteral("clip")).toInt();
    if (!m_app.isValidClipIndex(trackIndex, clipIndex))
        return start;
    drift::Clip &clip = m_app.m_project.tracks()[trackIndex].clips[clipIndex];

    const Tool tool = toolFromString(m_gizmoTool);
    const DragResult result = drag(gizmoPoseFromBox(start), tool, orientationFromString(m_gizmoOrientation),
                                   handle, QPointF(pressX, pressY), QPointF(nowX, nowY), snap, scale,
                                   gizmoView());
    const Pose &pose = result.pose;

    if (start.value(QStringLiteral("kind")).toString() == QLatin1String("camera")) {
        if (!drift::isCameraClip(clip) || tool == Tool::Scale)
            return start;
        // The gizmo moved or turned the eye. The camera stores C = R · translate(P) with the eye at
        // C · (0, 0, d), so the position that puts the eye there under the new turn is
        // P = R⁻¹ · eye - (0, 0, d).
        const QVector3D eye(float(pose.rect.center().x() - pose.canvas.width() * 0.5),
                            float(pose.rect.center().y() - pose.canvas.height() * 0.5),
                            float(pose.pose3d.positionZ));
        QMatrix4x4 turn;
        turn.rotate(float(pose.pose3d.rotationX), 1.f, 0.f, 0.f);
        turn.rotate(float(pose.pose3d.rotationY), 0.f, 1.f, 0.f);
        turn.rotate(float(pose.rotation), 0.f, 0.f, 1.f);
        const double lens = std::max(1.0, start.value(QStringLiteral("perspective")).toDouble());
        const QVector3D position =
            turn.transposed().map(eye) - QVector3D(0.f, 0.f, float(lens));

        m_app.beginImplicitPreviewDrag(tool == Tool::Move ? AppController::tr("Move camera") : AppController::tr("Rotate camera"));
        const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_app.m_playheadUs - clip.timelineStart);
        bool wrote = false;
        QStringList keys;
        const auto write = [&](drift::KeyframeTrack<double> &track, double value, const QString &key) {
            if (writeKeyframeValue(track, relative, value, m_app.m_autoKeyEnabled, false)) {
                wrote = true;
                keys << key;
            }
        };
        write(clip.transformX, position.x(), QStringLiteral("x"));
        write(clip.transformY, position.y(), QStringLiteral("y"));
        write(clip.positionZ, position.z(), QStringLiteral("z"));
        if (tool == Tool::Rotate) {
            write(clip.rotationX, pose.pose3d.rotationX, QStringLiteral("rotationX"));
            write(clip.rotationY, pose.pose3d.rotationY, QStringLiteral("rotationY"));
            write(clip.rotation, pose.rotation, QStringLiteral("rotation"));
        }
        if (!wrote) {
            emit m_app.transformBlocked(AppController::tr("Turn on Auto keyframes to change this"));
            return start;
        }
        m_app.emitPreviewEdit(trackIndex, clipIndex, keys);
        QVariantMap out = start;
        out.insert(QStringLiteral("x"), pose.rect.x());
        out.insert(QStringLiteral("y"), pose.rect.y());
        out.insert(QStringLiteral("z"), pose.pose3d.positionZ);
        out.insert(QStringLiteral("rotationX"), pose.pose3d.rotationX);
        out.insert(QStringLiteral("rotationY"), pose.pose3d.rotationY);
        out.insert(QStringLiteral("rotation"), pose.rotation);
        return out;
    }

    m_app.beginImplicitPreviewDrag(tool == Tool::Move     ? AppController::tr("Move clip in 3D")
                             : tool == Tool::Rotate ? AppController::tr("Rotate clip in 3D")
                                                    : AppController::tr("Scale clip"));
    const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_app.m_playheadUs - clip.timelineStart);
    bool wrote = false;
    QStringList keys;
    const auto write = [&](drift::KeyframeTrack<double> &track, double value, const QString &key) {
        if (writeKeyframeValue(track, relative, value, m_app.m_autoKeyEnabled, false)) {
            wrote = true;
            keys << key;
        }
    };
    if (tool == Tool::Rotate) {
        write(clip.rotationX, pose.pose3d.rotationX, QStringLiteral("rotationX"));
        write(clip.rotationY, pose.pose3d.rotationY, QStringLiteral("rotationY"));
        write(clip.rotation, pose.rotation, QStringLiteral("rotation"));
    } else {
        write(clip.transformX, pose.rect.x(), QStringLiteral("x"));
        write(clip.transformY, pose.rect.y(), QStringLiteral("y"));
        if (tool == Tool::Move) {
            write(clip.positionZ, pose.pose3d.positionZ, QStringLiteral("z"));
        } else {
            write(clip.transformW, pose.rect.width(), QStringLiteral("width"));
            write(clip.transformH, pose.rect.height(), QStringLiteral("height"));
        }
    }
    // Scaling a text box both ways scales what it shows, as the 2D corner grips do.
    const bool isText = clip.type == drift::ClipType::Text || clip.type == drift::ClipType::Subtitle;
    if (isText && handle == QLatin1String("xy")) {
        const int pixelSize = qBound(
            8, qRound(start.value(QStringLiteral("pixelSize"), 64).toDouble() * result.uniformFactor), 800);
        if (clip.textStyle.pixelSize != pixelSize) {
            clip.textStyle.pixelSize = pixelSize;
            wrote = true;
            keys << QStringLiteral("text.pixelSize");
        }
    }
    if (!wrote) {
        emit m_app.transformBlocked(AppController::tr("Turn on Auto keyframes to change this"));
        return start;
    }
    clip.layer3d = true;
    m_app.emitPreviewEdit(trackIndex, clipIndex, keys);

    QVariantMap out = start;
    out.insert(QStringLiteral("x"), pose.rect.x());
    out.insert(QStringLiteral("y"), pose.rect.y());
    out.insert(QStringLiteral("width"), pose.rect.width());
    out.insert(QStringLiteral("height"), pose.rect.height());
    out.insert(QStringLiteral("rotation"), pose.rotation);
    out.insert(QStringLiteral("rotationX"), pose.pose3d.rotationX);
    out.insert(QStringLiteral("rotationY"), pose.pose3d.rotationY);
    out.insert(QStringLiteral("z"), pose.pose3d.positionZ);
    return out;
}

drift::gizmo::View PreviewController::gizmoView() const
{
    drift::gizmo::View view;
    view.valid = viewProjection(&view.worldToCanvas);
    return view;
}

bool PreviewController::viewProjection(QMatrix4x4 *view) const
{
    const QSizeF canvas(m_app.m_project.width(), m_app.m_project.height());
    bool cameraActive = false;
    const drift::SceneCamera3d camera = sceneCamera(&cameraActive);
    if (m_mode == QLatin1String("3d")) {
        // The view renders the panel's frame; overlays measure from the project canvas centred in
        // it, so the same picture is shifted back by where that canvas sits.
        const drift::EditorFrame frame = drift::editorFrame(canvas, QSizeF(m_app.m_playback.previewRenderSize()));
        QMatrix4x4 shift;
        shift.translate(float(-frame.offset.x()), float(-frame.offset.y()));
        *view = shift * drift::editorViewProjection(m_view, frame.size, camera);
        return true;
    }
    if (!cameraActive)
        return false;
    *view = drift::cameraViewProjection(camera, canvas);
    return true;
}

void PreviewController::pushView()
{
    FrameCompositor::RenderOptions::EditorView request;
    request.active = m_mode == QLatin1String("3d");
    request.view = m_view;
    request.serial = m_viewSerial;
    if (m_app.isValidClipIndex(m_app.m_selectedTrack, m_app.m_selectedClip))
        request.selectedClipId = m_app.m_project.tracks().at(m_app.m_selectedTrack).clips.at(m_app.m_selectedClip).id;
    request.navigating = m_navigating;
    m_app.m_playback.setEditorView(request);
}

void PreviewController::setView(const drift::EditorView3d &view)
{
    m_view = view;
    ++m_viewSerial;
    pushView();
    emit viewChanged();
}

void PreviewController::setMode(const QString &mode)
{
    const QString next = mode == QLatin1String("3d") ? QStringLiteral("3d") : QStringLiteral("2d");
    if (next == m_mode)
        return;
    m_mode = next;
    // Cropping is a 2D tool: entering 3D leaves it, as entering it leaves 3D.
    if (next == QLatin1String("3d"))
        setCanvasCropMode(false);
    if (next == QLatin1String("3d") && !m_viewPlaced) {
        m_viewPlaced = true;
        m_view = drift::EditorView3d::overview(QSizeF(m_app.m_project.width(), m_app.m_project.height()));
    }
    ++m_viewSerial;
    pushView();
    emit modeChanged();
    emit viewChanged();
}

void PreviewController::orbit(double dxDeg, double dyDeg)
{
    setView(drift::orbitEditorView(m_view, dxDeg, dyDeg));
}

void PreviewController::pan(double dxCanvas, double dyCanvas)
{
    const drift::EditorFrame frame = drift::editorFrame(QSizeF(m_app.m_project.width(), m_app.m_project.height()),
                                                        QSizeF(m_app.m_playback.previewRenderSize()));
    setView(drift::panEditorView(m_view, dxCanvas, dyCanvas, frame.size));
}

void PreviewController::dolly(double steps)
{
    setView(drift::dollyEditorView(m_view, steps));
}

void PreviewController::frameSelection()
{
    const QSizeF canvas(m_app.m_project.width(), m_app.m_project.height());
    QVector3D centre;
    double radius = 0.5 * std::hypot(canvas.width(), canvas.height());
    if (m_app.isValidClipIndex(m_app.m_selectedTrack, m_app.m_selectedClip)) {
        const drift::Clip &clip = m_app.m_project.tracks().at(m_app.m_selectedTrack).clips.at(m_app.m_selectedClip);
        const drift::TimeUs relative = qMax<drift::TimeUs>(0, m_app.m_playheadUs - clip.timelineStart);
        const double w = clipTransformValue(clip.transformW, relative, canvas.width());
        const double h = clipTransformValue(clip.transformH, relative, canvas.height());
        if (clip.adjustmentKind == drift::AdjustmentKind::Camera && clip.type == drift::ClipType::Adjustment) {
            const drift::SceneCamera3d camera = drift::sceneCameraFromClip(clip, m_app.m_playheadUs, 1.0);
            centre = drift::sceneCameraEye(camera);
            radius = camera.perspective * 0.5;
        } else {
            centre = QVector3D(float(clipTransformValue(clip.transformX, relative, 0.0) + w / 2.0
                                     - canvas.width() / 2.0),
                               float(clipTransformValue(clip.transformY, relative, 0.0) + h / 2.0
                                     - canvas.height() / 2.0),
                               float(clipTransformValue(clip.positionZ, relative, 0.0)));
            radius = 0.5 * std::hypot(w, h);
        }
    }
    setView(drift::frameEditorView(m_view, centre, std::max(1.0, radius)));
}

void PreviewController::setAxisView(const QString &axis)
{
    drift::EditorView3d view = m_view;
    view.lookThrough = false;
    view.yaw = 0.0;
    view.pitch = 0.0;
    if (axis == QLatin1String("back"))
        view.yaw = 180.0;
    else if (axis == QLatin1String("right"))
        view.yaw = 90.0;
    else if (axis == QLatin1String("left"))
        view.yaw = -90.0;
    else if (axis == QLatin1String("top"))
        view.pitch = 89.0;
    else if (axis == QLatin1String("bottom"))
        view.pitch = -89.0;
    setView(view);
}

void PreviewController::toggleLookThrough()
{
    drift::EditorView3d view = m_view;
    view.lookThrough = !view.lookThrough;
    setView(view);
}

void PreviewController::notifyResized()
{
    if (m_mode != QLatin1String("3d"))
        return;
    // The view's frame follows the panel, so everything drawn through it moves.
    ++m_viewSerial;
    pushView();
    emit viewChanged();
}

void PreviewController::setNavigating(bool navigating)
{
    if (m_navigating == navigating)
        return;
    m_navigating = navigating;
    ++m_viewSerial;
    pushView();
}

void PreviewController::resetView()
{
    setView(drift::EditorView3d::overview(QSizeF(m_app.m_project.width(), m_app.m_project.height())));
}

QVariantList PreviewController::axes() const
{
    QVariantList out;
    QMatrix4x4 view;
    if (!viewProjection(&view))
        return out;
    const QVector3D origin = m_view.lookThrough ? QVector3D() : m_view.target;
    const QVector3D eye = drift::gizmo::eyeOf(view);
    const QVector3D toEye = (eye - origin).normalized();
    const float reach = float(std::max(1.0, double((eye - origin).length())) * 0.05);
    const auto project = [&view](const QVector3D &p, QPointF *out) {
        const QVector4D h = view.map(QVector4D(p, 1.f));
        if (h.w() <= 1e-6f)
            return false;
        *out = QPointF(h.x() / h.w(), h.y() / h.w());
        return true;
    };
    QPointF at;
    if (!project(origin, &at))
        return out;
    static const char *names[] = {"x", "y", "z"};
    for (int i = 0; i < 3; ++i) {
        const QVector3D axis(i == 0 ? 1.f : 0.f, i == 1 ? 1.f : 0.f, i == 2 ? 1.f : 0.f);
        QPointF tip;
        QPointF dir;
        if (project(origin + axis * reach, &tip)) {
            dir = tip - at;
            const double len = std::hypot(dir.x(), dir.y());
            // Scaled by how much of the axis lies across the screen, so one pointing at the
            // viewer shrinks toward the widget's centre.
            const double across = std::sqrt(std::max(0.0, 1.0 - std::pow(double(QVector3D::dotProduct(axis, toEye)), 2.0)));
            dir = len > 1e-9 ? dir / len * across : QPointF();
        }
        out.append(QVariantMap{{QStringLiteral("axis"), QString::fromLatin1(names[i])},
                               {QStringLiteral("x"), dir.x()},
                               {QStringLiteral("y"), dir.y()},
                               {QStringLiteral("depth"), double(QVector3D::dotProduct(axis, toEye))}});
    }
    return out;
}

drift::SceneCamera3d PreviewController::sceneCamera(bool *active) const
{
    // renderScale 1: the overlay measures in canvas pixels, and so does every box it is handed.
    if (const drift::Clip *clip = drift::cameraClipAt(m_app.m_project.tracks(), m_app.m_playheadUs)) {
        if (active)
            *active = true;
        return drift::sceneCameraFromClip(*clip, m_app.m_playheadUs, 1.0);
    }
    if (active)
        *active = false;
    return {};
}

bool PreviewController::cameraActive() const
{
    bool active = false;
    sceneCamera(&active);
    return active;
}

QVariantMap PreviewController::cameraBox() const
{
    if (m_mode != QLatin1String("3d"))
        return {};
    const QVariantMap state = m_app.cameraStateAtPlayhead();
    if (!state.value(QStringLiteral("active")).toBool())
        return {};
    drift::SceneCamera3d camera;
    camera.positionX = state.value(QStringLiteral("x")).toDouble();
    camera.positionY = state.value(QStringLiteral("y")).toDouble();
    camera.positionZ = state.value(QStringLiteral("z")).toDouble();
    camera.rotationX = state.value(QStringLiteral("rotationX")).toDouble();
    camera.rotationY = state.value(QStringLiteral("rotationY")).toDouble();
    camera.rotationZ = state.value(QStringLiteral("rotation")).toDouble();
    camera.perspective = state.value(QStringLiteral("perspective")).toDouble();
    const QVector3D eye = drift::sceneCameraEye(camera);
    const double width = m_app.m_project.width();
    const double height = m_app.m_project.height();
    // A gizmo "clip" standing at the eye and turned as the camera is: the clip gizmo then draws and
    // solves the camera exactly, and only the write-back differs.
    QVariantMap box = state;
    box.insert(QStringLiteral("kind"), QStringLiteral("camera"));
    box.insert(QStringLiteral("x"), eye.x() + width / 2.0 - 0.5);
    box.insert(QStringLiteral("y"), eye.y() + height / 2.0 - 0.5);
    box.insert(QStringLiteral("width"), 1.0);
    box.insert(QStringLiteral("height"), 1.0);
    box.insert(QStringLiteral("z"), eye.z());
    box.insert(QStringLiteral("layer3d"), true);
    box.insert(QStringLiteral("canvasWidth"), width);
    box.insert(QStringLiteral("canvasHeight"), height);
    return box;
}

bool PreviewController::pickCamera(double canvasX, double canvasY, double tolerance)
{
    const QVariantMap box = cameraBox();
    QMatrix4x4 view;
    if (box.isEmpty() || !viewProjection(&view))
        return false;
    const int trackIndex = box.value(QStringLiteral("track")).toInt();
    const int clipIndex = box.value(QStringLiteral("clip")).toInt();
    const drift::SceneCamera3d camera =
        drift::sceneCameraFromClip(m_app.m_project.tracks().at(trackIndex).clips.at(clipIndex), m_app.m_playheadUs, 1.0);
    const QMatrix4x4 c = drift::sceneCameraWorld(camera);
    const float d = float(std::max(1.0, camera.perspective));
    const float hw = float(m_app.m_project.width() * 0.5);
    const float hh = float(m_app.m_project.height() * 0.5);
    const float t = 0.15f;
    QList<QPointF> points;
    for (const QVector3D &local : {QVector3D(0.f, 0.f, d), QVector3D(-hw * t, -hh * t, d * (1.f - t)),
                                   QVector3D(hw * t, -hh * t, d * (1.f - t)),
                                   QVector3D(hw * t, hh * t, d * (1.f - t)),
                                   QVector3D(-hw * t, hh * t, d * (1.f - t))}) {
        const QVector4D h = view.map(QVector4D(c.map(local), 1.f));
        if (h.w() <= 1e-6f)
            return false;
        points.append(QPointF(h.x() / h.w(), h.y() / h.w()));
    }
    // The body as drawn: the eye, the near frame and the four sides between them.
    const QPointF p(canvasX, canvasY);
    bool hit = QLineF(p, points.at(0)).length() <= tolerance
               || QPolygonF({points.at(1), points.at(2), points.at(3), points.at(4)})
                      .containsPoint(p, Qt::OddEvenFill);
    for (int i = 1; i <= 4 && !hit; ++i)
        hit = QPolygonF({points.at(0), points.at(i), points.at(i % 4 + 1)}).containsPoint(p, Qt::OddEvenFill);
    if (!hit)
        return false;
    m_app.selectClip(trackIndex, clipIndex);
    return true;
}

QMatrix4x4 PreviewController::clipPoseMatrix(const QVariantMap &box, double x, double y,
                                                double w, double h, double rotation,
                                                double scaleX, double scaleY) const
{
    const QSizeF canvas(box.value(QStringLiteral("canvasWidth")).toDouble(),
                        box.value(QStringLiteral("canvasHeight")).toDouble());
    if (canvas.isEmpty() || scaleX <= 0.0 || scaleY <= 0.0)
        return {};
    QMatrix4x4 m;
    m.scale(float(scaleX), float(scaleY));
    m.translate(float(-x), float(-y));
    QMatrix4x4 view;
    if (viewProjection(&view)) {
        // Through the camera (or the 3D view), using the same placement the compositor draws
        // with, so the overlay sits exactly on the picture.
        const QTransform parent = previewBoxParent(box);
        m *= drift::viewClipLocalToCanvas(view, QRectF(x, y, w, h), rotation, previewBoxPose(box),
                                          parent, !parent.isIdentity(), canvas);
    } else {
        // A parented box, flat or not, is placed through its transform layers too.
        m *= liftHomography(previewBoxParent(box));
        m *= drift::clipLocalToCanvas(QRectF(x, y, w, h), rotation, previewBoxPose(box), canvas);
    }
    m.scale(float(1.0 / scaleX), float(1.0 / scaleY));
    return m;
}

drift::snap::PlacedClip PreviewController::placedClip(const QVariantMap &box) const
{
    const double x = box.value(QStringLiteral("x")).toDouble();
    const double y = box.value(QStringLiteral("y")).toDouble();
    const double w = box.value(QStringLiteral("width")).toDouble();
    const double h = box.value(QStringLiteral("height")).toDouble();
    const double rotation = box.value(QStringLiteral("rotation")).toDouble();
    // The pose matrix is relative to the item's own position, which sits at the rect origin.
    const auto placedAt = [&](double px, double py) {
        QMatrix4x4 m;
        m.translate(float(px), float(py));
        return m * clipPoseMatrix(box, px, py, w, h, rotation, 1.0, 1.0);
    };
    drift::snap::PlacedClip placed;
    placed.local = placedAt(x, y);
    // Moving the rect is linear in homogeneous coordinates, so a one-pixel step gives the exact
    // per-pixel change of every point.
    const QVector4D origin(0.f, 0.f, 0.f, 1.f);
    const QVector4D at0 = placed.local.map(origin);
    placed.ex = placedAt(x + 1.0, y).map(origin) - at0;
    placed.ey = placedAt(x, y + 1.0).map(origin) - at0;
    placed.size = QSizeF(w, h);
    return placed;
}

QVariantMap PreviewController::snapMove(const QVariantMap &box, double pressX, double pressY,
                                           double nowX, double nowY, double tolerance,
                                           bool snap) const
{
    const drift::snap::PlacedClip placed = placedClip(box);
    const std::optional<QPointF> delta =
        drift::snap::dragDelta(placed, QPointF(pressX, pressY), QPointF(nowX, nowY));
    if (!delta)
        return {{QStringLiteral("valid"), false}};
    drift::snap::MoveSnap moved{*delta, -1.0, -1.0};
    // A spun box has no edges along the screen axes to line up, so it does not snap.
    if (snap && qAbs(box.value(QStringLiteral("rotation")).toDouble()) < 0.01) {
        const double w = box.value(QStringLiteral("canvasWidth")).toDouble();
        const double h = box.value(QStringLiteral("canvasHeight")).toDouble();
        const QVariantMap guides = m_guidesEnabled ? m_app.guideSnapTargets(w, h) : QVariantMap{};
        moved = drift::snap::snapMove(placed, *delta, canvasSnapTargets(w, h, guides), tolerance);
    }
    return {
        {QStringLiteral("valid"), true},
        {QStringLiteral("x"), box.value(QStringLiteral("x")).toDouble() + moved.delta.x()},
        {QStringLiteral("y"), box.value(QStringLiteral("y")).toDouble() + moved.delta.y()},
        {QStringLiteral("guideX"), moved.guideX},
        {QStringLiteral("guideY"), moved.guideY},
    };
}

QVariantMap PreviewController::snapResize(const QVariantMap &box, double width, double height,
                                             int dxSign, int dySign, bool centrePivot,
                                             double tolerance) const
{
    QVariantMap out{
        {QStringLiteral("width"), width},
        {QStringLiteral("height"), height},
        {QStringLiteral("guideX"), -1.0},
        {QStringLiteral("guideY"), -1.0},
        {QStringLiteral("distX"), -1.0},
        {QStringLiteral("distY"), -1.0},
    };
    if (qAbs(box.value(QStringLiteral("rotation")).toDouble()) >= 0.01)
        return out;
    const drift::snap::PlacedClip placed = placedClip(box);
    const double cw = box.value(QStringLiteral("canvasWidth")).toDouble();
    const double ch = box.value(QStringLiteral("canvasHeight")).toDouble();
    const QVariantMap guides = m_guidesEnabled ? m_app.guideSnapTargets(cw, ch) : QVariantMap{};
    const drift::snap::Targets targets = canvasSnapTargets(cw, ch, guides);
    // Where the moving edge sits in the grabbed rect's own px, as anchor + scale * size: about
    // the centre it moves half as far, about the opposite edge the whole way.
    const auto axis = [&](int sign, double start, double size, bool horizontal,
                          const char *sizeKey, const char *guideKey, const char *distKey) {
        if (sign == 0)
            return;
        const double scale = centrePivot ? sign * 0.5 : double(sign);
        const double anchor = centrePivot ? start / 2.0 : (sign < 0 ? start : 0.0);
        if (const auto snapped = drift::snap::snapEdge(placed, horizontal, anchor, scale, size,
                                                       targets, tolerance)) {
            out.insert(QLatin1String(sizeKey), snapped->size);
            out.insert(QLatin1String(guideKey), snapped->guide);
            out.insert(QLatin1String(distKey), snapped->distance);
        }
    };
    axis(dxSign, placed.size.width(), width, true, "width", "guideX", "distX");
    axis(dySign, placed.size.height(), height, false, "height", "guideY", "distY");
    return out;
}

QVariantMap PreviewController::clipAtCanvasPoint(double canvasX, double canvasY) const
{
    // previewClipsAtPlayhead lists the top track first, so the first hit is the one on top.
    for (const QVariant &item : clipsAtPlayhead()) {
        const QVariantMap box = item.toMap();
        // A transform layer's box is the group's frame, not something drawn: picking through it
        // reaches the clips it moves.
        if (box.value(QStringLiteral("kind")).toString() == QLatin1String("transform"))
            continue;
        // With a camera the box is already projected into canvas space by previewClipsAtPlayhead,
        // parents and all, so the pointer is tested against those corners directly rather than
        // being mapped back into the clip's own frame — there is no flat frame left to map into.
        if (box.value(QStringLiteral("cameraActive")).toBool()) {
            QPolygonF quad;
            for (const QVariant &corner : box.value(QStringLiteral("quad")).toList())
                quad << corner.toPointF();
            if (quad.size() == 4 && quad.containsPoint(QPointF(canvasX, canvasY), Qt::OddEvenFill))
                return box;
            continue;
        }
        const QPointF local = mapToClipSpace(box, canvasX, canvasY);
        const double x = box.value(QStringLiteral("x")).toDouble();
        const double y = box.value(QStringLiteral("y")).toDouble();
        const double w = box.value(QStringLiteral("width")).toDouble();
        const double h = box.value(QStringLiteral("height")).toDouble();
        const drift::ClipPose3d pose = previewBoxPose(box);
        if (pose.isActive()) {
            const QPolygonF quad = drift::projectedClipQuad(
                QRectF(x, y, w, h), box.value(QStringLiteral("rotation")).toDouble(), pose,
                QSizeF(box.value(QStringLiteral("canvasWidth")).toDouble(),
                       box.value(QStringLiteral("canvasHeight")).toDouble()));
            if (quad.containsPoint(local, Qt::OddEvenFill))
                return box;
            continue;
        }
        const double radians = qDegreesToRadians(box.value(QStringLiteral("rotation")).toDouble());
        const double cx = x + w / 2.0;
        const double cy = y + h / 2.0;
        // Into the box's own frame: undo its rotation about its centre.
        const double dx = local.x() - cx;
        const double dy = local.y() - cy;
        const double lx = dx * std::cos(-radians) - dy * std::sin(-radians);
        const double ly = dx * std::sin(-radians) + dy * std::cos(-radians);
        if (qAbs(lx) <= w / 2.0 && qAbs(ly) <= h / 2.0)
            return box;
    }
    return {};
}

QVariantMap PreviewController::planDrop(const QString &kind, const QString &payload,
                                           double canvasX, double canvasY) const
{
    if (kind == QLatin1String("transition"))
        return rejectDrop();
    if (kind == QLatin1String("audioEffect"))
        return rejectDrop(AppController::tr("Audio effects go on the timeline."));
    if (kind == QLatin1String("media")) {
        const drift::MediaAsset *asset =
            m_app.m_assetLibrary ? m_app.m_project.asset(m_app.m_assetLibrary->assetIdAt(payload.toInt())) : nullptr;
        if (!asset)
            return rejectDrop();
        if (asset->kind == drift::MediaKind::Audio)
            return rejectDrop(AppController::tr("Audio goes on the timeline."));
        return {{QStringLiteral("accepted"), true}, {QStringLiteral("mode"), QStringLiteral("canvas")}};
    }
    if (placeableClipType(kind))
        return {{QStringLiteral("accepted"), true}, {QStringLiteral("mode"), QStringLiteral("canvas")}};
    if (isClipTargetedKind(kind)) {
        const QVariantMap box = clipAtCanvasPoint(canvasX, canvasY);
        if (box.isEmpty())
            return rejectDrop(AppController::tr("Drop that onto a clip in the preview."));
        QVariantMap plan = box;
        plan.insert(QStringLiteral("accepted"), true);
        plan.insert(QStringLiteral("mode"), QStringLiteral("clip"));
        plan.insert(QStringLiteral("clip"), box.value(QStringLiteral("clip")));
        return plan;
    }
    return rejectDrop();
}

QVariantMap PreviewController::dropAsset(const QString &kind, const QString &payload,
                                              const QString &label, double canvasX, double canvasY)
{
    // Lands at the playhead, so the playhead has to hold still for it.
    if (m_app.m_playing)
        m_app.setPlaying(false);

    const QVariantMap plan = planDrop(kind, payload, canvasX, canvasY);
    // A refusal's message is the caller's to show: it knows whether a toast fits.
    if (!plan.value(QStringLiteral("accepted")).toBool())
        return plan;
    const double at = drift::usToSeconds(m_app.m_playheadUs);

    if (plan.value(QStringLiteral("mode")).toString() == QLatin1String("clip")) {
        const int track = plan.value(QStringLiteral("track")).toInt();
        const int clip = plan.value(QStringLiteral("clip")).toInt();
        if (kind == QLatin1String("effect"))
            m_app.addEffect(track, clip, payload);
        else if (kind == QLatin1String("template"))
            m_app.applyEffectTemplate(track, clip, payload);
        else if (kind == QLatin1String("effectStack"))
            m_app.applyEffectPreset(track, clip, payload);
        else if (kind == QLatin1String("mask"))
            m_app.addMaskToClip(track, clip, payload);
        if (kind != QLatin1String("effect") && kind != QLatin1String("mask"))
            m_app.selectClip(track, clip);
        return plan;
    }

    if (kind == QLatin1String("media")) {
        m_app.addClipFromAssetOnNewTrackAt(payload.toInt(), 0, at);
        return plan;
    }

    // Overlays land on top, centred where they were dropped: the add and the placement are one
    // edit, so a single undo removes the clip rather than first moving it back to the default.
    m_app.mcp()->beginBatch();
    if (kind == QLatin1String("shape"))
        m_app.addShapeClipAt(payload, -1, at);
    else if (kind == QLatin1String("sticker"))
        m_app.addStickerClip(payload, at, -1);
    else if (kind == QLatin1String("emoji"))
        m_app.addEmojiClip(payload, label, at, -1);
    else if (kind == QLatin1String("textStyle"))
        m_app.addTextClip(QString(), at, payload, -1);
    else if (kind == QLatin1String("adjustment"))
        m_app.addAdjustmentClipAt(-1, at);
    if (kind != QLatin1String("adjustment") && m_app.m_selectedTrack >= 0
        && m_app.m_selectedTrack < m_app.m_project.tracks().size()
        && m_app.m_selectedClip >= 0 && m_app.m_selectedClip < m_app.m_project.tracks().at(m_app.m_selectedTrack).clips.size()) {
        drift::Clip &added = m_app.m_project.tracks()[m_app.m_selectedTrack].clips[m_app.m_selectedClip];
        const double w = added.transformW.evaluateAt(0);
        const double h = added.transformH.evaluateAt(0);
        const double x = qBound(-w / 2.0, canvasX - w / 2.0, m_app.m_project.width() - w / 2.0);
        const double y = qBound(-h / 2.0, canvasY - h / 2.0, m_app.m_project.height() - h / 2.0);
        setClipLayoutPixels(added, x, y, w, h);
    }
    m_app.mcp()->endBatch(AppController::tr("Add to preview"), true);
    return plan;
}

QVariantList PreviewController::clipsAtPlayhead() const
{
    QVariantList out;
    const int canvasWidth = m_app.m_project.width();
    const int canvasHeight = m_app.m_project.height();
    if (canvasWidth <= 0 || canvasHeight <= 0)
        return out;

    const QList<drift::Track> &tracks = m_app.m_project.tracks();
    const QList<drift::TransformParent> parents =
        drift::transformParentsAt(m_app.m_project, m_app.m_playheadUs, 1.0);
    QMatrix4x4 view;
    const bool cameraActive = viewProjection(&view);
    const bool mode3d = m_mode == QLatin1String("3d");
    for (int trackIndex = 0; trackIndex < tracks.size(); ++trackIndex) {
        const drift::Track &track = tracks.at(trackIndex);
        if (track.hidden)
            continue;
        if (track.type != drift::TrackType::Video && track.type != drift::TrackType::Shape
            && track.type != drift::TrackType::Text && track.type != drift::TrackType::Subtitle
            && !track.isTransformLayer())
            continue;
        const drift::TransformParent parent =
            parents.isEmpty() ? drift::TransformParent{} : parents.at(trackIndex);
        QVariantList parentChain;
        for (const int layer : drift::transformLayersCovering(tracks, trackIndex)) {
            for (int c = 0; c < tracks.at(layer).clips.size(); ++c) {
                const drift::Clip &candidate = tracks.at(layer).clips.at(c);
                if (tracks.at(layer).hidden || !candidate.containsTime(m_app.m_playheadUs))
                    continue;
                parentChain.prepend(QVariantMap{{QStringLiteral("track"), layer},
                                                {QStringLiteral("clip"), c},
                                                {QStringLiteral("name"), candidate.name}});
            }
        }
        const QVariantList parentMatrix = transformToList(parent.matrix);
        QString parentSig;
        if (parent.hasParent) {
            for (const QVariant &v : parentMatrix)
                parentSig += QString::number(v.toDouble(), 'g', 6) + QLatin1Char(',');
        }
        parentSig += QString::number(parent.opacity, 'g', 4);

        for (int clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const drift::Clip &clip = track.clips.at(clipIndex);
            if (!clip.containsTime(m_app.m_playheadUs) || !clipAcceptsPreviewTransform(clip))
                continue;

            const drift::TimeUs relative = m_app.m_playheadUs - clip.timelineStart;
            const double x = clipTransformValue(clip.transformX, relative, 0.0);
            const double y = clipTransformValue(clip.transformY, relative, 0.0);
            const double w = clipTransformValue(clip.transformW, relative, static_cast<double>(canvasWidth));
            const double h = clipTransformValue(clip.transformH, relative, static_cast<double>(canvasHeight));
            const double rotation = clipTransformValue(clip.rotation, relative, 0.0);

            const bool isTransform = track.isTransformLayer();
            int childCount = 0;
            if (isTransform) {
                for (const int t : drift::transformSpanTrackIndexes(tracks, trackIndex)) {
                    if (tracks.at(t).hidden)
                        continue;
                    for (const drift::Clip &child : tracks.at(t).clips)
                        childCount += child.containsTime(m_app.m_playheadUs) ? 1 : 0;
                }
            }
            QVariantMap entry{
                {QStringLiteral("track"), trackIndex},
                {QStringLiteral("clip"), clipIndex},
                {QStringLiteral("kind"), isTransform ? QStringLiteral("transform")
                                                     : drift::clipTypeToString(clip.type)},
                {QStringLiteral("childCount"), childCount},
                {QStringLiteral("parentActive"), parent.hasParent},
                {QStringLiteral("parentAffine"), parent.matrix.isAffine()},
                {QStringLiteral("parent"), parentMatrix},
                {QStringLiteral("parentMatrix"), liftHomography(parent.matrix)},
                {QStringLiteral("parentOpacity"), parent.opacity},
                {QStringLiteral("parentSig"), parentSig},
                {QStringLiteral("parents"), parentChain},
                {QStringLiteral("name"), clip.name},
                {QStringLiteral("pixelSize"), clip.textStyle.pixelSize},
                {QStringLiteral("x"), x},
                {QStringLiteral("y"), y},
                {QStringLiteral("width"), w},
                {QStringLiteral("height"), h},
                {QStringLiteral("rotation"), rotation},
                {QStringLiteral("layer3d"), clip.layer3d},
                {QStringLiteral("rotationX"), clipTransformValue(clip.rotationX, relative, 0.0)},
                {QStringLiteral("rotationY"), clipTransformValue(clip.rotationY, relative, 0.0)},
                {QStringLiteral("z"), clipTransformValue(clip.positionZ, relative, 0.0)},
                {QStringLiteral("perspective"),
                 clipTransformValue(clip.perspective, relative, drift::kDefaultClipPerspective)},
                {QStringLiteral("canvasWidth"), canvasWidth},
                {QStringLiteral("canvasHeight"), canvasHeight},
            };
            // Where the box lands on screen, through its own pose, every parent and the camera.
            {
                const QRectF rect(entry.value(QStringLiteral("x")).toDouble(),
                                  entry.value(QStringLiteral("y")).toDouble(),
                                  entry.value(QStringLiteral("width")).toDouble(),
                                  entry.value(QStringLiteral("height")).toDouble());
                const double spin = entry.value(QStringLiteral("rotation")).toDouble();
                const drift::ClipPose3d pose = previewBoxPose(entry);
                QMatrix4x4 placed;
                if (cameraActive) {
                    placed = drift::viewQuadToCanvas(view, rect, spin, false, false, pose,
                                                     parent.matrix, parent.hasParent,
                                                     QSizeF(canvasWidth, canvasHeight));
                } else {
                    const QMatrix4x4 quadMatrix =
                        pose.isActive() ? drift::clipQuadToCanvas(rect, spin, false, false, pose,
                                                                  QSizeF(canvasWidth, canvasHeight))
                                        : drift::flatQuadToCanvas(rect, spin, false, false);
                    placed = parent.hasParent
                                 ? drift::parentedQuadToCanvas(parent.matrix, quadMatrix)
                                 : quadMatrix;
                }
                QVariantList quad;
                for (const QPointF &p : drift::projectedQuad(placed))
                    quad.append(p);
                // The 3D view can look past a clip or stand inside it; a box partly behind the eye
                // has no outline to draw.
                if (mode3d && quad.isEmpty())
                    continue;
                entry.insert(QStringLiteral("quad"), quad);
            }
            // The overlay reads this to take its 3D drawing path: under a camera the box is drawn
            // and dragged through the camera's projection rather than straight onto the canvas.
            entry.insert(QStringLiteral("cameraActive"), cameraActive);
            out.append(entry);
        }
    }
    return out;
}

void PreviewController::setCanvasCropMode(bool active)
{
    if (m_canvasCropMode == active)
        return;
    m_canvasCropMode = active;
    // Both modes claim the preview's grips and pointer, so entering one leaves the other.
    if (active && m_maskEditMode) {
        m_maskEditMode = false;
        emit maskEditModeChanged();
    }
    if (active) {
        setGuideEditSetId(QString());
        setMode(QStringLiteral("2d"));
    }
    emit canvasCropModeChanged();
}

bool PreviewController::maskEditActive() const
{
    if (m_maskEditMode)
        return true;
    // Selecting a mask clip on a lane is itself the request to edit it — asking the user to then
    // find a toolbar toggle would make the handles undiscoverable.
    if (m_app.m_selectedTrack < 0 || m_app.m_selectedTrack >= m_app.m_project.tracks().size())
        return false;
    const drift::Track &track = m_app.m_project.tracks().at(m_app.m_selectedTrack);
    if (m_app.m_selectedClip < 0 || m_app.m_selectedClip >= track.clips.size())
        return false;
    const drift::Clip &clip = track.clips.at(m_app.m_selectedClip);
    return clip.type == drift::ClipType::Adjustment
           && clip.adjustmentKind == drift::AdjustmentKind::Mask;
}

void PreviewController::setMaskEditMode(bool active)
{
    if (m_maskEditMode == active)
        return;
    m_maskEditMode = active;
    if (active && m_canvasCropMode) {
        m_canvasCropMode = false;
        emit canvasCropModeChanged();
    }
    if (active)
        setGuideEditSetId(QString());
    emit maskEditModeChanged();
}

QPointF PreviewController::mapToClipSpace(const QVariantMap &box, double x, double y) const
{
    bool invertible = false;
    const QTransform inverse = previewBoxParent(box).inverted(&invertible);
    return invertible ? inverse.map(QPointF(x, y)) : QPointF(x, y);
}

QPointF PreviewController::mapFromClipSpace(const QVariantMap &box, double x, double y) const
{
    return previewBoxParent(box).map(QPointF(x, y));
}

QMatrix4x4 PreviewController::parentOverlayMatrix(const QVariantMap &box, double scale) const
{
    if (scale <= 0.0)
        return {};
    QMatrix4x4 m;
    m.scale(float(scale), float(scale));
    m *= liftHomography(previewBoxParent(box));
    m.scale(float(1.0 / scale), float(1.0 / scale));
    return m;
}

QVariantMap PreviewController::depthEffectEditorState() const
{
    QVariantMap out;
    // The effect stack of a media clip lives on the adjustment pinned to it, and the depth on the
    // media clip itself: resolve both from whichever of the two is selected.
    const drift::ClipRef source = m_app.sourceClipRef(m_app.m_selectedTrack, m_app.m_selectedClip);
    if (source.trackIndex < 0 || source.trackIndex >= m_app.m_project.tracks().size())
        return out;
    const drift::Track &track = m_app.m_project.tracks().at(source.trackIndex);
    if (source.clipIndex < 0 || source.clipIndex >= track.clips.size())
        return out;
    const drift::Clip &media = track.clips.at(source.clipIndex);
    // A standalone adjustment has no depth of its own, and a clip off the playhead has no pixels
    // on screen to put handles against.
    if (media.type == drift::ClipType::Adjustment || !media.containsTime(m_app.m_playheadUs))
        return out;
    const drift::Clip *host =
        m_app.effectHostClip(source.trackIndex, source.clipIndex, drift::AdjustmentKind::VideoEffects);
    const drift::Clip &stack = host ? *host : media;

    QVariantList effects;
    const drift::TimeUs relative = m_app.m_playheadUs - stack.timelineStart;
    for (int i = 0; i < stack.effects.size(); ++i) {
        const drift::Effect &effect = stack.effects.at(i);
        if (!effect.enabled)
            continue;
        if (effect.catalogId != QLatin1String("depth.relight")
            && effect.catalogId != QLatin1String("depth.focus")) {
            continue;
        }
        const EffectPresetEntry *def = effectDefForId(effect.catalogId);
        if (!def)
            continue;
        const QMap<QString, QVariant> params =
            resolvedEffectParameters(effect.resolvedAt(relative), *def);
        QVariantMap map;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            map.insert(it.key(), it.value());
        effects.append(QVariantMap{{QStringLiteral("index"), i},
                                   {QStringLiteral("catalogId"), effect.catalogId},
                                   {QStringLiteral("params"), map}});
    }
    if (effects.isEmpty())
        return out;

    // Effect coordinates are normalized to the clip's own frame, the same frame masks use.
    const drift::TimeUs clipRelative = m_app.m_playheadUs - media.timelineStart;
    const auto value = [&](const drift::KeyframeTrack<double> &kt, double fallback) {
        return kt.isEmpty() ? fallback : kt.evaluateAt(clipRelative);
    };
    out.insert(QStringLiteral("hasFrame"), true);
    out.insert(QStringLiteral("canvasWidth"), m_app.m_project.width());
    out.insert(QStringLiteral("canvasHeight"), m_app.m_project.height());
    out.insert(QStringLiteral("x"), value(media.transformX, 0.0));
    out.insert(QStringLiteral("y"), value(media.transformY, 0.0));
    out.insert(QStringLiteral("width"), value(media.transformW, m_app.m_project.width()));
    out.insert(QStringLiteral("height"), value(media.transformH, m_app.m_project.height()));
    out.insert(QStringLiteral("rotation"), value(media.rotation, 0.0));
    out.insert(QStringLiteral("rotationX"), value(media.rotationX, 0.0));
    out.insert(QStringLiteral("rotationY"), value(media.rotationY, 0.0));
    out.insert(QStringLiteral("z"), value(media.positionZ, 0.0));
    out.insert(QStringLiteral("perspective"), value(media.perspective, drift::kDefaultClipPerspective));
    out.insert(QStringLiteral("hasDepth"), !media.depthPath.isEmpty());
    out.insert(QStringLiteral("effects"), effects);
    return out;
}

QVariantMap PreviewController::maskEditorState() const
{
    QVariantMap out;
    if (m_app.m_selectedTrack < 0 || m_app.m_selectedTrack >= m_app.m_project.tracks().size())
        return out;
    const drift::Track &selectedTrack = m_app.m_project.tracks().at(m_app.m_selectedTrack);
    if (m_app.m_selectedClip < 0 || m_app.m_selectedClip >= selectedTrack.clips.size())
        return out;
    const drift::Clip &selectedClip = selectedTrack.clips.at(m_app.m_selectedClip);

    // Three ways to arrive here, and they differ only in what frame the handles are placed
    // against — mask coordinates are normalized to that frame, not to the canvas.
    int hostTrack = -1;
    QString selectedAdjustmentId;
    QVariantList layers;

    const auto appendLayer = [&](const drift::Clip &adjustment, int trackIndex, int clipIndex) {
        // Resolved, so a handle sits where the animation actually puts the mask this frame rather
        // than on its static value.
        const drift::TimeUs maskTimeUs = m_app.m_playheadUs - adjustment.timelineStart;
        const drift::Mask resolved = adjustment.mask.isAnimated()
                                         ? adjustment.mask.resolvedAt(maskTimeUs)
                                         : adjustment.mask;
        layers.append(QVariantMap{
            {QStringLiteral("track"), trackIndex},
            {QStringLiteral("clip"), clipIndex},
            {QStringLiteral("selected"), adjustment.id == selectedAdjustmentId},
            {QStringLiteral("animated"), adjustment.mask.isAnimated()},
            {QStringLiteral("mask"), maskToMap(resolved, adjustment.timelineStart)},
        });
    };

    if (selectedClip.type == drift::ClipType::Adjustment) {
        if (selectedClip.adjustmentKind != drift::AdjustmentKind::Mask)
            return out;
        selectedAdjustmentId = selectedClip.id;
        // A nested lane borrows the frame of whichever clip of its parent track is under the
        // playhead. A standalone adjustment masks the canvas composited so far, so it is its own
        // frame — there is no clip underneath that its coordinates belong to.
        hostTrack = selectedTrack.isAdjustmentLane()
                        ? drift::adjustmentLaneParentIndex(m_app.m_project, m_app.m_selectedTrack)
                        : -1;
        if (hostTrack < 0)
            appendLayer(selectedClip, m_app.m_selectedTrack, m_app.m_selectedClip);
    } else {
        hostTrack = m_app.m_selectedTrack;
        if (const drift::Clip *host =
                m_app.effectHostClip(m_app.m_selectedTrack, m_app.m_selectedClip, drift::AdjustmentKind::Mask)) {
            selectedAdjustmentId = host->id;
        }
    }

    // Default frame: the whole canvas, which is both the standalone case and the right fallback
    // for an untransformed clip.
    double frameX = 0.0;
    double frameY = 0.0;
    double frameW = m_app.m_project.width();
    double frameH = m_app.m_project.height();
    double frameRotation = 0.0;
    drift::ClipPose3d framePose;
    bool hasFrame = hostTrack < 0;
    int hostClip = -1;

    if (hostTrack >= 0 && hostTrack < m_app.m_project.tracks().size()) {
        const drift::Track &track = m_app.m_project.tracks().at(hostTrack);
        for (int c = 0; c < track.clips.size(); ++c) {
            if (track.clips.at(c).type != drift::ClipType::Adjustment
                && track.clips.at(c).containsTime(m_app.m_playheadUs)) {
                hostClip = c;
                break;
            }
        }
        if (hostClip >= 0) {
            const drift::Clip &clip = track.clips.at(hostClip);
            const drift::TimeUs relative = m_app.m_playheadUs - clip.timelineStart;
            const auto value = [&](const drift::KeyframeTrack<double> &kt, double fallback) {
                return kt.isEmpty() ? fallback : kt.evaluateAt(relative);
            };
            frameX = value(clip.transformX, 0.0);
            frameY = value(clip.transformY, 0.0);
            frameW = value(clip.transformW, m_app.m_project.width());
            frameH = value(clip.transformH, m_app.m_project.height());
            frameRotation = value(clip.rotation, 0.0);
            if (clip.type != drift::ClipType::Model3d) {
                framePose.rotationX = value(clip.rotationX, 0.0);
                framePose.rotationY = value(clip.rotationY, 0.0);
                framePose.positionZ = value(clip.positionZ, 0.0);
                framePose.perspective = value(clip.perspective, drift::kDefaultClipPerspective);
            }
            hasFrame = true;
        }

        for (const int laneIndex : drift::adjustmentLaneIndexes(m_app.m_project, hostTrack)) {
            const drift::Track &lane = m_app.m_project.tracks().at(laneIndex);
            for (int c = 0; c < lane.clips.size(); ++c) {
                const drift::Clip &adjustment = lane.clips.at(c);
                if (adjustment.adjustmentKind != drift::AdjustmentKind::Mask)
                    continue;
                if (!adjustment.containsTime(m_app.m_playheadUs))
                    continue;
                appendLayer(adjustment, laneIndex, c);
            }
        }
    }

    out.insert(QStringLiteral("hostTrack"), hostTrack);
    out.insert(QStringLiteral("hostClip"), hostClip);
    out.insert(QStringLiteral("hasFrame"), hasFrame);
    out.insert(QStringLiteral("canvasWidth"), m_app.m_project.width());
    out.insert(QStringLiteral("canvasHeight"), m_app.m_project.height());
    out.insert(QStringLiteral("layers"), layers);
    out.insert(QStringLiteral("x"), frameX);
    out.insert(QStringLiteral("y"), frameY);
    out.insert(QStringLiteral("width"), frameW);
    out.insert(QStringLiteral("height"), frameH);
    out.insert(QStringLiteral("rotation"), frameRotation);
    out.insert(QStringLiteral("rotationX"), framePose.rotationX);
    out.insert(QStringLiteral("rotationY"), framePose.rotationY);
    out.insert(QStringLiteral("z"), framePose.positionZ);
    out.insert(QStringLiteral("perspective"), framePose.perspective);
    // A host moved by a transform layer: the handles are placed through that parent too.
    const drift::TransformParent parent =
        hostTrack >= 0 ? m_app.transformParentFor(hostTrack) : drift::TransformParent{};
    out.insert(QStringLiteral("parentActive"), parent.hasParent);
    out.insert(QStringLiteral("parent"), transformToList(parent.matrix));
    return out;
}

bool PreviewController::resetSceneCamera(int trackIndex, int clipIndex)
{
    if (trackIndex < 0) {
        const QVariantMap state = m_app.cameraStateAtPlayhead();
        if (!state.value(QStringLiteral("active")).toBool())
            return false;
        trackIndex = state.value(QStringLiteral("track")).toInt();
        clipIndex = state.value(QStringLiteral("clip")).toInt();
    }
    if (!m_app.isValidClipIndex(trackIndex, clipIndex)
        || !drift::isCameraClip(m_app.m_project.tracks().at(trackIndex).clips.at(clipIndex)))
        return false;
    const drift::Project before = m_app.m_project;
    drift::Clip &clip = m_app.m_project.tracks()[trackIndex].clips[clipIndex];
    clip.transformX = {};
    clip.transformY = {};
    clip.positionZ = {};
    clip.rotationX = {};
    clip.rotationY = {};
    clip.rotation = {};
    m_app.pushProjectEdit(before, AppController::tr("Reset camera"));
    m_app.finishEdit(AppController::tr("Camera reset"));
    return true;
}
