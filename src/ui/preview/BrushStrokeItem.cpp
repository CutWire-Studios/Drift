#include "ui/preview/BrushStrokeItem.h"

#include "app/editor/sessions/MaskingController.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <algorithm>
#include <cmath>

namespace {

struct Vertex
{
    float x;
    float y;
};

void addTriangle(QVector<Vertex> &out, QPointF a, QPointF b, QPointF c)
{
    out.append({float(a.x()), float(a.y())});
    out.append({float(b.x()), float(b.y())});
    out.append({float(c.x()), float(c.y())});
}

void addDisc(QVector<Vertex> &out, QPointF centre, double radius)
{
    const int sides = std::clamp(static_cast<int>(radius / 2.0) + 8, 8, 32);
    QPointF prev = centre + QPointF(radius, 0);
    for (int i = 1; i <= sides; ++i) {
        const double a = 2.0 * M_PI * i / sides;
        const QPointF next = centre + QPointF(std::cos(a) * radius, std::sin(a) * radius);
        addTriangle(out, centre, prev, next);
        prev = next;
    }
}

void addSegment(QVector<Vertex> &out, QPointF from, double r0, QPointF to, double r1)
{
    const QPointF d = to - from;
    const double len = std::hypot(d.x(), d.y());
    if (len < 1e-6)
        return;
    const QPointF n(-d.y() / len, d.x() / len);
    const QPointF a = from + n * r0;
    const QPointF b = from - n * r0;
    const QPointF c = to + n * r1;
    const QPointF e = to - n * r1;
    addTriangle(out, a, b, c);
    addTriangle(out, b, e, c);
}

} // namespace

BrushStrokeItem::BrushStrokeItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

QObject *BrushStrokeItem::masking() const
{
    return m_masking;
}

void BrushStrokeItem::setMasking(QObject *masking)
{
    auto *controller = qobject_cast<MaskingController *>(masking);
    if (controller == m_masking)
        return;
    if (m_masking)
        disconnect(m_masking, &MaskingController::strokePreviewChanged, this, nullptr);
    m_masking = controller;
    if (m_masking) {
        connect(m_masking, &MaskingController::strokePreviewChanged, this, [this] {
            emit strokeChanged();
            update();
        });
    }
    emit maskingChanged();
    emit strokeChanged();
    update();
}

void BrushStrokeItem::setStrokeIndex(int index)
{
    if (index == m_strokeIndex)
        return;
    m_strokeIndex = index;
    emit strokeIndexChanged();
    emit strokeChanged();
    update();
}

void BrushStrokeItem::setColor(const QColor &color)
{
    if (color == m_color)
        return;
    m_color = color;
    emit colorChanged();
    update();
}

bool BrushStrokeItem::erase() const
{
    const BrushPreviewStroke *s = m_masking ? m_masking->previewStroke(m_strokeIndex) : nullptr;
    return s && s->erase;
}

bool BrushStrokeItem::hasStroke() const
{
    return m_masking && m_masking->previewStroke(m_strokeIndex) != nullptr;
}

double BrushStrokeItem::strokeOpacity() const
{
    const BrushPreviewStroke *s = m_masking ? m_masking->previewStroke(m_strokeIndex) : nullptr;
    return s ? 0.35 + 0.5 * s->opacity : 0.0;
}

void BrushStrokeItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

QSGNode *BrushStrokeItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGGeometryNode *>(old);
    const BrushPreviewStroke *stroke = m_masking ? m_masking->previewStroke(m_strokeIndex) : nullptr;
    if (!stroke || stroke->points.isEmpty()) {
        delete node;
        return nullptr;
    }

    const double w = width();
    const double h = height();
    const int n = stroke->points.size();
    QVector<QPointF> pos(n);
    QVector<double> radius(n);
    for (int i = 0; i < n; ++i) {
        pos[i] = QPointF(stroke->points.at(i).x() * w, stroke->points.at(i).y() * h);
        radius[i] = std::max(0.5, stroke->size * w / 2.0 * brushPressureScale(stroke->pressures.at(i),
                                                                              stroke->pressureSize));
    }

    QVector<Vertex> verts;
    verts.reserve(n * 12);
    for (int i = 0; i < n; ++i) {
        if (i > 0)
            addSegment(verts, pos.at(i - 1), radius.at(i - 1), pos.at(i), radius.at(i));
        // A point in a nearly straight run is covered by its two segments.
        bool needsDisc = true;
        if (i > 0 && i + 1 < n) {
            const QPointF a = pos.at(i) - pos.at(i - 1);
            const QPointF b = pos.at(i + 1) - pos.at(i);
            const double la = std::hypot(a.x(), a.y());
            const double lb = std::hypot(b.x(), b.y());
            if (la > 1e-6 && lb > 1e-6)
                needsDisc = (a.x() * b.x() + a.y() * b.y()) / (la * lb) < 0.985
                            || std::abs(radius.at(i) - radius.at(i - 1)) > 0.5;
        }
        if (needsDisc)
            addDisc(verts, pos.at(i), radius.at(i));
    }

    QSGGeometry *geometry;
    QSGFlatColorMaterial *material;
    if (!node) {
        node = new QSGGeometryNode;
        geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        material = new QSGFlatColorMaterial;
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
    }
    geometry = node->geometry();
    material = static_cast<QSGFlatColorMaterial *>(node->material());
    material->setColor(m_color);
    geometry->allocate(verts.size());
    std::copy(verts.cbegin(), verts.cend(), reinterpret_cast<Vertex *>(geometry->vertexData()));
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}
