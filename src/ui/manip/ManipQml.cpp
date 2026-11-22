#include "ui/manip/ManipQml.h"

#include "ui/manip/SnapService.h"

namespace drift::manip {

ClipFrameMapperItem::ClipFrameMapperItem(QObject *parent) : QObject(parent) {}

template <typename T> void ClipFrameMapperItem::assign(T &field, const T &value)
{
    if (field == value)
        return;
    field = value;
    rebuild();
}

void ClipFrameMapperItem::rebuild()
{
    ClipFrameMapper next;
    next.setViewport(m_canvas, m_viewport);
    next.setClip(m_layout, m_rotation);
    if (m_hasPivot)
        next.setPivot(m_pivot);
    if (m_hasPose)
        next.setPose(m_pose);
    if (m_hasParent)
        next.setParent(m_parent);
    m_mapper = next;
    ++m_revision;
    emit changed();
}

void ClipFrameMapperItem::setCanvasSize(const QSizeF &v) { assign(m_canvas, v); }
void ClipFrameMapperItem::setViewportRect(const QRectF &v) { assign(m_viewport, v); }
void ClipFrameMapperItem::setLayoutRect(const QRectF &v) { assign(m_layout, v); }
void ClipFrameMapperItem::setRotation(double v) { assign(m_rotation, v); }
void ClipFrameMapperItem::setPivot(const QPointF &v) { assign(m_pivot, v); }
void ClipFrameMapperItem::setHasPivot(bool v) { assign(m_hasPivot, v); }
void ClipFrameMapperItem::setPose(const QMatrix4x4 &v) { assign(m_pose, v); }
void ClipFrameMapperItem::setHasPose(bool v) { assign(m_hasPose, v); }
void ClipFrameMapperItem::setParentMatrix(const QMatrix4x4 &v) { assign(m_parent, v); }
void ClipFrameMapperItem::setHasParent(bool v) { assign(m_hasParent, v); }

ManipService::ManipService(QObject *parent) : QObject(parent) {}

double ManipService::snapTolerancePx() const
{
    return drift::manip::snapTolerancePx();
}

ManipModifiers ManipService::fromQtFlags(int modifiers) const
{
    return ManipModifiers::fromQtFlags(modifiers);
}

QVariantMap ManipService::snapValue(double value, const QVariantList &targets, double screenPerUnit,
                                    bool noSnap) const
{
    QVariantMap out{{QStringLiteral("value"), value},
                    {QStringLiteral("snapped"), false},
                    {QStringLiteral("target"), -1.0}};
    if (noSnap)
        return out;
    QList<double> list;
    list.reserve(targets.size());
    for (const QVariant &t : targets)
        list.append(t.toDouble());
    const AxisHit hit = drift::manip::snapAxis({value}, list, toleranceInUnits(screenPerUnit));
    if (hit.snapped) {
        out[QStringLiteral("value")] = value + hit.delta;
        out[QStringLiteral("snapped")] = true;
        out[QStringLiteral("target")] = hit.target;
    }
    return out;
}

double ManipService::constrainAngle(double degrees, double stepDeg) const
{
    return drift::manip::constrainAngle(degrees, stepDeg);
}

} // namespace drift::manip

