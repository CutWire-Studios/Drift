#include "ui/preview/ConfidenceStripItem.h"

#include "ui/preview/TrackerOverlayMath.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>

#include <algorithm>
#include <cmath>
#include <map>

ConfidenceStripItem::ConfidenceStripItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    connect(this, &ConfidenceStripItem::colorsChanged, this, [this] { update(); });
}

QObject *ConfidenceStripItem::tracking() const
{
    return m_tracking;
}

void ConfidenceStripItem::setTracking(QObject *tracking)
{
    auto *controller = qobject_cast<TrackingController *>(tracking);
    if (controller == m_tracking)
        return;
    if (m_tracking)
        disconnect(m_tracking, nullptr, this, nullptr);
    m_tracking = controller;
    if (m_tracking) {
        connect(m_tracking, &TrackingController::samplesUpdated, this, [this](const QString &id) {
            if (id == m_trackerId)
                update();
        });
        connect(m_tracking, &TrackingController::trackersChanged, this, [this] { update(); });
    }
    emit trackingChanged();
    update();
}

void ConfidenceStripItem::setClipId(const QString &id)
{
    if (id == m_clipId)
        return;
    m_clipId = id;
    emit clipIdChanged();
    update();
}

void ConfidenceStripItem::setTrackerId(const QString &id)
{
    if (id == m_trackerId)
        return;
    m_trackerId = id;
    emit trackerIdChanged();
    update();
}

void ConfidenceStripItem::setMinConfidence(double value)
{
    if (value == m_minConfidence)
        return;
    m_minConfidence = value;
    emit minConfidenceChanged();
    update();
}

void ConfidenceStripItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        update();
}

QSGNode *ConfidenceStripItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    using drift::trackerview::StripTint;
    auto *root = old ? old : new QSGNode;
    while (QSGNode *child = root->firstChild()) {
        root->removeChildNode(child);
        delete child;
    }
    // The scene graph syncs with the GUI thread blocked, so reading the project here is safe.
    const int buckets = std::max(20, int(std::floor(width() / 3)));
    const QVector<double> values = m_tracking ? m_tracking->confidenceBuckets(m_clipId, m_trackerId, buckets)
                                              : QVector<double>{};
    if (values.isEmpty() || width() <= 0)
        return root;

    const double bucketWidth = width() / values.size();
    std::map<int, QVector<float>> byTint;
    for (const auto &run : drift::trackerview::stripRuns(values, m_minConfidence)) {
        const double x0 = run.first * bucketWidth;
        const double x1 = std::min(width(), (run.first + run.count) * bucketWidth + 0.5);
        const double y1 = height();
        QVector<float> &v = byTint[int(run.tint)];
        const float q[12] = {float(x0), 0, float(x1), 0, float(x1), float(y1),
                             float(x0), 0, float(x1), float(y1), float(x0), float(y1)};
        v.append(QVector<float>(q, q + 12));
    }
    for (const auto &[tint, verts] : byTint) {
        QColor color;
        switch (StripTint(tint)) {
        case StripTint::Empty: color = m_empty; break;
        case StripTint::Bad: color = m_bad; break;
        case StripTint::Warn: color = m_warn; break;
        case StripTint::Good: color = m_good; break;
        }
        auto *node = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), verts.size() / 2);
        geometry->setDrawingMode(QSGGeometry::DrawTriangles);
        std::copy(verts.cbegin(), verts.cend(), static_cast<float *>(geometry->vertexData()));
        node->setGeometry(geometry);
        node->setFlag(QSGNode::OwnsGeometry);
        auto *material = new QSGFlatColorMaterial;
        material->setColor(color);
        node->setMaterial(material);
        node->setFlag(QSGNode::OwnsMaterial);
        root->appendChildNode(node);
    }
    return root;
}
