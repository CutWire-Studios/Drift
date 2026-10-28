#include "app/editor/sessions/MaskEditSession.h"

#include "app/editor/AppController.h"
#include "app/editor/sessions/MaskingController.h"
#include "app/jobs/vision/TrackingController.h"
#include "core/project/Project.h"
#include "core/project/Track.h"

#include <QSizeF>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

constexpr double kSnapTolPx = 8.0;

// Nearest target within `tol` of any candidate, as the delta to add to the moving value.
double snapAxis(const double (&candidates)[3], const double (&targets)[3], double tol)
{
    double result = 0.0;
    double best = tol;
    for (const double c : candidates) {
        for (const double t : targets) {
            const double d = t - c;
            if (std::abs(d) < best) {
                best = std::abs(d);
                result = d;
            }
        }
    }
    return result;
}

QVariantList pointsToVariant(const QList<QPointF> &points)
{
    QVariantList out;
    for (const QPointF &p : points)
        out.append(QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});
    return out;
}

} // namespace

MaskEditSession::MaskEditSession(AppController *app, MaskingController *owner)
    : QObject(owner)
    , m_app(app)
    , m_owner(owner)
{
}

QPointF MaskEditSession::bounds(const QList<QPointF> &points, QSizeF *size)
{
    if (points.isEmpty()) {
        if (size)
            *size = QSizeF(0.001, 0.001);
        return {};
    }
    double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
    for (const QPointF &p : points) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    if (size)
        *size = QSizeF(std::max(0.001, maxX - minX), std::max(0.001, maxY - minY));
    return QPointF((minX + maxX) / 2, (minY + maxY) / 2);
}

MaskEditSession::Ref MaskEditSession::find(const QString &clipId) const
{
    const QList<drift::Track> &tracks = m_app->m_project.tracks();
    for (int t = 0; t < tracks.size(); ++t) {
        for (int c = 0; c < tracks.at(t).clips.size(); ++c) {
            if (tracks.at(t).clips.at(c).id == clipId)
                return {t, c};
        }
    }
    return {};
}

bool MaskEditSession::load(const QString &clipId)
{
    const Ref ref = find(clipId);
    if (ref.track < 0)
        return false;
    const drift::Clip &clip = m_app->m_project.tracks().at(ref.track).clips.at(ref.clip);
    if (clip.type != drift::ClipType::Adjustment)
        return false;

    const drift::TimeUs maskUs = m_app->m_playheadUs - clip.timelineStart;
    m_animated = clip.mask.isAnimated();
    m_pathAnimated = !clip.mask.pathKeys.isEmpty();
    m_base = m_animated ? clip.mask.resolvedAt(maskUs) : clip.mask;
    m_base.keyframes.clear();
    m_base.pathKeys.clear();
    m_freeform = m_base.shape == drift::MaskShape::Freeform;

    m_clipId = clipId;
    m_x = m_base.x;
    m_y = m_base.y;
    m_w = m_base.w;
    m_h = m_base.h;
    m_rotation = m_base.rotation;
    m_points = QList<QPointF>(m_base.points.begin(), m_base.points.end());
    if (m_freeform) {
        QSizeF size;
        const QPointF centre = bounds(m_points, &size);
        if (!m_points.isEmpty()) {
            m_x = centre.x();
            m_y = centre.y();
            m_w = size.width();
            m_h = size.height();
        }
        m_rotation = 0.0;
    }
    return true;
}

bool MaskEditSession::begin(const QString &clipId, double frameW, double frameH)
{
    if (!load(clipId))
        return false;
    m_frameW = std::max(1.0, frameW);
    m_frameH = std::max(1.0, frameH);
    m_active = true;
    m_bodyMoved = false;
    m_startPoints = m_points;
    m_app->beginPreviewDrag(tr("Mask changed"));
    emit liveChanged();
    return true;
}

void MaskEditSession::commit()
{
    if (!m_active)
        return;
    const QString clipId = m_clipId;
    const bool movedBody = m_bodyMoved;
    m_active = false;
    m_bodyMoved = false;
    m_clipId.clear();
    emit liveChanged();
    m_app->commitPreviewDrag();
    // Dragging a tracked mask back onto the object is the correction gesture.
    if (movedBody)
        m_app->tracking()->correctFromMask(clipId);
}

void MaskEditSession::cancel()
{
    if (!m_active)
        return;
    m_active = false;
    m_bodyMoved = false;
    m_clipId.clear();
    emit liveChanged();
    m_app->cancelPreviewDrag();
}

void MaskEditSession::writeStatic(const drift::Mask &mask)
{
    const Ref ref = find(m_clipId);
    if (ref.track < 0)
        return;
    m_app->beginImplicitPreviewDrag(tr("Mask changed"));
    m_app->writeClipMask(ref.track, ref.clip, mask);
    m_app->emitPreviewEdit(ref.track, ref.clip, {QStringLiteral("mask.*")});
}

void MaskEditSession::write(bool includeRotation)
{
    if (m_freeform) {
        if (m_pathAnimated) {
            const Ref ref = find(m_clipId);
            if (ref.track >= 0) {
                m_owner->previewSetMaskPathKey(ref.track, ref.clip, m_app->playheadSeconds(),
                                               pointsToVariant(m_points));
            }
            return;
        }
        drift::Mask mask = m_base;
        mask.points = QVector<QPointF>(m_points.begin(), m_points.end());
        writeStatic(mask);
        return;
    }
    if (m_animated) {
        const Ref ref = find(m_clipId);
        if (ref.track < 0)
            return;
        const double at = m_app->playheadSeconds();
        const auto put = [&](const char *param, double value) {
            m_app->previewSetClipKeyframe(ref.track, ref.clip, QStringLiteral("mask.") + QLatin1String(param), at,
                                          value);
        };
        put("x", m_x);
        put("y", m_y);
        put("w", m_w);
        put("h", m_h);
        if (includeRotation)
            put("rotation", m_rotation);
        return;
    }
    drift::Mask mask = m_base;
    mask.x = m_x;
    mask.y = m_y;
    mask.w = m_w;
    mask.h = m_h;
    if (includeRotation)
        mask.rotation = m_rotation;
    writeStatic(mask);
}

void MaskEditSession::beginBody(double nx, double ny)
{
    m_pressX = nx;
    m_pressY = ny;
    m_bodyMoved = false;
    m_startX = m_x;
    m_startY = m_y;
    m_startPoints = m_points;
}

void MaskEditSession::snapCentre(double &nx, double &ny) const
{
    if (std::abs(m_rotation) >= 0.01)
        return;
    static constexpr double targets[3] = {0.0, 0.5, 1.0};
    const double halfW = m_w / 2;
    const double halfH = m_h / 2;
    const double xs[3] = {nx - halfW, nx, nx + halfW};
    const double ys[3] = {ny - halfH, ny, ny + halfH};
    nx += snapAxis(xs, targets, kSnapTolPx / m_frameW);
    ny += snapAxis(ys, targets, kSnapTolPx / m_frameH);
}

void MaskEditSession::moveBody(double nx, double ny, bool bypassSnap)
{
    if (!m_active)
        return;
    double x = m_startX + (nx - m_pressX);
    double y = m_startY + (ny - m_pressY);
    if (!bypassSnap)
        snapCentre(x, y);
    m_x = x;
    m_y = y;
    m_bodyMoved = true;
    if (m_freeform) {
        const double dx = x - m_startX;
        const double dy = y - m_startY;
        m_points = m_startPoints;
        for (QPointF &p : m_points)
            p += QPointF(dx, dy);
    }
    write(false);
    emit liveChanged();
}

void MaskEditSession::beginResize(int gripDx, int gripDy, double nx, double ny)
{
    m_gripDx = gripDx;
    m_gripDy = gripDy;
    m_pressX = nx;
    m_pressY = ny;
    m_startX = m_x;
    m_startY = m_y;
    m_startW = m_w;
    m_startH = m_h;
}

void MaskEditSession::resize(double nx, double ny, bool lockAspect)
{
    if (!m_active)
        return;
    const double a = m_rotation * M_PI / 180.0;
    const double ddx = nx - m_pressX;
    const double ddy = ny - m_pressY;
    const double lx = ddx * std::cos(a) + ddy * std::sin(a);
    const double ly = -ddx * std::sin(a) + ddy * std::cos(a);

    double w = std::max(0.01, m_startW + lx * m_gripDx);
    double h = std::max(0.01, m_startH + ly * m_gripDy);
    if (m_gripDx != 0 && m_gripDy != 0 && lockAspect) {
        const double s = std::max(w / std::max(0.01, m_startW), h / std::max(0.01, m_startH));
        w = std::max(0.01, m_startW * s);
        h = std::max(0.01, m_startH * s);
    }

    const double shiftX = (w - m_startW) / 2 * m_gripDx;
    const double shiftY = (h - m_startH) / 2 * m_gripDy;
    m_x = m_startX + shiftX * std::cos(a) - shiftY * std::sin(a);
    m_y = m_startY + shiftX * std::sin(a) + shiftY * std::cos(a);
    m_w = w;
    m_h = h;
    write(false);
    emit liveChanged();
}

void MaskEditSession::rotate(double nx, double ny, bool snap15)
{
    if (!m_active)
        return;
    double deg = std::atan2((ny - m_y) * m_frameH, (nx - m_x) * m_frameW) * 180.0 / M_PI + 90.0;
    if (snap15)
        deg = std::round(deg / 15.0) * 15.0;
    m_rotation = deg;
    write(true);
    emit liveChanged();
}

void MaskEditSession::movePoint(int index, double nx, double ny)
{
    if (!m_active || !m_freeform || index < 0 || index >= m_points.size())
        return;
    m_points[index] = QPointF(nx, ny);
    syncFreeformBounds();
    write(false);
    emit liveChanged();
}

void MaskEditSession::syncFreeformBounds()
{
    QSizeF size;
    const QPointF centre = bounds(m_points, &size);
    m_x = centre.x();
    m_y = centre.y();
    m_w = size.width();
    m_h = size.height();
}

void MaskEditSession::movePointGroup(int anchorIndex, double nx, double ny)
{
    if (!m_active || !m_freeform || anchorIndex < 0 || anchorIndex >= m_points.size()
        || m_startPoints.size() != m_points.size()) {
        return;
    }
    const QPointF delta = QPointF(nx, ny) - m_startPoints.at(anchorIndex);
    m_points = m_startPoints;
    if (m_selClipId == m_clipId && m_selected.contains(anchorIndex)) {
        for (const int i : std::as_const(m_selected)) {
            if (i < m_points.size())
                m_points[i] += delta;
        }
    } else {
        m_points[anchorIndex] += delta;
    }
    syncFreeformBounds();
    write(false);
    emit liveChanged();
}

QList<QPointF> MaskEditSession::pointsOf(const QString &clipId) const
{
    if (m_active && m_clipId == clipId)
        return m_points;
    const Ref ref = find(clipId);
    if (ref.track < 0)
        return {};
    const drift::Clip &clip = m_app->m_project.tracks().at(ref.track).clips.at(ref.clip);
    if (clip.mask.shape != drift::MaskShape::Freeform)
        return {};
    const drift::Mask mask =
        clip.mask.isAnimated() ? clip.mask.resolvedAt(m_app->m_playheadUs - clip.timelineStart) : clip.mask;
    return QList<QPointF>(mask.points.begin(), mask.points.end());
}

void MaskEditSession::setSelection(const QString &clipId, QList<int> indices)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (clipId == m_selClipId && indices == m_selected)
        return;
    m_selClipId = clipId;
    m_selected = indices;
    emit selectionChanged();
}

void MaskEditSession::setPointSelected(const QString &clipId, int index, int mode)
{
    QList<int> next = clipId == m_selClipId ? m_selected : QList<int>();
    if (mode == 0) {
        next = {index};
    } else if (mode == 1) {
        next.append(index);
    } else if (next.contains(index)) {
        next.removeAll(index);
    } else {
        next.append(index);
    }
    setSelection(clipId, next);
}

void MaskEditSession::beginPointMarquee(const QString &clipId)
{
    m_marqueeBase = clipId == m_selClipId ? m_selected : QList<int>();
}

void MaskEditSession::selectPointsInRect(const QString &clipId, const QRectF &rect, int mode)
{
    const QList<QPointF> points = pointsOf(clipId);
    std::set<int> result;
    if (mode != 0)
        result.insert(m_marqueeBase.begin(), m_marqueeBase.end());
    for (int i = 0; i < points.size(); ++i) {
        if (!rect.contains(points.at(i)))
            continue;
        if (mode == 2 && result.erase(i) > 0)
            continue;
        result.insert(i);
    }
    setSelection(clipId, QList<int>(result.begin(), result.end()));
}

void MaskEditSession::clearPointSelection()
{
    setSelection(QString(), {});
}

void MaskEditSession::keepSelectionFor(const QString &clipId)
{
    if (clipId != m_selClipId)
        clearPointSelection();
}

void MaskEditSession::setBarsHeight(double h)
{
    if (!m_active)
        return;
    m_h = std::clamp(h, 0.0, 2.0);
    if (m_animated) {
        const Ref ref = find(m_clipId);
        if (ref.track >= 0) {
            m_app->previewSetClipKeyframe(ref.track, ref.clip, QStringLiteral("mask.h"), m_app->playheadSeconds(),
                                          m_h);
        }
    } else {
        drift::Mask mask = m_base;
        mask.h = m_h;
        writeStatic(mask);
    }
    emit liveChanged();
}

void MaskEditSession::insertPoint(const QString &clipId, int index, double nx, double ny)
{
    const Ref ref = find(clipId);
    if (ref.track >= 0)
        m_app->insertMaskPoint(ref.track, ref.clip, index, nx, ny);
    clearPointSelection();
}

void MaskEditSession::removePoint(const QString &clipId, int index)
{
    const Ref ref = find(clipId);
    if (ref.track >= 0)
        m_app->removeMaskPoint(ref.track, ref.clip, index);
    clearPointSelection();
}

void MaskEditSession::nudge(const QString &clipId, int dirX, int dirY, bool coarse, double frameW, double frameH)
{
    if (m_active || !begin(clipId, frameW, frameH))
        return;
    const double step = coarse ? 10.0 : 1.0;
    const double dx = dirX * step / m_frameW;
    const double dy = dirY * step / m_frameH;
    if (m_freeform && m_selClipId == clipId && !m_selected.isEmpty()) {
        for (const int i : std::as_const(m_selected)) {
            if (i < m_points.size())
                m_points[i] += QPointF(dx, dy);
        }
        syncFreeformBounds();
    } else if (m_freeform) {
        for (QPointF &p : m_points)
            p += QPointF(dx, dy);
        m_x += dx;
        m_y += dy;
    } else {
        m_x += dx;
        m_y += dy;
    }
    write(false);
    commit();
}
