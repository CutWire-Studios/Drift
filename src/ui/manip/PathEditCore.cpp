#include "ui/manip/PathEditCore.h"

#include "ui/manip/SnapService.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <set>

namespace drift::manip {

namespace {

using drift::geom::TangentHandle;

constexpr int kHitSteps = 24;
constexpr int kDrawSteps = 16;
constexpr double kDragThresholdPx = 3.0;

bool isZero(const QPointF &p)
{
    return std::abs(p.x()) < 1e-12 && std::abs(p.y()) < 1e-12;
}

double dist(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

struct SegDistance
{
    double d = 0.0;
    double s = 0.0;
};

SegDistance distToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF d = b - a;
    const double l2 = d.x() * d.x() + d.y() * d.y();
    const double s = l2 > 0.0 ? std::clamp(QPointF::dotProduct(p - a, d) / l2, 0.0, 1.0) : 0.0;
    return {dist(p, a + d * s), s};
}

bool pointInPolygon(const QVector<QPointF> &poly, const QPointF &p)
{
    bool inside = false;
    for (int i = 0, j = int(poly.size()) - 1; i < poly.size(); j = i++) {
        const QPointF a = poly[i];
        const QPointF b = poly[j];
        if ((a.y() > p.y()) != (b.y() > p.y())
            && p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()) {
            inside = !inside;
        }
    }
    return inside;
}

QString num(double v)
{
    return QString::number(v, 'f', 2);
}

bool hasHandles(const ShapeVertex &v)
{
    return !isZero(v.in) || !isZero(v.out);
}

} // namespace

void PathEditCore::setContours(const QVector<ShapeContour> &contours)
{
    m_contours = contours;
    QList<VRef> keep;
    for (const VRef &r : std::as_const(m_selected)) {
        if (r.c < m_contours.size() && r.v < m_contours[r.c].vertices.size())
            keep.append(r);
    }
    m_selected = keep;
    if (m_penContour >= m_contours.size() || (m_penContour >= 0 && m_contours[m_penContour].closed))
        m_penContour = -1;
}

void PathEditCore::clear()
{
    m_contours.clear();
    m_selected.clear();
    m_marqueeBase.clear();
    m_ops.clear();
    m_gesture = Gesture::None;
    m_penContour = -1;
    m_hud.clear();
    m_guideX = m_guideY = -1.0;
}

void PathEditCore::setView(const QTransform &normalizedToViewport)
{
    if (m_gesture != Gesture::None) {
        m_pendingView = normalizedToViewport;
        m_hasPendingView = true;
        return;
    }
    applyView(normalizedToViewport);
}

void PathEditCore::applyView(const QTransform &normalizedToViewport)
{
    m_view = normalizedToViewport;
    bool ok = false;
    m_inverse = m_view.inverted(&ok);
    if (!ok)
        m_inverse = QTransform();
    m_axisAligned = std::abs(m_view.m12()) < 1e-9 && std::abs(m_view.m21()) < 1e-9 && std::abs(m_view.m13()) < 1e-9
                    && std::abs(m_view.m23()) < 1e-9;
}

double PathEditCore::pxPerNormX() const
{
    return std::max(1e-6, dist(toView({1, 0}), toView({0, 0})));
}

double PathEditCore::pxPerNormY() const
{
    return std::max(1e-6, dist(toView({0, 1}), toView({0, 0})));
}

QPointF PathEditCore::vertexView(int c, int v) const
{
    return toView(m_contours[c].vertices[v].p);
}

int PathEditCore::vertexCount() const
{
    int n = 0;
    for (const ShapeContour &c : m_contours)
        n += int(c.vertices.size());
    return n;
}

// ---- selection ----

bool PathEditCore::isSelected(int c, int v) const
{
    return m_selected.contains(VRef{c, v});
}

void PathEditCore::setSelectionSorted(QList<VRef> list)
{
    std::sort(list.begin(), list.end());
    list.erase(std::unique(list.begin(), list.end()), list.end());
    m_selected = list;
}

void PathEditCore::select(int c, int v, SelectMode mode)
{
    QList<VRef> next = m_selected;
    const VRef r{c, v};
    if (mode == SelectMode::Replace)
        next = {r};
    else if (mode == SelectMode::Add)
        next.append(r);
    else if (next.contains(r))
        next.removeAll(r);
    else
        next.append(r);
    setSelectionSorted(next);
}

void PathEditCore::selectAll()
{
    QList<VRef> all;
    for (int c = 0; c < m_contours.size(); ++c) {
        for (int v = 0; v < m_contours[c].vertices.size(); ++v)
            all.append({c, v});
    }
    setSelectionSorted(all);
}

void PathEditCore::clearSelection()
{
    m_selected.clear();
}

void PathEditCore::beginMarquee()
{
    m_marqueeBase = m_selected;
}

void PathEditCore::selectInRect(const QRectF &rect, SelectMode mode)
{
    std::set<std::pair<int, int>> result;
    if (mode != SelectMode::Replace) {
        for (const VRef &r : std::as_const(m_marqueeBase))
            result.insert({r.c, r.v});
    }
    for (int c = 0; c < m_contours.size(); ++c) {
        for (int v = 0; v < m_contours[c].vertices.size(); ++v) {
            if (!rect.contains(vertexView(c, v)))
                continue;
            if (mode == SelectMode::Toggle && result.erase({c, v}) > 0)
                continue;
            result.insert({c, v});
        }
    }
    QList<VRef> list;
    for (const auto &p : result)
        list.append({p.first, p.second});
    setSelectionSorted(list);
}

int PathEditCore::selectedMode() const
{
    if (m_selected.isEmpty())
        return -1;
    int mode = -1;
    for (const VRef &r : m_selected) {
        const int m = int(m_contours[r.c].vertices[r.v].mode);
        if (mode >= 0 && m != mode)
            return -2;
        mode = m;
    }
    return mode;
}

// ---- hit testing ----

QVector<PathEditCore::HandleView> PathEditCore::visibleHandles() const
{
    QVector<HandleView> out;
    std::set<std::pair<int, int>> shown;
    for (const VRef &r : m_selected) {
        const ShapeContour &c = m_contours[r.c];
        const int n = int(c.vertices.size());
        shown.insert({r.c, r.v});
        if (r.v > 0 || c.closed)
            shown.insert({r.c, (r.v - 1 + n) % n});
        if (r.v < n - 1 || c.closed)
            shown.insert({r.c, (r.v + 1) % n});
    }
    for (const auto &p : shown) {
        const ShapeVertex &v = m_contours[p.first].vertices[p.second];
        const QPointF anchor = toView(v.p);
        if (!isZero(v.in))
            out.append({p.first, p.second, TangentHandle::In, toView(v.p + v.in), anchor});
        if (!isZero(v.out))
            out.append({p.first, p.second, TangentHandle::Out, toView(v.p + v.out), anchor});
    }
    return out;
}

PathEditCore::Hit PathEditCore::hitTest(const QPointF &vp, const Tolerances &tol) const
{
    Hit hit;
    double best = tol.handle;
    for (const HandleView &h : visibleHandles()) {
        const double d = dist(vp, h.tip);
        if (d <= best) {
            best = d;
            hit = {HitKind::Handle, h.c, h.v, h.which, -1, 0.0, h.tip};
        }
    }
    if (hit.kind == HitKind::Handle)
        return hit;

    best = tol.vertex;
    for (int c = 0; c < m_contours.size(); ++c) {
        for (int v = 0; v < m_contours[c].vertices.size(); ++v) {
            const QPointF at = vertexView(c, v);
            const double d = dist(vp, at);
            if (d <= best) {
                best = d;
                hit = {HitKind::Vertex, c, v, TangentHandle::Out, -1, 0.0, at};
            }
        }
    }
    if (hit.kind == HitKind::Vertex)
        return hit;

    best = tol.segment;
    for (int c = 0; c < m_contours.size(); ++c) {
        const ShapeContour &contour = m_contours[c];
        const int segs = geom::segmentCount(contour);
        for (int s = 0; s < segs; ++s) {
            const ShapeVertex &a = contour.vertices[s];
            const ShapeVertex &b = contour.vertices[(s + 1) % contour.vertices.size()];
            QPointF prev = toView(a.p);
            for (int k = 1; k <= kHitSteps; ++k) {
                const QPointF cur = toView(geom::cubicPoint(a, b, double(k) / kHitSteps));
                const SegDistance r = distToSegment(vp, prev, cur);
                if (r.d < best) {
                    best = r.d;
                    hit = {HitKind::Segment, c, -1, TangentHandle::Out, s, (k - 1 + r.s) / kHitSteps,
                           prev + (cur - prev) * r.s};
                }
                prev = cur;
            }
        }
    }
    if (hit.kind == HitKind::Segment)
        return hit;

    for (int c = 0; c < m_contours.size(); ++c) {
        if (!m_contours[c].closed || m_contours[c].vertices.size() < 3)
            continue;
        QVector<QPointF> poly;
        for (const QPointF &p : geom::flatten(m_contours[c], kHitSteps))
            poly.append(toView(p));
        if (pointInPolygon(poly, vp)) {
            hit.kind = HitKind::Inside;
            hit.contour = c;
            hit.point = vp;
            return hit;
        }
    }
    return hit;
}

// ---- one-shot edits ----

QVector<geom::PathOp> PathEditCore::takeOps()
{
    const QVector<geom::PathOp> out = m_ops;
    m_ops.clear();
    return out;
}

int PathEditCore::insertAt(int c, int segment, double t)
{
    if (c < 0 || c >= m_contours.size() || segment < 0 || segment >= geom::segmentCount(m_contours[c]))
        return -1;
    const int at = geom::splitSegment(m_contours[c], segment, t);
    m_ops.append({geom::PathOp::Kind::Insert, segment, t, false});
    m_selected = {{c, at}};
    return at;
}

bool PathEditCore::deleteSelected(bool heal)
{
    if (m_selected.isEmpty())
        return false;
    QVector<ShapeContour> work = m_contours;
    QVector<geom::PathOp> ops;
    bool changed = false;
    for (auto it = m_selected.crbegin(); it != m_selected.crend(); ++it) {
        ShapeContour &c = work[it->c];
        if (it->v >= c.vertices.size() || (m_closedOnly && c.vertices.size() <= 3))
            continue;
        if (heal)
            geom::removeVertexHeal(c, it->v);
        else
            geom::removeVertex(c, it->v);
        ops.append({geom::PathOp::Kind::Remove, it->v, 0.0, heal});
        changed = true;
    }
    if (!changed)
        return false;
    const VRef first = m_selected.first();
    const bool single = m_selected.size() == 1;
    if (!m_closedOnly) {
        for (int c = int(work.size()) - 1; c >= 0; --c) {
            if (work[c].vertices.size() < 2)
                work.remove(c);
        }
    }
    if (work.isEmpty())
        return false;
    const bool survived = first.c < work.size() && work[first.c].vertices.size() == m_contours[first.c].vertices.size() - 1;
    m_contours = work;
    m_ops += ops;
    m_penContour = -1;
    m_selected.clear();
    if (single && survived)
        m_selected = {{first.c, std::min(first.v, int(m_contours[first.c].vertices.size()) - 1)}};
    return true;
}

bool PathEditCore::setSelectedMode(TangentMode mode)
{
    bool changed = false;
    for (const VRef &r : std::as_const(m_selected)) {
        ShapeContour &c = m_contours[r.c];
        const ShapeVertex before = c.vertices[r.v];
        if (mode == TangentMode::Corner) {
            geom::makeCorner(c, r.v);
        } else if (!hasHandles(before)) {
            geom::makeSmooth(c, r.v, mode);
        } else {
            ShapeVertex &v = c.vertices[r.v];
            if (mode == TangentMode::Mirrored) {
                if (!isZero(v.out))
                    v.in = -v.out;
                else
                    v.out = -v.in;
            } else if (mode == TangentMode::Aligned) {
                const QPointF lead = !isZero(v.out) ? v.out : -v.in;
                const double len = std::hypot(lead.x(), lead.y());
                const double inLen = isZero(v.in) ? len : std::hypot(v.in.x(), v.in.y());
                v.out = lead;
                v.in = -lead / len * inLen;
            }
            v.mode = mode;
        }
        changed = changed || c.vertices[r.v] != before;
    }
    return changed;
}

bool PathEditCore::toggleSmooth(int c, int v)
{
    if (c < 0 || c >= m_contours.size() || v < 0 || v >= m_contours[c].vertices.size())
        return false;
    ShapeContour &contour = m_contours[c];
    const ShapeVertex &vert = contour.vertices[v];
    if (vert.smooth() || hasHandles(vert))
        geom::makeCorner(contour, v);
    else
        geom::makeSmooth(contour, v, TangentMode::Mirrored);
    return true;
}

bool PathEditCore::toggleClosed(int c)
{
    if (m_closedOnly || c < 0 || c >= m_contours.size())
        return false;
    ShapeContour &contour = m_contours[c];
    if (!contour.closed && contour.vertices.size() < 3)
        return false;
    contour.closed = !contour.closed;
    if (m_penContour == c)
        m_penContour = -1;
    return true;
}

void PathEditCore::shiftSelection(const QPointF &deltaNorm)
{
    if (m_selected.isEmpty()) {
        for (ShapeContour &c : m_contours) {
            for (ShapeVertex &v : c.vertices)
                v.p += deltaNorm;
        }
        return;
    }
    for (const VRef &r : std::as_const(m_selected))
        m_contours[r.c].vertices[r.v].p += deltaNorm;
}

bool PathEditCore::nudge(double dxPx, double dyPx)
{
    if (m_contours.isEmpty())
        return false;
    shiftSelection({dxPx / std::max(1.0, m_frame.width()), dyPx / std::max(1.0, m_frame.height())});
    return true;
}

// ---- gestures ----

void PathEditCore::beginMove(int c, int v, const QPointF &pressVp)
{
    m_start = m_contours;
    m_gesture = Gesture::Move;
    m_gc = c;
    m_gv = v;
    m_pressVp = pressVp;
    m_grab = vertexView(c, v) - pressVp;
    m_moved = false;
}

void PathEditCore::beginHandle(int c, int v, TangentHandle which, const QPointF &pressVp, const ManipModifiers &mods)
{
    m_start = m_contours;
    m_gesture = Gesture::Handle;
    m_gc = c;
    m_gv = v;
    m_gh = which;
    m_pressVp = pressVp;
    const ShapeVertex &sv = m_contours[c].vertices[v];
    m_grab = toView(sv.p + (which == TangentHandle::In ? sv.in : sv.out)) - pressVp;
    m_gbreak = mods.breakTangent;
    m_moved = false;
}

void PathEditCore::beginBend(int c, int segment, double t, const QPointF &pressVp)
{
    m_start = m_contours;
    m_gesture = Gesture::Bend;
    m_gc = c;
    m_gseg = segment;
    m_gt = t;
    m_pressVp = pressVp;
    m_moved = false;
}

void PathEditCore::beginBody(const QPointF &pressVp)
{
    m_start = m_contours;
    m_gesture = Gesture::Body;
    m_pressVp = pressVp;
    m_bodyMoved = false;
    m_moved = false;
}

void PathEditCore::beginPenDrag(int c, int v, const QPointF &pressVp)
{
    m_start = m_contours;
    m_gesture = Gesture::PenDrag;
    m_gc = c;
    m_gv = v;
    m_pressVp = pressVp;
    m_moved = false;
}

void PathEditCore::applySnap(QPointF &deltaNorm, const QVector<double> &xs, const QVector<double> &ys,
                             const ManipModifiers &mods, bool lockedX, bool lockedY)
{
    m_guideX = m_guideY = -1.0;
    if (mods.noSnap || !m_axisAligned)
        return;
    const snap::Targets targets = clipFrameTargets();
    if (!lockedX) {
        const AxisHit hit = snapAxis(QList<double>(xs.begin(), xs.end()), targets.x, toleranceInUnits(pxPerNormX()));
        if (hit.snapped) {
            deltaNorm.rx() += hit.delta;
            m_guideX = hit.target;
        }
    }
    if (!lockedY) {
        const AxisHit hit = snapAxis(QList<double>(ys.begin(), ys.end()), targets.y, toleranceInUnits(pxPerNormY()));
        if (hit.snapped) {
            deltaNorm.ry() += hit.delta;
            m_guideY = hit.target;
        }
    }
}

bool PathEditCore::drag(const QPointF &vp, const ManipModifiers &mods)
{
    if (m_gesture == Gesture::None)
        return false;
    if (!m_moved && dist(vp, m_pressVp) < kDragThresholdPx)
        return false;
    m_moved = true;
    m_contours = m_start;
    m_guideX = m_guideY = -1.0;

    switch (m_gesture) {
    case Gesture::Move: {
        const QPointF startP = m_start[m_gc].vertices[m_gv].p;
        QPointF d = vp + m_grab - toView(startP);
        bool lockedX = false;
        bool lockedY = false;
        if (mods.constrain) {
            const QPointF locked = lockAxis(d);
            lockedX = locked.x() == 0.0;
            lockedY = locked.y() == 0.0;
            d = locked;
        }
        const QPointF target = toNorm(toView(startP) + d);
        QPointF dn = target - startP;
        applySnap(dn, {target.x()}, {target.y()}, mods, lockedX, lockedY);
        shiftSelection(dn);
        const QPointF now = startP + dn;
        m_hud = QStringLiteral("%1, %2").arg(num(now.x() * m_frame.width()), num(now.y() * m_frame.height()));
        break;
    }
    case Gesture::Handle: {
        const ShapeVertex &sv = m_start[m_gc].vertices[m_gv];
        const QPointF anchor = toView(sv.p);
        QPointF tip = vp + m_grab;
        if (mods.constrain)
            tip = constrain45(anchor, tip);
        const TangentMode mode = (m_gbreak || mods.breakTangent) ? TangentMode::Free : sv.mode;
        m_contours[m_gc].vertices[m_gv] = geom::applyTangentDrag(sv, m_gh, toNorm(tip), mode, false);
        const ShapeVertex &nv = m_contours[m_gc].vertices[m_gv];
        const QPointF h = m_gh == TangentHandle::In ? nv.in : nv.out;
        const double lenPx = std::hypot(h.x() * m_frame.width(), h.y() * m_frame.height());
        const double deg = std::atan2(h.y() * m_frame.height(), h.x() * m_frame.width()) * 180.0 / M_PI;
        m_hud = QStringLiteral("%1 px  %2°").arg(num(lenPx), num(deg));
        break;
    }
    case Gesture::PenDrag: {
        ShapeVertex &v = m_contours[m_gc].vertices[m_gv];
        QPointF tip = vp;
        if (mods.constrain)
            tip = constrain45(toView(v.p), tip);
        v.out = toNorm(tip) - v.p;
        v.in = -v.out;
        v.mode = TangentMode::Mirrored;
        const double lenPx = std::hypot(v.out.x() * m_frame.width(), v.out.y() * m_frame.height());
        const double deg = std::atan2(v.out.y() * m_frame.height(), v.out.x() * m_frame.width()) * 180.0 / M_PI;
        m_hud = QStringLiteral("%1 px  %2°").arg(num(lenPx), num(deg));
        break;
    }
    case Gesture::Bend: {
        ShapeContour &c = m_contours[m_gc];
        const int n = int(c.vertices.size());
        const ShapeVertex a = m_start[m_gc].vertices[m_gseg];
        const ShapeVertex b = m_start[m_gc].vertices[(m_gseg + 1) % n];
        const double t = std::clamp(m_gt, 0.12, 0.88);
        const QPointF delta = toNorm(vp) - geom::cubicPoint(a, b, m_gt);
        const QPointF shift = delta / (3.0 * t * (1.0 - t));
        c.vertices[m_gseg] = geom::applyTangentDrag(a, TangentHandle::Out, a.p + a.out + shift, a.mode, false);
        c.vertices[(m_gseg + 1) % n] = geom::applyTangentDrag(b, TangentHandle::In, b.p + b.in + shift, b.mode, false);
        m_hud.clear();
        break;
    }
    case Gesture::Body: {
        const QPointF raw = mods.constrain ? lockAxis(vp - m_pressVp) : vp - m_pressVp;
        QPointF dn = toNorm(m_pressVp + raw) - toNorm(m_pressVp);
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const ShapeContour &c : std::as_const(m_start)) {
            for (const ShapeVertex &v : c.vertices) {
                minX = std::min(minX, v.p.x());
                maxX = std::max(maxX, v.p.x());
                minY = std::min(minY, v.p.y());
                maxY = std::max(maxY, v.p.y());
            }
        }
        const bool lockX = mods.constrain && std::abs(vp.x() - m_pressVp.x()) < std::abs(vp.y() - m_pressVp.y());
        const bool lockY = mods.constrain && !lockX;
        applySnap(dn, {minX + dn.x(), (minX + maxX) / 2 + dn.x(), maxX + dn.x()},
                  {minY + dn.y(), (minY + maxY) / 2 + dn.y(), maxY + dn.y()}, mods, lockX, lockY);
        for (ShapeContour &c : m_contours) {
            for (ShapeVertex &v : c.vertices)
                v.p += dn;
        }
        m_bodyMoved = true;
        m_hud = QStringLiteral("%1, %2").arg(num(dn.x() * m_frame.width()), num(dn.y() * m_frame.height()));
        break;
    }
    case Gesture::None:
        break;
    }
    return true;
}

void PathEditCore::endGesture()
{
    m_gesture = Gesture::None;
    if (m_hasPendingView) {
        m_hasPendingView = false;
        applyView(m_pendingView);
    }
    m_start.clear();
    m_hud.clear();
    m_guideX = m_guideY = -1.0;
}

void PathEditCore::cancelGesture()
{
    if (m_gesture != Gesture::None && !m_start.isEmpty())
        m_contours = m_start;
    endGesture();
}

// ---- pen ----

bool PathEditCore::penNearFirst(const QPointF &vp, double tolPx) const
{
    if (m_penContour < 0 || m_penContour >= m_contours.size())
        return false;
    const ShapeContour &c = m_contours[m_penContour];
    return c.vertices.size() >= 3 && dist(vp, toView(c.vertices.first().p)) <= tolPx;
}

bool PathEditCore::isOpenEnd(int c, int v) const
{
    if (c < 0 || c >= m_contours.size())
        return false;
    const ShapeContour &contour = m_contours[c];
    return !contour.closed && (v == 0 || v == contour.vertices.size() - 1);
}

QPointF PathEditCore::constrainedFromLast(const QPointF &vp, const ManipModifiers &mods) const
{
    if (!mods.constrain || m_penContour < 0 || m_contours[m_penContour].vertices.isEmpty())
        return vp;
    return constrain45(toView(m_contours[m_penContour].vertices.last().p), vp);
}

PathEditCore::PenResult PathEditCore::penPress(const QPointF &vp, const ManipModifiers &mods, double closeTolPx)
{
    if (m_penContour >= 0 && penNearFirst(vp, closeTolPx)) {
        m_contours[m_penContour].closed = true;
        m_selected = {{m_penContour, 0}};
        m_penContour = -1;
        return PenResult::Closed;
    }
    const QPointF p = toNorm(constrainedFromLast(vp, mods));
    ShapeVertex v;
    v.p = p;
    if (m_penContour < 0) {
        ShapeContour c;
        c.closed = false;
        c.vertices.append(v);
        m_contours.append(c);
        m_penContour = int(m_contours.size()) - 1;
    } else {
        m_contours[m_penContour].vertices.append(v);
    }
    m_selected = {{m_penContour, int(m_contours[m_penContour].vertices.size()) - 1}};
    return PenResult::Added;
}

bool PathEditCore::penContinue(int c, int v)
{
    if (!isOpenEnd(c, v))
        return false;
    ShapeContour &contour = m_contours[c];
    if (v == 0 && contour.vertices.size() > 1)
        geom::reverseContour(contour);
    m_penContour = c;
    m_selected = {{c, int(contour.vertices.size()) - 1}};
    return true;
}

void PathEditCore::penEnd()
{
    m_penContour = -1;
}

bool PathEditCore::penUndo()
{
    if (m_penContour < 0 || m_penContour >= m_contours.size())
        return false;
    ShapeContour &c = m_contours[m_penContour];
    c.vertices.removeLast();
    if (c.vertices.isEmpty()) {
        m_contours.remove(m_penContour);
        m_penContour = -1;
        m_selected.clear();
    } else {
        m_selected = {{m_penContour, int(c.vertices.size()) - 1}};
    }
    return true;
}

// ---- drawing ----

QString PathEditCore::outlineSvg() const
{
    QString d;
    for (const ShapeContour &c : m_contours) {
        const QVector<QPointF> pts = geom::flatten(c, kDrawSteps);
        if (pts.isEmpty())
            continue;
        for (int i = 0; i < pts.size(); ++i) {
            const QPointF p = toView(pts[i]);
            d += (i == 0 ? QLatin1String("M") : QLatin1String("L")) + num(p.x()) + QLatin1Char(' ') + num(p.y());
        }
        if (c.closed && pts.size() > 1)
            d += QLatin1Char('Z');
    }
    return d.isEmpty() ? QStringLiteral("M0 0") : d;
}

QString PathEditCore::handleSvg() const
{
    QString d;
    for (const HandleView &h : visibleHandles()) {
        d += QLatin1String("M") + num(h.anchor.x()) + QLatin1Char(' ') + num(h.anchor.y()) + QLatin1String("L")
             + num(h.tip.x()) + QLatin1Char(' ') + num(h.tip.y());
    }
    return d.isEmpty() ? QStringLiteral("M0 0") : d;
}

QString PathEditCore::rubberSvg(const QPointF &vp, const ManipModifiers &mods) const
{
    if (m_penContour < 0 || m_penContour >= m_contours.size() || m_contours[m_penContour].vertices.isEmpty())
        return QStringLiteral("M0 0");
    const ShapeVertex &last = m_contours[m_penContour].vertices.last();
    ShapeVertex end;
    end.p = toNorm(constrainedFromLast(vp, mods));
    QString d;
    for (int k = 0; k <= kDrawSteps; ++k) {
        const QPointF p = toView(geom::cubicPoint(last, end, double(k) / kDrawSteps));
        d += (k == 0 ? QLatin1String("M") : QLatin1String("L")) + num(p.x()) + QLatin1Char(' ') + num(p.y());
    }
    return d;
}

} // namespace drift::manip
