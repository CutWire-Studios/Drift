#include "core/edit/MaskSpace.h"

#include <QtMath>

#include <cmath>

namespace drift {

HostFrame hostFrameAt(const Project &project, const Clip &host, TimeUs timelineUs)
{
    const TimeUs relative = qMax<TimeUs>(0, timelineUs - host.timelineStart);
    const auto value = [&](const KeyframeTrack<double> &track, double fallback) {
        return track.isEmpty() ? fallback : track.evaluateAt(relative);
    };
    HostFrame f;
    f.w = qMax(1.0, value(host.transformW, project.width()));
    f.h = qMax(1.0, value(host.transformH, project.height()));
    f.centre = QPointF(value(host.transformX, 0.0) + f.w / 2.0, value(host.transformY, 0.0) + f.h / 2.0);
    f.rotation = value(host.rotation, 0.0);
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
