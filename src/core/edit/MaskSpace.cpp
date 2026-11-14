#include "core/edit/MaskSpace.h"

#include <QtMath>

#include <cmath>

namespace drift {

HostFrame hostFrameAt(const Project &project, const Clip &host, TimeUs timelineUs)
{
    const TimeUs relative = qMax<TimeUs>(0, timelineUs - host.timelineStart);
    const ClipLayout layout = clipLayoutAt(host, relative, QSizeF(project.width(), project.height()));
    const QRectF box = layout.box();
    HostFrame f;
    f.w = qMax(1.0, qAbs(box.width()));
    f.h = qMax(1.0, qAbs(box.height()));
    f.rotation = layout.rotation;
    const double a = qDegreesToRadians(layout.rotation);
    const QPointF offset = box.center() - layout.pivot();
    f.centre = layout.pivot() + QPointF(offset.x() * std::cos(a) - offset.y() * std::sin(a),
                                        offset.x() * std::sin(a) + offset.y() * std::cos(a));
    return f;
}

QPointF hostFrameToCanvas(const Project &project, const Clip &host, TimeUs timelineUs, QPointF normalized)
{
    const HostFrame f = hostFrameAt(project, host, timelineUs);
    const double a = qDegreesToRadians(f.rotation);
    const QPointF local((normalized.x() - 0.5) * f.w, (normalized.y() - 0.5) * f.h);
    const QPointF canvas = f.centre + QPointF(local.x() * std::cos(a) - local.y() * std::sin(a),
                                              local.x() * std::sin(a) + local.y() * std::cos(a));
    return QPointF(canvas.x() / qMax(1, project.width()), canvas.y() / qMax(1, project.height()));
}

QPointF canvasToHostFrame(const Project &project, const Clip &host, TimeUs timelineUs, QPointF canvasNormalized)
{
    const HostFrame f = hostFrameAt(project, host, timelineUs);
    const double a = qDegreesToRadians(-f.rotation);
    const QPointF d(canvasNormalized.x() * project.width() - f.centre.x(),
                    canvasNormalized.y() * project.height() - f.centre.y());
    const QPointF local(d.x() * std::cos(a) - d.y() * std::sin(a), d.x() * std::sin(a) + d.y() * std::cos(a));
    return QPointF(local.x() / f.w + 0.5, local.y() / f.h + 0.5);
}

QSizeF hostFrameSizeToCanvas(const Project &project, const Clip &host, TimeUs timelineUs, QSizeF normalized)
{
    const HostFrame f = hostFrameAt(project, host, timelineUs);
    return QSizeF(normalized.width() * f.w / qMax(1, project.width()),
                  normalized.height() * f.h / qMax(1, project.height()));
}

} // namespace drift
