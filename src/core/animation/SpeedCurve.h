#pragma once

#include "core/base/Time.h"

#include <QJsonArray>
#include <QList>
#include <QVector>

namespace drift {

// Speed ramps live in a wider range than the scalar speed slider: curves are where extreme
// ramps belong. The floor must stay above zero — at 0 the clip would take forever to play.
inline constexpr double kMinCurveSpeed = 0.1;
inline constexpr double kMaxCurveSpeed = 100.0;

// One control point of a speed curve. `pos` is normalised over the clip's trimmed source
// range in *playback* order — so it reads left-to-right in the editor whether or not the clip
// is reversed. The curve is therefore self-contained: nothing outside it has to be consulted
// to evaluate it, and a trim simply rescales the ramp over whatever source survives.
//
// The handles are the cubic's tangents in curve space (x = pos, y = speed), stored relative
// to the point. `corner` breaks the two apart; otherwise they are held collinear.
struct SpeedPoint
{
    double pos = 0.0;
    double speed = 1.0;
    double inDx = 0.0;
    double inDy = 0.0;
    double outDx = 0.0;
    double outDy = 0.0;
    bool corner = false;
};

// Variable playback rate across a clip.
//
// Speed is a function of position in the source; the timeline duration it produces is the
// integral of its reciprocal. Both directions of that mapping are needed per frame, so the
// curve is flattened into a fixed sample table once, up front, in setPoints():
//
//   * evaluation is const, allocation-free and lock-free, so the compositor thread can call
//     it directly;
//   * the table travels with the value, so the snapshot copies QUndoStack makes carry a
//     correct table instead of a cache someone forgot to invalidate.
class SpeedCurve
{
public:
    // Below two points there is no ramp to speak of, and the clip falls back to Clip::speed.
    bool isEmpty() const { return m_points.size() < 2; }

    const QList<SpeedPoint> &points() const { return m_points; }
    // Sorts, clamps and pins the ends to pos 0 and 1, then rebuilds the sample table.
    void setPoints(QList<SpeedPoint> points);
    void clear();

    double speedAt(double pos) const;

    // Timeline duration this curve stretches `sourceSpanUs` of media into.
    TimeUs retimedDurationUs(TimeUs sourceSpanUs) const;

    // Inverse mapping: how far into the source a timeline offset lands.
    TimeUs sourceOffsetForTimelineOffset(TimeUs timelineOffsetUs, TimeUs sourceSpanUs) const;

    // Forward mapping: when a given moment of source arrives on the timeline. Used to drag
    // keyframes along with the picture when a clip is retimed.
    TimeUs timelineOffsetForSourceOffset(TimeUs sourceOffsetUs, TimeUs sourceSpanUs) const;

    // Playback rate at a timeline offset — the tempo the audio has to be stretched by.
    double speedAtTimelineOffset(TimeUs timelineOffsetUs, TimeUs sourceSpanUs) const;

    // The ramp over [p0, p1] of this curve, renormalised onto its own 0..1 range. Used when a
    // curved clip is split: each half has to keep playing exactly as it did before the cut.
    SpeedCurve subRange(double p0, double p1) const;

    QJsonArray toJson() const;
    static SpeedCurve fromJson(const QJsonArray &array);

    // A constant-rate curve — the seed for a fresh editing session.
    static SpeedCurve flat(double speed);

    // Break Qt implicit sharing so a cross-thread reader cannot race a detach on the live copy.
    void detachSharedData()
    {
        m_points.detach();
        m_speeds.detach();
        m_cum.detach();
    }

private:
    void rebuild();
    // Index of the sample bucket holding `unitTime`, with the leftover time inside it.
    int bucketForUnitTime(double unitTime, double *localOut) const;

    QList<SpeedPoint> m_points;
    QVector<double> m_speeds; // speed at pos i/kSamples
    QVector<double> m_cum;    // cumulative integral of 1/speed, in units of the source span
};

} // namespace drift
