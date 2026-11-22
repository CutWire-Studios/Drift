#include "ui/preview/TrackerOverlayItem.h"

#include "ui/preview/TrackerOverlayMath.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

using drift::trackerview::Tint;

constexpr int kPathRange = 10;

struct Vertex
{
    float x;
    float y;
};

using Batches = std::map<QRgb, QVector<Vertex>>;

void tri(QVector<Vertex> &out, QPointF a, QPointF b, QPointF c)
{
    out.append({float(a.x()), float(a.y())});
    out.append({float(b.x()), float(b.y())});
    out.append({float(c.x()), float(c.y())});
}

void quad(QVector<Vertex> &out, QPointF a, QPointF b, QPointF c, QPointF d)
{
    tri(out, a, b, c);
    tri(out, a, c, d);
}

void rect(QVector<Vertex> &out, double x, double y, double w, double h)
{
    quad(out, {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h});
}

void line(QVector<Vertex> &out, QPointF from, QPointF to, double width)
{
    const QPointF d = to - from;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-6)
        return;
    const QPointF n(-d.y() / len * width / 2, d.x() / len * width / 2);
    quad(out, from + n, from - n, to - n, to + n);
}

// A Rectangle-style border drawn inside the box.
void border(QVector<Vertex> &out, double x, double y, double w, double h, double t)
{
    t = std::min({t, w / 2, h / 2});
    rect(out, x, y, w, t);
    rect(out, x, y + h - t, w, t);
    rect(out, x, y + t, t, h - 2 * t);
    rect(out, x + w - t, y + t, t, h - 2 * t);
}

QColor withAlpha(QColor c, double a)
{
    c.setAlphaF(a);
    return c;
}

} // namespace

TrackerOverlayItem::TrackerOverlayItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

QObject *TrackerOverlayItem::tracking() const
{
    return m_tracking;
}

void TrackerOverlayItem::setTracking(QObject *tracking)
{
    auto *controller = qobject_cast<TrackingController *>(tracking);
    if (controller == m_tracking)
        return;
    m_tracking = controller;
    emit trackingChanged();
    refresh();
}

void TrackerOverlayItem::setClipId(const QString &id)
{
    if (id == m_clipId)
        return;
    m_clipId = id;
    emit clipIdChanged();
    refresh();
}

void TrackerOverlayItem::setTrackerId(const QString &id)
{
    if (id == m_trackerId)
        return;
    m_trackerId = id;
    if (!m_selected.isEmpty()) {
        m_selected.clear();
        emit selectionChanged();
    }
    emit trackerIdChanged();
    refresh();
}

void TrackerOverlayItem::setSeconds(double seconds)
{
    if (seconds == m_seconds)
        return;
    m_seconds = seconds;
    emit secondsChanged();
}

void TrackerOverlayItem::setMinConfidence(double value)
{
    if (value == m_minConfidence)
        return;
    m_minConfidence = value;
    emit minConfidenceChanged();
    update();
}

void TrackerOverlayItem::setShowTrails(bool show)
{
    if (show == m_showTrails)
        return;
    m_showTrails = show;
    emit showTrailsChanged();
    update();
}

void TrackerOverlayItem::setLineWidth(double width)
{
    if (width == m_lineWidth)
        return;
    m_lineWidth = width;
    emit lineWidthChanged();
    update();
}

void TrackerOverlayItem::bump()
{
    ++m_revision;
    emit layoutChanged();
    update();
}

QString TrackerOverlayItem::dragKind() const
{
    switch (m_drag) {
    case Drag::Point: return QStringLiteral("point");
    case Drag::Corner: return QStringLiteral("corner");
    case Drag::Pattern: return QStringLiteral("pattern");
    case Drag::Search: return QStringLiteral("search");
    case Drag::None: break;
    }
    return {};
}

void TrackerOverlayItem::refresh()
{
    if (m_drag != Drag::None)
        return;
    m_frame = m_tracking && !m_trackerId.isEmpty() ? m_tracking->frameView(m_clipId, m_trackerId, m_seconds)
                                                  : TrackingController::FrameView{};
    bool pruned = false;
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        if (*it >= m_frame.points.size()) {
            it = m_selected.erase(it);
            pruned = true;
        } else {
            ++it;
        }
    }
    if (pruned)
        emit selectionChanged();
    emit frameChanged();
    bump();
}

QPointF TrackerOverlayItem::pointPos(int i) const
{
    const TrackingController::FramePoint &p = m_frame.points.at(i);
    if (m_drag == Drag::Point && m_dragNorms.contains(i))
        return {m_dragNorms.value(i).x() * width(), m_dragNorms.value(i).y() * height()};
    return {p.x * width(), p.y * height()};
}

QPointF TrackerOverlayItem::cornerPos(int i) const
{
    const QPointF c = m_drag == Drag::Corner ? m_dragCorners.at(i) : m_frame.region.at(i);
    return {c.x() * width(), c.y() * height()};
}

QPointF TrackerOverlayItem::cornerAt(int index) const
{
    return index >= 0 && index < m_frame.region.size() ? cornerPos(index) : QPointF();
}

// Boxes are square in source pixels, and the item has the clip frame's aspect, so a normalized
// width w is w * width() px on both sides.
QSizeF TrackerOverlayItem::boxPx(const TrackingController::FramePoint &p, bool search) const
{
    if ((search && m_drag == Drag::Search) || (!search && m_drag == Drag::Pattern))
        return {m_dragWidth * width(), m_dragWidth * width()};
    return search ? QSizeF(p.searchW * width(), p.searchH * height()) : QSizeF(p.patternW * width(), p.patternH * height());
}

bool TrackerOverlayItem::isSelected(int index) const
{
    return m_selected.contains(index);
}

QVector<int> TrackerOverlayItem::targets() const
{
    QVector<int> out;
    for (int i = 0; i < m_frame.points.size(); ++i) {
        if (m_selected.isEmpty() || m_selected.contains(i))
            out.append(i);
    }
    return out;
}

QVariantMap TrackerOverlayItem::hitTest(double x, double y, double minSide) const
{
    using namespace drift::trackerview;
    const QPointF at(x, y);
    for (int i = m_frame.points.size() - 1; i >= 0; --i) {
        const TrackingController::FramePoint &p = m_frame.points.at(i);
        if (!p.has && !p.ghost)
            continue;
        const QSizeF box = boxPx(p, false);
        if (pointHit(at, pointPos(i), box.width(), box.height(), minSide))
            return {{QStringLiteral("kind"), QStringLiteral("point")}, {QStringLiteral("index"), i}};
    }
    return {{QStringLiteral("kind"), QString()}, {QStringLiteral("index"), -1}};
}

QVariantMap TrackerOverlayItem::pointInfo(int index) const
{
    using namespace drift::trackerview;
    if (index < 0 || index >= m_frame.points.size())
        return {};
    const TrackingController::FramePoint &p = m_frame.points.at(index);
    const QPointF c = pointPos(index);
    const QSizeF pattern = boxPx(p, false);
    const QSizeF search = boxPx(p, true);
    QString tint = QStringLiteral("warn");
    if (p.has) {
        switch (tintFor(p.confidence, m_minConfidence)) {
        case Tint::Bad: tint = QStringLiteral("bad"); break;
        case Tint::Good: tint = QStringLiteral("good"); break;
        case Tint::Warn: break;
        }
    }
    return {{QStringLiteral("x"), c.x()},
            {QStringLiteral("y"), c.y()},
            {QStringLiteral("pw"), pattern.width()},
            {QStringLiteral("ph"), pattern.height()},
            {QStringLiteral("sw"), search.width()},
            {QStringLiteral("sh"), search.height()},
            {QStringLiteral("nx"), c.x() / std::max(1.0, width())},
            {QStringLiteral("ny"), c.y() / std::max(1.0, height())},
            {QStringLiteral("boxW"), pattern.width() / std::max(1.0, width())},
            {QStringLiteral("confidence"), p.confidence},
            {QStringLiteral("tint"), tint},
            {QStringLiteral("has"), p.has},
            {QStringLiteral("ghost"), p.ghost},
            {QStringLiteral("userKey"), p.userKey},
            {QStringLiteral("shown"), p.has || p.ghost},
            {QStringLiteral("selected"), m_selected.isEmpty() || m_selected.contains(index)}};
}

bool TrackerOverlayItem::beginDrag(const QString &kind, int index, double x, double y, bool extend)
{
    if (m_drag != Drag::None)
        return false;
    m_dragMoved = false;
    m_dragPress = {x, y};
    if (kind == QLatin1String("corner")) {
        if (index < 0 || index >= m_frame.region.size())
            return false;
        m_dragCorners = m_frame.region;
        m_dragIndex = index;
        m_drag = Drag::Corner;
    } else if (kind == QLatin1String("pattern") || kind == QLatin1String("search")) {
        if (index < 0 || index >= m_frame.points.size())
            return false;
        const TrackingController::FramePoint &p = m_frame.points.at(index);
        m_dragWidth = kind == QLatin1String("search") ? p.searchW : p.patternW;
        m_dragIndex = index;
        m_drag = kind == QLatin1String("search") ? Drag::Search : Drag::Pattern;
    } else if (kind == QLatin1String("point")) {
        if (index < 0 || index >= m_frame.points.size())
            return false;
        const bool was = m_selected.contains(index);
        if (extend) {
            if (was)
                m_selected.remove(index);
            else
                m_selected.insert(index);
            emit selectionChanged();
            if (was) {
                bump();
                return false;
            }
        } else if (!was) {
            m_selected = {index};
            emit selectionChanged();
        }
        m_dragStart.clear();
        m_dragNorms.clear();
        for (const int i : std::as_const(m_selected)) {
            const QPointF at(m_frame.points.at(i).x, m_frame.points.at(i).y);
            m_dragStart.insert(i, at);
            m_dragNorms.insert(i, at);
        }
        m_dragIndex = index;
        m_drag = Drag::Point;
    } else {
        return false;
    }
    bump();
    return true;
}

void TrackerOverlayItem::dragTo(double x, double y, bool constrain)
{
    using namespace drift::trackerview;
    if (m_drag == Drag::None)
        return;
    m_dragMoved = true;
    if (m_drag == Drag::Point) {
        QPointF delta = QPointF(x, y) - m_dragPress;
        if (constrain)
            delta = constrainedDelta(delta);
        int t = -1;
        int c = -1;
        const bool located = m_tracking && m_tracking->locateClip(m_clipId, &t, &c);
        for (auto it = m_dragStart.cbegin(); it != m_dragStart.cend(); ++it) {
            const QPointF n = movedNorm(it.value(), delta, width(), height());
            m_dragNorms[it.key()] = n;
            if (located)
                m_tracking->previewSetTrackerPoint(t, c, m_trackerId, it.key(), n.x(), n.y());
        }
    } else if (m_drag == Drag::Corner) {
        m_dragCorners[m_dragIndex] = QPointF(std::clamp(x / std::max(1.0, width()), 0.0, 1.0),
                                             std::clamp(y / std::max(1.0, height()), 0.0, 1.0));
    } else {
        const TrackingController::FramePoint &p = m_frame.points.at(m_dragIndex);
        const bool search = m_drag == Drag::Search;
        const double lo = search ? p.patternW : 0.01;
        const double hi = search ? 0.9 : p.searchW > 0 ? p.searchW : 0.9;
        m_dragWidth = std::clamp(boxWidthFor(pointPos(m_dragIndex).x(), x, width()), std::min(lo, hi), hi);
    }
    bump();
}

QString TrackerOverlayItem::endDrag()
{
    const Drag drag = m_drag;
    const bool moved = m_dragMoved;
    const double dragWidth = m_dragWidth;
    const QVector<QPointF> corners = m_dragCorners;
    m_drag = Drag::None;
    m_dragIndex = -1;
    m_dragMoved = false;
    m_dragCorners.clear();
    m_dragNorms.clear();
    m_dragStart.clear();
    if (moved && m_tracking) {
        int t = -1;
        int c = -1;
        if (m_tracking->locateClip(m_clipId, &t, &c)) {
            if (drag == Drag::Corner) {
                QVariantList region;
                for (const QPointF &p : corners)
                    region.append(QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});
                m_tracking->correctPlanarCorners(t, c, m_trackerId, region);
            } else if (drag == Drag::Pattern || drag == Drag::Search) {
                m_tracking->setTrackerProperty(t, c, m_trackerId,
                                               drag == Drag::Pattern ? QStringLiteral("patternSize")
                                                                     : QStringLiteral("searchSize"),
                                               dragWidth);
            }
        }
    }
    refresh();
    if (!moved)
        return {};
    switch (drag) {
    case Drag::Point: return QStringLiteral("point");
    case Drag::Corner: return QStringLiteral("corner");
    case Drag::Pattern: return QStringLiteral("pattern");
    case Drag::Search: return QStringLiteral("search");
    case Drag::None: break;
    }
    return {};
}

void TrackerOverlayItem::cancelDrag()
{
    if (m_drag == Drag::None)
        return;
    m_drag = Drag::None;
    m_dragIndex = -1;
    m_dragMoved = false;
    m_dragCorners.clear();
    m_dragNorms.clear();
    m_dragStart.clear();
    refresh();
}

void TrackerOverlayItem::nudge(double dx, double dy)
{
    using namespace drift::trackerview;
    int t = -1;
    int c = -1;
    if (m_drag != Drag::None || !m_tracking || !m_tracking->locateClip(m_clipId, &t, &c))
        return;
    for (const int i : targets()) {
        const TrackingController::FramePoint &p = m_frame.points.at(i);
        if (!p.has && !p.ghost)
            continue;
        const QPointF n = movedNorm({p.x, p.y}, {dx, dy}, width(), height());
        m_tracking->previewSetTrackerPoint(t, c, m_trackerId, i, n.x(), n.y());
    }
    refresh();
}

void TrackerOverlayItem::selectInRect(double x0, double y0, double x1, double y1, bool extend)
{
    const QRectF rect = drift::trackerview::marqueeRect({x0, y0}, {x1, y1});
    QSet<int> next = extend ? m_selected : QSet<int>{};
    for (int i = 0; i < m_frame.points.size(); ++i) {
        const TrackingController::FramePoint &p = m_frame.points.at(i);
        if ((p.has || p.ghost) && rect.contains(pointPos(i)))
            next.insert(i);
    }
    if (next == m_selected)
        return;
    m_selected = next;
    emit selectionChanged();
    bump();
}

void TrackerOverlayItem::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    emit selectionChanged();
    bump();
}

void TrackerOverlayItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode *TrackerOverlayItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    using namespace drift::trackerview;
    auto *root = old ? old : new QSGNode;
    while (QSGNode *child = root->firstChild()) {
        root->removeChildNode(child);
        delete child;
    }

    const double w = width();
    const double h = height();
    Batches batches;
    const auto batch = [&](const QColor &c) -> QVector<Vertex> & { return batches[c.rgba()]; };

    if (m_showTrails) {
        QVector<Vertex> &out = batch(withAlpha(m_trail, 0.45));
        for (const QVector<QPointF> &trail : m_frame.trails) {
            for (int i = 1; i < trail.size(); ++i)
                line(out, {trail.at(i - 1).x() * w, trail.at(i - 1).y() * h}, {trail.at(i).x() * w, trail.at(i).y() * h},
                     m_lineWidth);
        }
        for (int k = 0; k < m_frame.trails.size(); ++k) {
            const QVector<QPointF> &trail = m_frame.trails.at(k);
            const QVector<int> &offsets = m_frame.trailOffsets.value(k);
            const QVector<bool> &keys = m_frame.trailKeys.value(k);
            for (int i = 0; i < trail.size() && i < offsets.size(); ++i) {
                const int offset = offsets.at(i);
                const bool key = keys.value(i);
                const double alpha = pathDotAlpha(offset, kPathRange);
                const double r = offset == 0 ? 3.5 : key ? 3.0 : 2.0;
                const QPointF c(trail.at(i).x() * w, trail.at(i).y() * h);
                const QColor &base = key ? m_userKey : m_trail;
                quad(batch(withAlpha(base, base.alphaF() * alpha)), {c.x(), c.y() - r}, {c.x() + r, c.y()},
                     {c.x(), c.y() + r}, {c.x() - r, c.y()});
            }
        }
    }

    const int corners = m_frame.region.size();
    if (corners >= 3) {
        QVector<QPointF> px(corners);
        for (int i = 0; i < corners; ++i)
            px[i] = cornerPos(i);
        const QVector<int> tris = triangulate(px);
        QVector<Vertex> &fill = batch(withAlpha(m_region, 0.12));
        for (int i = 0; i + 2 < tris.size(); i += 3)
            tri(fill, px.at(tris.at(i)), px.at(tris.at(i + 1)), px.at(tris.at(i + 2)));
        QVector<Vertex> &outline = batch(m_region);
        for (int i = 0; i < corners; ++i)
            line(outline, px.at(i), px.at((i + 1) % corners), m_lineWidth);
    }

    for (int i = 0; i < m_frame.points.size(); ++i) {
        const TrackingController::FramePoint &p = m_frame.points.at(i);
        if (!p.has && !p.ghost)
            continue;
        const double opacity = p.has ? 1.0 : 0.6;
        const QPointF c = pointPos(i);
        const QSizeF pattern = boxPx(p, false);
        const bool selected = m_selected.isEmpty() || m_selected.contains(i);
        const double pw = pattern.width();
        const double ph = pattern.height();
        QColor tint = m_warn;
        if (p.has) {
            switch (tintFor(p.confidence, m_minConfidence)) {
            case Tint::Bad: tint = m_bad; break;
            case Tint::Good: tint = m_good; break;
            case Tint::Warn: tint = m_warn; break;
            }
        }
        if (selected) {
            const QSizeF search = boxPx(p, true);
            if (search.width() > pw + 1)
                border(batch(withAlpha(m_search, m_search.alphaF() * opacity)), c.x() - search.width() / 2,
                       c.y() - search.height() / 2, search.width(), search.height(), m_lineWidth);
        }
        const bool picked = m_selected.contains(i);
        border(batch(withAlpha(tint, tint.alphaF() * opacity)), c.x() - pw / 2, c.y() - ph / 2, pw, ph,
               picked ? m_lineWidth * 2 : m_lineWidth);
        QVector<Vertex> &cross = batch(withAlpha(m_pattern, m_pattern.alphaF() * opacity));
        rect(cross, c.x() - 6, c.y() - 0.5, 12, 1);
        rect(cross, c.x() - 0.5, c.y() - 6, 1, 12);
        if (picked)
            rect(cross, c.x() - 2, c.y() - 2, 4, 4);
        if (p.userKey) {
            const double r = 3.5 * std::sqrt(2.0);
            quad(batch(withAlpha(m_userKey, m_userKey.alphaF() * opacity)), {c.x(), c.y() - r}, {c.x() + r, c.y()},
                 {c.x(), c.y() + r}, {c.x() - r, c.y()});
        }
    }

    for (const auto &[rgba, verts] : batches) {
        if (verts.isEmpty())
            continue;
        auto *node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), verts.size());
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        std::copy(verts.cbegin(), verts.cend(), reinterpret_cast<Vertex *>(geometry->vertexData()));
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        auto *material = new QSGFlatColorMaterial;
        material->setColor(QColor::fromRgba(rgba));
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    }
    return root;
}

#include "moc_TrackerOverlayItem.cpp"
