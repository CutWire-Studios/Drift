#pragma once

#include <QImage>
#include <QRectF>

namespace drift {

// Cuts a clip's source frame (Clip::sourceFrame, normalised to `image`) out of a decoded frame.
// A non-zero `angle` straightens first: the picture is turned clockwise by that many degrees
// about its centre, in pixels so a non-square source does not skew, and the frame is cut from the
// turned picture. Where the turned picture does not reach, the result is transparent. The GPU
// path (GpuCompositor's straightened crop) samples the same mapping.
QImage cropSourceFrame(const QImage &image, const QRectF &frame, double angle);

} // namespace drift
