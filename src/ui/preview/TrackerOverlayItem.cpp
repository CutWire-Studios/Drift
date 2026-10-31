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

void TrackerOverlayItem::setHandleSize(double size)
{
    if (size == m_handleSize)
        return;
    m_handleSize = size;
    emit handleSizeChanged();
    update();
}

void TrackerOverlayItem::setHandleMargin(double margin)
{
    if (margin == m_handleMargin)
        return;
    m_handleMargin = margin;
    emit handleMarginChanged();
}

void TrackerOverlayItem::refresh()
{
    if (m_drag != Drag::None)
        return;
    m_frame = m_tracking && !m_trackerId.isEmpty() ? m_tracking->frameView(m_clipId, m_trackerId, m_seconds)
                                                  : TrackingController::FrameView{};
    emit frameChanged();
    update();
}

QPointF TrackerOverlayItem::pointPos(int i) const
{
    const TrackingController::FramePoint &p = m_frame.points.at(i);
    if (m_drag == Drag::Point && m_dragIndex == i)
        return {m_dragNorm.x() * width(), m_dragNorm.y() * height()};
    return {p.x * width(), p.y * height()};
}

QPointF TrackerOverlayItem::cornerPos(int i) const
{
    const QPointF c = m_drag == Drag::Corner ? m_dragCorners.at(i) : m_frame.region.at(i);
    return {c.x() * width(), c.y() * height()};
}

QVariantMap TrackerOverlayItem::hitTest(double x, double y) const
{
    using namespace drift::trackerview;
    const QPointF at(x, y);
    for (int i = m_frame.region.size() - 1; i >= 0; --i) {
        if (cornerHit(at, cornerPos(i), m_handleSize, m_handleMargin))
            return {{QStringLiteral("kind"), QStringLiteral("corner")}, {QStringLiteral("index"), i}};
    }
    for (int i = m_frame.points.size() - 1; i >= 0; --i) {
        const TrackingController::FramePoint &p = m_frame.points.at(i);
        if (!p.has && !p.ghost)
            continue;
        if (pointHit(at, pointPos(i), p.patternW * width(), p.patternH * height()))
            return {{QStringLiteral("kind"), QStringLiteral("point")}, {QStringLiteral("index"), i}};
    }
    return {{QStringLiteral("kind"), QString()}, {QStringLiteral("index"), -1}};
}

bool TrackerOverlayItem::beginDrag(double x, double y)
{
    const QVariantMap hit = hitTest(x, y);
    const int index = hit.value(QStringLiteral("index")).toInt();
    if (index < 0)
        return false;
    m_dragIndex = index;
    m_dragMoved = false;
    if (hit.value(QStringLiteral("kind")).toString() == QStringLiteral("point")) {
        const QPointF centre = pointPos(index);
        m_dragGrab = QPointF(x, y) - centre;
        m_dragNorm = {m_frame.points.at(index).x, m_frame.points.at(index).y};
        m_drag = Drag::Point;
    } else {
        m_dragCorners = m_frame.region;
        m_drag = Drag::Corner;
    }
    return true;
}

void TrackerOverlayItem::dragTo(double x, double y)
{
    if (m_drag == Drag::None)
        return;
    const auto norm = [](double v, double size) { return std::clamp(v / std::max(1.0, size), 0.0, 1.0); };
    if (m_drag == Drag::Point) {
        const QPointF n(norm(x - m_dragGrab.x(), width()), norm(y - m_dragGrab.y(), height()));
        m_dragNorm = n;
        m_dragMoved = true;
        int t = -1;
        int c = -1;
        if (m_tracking && m_tracking->locateClip(m_clipId, &t, &c))
            m_tracking->previewSetTrackerPoint(t, c, m_trackerId, m_dragIndex, n.x(), n.y());
    } else {
        m_dragCorners[m_dragIndex] = QPointF(norm(x, width()), norm(y, height()));
        m_dragMoved = true;
    }
    update();
}

QString TrackerOverlayItem::endDrag()
{
    const Drag drag = m_drag;
    const bool moved = m_dragMoved;
    if (drag == Drag::Corner && moved && m_tracking) {
        int t = -1;
        int c = -1;
        if (m_tracking->locateClip(m_clipId, &t, &c)) {
            QVariantList region;
            for (const QPointF &p : m_dragCorners)
                region.append(QVariantMap{{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}});
            m_tracking->correctPlanarCorners(t, c, m_trackerId, region);
        }
    }
    m_drag = Drag::None;
    m_dragIndex = -1;
    m_dragMoved = false;
    m_dragCorners.clear();
    refresh();
    if (!moved)
        return {};
    return drag == Drag::Point ? QStringLiteral("point") : QStringLiteral("corner");
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
        QVector<Vertex> &out = batch(withAlpha(m_trail, 0.6));
        for (const QVector<QPointF> &trail : m_frame.trails) {
            for (int i = 1; i < trail.size(); ++i)
                line(out, {trail.at(i - 1).x() * w, trail.at(i - 1).y() * h}, {trail.at(i).x() * w, trail.at(i).y() * h},
                     m_lineWidth);
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
        const double pw = p.patternW * w;
        const double ph = p.patternH * h;
        QColor tint = m_warn;
        if (p.has) {
            switch (tintFor(p.confidence, m_minConfidence)) {
            case Tint::Bad: tint = m_bad; break;
            case Tint::Good: tint = m_good; break;
            case Tint::Warn: tint = m_warn; break;
            }
        }
        border(batch(withAlpha(tint, tint.alphaF() * opacity)), c.x() - pw / 2, c.y() - ph / 2, pw, ph, m_lineWidth);
        QVector<Vertex> &cross = batch(withAlpha(m_pattern, m_pattern.alphaF() * opacity));
        rect(cross, c.x() - 6, c.y() - 0.5, 12, 1);
        rect(cross, c.x() - 0.5, c.y() - 6, 1, 12);
        if (p.userKey) {
            const double r = 3.5 * std::sqrt(2.0);
            quad(batch(withAlpha(m_userKey, m_userKey.alphaF() * opacity)), {c.x(), c.y() - r}, {c.x() + r, c.y()},
                 {c.x(), c.y() + r}, {c.x() - r, c.y()});
        }
    }

    if (corners > 0) {
        QVector<Vertex> &handles = batch(m_handle);
        for (int i = 0; i < corners; ++i) {
            const QPointF c = cornerPos(i);
            rect(handles, c.x() - m_handleSize / 2, c.y() - m_handleSize / 2, m_handleSize, m_handleSize);
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
