#pragma once

#include <QRectF>

namespace drift {

// Box `frame` (normalised to the displayed source, as Clip::sourceFrame) and the source turned
// clockwise by `angleDeg` about its centre (Clip::sourceFrameAngle), as drift::cropSourceFrame
// cuts it. True when every point of the box shows picture (and the box is inside the frame).
// srcW/srcH are the displayed source size in pixels; unknown (<= 0) counts as covered.
bool sourceFrameCovered(const QRectF &frame, double angleDeg, double srcW, double srcH,
                        double epsilon = 1e-9);

// The box, shrunk about its centre (aspect kept) only as far as it must to show picture only.
// If the centre is so close to the turned picture's edge that the box would fall below
// minFraction (of the source, either side), the centre moves toward the middle too.
// Identity when it already fits, at angle 0, or when srcW/srcH are not positive.
//
// When the box does shrink it is also inset by half a source pixel on its shorter side (aspect
// exact): the renderer smooths the turned picture, so an exactly touching edge would leave a
// faint transparent row.
QRectF fitCropInsideRotation(const QRectF &frame, double angleDeg, double srcW, double srcH,
                             double minFraction = 0.08);

} // namespace drift
