#include "app/editor/sessions/PathEditSession.h"

#include "app/editor/AppController.h"
#include "app/editor/AppControllerDetail.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "app/editor/sessions/MaskEditSession.h"
#include "app/editor/sessions/MaskingController.h"

#include <algorithm>

using drift::manip::DragSession;
using drift::manip::ManipModifiers;

namespace {

constexpr double kTouchVertexTol = 24.0;
constexpr double kMouseVertexTol = 8.0;

QVariantMap vertexMap(int c, int v, const QPointF &at, const drift::ShapeVertex &sv, bool selected, bool open)
{
    return {{QStringLiteral("c"), c},
            {QStringLiteral("v"), v},
            {QStringLiteral("x"), at.x()},
            {QStringLiteral("y"), at.y()},
            {QStringLiteral("mode"), int(sv.mode)},
            {QStringLiteral("smooth"), sv.smooth() || !sv.in.isNull() || !sv.out.isNull()},
            {QStringLiteral("selected"), selected},
            {QStringLiteral("first"), v == 0},
            {QStringLiteral("open"), open}};
}

QVector<drift::ShapeContour> scaled(const QVector<drift::ShapeContour> &contours, double w, double h)
{
    QVector<drift::ShapeContour> out = contours;
    for (drift::ShapeContour &c : out) {
        for (drift::ShapeVertex &v : c.vertices) {
            v.p = {v.p.x() * w, v.p.y() * h};
            v.in = {v.in.x() * w, v.in.y() * h};
            v.out = {v.out.x() * w, v.out.y() * h};
        }
    }
    return out;
}

} // namespace

PathEditSession::PathEditSession(AppController *app, MaskingController *owner)
    : QObject(owner)
    , m_app(app)
    , m_owner(owner)
{
}

void PathEditSession::bump()
{
    ++m_revision;
    emit changed();
}

PathEditSession::Core::Tolerances PathEditSession::tolerances(int device) const
{
    if (device == int(DragSession::Device::Touch))
        return {kTouchVertexTol, kTouchVertexTol, kTouchVertexTol - 4.0};
    return {kMouseVertexTol, kMouseVertexTol, 6.0};
}

double PathEditSession::closeTolerance(int device) const
{
    return device == int(DragSession::Device::Touch) ? kTouchVertexTol : 10.0;
}

ManipModifiers PathEditSession::modifiersOf(int qtFlags) const
{
    return ManipModifiers::fromQt(Qt::KeyboardModifiers::fromInt(qtFlags));
}

void PathEditSession::setPenMode(bool on)
{
    if (m_penMode == on)
        return;
    m_penMode = on;
    if (!on)
        m_core.penEnd();
    bump();
}

void PathEditSession::setHealOnDelete(bool on)
{
    if (m_heal == on)
        return;
    m_heal = on;
    emit changed();
}

bool PathEditSession::canExtend() const
{
    const auto &sel = m_core.selection();
    return canPen() && sel.size() == 1 && m_core.isOpenEnd(sel.first().c, sel.first().v);
}

bool PathEditSession::canToggleClosed() const
{
    return !m_core.closedOnly() && attached() && !m_core.selection().isEmpty();
}

bool PathEditSession::selectedClosed() const
{
    const auto &sel = m_core.selection();
    return !sel.isEmpty() && m_core.contours().at(sel.first().c).closed;
}

void PathEditSession::setView(const QPointF &p00, const QPointF &p10, const QPointF &p11, const QPointF &p01,
                              double frameW, double frameH)
{
    QTransform view;
    const QPolygonF unit({QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)});
    const QPolygonF quad({p00, p10, p11, p01});
    if (!QTransform::quadToQuad(unit, quad, view))
        return;
    m_liveView = view;
    bool ok = false;
    m_liveInverse = view.inverted(&ok);
    if (!ok)
        m_liveInverse = QTransform();
    m_frameW = std::max(1.0, frameW);
    m_frameH = std::max(1.0, frameH);
    m_core.setView(view);
    m_core.setFrameSize({m_frameW, m_frameH});
    bump();
}

bool PathEditSession::attachShape(int track, int clip)
{
    const bool same = m_kind == QLatin1String("shape") && m_track == track && m_clip == clip;
    if (!same) {
        detach();
        m_kind = QStringLiteral("shape");
        m_track = track;
        m_clip = clip;
        m_core.setClosedOnly(false);
    }
    return reload();
}

bool PathEditSession::attachMask(const QString &clipId)
{
    const bool same = m_kind == QLatin1String("mask") && m_clipId == clipId;
    if (!same) {
        detach();
        m_kind = QStringLiteral("mask");
        m_clipId = clipId;
        m_core.setClosedOnly(true);
    }
    return reload();
}

void PathEditSession::attachDraft(bool closedOnly)
{
    if (m_kind == QLatin1String("draft") && m_core.closedOnly() == closedOnly)
        return;
    detach();
    m_kind = QStringLiteral("draft");
    m_core.setClosedOnly(closedOnly);
    bump();
}

void PathEditSession::detach()
{
    if (m_open && m_app->drag())
        m_app->drag()->cancel();
    m_open = false;
    m_press = {};
    m_marqueeActive = false;
    m_kind.clear();
    m_track = m_clip = -1;
    m_clipId.clear();
    m_core.clear();
    m_penMode = false;
    bump();
}

bool PathEditSession::reload()
{
    if (!attached() || m_kind == QLatin1String("draft") || m_open || gesturing())
        return false;
    QVector<drift::ShapeContour> contours;
    if (m_kind == QLatin1String("shape")) {
        contours = drift::appcontroller_detail::shapeContoursFromVariant(m_app->shapeContoursAt(m_track, m_clip));
    } else {
        const QVector<drift::ShapeVertex> outline = m_owner->editSession()->outlineOf(m_clipId);
        if (!outline.isEmpty())
            contours.append(drift::ShapeContour{outline, true});
    }
    m_core.setContours(contours);
    bump();
    return !contours.isEmpty();
}

// ---- project writes ----

void PathEditSession::ensureOpen(const QString &kind, int device, const QString &undoText)
{
    if (m_open)
        return;
    DragSession *drag = m_app->drag();
    connect(drag, &DragSession::committed, this, &PathEditSession::onCommitted, Qt::UniqueConnection);
    connect(drag, &DragSession::cancelled, this, &PathEditSession::onCancelled, Qt::UniqueConnection);
    drag->begin(kind, DragSession::Device(device), undoText);
    m_open = true;
    if (m_kind == QLatin1String("mask"))
        m_owner->editSession()->begin(m_clipId, m_frameW, m_frameH, false);
}

void PathEditSession::writeLive()
{
    if (m_kind == QLatin1String("shape")) {
        const bool inGesture = gesturing();
        QVector<drift::ShapeContour> out = m_core.contours();
        if (inGesture) {
            // The outline is held in the units of the box the gesture began in; every write refits
            // the box, so it goes out in the units of the box as it is now.
            const QTransform map = m_core.view() * m_liveInverse;
            for (drift::ShapeContour &c : out) {
                for (drift::ShapeVertex &v : c.vertices)
                    v = drift::mapVertex(v, [&map](const QPointF &p) { return map.map(p); });
            }
        }
        const QVariantList stored = m_app->setShapeContours(
            m_track, m_clip, drift::appcontroller_detail::shapeContoursToVariant(out), false);
        m_core.takeOps();
        if (!inGesture && !stored.isEmpty())
            m_core.setContours(drift::appcontroller_detail::shapeContoursFromVariant(stored));
        emit frameInvalidated();
    } else if (m_kind == QLatin1String("mask") && !m_core.contours().isEmpty()) {
        m_owner->editSession()->setOutline(m_core.contours().first().vertices, m_core.takeOps());
    }
}

void PathEditSession::onCommitted()
{
    if (!m_open)
        return;
    m_open = false;
    m_core.endGesture();
    if (m_kind == QLatin1String("mask"))
        m_owner->editSession()->commit();
    reload();
    emit frameInvalidated();
}

void PathEditSession::onCancelled()
{
    if (!m_open)
        return;
    m_open = false;
    m_press = {};
    m_core.cancelGesture();
    if (m_kind == QLatin1String("mask"))
        m_owner->editSession()->cancel();
    reload();
    emit frameInvalidated();
}

bool PathEditSession::oneShot(const QString &undoText, const std::function<bool()> &edit)
{
    if (!attached())
        return false;
    if (!edit())
        return false;
    if (m_kind != QLatin1String("draft")) {
        ensureOpen(QStringLiteral("edit"), int(DragSession::Device::Mouse), undoText);
        writeLive();
        m_app->drag()->commit();
    }
    bump();
    return true;
}

// ---- pointer ----

QVariantMap PathEditSession::hover(const QPointF &vp, int modifiers, int device)
{
    m_hover = vp;
    m_hoverMods = modifiersOf(modifiers);
    QVariantMap out{{QStringLiteral("cursor"), QString()}, {QStringLiteral("kind"), QStringLiteral("none")},
                    {QStringLiteral("x"), vp.x()},         {QStringLiteral("y"), vp.y()},
                    {QStringLiteral("c"), -1},             {QStringLiteral("v"), -1},
                    {QStringLiteral("nearFirst"), false}};
    if (!attached() || m_open)
        return out;
    if (penMode()) {
        const bool near = m_core.penNearFirst(vp, closeTolerance(device));
        out[QStringLiteral("nearFirst")] = near;
        out[QStringLiteral("cursor")] = near ? QStringLiteral("penClose") : QStringLiteral("polygon");
        out[QStringLiteral("kind")] = QStringLiteral("pen");
        return out;
    }
    const Core::Hit hit = m_core.hitTest(vp, tolerances(device));
    out[QStringLiteral("c")] = hit.contour;
    out[QStringLiteral("v")] = hit.vertex;
    out[QStringLiteral("x")] = hit.point.x();
    out[QStringLiteral("y")] = hit.point.y();
    switch (hit.kind) {
    case Core::HitKind::Vertex:
        out[QStringLiteral("kind")] = QStringLiteral("vertex");
        out[QStringLiteral("cursor")] = m_hoverMods.breakTangent ? QStringLiteral("convert") : QStringLiteral("move");
        break;
    case Core::HitKind::Handle:
        out[QStringLiteral("kind")] = QStringLiteral("handle");
        out[QStringLiteral("cursor")] = QStringLiteral("move");
        break;
    case Core::HitKind::Segment:
        out[QStringLiteral("kind")] = QStringLiteral("segment");
        out[QStringLiteral("cursor")] = QStringLiteral("penPlus");
        break;
    case Core::HitKind::Inside:
        out[QStringLiteral("kind")] = QStringLiteral("body");
        out[QStringLiteral("cursor")] = QStringLiteral("move");
        break;
    case Core::HitKind::None:
        break;
    }
    return out;
}

QString PathEditSession::press(const QPointF &vp, int modifiers, int device, int button)
{
    if (!attached())
        return QStringLiteral("none");
    m_press = {};
    m_press.vp = vp;
    m_press.device = device;
    const ManipModifiers mm = modifiersOf(modifiers);
    const Qt::KeyboardModifiers q = Qt::KeyboardModifiers::fromInt(modifiers);
    const bool shift = q.testFlag(Qt::ShiftModifier);
    const bool ctrl = q.testFlag(Qt::ControlModifier);
    const Core::Hit hit = m_core.hitTest(vp, tolerances(device));

    if (button == int(Qt::RightButton)) {
        if (hit.kind == Core::HitKind::Vertex && m_kind != QLatin1String("draft")) {
            m_core.select(hit.contour, hit.vertex, Core::SelectMode::Replace);
            deleteSelected(!shift);
            return QStringLiteral("right");
        }
        return QStringLiteral("none");
    }

    if (penMode()) {
        if (hit.kind == Core::HitKind::Vertex && m_core.penContour() < 0 && m_kind == QLatin1String("shape")
            && m_core.isOpenEnd(hit.contour, hit.vertex)) {
            m_core.penContinue(hit.contour, hit.vertex);
            bump();
            return QStringLiteral("pen");
        }
        if (m_kind == QLatin1String("shape") && m_core.penContour() < 0)
            return QStringLiteral("none");
        const Core::PenResult r = m_core.penPress(vp, mm, closeTolerance(device));
        if (r == Core::PenResult::Closed) {
            if (m_kind == QLatin1String("draft")) {
                bump();
                emit draftClosed();
            } else {
                oneShot(tr("Close path"), [] { return true; });
            }
            return QStringLiteral("pen");
        }
        m_press.active = true;
        m_press.pen = true;
        m_press.penAdded = true;
        const int c = m_core.penContour();
        m_core.beginPenDrag(c, int(m_core.contours()[c].vertices.size()) - 1, vp);
        bump();
        return QStringLiteral("pen");
    }

    m_press.hit = hit;
    m_press.alt = mm.breakTangent;
    m_press.additive = shift || ctrl;
    switch (hit.kind) {
    case Core::HitKind::Handle:
        m_press.active = true;
        m_core.beginHandle(hit.contour, hit.vertex, hit.handle, vp, mm);
        return QStringLiteral("handle");
    case Core::HitKind::Vertex: {
        m_press.active = true;
        if (mm.breakTangent) {
            m_core.select(hit.contour, hit.vertex, Core::SelectMode::Replace);
            m_core.beginPenDrag(hit.contour, hit.vertex, vp);
            bump();
            return QStringLiteral("vertex");
        }
        const bool picked = m_core.isSelected(hit.contour, hit.vertex);
        if (ctrl) {
            m_core.select(hit.contour, hit.vertex, Core::SelectMode::Toggle);
        } else if (shift) {
            m_core.select(hit.contour, hit.vertex, Core::SelectMode::Add);
        } else if (!picked) {
            m_core.select(hit.contour, hit.vertex, Core::SelectMode::Replace);
        } else {
            m_press.deferReplace = true;
        }
        if (m_core.isSelected(hit.contour, hit.vertex))
            m_core.beginMove(hit.contour, hit.vertex, vp);
        bump();
        return QStringLiteral("vertex");
    }
    case Core::HitKind::Segment:
        m_press.active = true;
        m_core.beginBend(hit.contour, hit.segment, hit.t, vp);
        return QStringLiteral("segment");
    case Core::HitKind::Inside:
        if (shift || ctrl)
            return QStringLiteral("none");
        m_press.active = true;
        m_core.beginBody(vp);
        return QStringLiteral("body");
    case Core::HitKind::None:
        break;
    }
    return QStringLiteral("none");
}

void PathEditSession::move(const QPointF &vp, int modifiers)
{
    if (!m_press.active || !gesturing())
        return;
    if (!m_core.drag(vp, modifiersOf(modifiers)))
        return;
    m_press.moved = true;
    if (!m_open) {
        QString kind = QStringLiteral("point");
        QString text = tr("Move point");
        switch (m_core.gesture()) {
        case Core::Gesture::Handle:
        case Core::Gesture::PenDrag:
            kind = QStringLiteral("handle");
            text = tr("Edit handle");
            break;
        case Core::Gesture::Bend:
            kind = QStringLiteral("bend");
            text = tr("Bend segment");
            break;
        case Core::Gesture::Body:
            kind = QStringLiteral("body");
            text = tr("Move path");
            break;
        default:
            break;
        }
        ensureOpen(kind, m_press.device, text);
    }
    if (m_core.gesture() == Core::Gesture::Body && m_core.bodyMoved() && m_kind == QLatin1String("mask"))
        m_owner->editSession()->markBodyMoved();
    writeLive();
    m_app->drag()->update(m_core.hudText());
    bump();
}

QString PathEditSession::release(const QPointF &vp, int modifiers)
{
    Q_UNUSED(vp)
    Q_UNUSED(modifiers)
    if (!m_press.active)
        return {};
    const Press p = m_press;
    m_press = {};
    if (p.moved && m_open) {
        m_app->drag()->commit();
        return QStringLiteral("drag");
    }
    m_core.endGesture();
    if (p.pen) {
        if (m_kind == QLatin1String("draft"))
            bump();
        else
            oneShot(tr("Add point"), [] { return true; });
        return QStringLiteral("pen");
    }
    switch (p.hit.kind) {
    case Core::HitKind::Vertex:
        if (p.alt) {
            oneShot(tr("Convert point"), [&] { return m_core.toggleSmooth(p.hit.contour, p.hit.vertex); });
            return QStringLiteral("convert");
        }
        if (p.deferReplace)
            m_core.select(p.hit.contour, p.hit.vertex, Core::SelectMode::Replace);
        bump();
        return QStringLiteral("select");
    case Core::HitKind::Segment:
        oneShot(tr("Add point"), [&] { return m_core.insertAt(p.hit.contour, p.hit.segment, p.hit.t) >= 0; });
        return QStringLiteral("insert");
    case Core::HitKind::Inside:
        if (!p.additive)
            m_core.clearSelection();
        bump();
        return QStringLiteral("select");
    default:
        bump();
        break;
    }
    return {};
}

void PathEditSession::cancelPress()
{
    if (!m_press.active || m_open)
        return;
    m_press = {};
    m_core.cancelGesture();
    bump();
}

void PathEditSession::marqueeBegin(const QPointF &vp, int modifiers)
{
    const Qt::KeyboardModifiers q = Qt::KeyboardModifiers::fromInt(modifiers);
    m_marqueeMode = q.testFlag(Qt::ControlModifier) ? 2 : (q.testFlag(Qt::ShiftModifier) ? 1 : 0);
    m_marqueeOrigin = vp;
    m_marquee = QRectF(vp, vp);
    m_marqueeActive = true;
    m_core.beginMarquee();
    marqueeTo(vp);
}

void PathEditSession::marqueeTo(const QPointF &vp)
{
    if (!m_marqueeActive)
        return;
    m_marquee = QRectF(m_marqueeOrigin, vp).normalized();
    const Core::SelectMode mode = m_marqueeMode == 2 ? Core::SelectMode::Toggle
                                  : m_marqueeMode == 1 ? Core::SelectMode::Add
                                                       : Core::SelectMode::Replace;
    m_core.selectInRect(m_marquee, mode);
    bump();
}

void PathEditSession::marqueeEnd()
{
    if (!m_marqueeActive)
        return;
    m_marqueeActive = false;
    bump();
}

// ---- commands ----

bool PathEditSession::deleteSelected(bool heal)
{
    if (m_core.selection().isEmpty())
        return false;
    return oneShot(tr("Delete point"), [&] { return m_core.deleteSelected(heal); });
}

bool PathEditSession::setSelectedMode(int mode)
{
    if (mode < 0 || mode > int(drift::TangentMode::Free))
        return false;
    return oneShot(tr("Change point type"), [&] { return m_core.setSelectedMode(drift::TangentMode(mode)); });
}

bool PathEditSession::toggleSmoothSelected()
{
    const int mode = m_core.selectedMode();
    bool anySmooth = mode == -2 || mode == int(drift::TangentMode::Mirrored) || mode == int(drift::TangentMode::Aligned)
                     || mode == int(drift::TangentMode::Free);
    if (!anySmooth) {
        for (const Core::VRef &r : m_core.selection()) {
            const drift::ShapeVertex &v = m_core.contours().at(r.c).vertices.at(r.v);
            anySmooth = anySmooth || !v.in.isNull() || !v.out.isNull();
        }
    }
    return setSelectedMode(int(anySmooth ? drift::TangentMode::Corner : drift::TangentMode::Mirrored));
}

bool PathEditSession::toggleClosedSelected()
{
    if (!canToggleClosed())
        return false;
    const int c = m_core.selection().first().c;
    return oneShot(tr("Change path"), [&] { return m_core.toggleClosed(c); });
}

void PathEditSession::nudge(double dxPx, double dyPx)
{
    if (!attached() || m_kind == QLatin1String("draft"))
        return;
    if (!m_open) {
        // ManipOverlay opened the burst on EditorState.drag before calling in.
        DragSession *drag = m_app->drag();
        connect(drag, &DragSession::committed, this, &PathEditSession::onCommitted, Qt::UniqueConnection);
        connect(drag, &DragSession::cancelled, this, &PathEditSession::onCancelled, Qt::UniqueConnection);
        m_open = true;
        if (m_kind == QLatin1String("mask"))
            m_owner->editSession()->begin(m_clipId, m_frameW, m_frameH, false);
    }
    if (!m_core.nudge(dxPx, dyPx))
        return;
    writeLive();
    bump();
}

void PathEditSession::selectAll()
{
    m_core.selectAll();
    bump();
}

void PathEditSession::selectOnly(int contour, int vertex)
{
    if (contour < 0 || contour >= m_core.contours().size() || vertex < 0
        || vertex >= m_core.contours().at(contour).vertices.size())
        return;
    m_core.select(contour, vertex, Core::SelectMode::Replace);
    bump();
}

void PathEditSession::clearSelection()
{
    m_core.clearSelection();
    bump();
}

void PathEditSession::penEnd()
{
    m_core.penEnd();
    bump();
}

bool PathEditSession::penUndo()
{
    if (m_core.penContour() < 0)
        return false;
    return oneShot(tr("Remove point"), [&] { return m_core.penUndo(); });
}

QVariantList PathEditSession::draftContoursScaled(double w, double h) const
{
    return drift::appcontroller_detail::shapeContoursToVariant(scaled(m_core.contours(), w, h));
}

QVariantList PathEditSession::draftVertices() const
{
    return m_core.contours().isEmpty() ? QVariantList()
                                       : drift::appdetail::maskVerticesToVariant(m_core.contours().first().vertices);
}

int PathEditSession::draftContourCount() const
{
    return int(m_core.contours().size());
}

void PathEditSession::clearDraft()
{
    m_core.clear();
    bump();
}

// ---- drawing ----

QVariantList PathEditSession::vertexList() const
{
    QVariantList out;
    const auto &contours = m_core.contours();
    for (int c = 0; c < contours.size(); ++c) {
        for (int v = 0; v < contours[c].vertices.size(); ++v) {
            out.append(vertexMap(c, v, m_core.vertexView(c, v), contours[c].vertices[v], m_core.isSelected(c, v),
                                 !contours[c].closed));
        }
    }
    return out;
}

QVariantList PathEditSession::handleList() const
{
    QVariantList out;
    for (const Core::HandleView &h : m_core.visibleHandles()) {
        out.append(QVariantMap{{QStringLiteral("c"), h.c},
                               {QStringLiteral("v"), h.v},
                               {QStringLiteral("which"), h.which == drift::geom::TangentHandle::In ? QStringLiteral("in")
                                                                                                    : QStringLiteral("out")},
                               {QStringLiteral("x"), h.tip.x()},
                               {QStringLiteral("y"), h.tip.y()},
                               {QStringLiteral("ax"), h.anchor.x()},
                               {QStringLiteral("ay"), h.anchor.y()}});
    }
    return out;
}

QString PathEditSession::rubberSvg() const
{
    if (!penMode() || !penActive())
        return QStringLiteral("M0 0");
    return m_core.rubberSvg(m_hover, m_hoverMods);
}

#include "moc_PathEditSession.cpp"
