#include "ui/manip/ClipFrameMapper.h"

#include <QVector4D>

#include <algorithm>

namespace drift::manip {

namespace {

QPointF project(const QMatrix4x4 &m, const QPointF &p)
{
    const QVector4D v = m.map(QVector4D(float(p.x()), float(p.y()), 0.0f, 1.0f));
    return v.w() != 0.0f ? QPointF(v.x() / v.w(), v.y() / v.w()) : QPointF(v.x(), v.y());
}

// What a z = 0 point sees of a matrix: rows and columns x, y and w only, so a tilted plane inverts
// exactly instead of through a z the plane no longer has.
QMatrix4x4 planeHomography(const QMatrix4x4 &m)
{
    return QMatrix4x4(m(0, 0), m(0, 1), 0, m(0, 3),
                      m(1, 0), m(1, 1), 0, m(1, 3),
                      0, 0, 1, 0,
                      m(3, 0), m(3, 1), 0, m(3, 3));
}

} // namespace

void ClipFrameMapper::setViewport(const QSizeF &canvas, const QRectF &viewportRect)
{
    m_canvas = canvas;
    m_viewport = viewportRect;
}

void ClipFrameMapper::setClip(const QRectF &layoutRect, double rotationDegrees)
{
    m_clip = layoutRect;
    m_rotation = rotationDegrees;
    m_hasPose = false;
}

void ClipFrameMapper::setPivot(const QPointF &layoutPivot)
{
    m_pivot = layoutPivot;
    m_hasPivot = true;
}

void ClipFrameMapper::setPose(const QMatrix4x4 &layoutToCanvas)
{
    m_pose = layoutToCanvas;
    m_hasPose = true;
}

void ClipFrameMapper::setParent(const QMatrix4x4 &parentViewportMatrix)
{
    m_parentInverse = planeHomography(parentViewportMatrix).inverted();
    m_hasParent = true;
}

double ClipFrameMapper::viewScale() const
{
    return m_canvas.width() > 0.0 ? m_viewport.width() / m_canvas.width() : 1.0;
}

QMatrix4x4 ClipFrameMapper::pose() const
{
    if (m_hasPose)
        return m_pose;
    const QPointF pivot = m_hasPivot ? m_pivot : QPointF(m_clip.width() / 2.0, m_clip.height() / 2.0);
    QMatrix4x4 m;
    m.translate(float(m_clip.x() + pivot.x()), float(m_clip.y() + pivot.y()));
    m.rotate(float(m_rotation), 0.0f, 0.0f, 1.0f);
    m.translate(float(-pivot.x()), float(-pivot.y()));
    return m;
}

QPointF ClipFrameMapper::canvasToViewport(const QPointF &p) const
{
    const double s = viewScale();
    return m_viewport.topLeft() + p * s;
}

QPointF ClipFrameMapper::viewportToCanvas(const QPointF &p) const
{
    const double s = viewScale();
    return (p - m_viewport.topLeft()) / (s != 0.0 ? s : 1.0);
}

QPointF ClipFrameMapper::layoutToCanvas(const QPointF &p) const
{
    return project(pose(), p);
}

QPointF ClipFrameMapper::canvasToLayout(const QPointF &p) const
{
    return project(planeHomography(pose()).inverted(), p);
}

QPointF ClipFrameMapper::clipToCanvas(const QPointF &n) const
{
    return layoutToCanvas({n.x() * m_clip.width(), n.y() * m_clip.height()});
}

QPointF ClipFrameMapper::canvasToClip(const QPointF &p) const
{
    const QPointF l = canvasToLayout(p);
    return {l.x() / std::max(1e-9, m_clip.width()), l.y() / std::max(1e-9, m_clip.height())};
}

QPointF ClipFrameMapper::clipToViewport(const QPointF &n) const
{
    return canvasToViewport(clipToCanvas(n));
}

QPointF ClipFrameMapper::viewportToClip(const QPointF &p) const
{
    return canvasToClip(viewportToCanvas(p));
}

QPointF ClipFrameMapper::toParentLocal(const QPointF &viewportPoint) const
{
    return m_hasParent ? project(m_parentInverse, viewportPoint) : viewportPoint;
}

} // namespace drift::manip
