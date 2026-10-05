#pragma once

#include "Clip.h"
#include "Time.h"

#include <QPointF>
#include <QString>
#include <QVector>

namespace drift {

struct StabilizeKeyframe
{
    TimeUs timeUs = 0;
    double dx = 0.0;
    double dy = 0.0;
};

struct StabilizePlan
{
    QVector<StabilizeKeyframe> keys;
};

// One (vx, vy) translation in source pixels for each detected frame, in decode
// order. Empty when the file cannot be parsed.
QVector<QPointF> readTrfFrameTranslations(const QString &path);

// Indices (including 0 and n-1) that reconstruct `points` within `epsilon` by
// linearly interpolating between breakpoints. Used to place sparse keys on a
// camera path instead of one key per frame.
QVector<int> piecewiseLinearBreakpoints(const QVector<QPointF> &points, double epsilon);

// Builds clip-local X/Y offsets from a vid.stab .trf whose first frame sits at source time
// trfStartUs. Keys are a piecewise-linear fit of the compensation path, not one per frame.
StabilizePlan planStabilizeKeyframes(const QString &trfPath, TimeUs trfStartUs, const Clip &clip,
                                     double fps, double scaleX, double scaleY, int smoothing,
                                     bool tripod, double epsilonCanvasPx = 1.0);

void captureStabilizeRestPose(Clip &clip);
void restoreStabilizeRestPose(Clip &clip);
void applyStabilizePlan(Clip &clip, const StabilizePlan &plan);

} // namespace drift
