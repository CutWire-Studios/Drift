#pragma once

#include "core/base/Time.h"
#include "core/project/Project.h"

#include <QPointF>
#include <QSizeF>

namespace drift {

// Where a clip sits on the canvas at one time, in canvas pixels. A mask pinned to a clip is
// normalized to this rectangle; a mask on a standalone effects layer, to the canvas.
struct HostFrame
{
    QPointF centre;
    double w = 1;
    double h = 1;
    double rotation = 0;
};

HostFrame hostFrameAt(const Project &project, const Clip &host, TimeUs timelineUs);

// A point normalized to the host's frame, as a point normalized to the canvas. The host's
// rotation is applied.
QPointF hostFrameToCanvas(const Project &project, const Clip &host, TimeUs timelineUs, QPointF normalized);

// The inverse of hostFrameToCanvas.
QPointF canvasToHostFrame(const Project &project, const Clip &host, TimeUs timelineUs, QPointF canvasNormalized);

// A size normalized to the host's frame, as a size normalized to the canvas.
QSizeF hostFrameSizeToCanvas(const Project &project, const Clip &host, TimeUs timelineUs, QSizeF normalized);

} // namespace drift
