#include "ui/nodes/NodeGridItem.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometry>
#include <QSGGeometryNode>
#include <QtMath>

#include <cmath>

namespace {

constexpr int kMaxDots = 40000;

QSGGeometryNode *makeLayer()
{
    auto *node = new QSGGeometryNode;
    auto *geometry = new QSGGeometry(QSGGeometry::defaultAttributes_Point2D(), 0);
    geometry->setDrawingMode(QSGGeometry::DrawTriangles);
    node->setGeometry(geometry);
    node->setFlag(QSGNode::OwnsGeometry);
    node->setMaterial(new QSGFlatColorMaterial);
    node->setFlag(QSGNode::OwnsMaterial);
    return node;
}

void applyColor(QSGGeometryNode *node, const QColor &c)
{
    auto *m = static_cast<QSGFlatColorMaterial *>(node->material());
    const qreal a = c.alphaF();
    m->setColor(QColor::fromRgbF(c.redF() * a, c.greenF() * a, c.blueF() * a, a));
    node->markDirty(QSGNode::DirtyMaterial);
}

void fillDots(QSGGeometryNode *node, const QVector<QPointF> &dots, qreal half)
{
    QSGGeometry *g = node->geometry();
    g->allocate(int(dots.size()) * 6);
    auto *v = g->vertexDataAsPoint2D();
    for (const QPointF &p : dots) {
        const float l = float(p.x() - half), r = float(p.x() + half);
        const float t = float(p.y() - half), b = float(p.y() + half);
        v[0].set(l, t); v[1].set(r, t); v[2].set(l, b);
        v[3].set(r, t); v[4].set(r, b); v[5].set(l, b);
        v += 6;
    }
    node->markDirty(QSGNode::DirtyGeometry);
}

}  // namespace

NodeGridItem::NodeGridItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

#define DRIFT_GRID_SETTER(Type, name, Name)                          \
    void NodeGridItem::set##Name(Type v)                             \
    {                                                                \
        if (m_##name == v)                                           \
            return;                                                  \
        m_##name = v;                                                \
        update();                                                    \
        emit name##Changed();                                        \
    }

DRIFT_GRID_SETTER(qreal, zoom, Zoom)
DRIFT_GRID_SETTER(qreal, panX, PanX)
DRIFT_GRID_SETTER(qreal, panY, PanY)
DRIFT_GRID_SETTER(qreal, spacing, Spacing)
DRIFT_GRID_SETTER(int, majorEvery, MajorEvery)
DRIFT_GRID_SETTER(const QColor &, minorColor, MinorColor)
DRIFT_GRID_SETTER(const QColor &, majorColor, MajorColor)

#undef DRIFT_GRID_SETTER

QSGNode *NodeGridItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QSGNode *root = old;
    QSGGeometryNode *minor = nullptr;
    QSGGeometryNode *major = nullptr;
    if (!root) {
        root = new QSGNode;
        minor = makeLayer();
        major = makeLayer();
        root->appendChildNode(minor);
        root->appendChildNode(major);
    } else {
        minor = static_cast<QSGGeometryNode *>(root->childAtIndex(0));
        major = static_cast<QSGGeometryNode *>(root->childAtIndex(1));
    }
    applyColor(minor, m_minorColor);
    applyColor(major, m_majorColor);

    QVector<QPointF> minorDots, majorDots;
    if (width() > 0 && height() > 0 && m_zoom > 0 && m_spacing > 0) {
        // Thin the grid out while zoomed far out so dots stay at least ~10px apart.
        int step = 1;
        while (m_spacing * step * m_zoom < 10.0 && step < (1 << 12))
            step *= 2;
        const qreal cell = m_spacing * step;
        const int majorStep = qMax(1, m_majorEvery);

        const qreal wx0 = -m_panX / m_zoom;
        const qreal wy0 = -m_panY / m_zoom;
        const qreal wx1 = (width() - m_panX) / m_zoom;
        const qreal wy1 = (height() - m_panY) / m_zoom;
        const qint64 i0 = qint64(std::floor(wx0 / cell)), i1 = qint64(std::ceil(wx1 / cell));
        const qint64 j0 = qint64(std::floor(wy0 / cell)), j1 = qint64(std::ceil(wy1 / cell));

        if ((i1 - i0 + 1) * (j1 - j0 + 1) <= kMaxDots) {
            for (qint64 i = i0; i <= i1; ++i) {
                const qreal sx = m_panX + i * cell * m_zoom;
                for (qint64 j = j0; j <= j1; ++j) {
                    const QPointF p(sx, m_panY + j * cell * m_zoom);
                    const bool isMajor = (((i % majorStep) + majorStep) % majorStep == 0)
                                      && (((j % majorStep) + majorStep) % majorStep == 0);
                    (isMajor ? majorDots : minorDots).append(p);
                }
            }
        }
    }
    fillDots(minor, minorDots, 1.0);
    fillDots(major, majorDots, 1.5);
    return root;
}
