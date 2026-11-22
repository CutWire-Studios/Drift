#pragma once

#include "engine/compositor/PreviewSnap.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace drift::manip {

constexpr double kDefaultSnapTolerancePx = 8.0;
constexpr double kDefaultAngleStepDeg = 15.0;

// The one place the snap pull distance lives, in screen px.
double snapTolerancePx();
void setSnapTolerancePx(double px);
// The same tolerance in canvas (or clip-frame) units at a view scale of `screenPerUnit` px per unit.
double toleranceInUnits(double screenPerUnit);

struct AxisHit
{
    double delta = 0.0; // to add to the moving value
    double target = -1.0;
    bool snapped = false;
};

// The nearest target within `tolerance` of any candidate (a left/centre/right edge, say).
AxisHit snapAxis(const QList<double> &candidates, const QList<double> &targets, double tolerance);

// Canvas edges and centre, user guides, every other clip's edges and centre, and grid lines at
// multiples of `gridStep` (0 = no grid), all in canvas px.
snap::Targets canvasTargets(const QSizeF &canvas, const QList<double> &guidesX,
                            const QList<double> &guidesY, const QList<QRectF> &otherClips = {},
                            double gridStep = 0.0);

// A mask or crop lives in the clip's own frame: 0, 0.5 and 1 plus the guides, all normalized.
snap::Targets clipFrameTargets(const QList<double> &guidesX = {}, const QList<double> &guidesY = {});

struct RectSnap
{
    QPointF delta;
    double guideX = -1.0;
    double guideY = -1.0;
};
// An axis-aligned rect moved by `delta`, pulled so an edge or the centre lands on a target.
RectSnap snapRectMove(const QRectF &rect, const QPointF &delta, const snap::Targets &targets,
                      double tolerance, bool noSnap);

// PreviewSnap::snapMove behind the shared bypass.
snap::MoveSnap snapClipMove(const snap::PlacedClip &clip, const QPointF &delta,
                            const snap::Targets &targets, double tolerance, bool noSnap);

// Shift axis-lock: the smaller component of a move is dropped.
QPointF lockAxis(const QPointF &delta);
// Shift on a point: p moved onto the nearest 45 degree ray from `origin`.
QPointF constrain45(const QPointF &origin, const QPointF &p);
// Shift on a rotation: the angle on the step grid. Rotation is free otherwise.
double constrainAngle(double degrees, double stepDeg = kDefaultAngleStepDeg);
// Detent snapping: pulled onto the step grid only within `toleranceDeg` of it.
AxisHit snapAngle(double degrees, double stepDeg, double toleranceDeg, bool noSnap);

} // namespace drift::manip
