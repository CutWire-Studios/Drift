#pragma once

#include "Bezier.h"
#include "Time.h"

#include <QMap>
#include <QString>
#include <QtMath>

namespace drift {

// Named tangent shapes. These are *presets* that write handles onto a single key, not a
// separate interpolation system — except Hold, which is a step discontinuity and therefore
// not expressible as a bezier at all, so it stays a flag on the key.
enum class Interpolation { Linear, Hold, Ease };

inline QString interpolationToString(Interpolation mode)
{
    switch (mode) {
    case Interpolation::Hold:
        return QStringLiteral("hold");
    case Interpolation::Ease:
        return QStringLiteral("ease");
    case Interpolation::Linear:
    default:
        return QStringLiteral("linear");
    }
}

inline Interpolation interpolationFromString(const QString &mode)
{
    if (mode == QLatin1String("hold"))
        return Interpolation::Hold;
    if (mode == QLatin1String("ease"))
        return Interpolation::Ease;
    return Interpolation::Linear;
}

// A keyframe and its outgoing/incoming tangents.
//
// Handles are stored relative to the key, in curve space: dx in microseconds, dy in the
// property's own units. `outDx` runs forward (>= 0), `inDx` back (<= 0). Zero-length handles
// evaluate to a straight line, because x and y then share the same blend weights — so a
// default-constructed key is exactly the old Linear behaviour.
template<typename T>
struct Keyframe
{
    T value{};
    double inDx = 0.0;
    double inDy = 0.0;
    double outDx = 0.0;
    double outDy = 0.0;
    bool corner = false; // when false the editor holds the two tangents collinear
    bool hold = false;   // step: keep this value until the next key

    bool operator==(const Keyframe &other) const
    {
        return value == other.value && qFuzzyCompare(inDx + 1.0, other.inDx + 1.0)
            && qFuzzyCompare(inDy + 1.0, other.inDy + 1.0)
            && qFuzzyCompare(outDx + 1.0, other.outDx + 1.0)
            && qFuzzyCompare(outDy + 1.0, other.outDy + 1.0) && corner == other.corner
            && hold == other.hold;
    }
};

template<typename T>
class KeyframeTrack
{
public:
    bool isEmpty() const { return m_values.isEmpty(); }

    // Animation can be switched off for a property without discarding its keys: the track then
    // holds its first key's value for the whole clip, and switching it back on restores the
    // animation exactly as it was.
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled) { m_enabled = enabled; }

    void setKeyframe(TimeUs time, const T &value)
    {
        auto it = m_values.find(time);
        if (it != m_values.end()) {
            it->value = value; // retune in place; the key keeps the tangents it was given
            return;
        }
        Keyframe<T> key;
        key.value = value;
        m_values.insert(time, key);
    }

    void setKeyframe(TimeUs time, const Keyframe<T> &key) { m_values.insert(time, key); }

    void removeKeyframe(TimeUs time) { m_values.remove(time); }

    // Move every key along the clip's own timeline. Key times are clip-relative, so a clip that
    // keeps its material but changes where it starts — the tail of a split, for one — has to carry
    // its curve with it or the animation replays at the wrong moment. Keys that land before the
    // clip's start are dropped; the curve still evaluates from whatever key survives.
    void shiftBy(TimeUs delta)
    {
        if (delta == 0 || m_values.isEmpty())
            return;
        QMap<TimeUs, Keyframe<T>> moved;
        for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
            const TimeUs at = it.key() + delta;
            if (at >= 0)
                moved.insert(at, it.value());
        }
        m_values = moved;
    }

    const QMap<TimeUs, Keyframe<T>> &keyframes() const { return m_values; }

    // Mutable access for the curve editor. Null when there is no key exactly at `time`.
    Keyframe<T> *keyframeRef(TimeUs time)
    {
        auto it = m_values.find(time);
        return it == m_values.end() ? nullptr : &it.value();
    }

    // Returns the key time within tolerance, or -1 if none.
    TimeUs nearestKeyframe(TimeUs time, TimeUs tolerance) const
    {
        TimeUs best = -1;
        TimeUs bestDist = tolerance + 1;
        for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it) {
            const TimeUs d = qAbs(it.key() - time);
            if (d < bestDist) {
                bestDist = d;
                best = it.key();
            }
        }
        return bestDist <= tolerance ? best : TimeUs{-1};
    }

    // Writes the tangents a named shape implies onto one key. Ease uses flat tangents a third
    // of the way to each neighbour, which reproduces the smoothstep the old track-wide Ease
    // mode produced, exactly.
    void setEasing(TimeUs time, Interpolation mode)
    {
        auto it = m_values.find(time);
        if (it == m_values.end())
            return;

        it->hold = mode == Interpolation::Hold;
        it->corner = false;
        if (mode == Interpolation::Ease) {
            const TimeUs toPrev = it == m_values.begin() ? 0 : time - std::prev(it).key();
            auto next = std::next(it);
            const TimeUs toNext = next == m_values.end() ? 0 : next.key() - time;
            it->inDx = -static_cast<double>(toPrev) / 3.0;
            it->outDx = static_cast<double>(toNext) / 3.0;
            it->inDy = 0.0;
            it->outDy = 0.0;
        } else {
            it->inDx = 0.0;
            it->inDy = 0.0;
            it->outDx = 0.0;
            it->outDy = 0.0;
        }
    }

    // Best-effort classification for highlighting the preset chips. Hand-dragged tangents
    // match nothing, which is what leaves every chip unlit.
    Interpolation easingAt(TimeUs time) const
    {
        auto it = m_values.constFind(time);
        if (it == m_values.constEnd())
            return Interpolation::Linear;
        if (it->hold)
            return Interpolation::Hold;

        const bool flat = qFuzzyIsNull(it->inDy) && qFuzzyIsNull(it->outDy);
        if (!flat)
            return Interpolation::Linear; // no chip will match; treated as custom by callers
        if (qFuzzyIsNull(it->inDx) && qFuzzyIsNull(it->outDx))
            return Interpolation::Linear;
        return Interpolation::Ease;
    }

    // True when the key's tangents are not one of the presets, so the UI can show "custom".
    bool hasCustomTangents(TimeUs time) const
    {
        auto it = m_values.constFind(time);
        if (it == m_values.constEnd() || it->hold)
            return false;
        if (!qFuzzyIsNull(it->inDy) || !qFuzzyIsNull(it->outDy))
            return true;
        if (qFuzzyIsNull(it->inDx) && qFuzzyIsNull(it->outDx))
            return false;

        // Flat tangents still only count as Ease at the canonical third.
        const TimeUs time_ = it.key();
        const TimeUs toPrev = it == m_values.constBegin() ? 0 : time_ - std::prev(it).key();
        auto next = std::next(it);
        const TimeUs toNext = next == m_values.constEnd() ? 0 : next.key() - time_;
        const double wantIn = -static_cast<double>(toPrev) / 3.0;
        const double wantOut = static_cast<double>(toNext) / 3.0;
        return !qFuzzyCompare(it->inDx + 1.0, wantIn + 1.0)
            || !qFuzzyCompare(it->outDx + 1.0, wantOut + 1.0);
    }

    T evaluateAt(TimeUs time) const
    {
        if (m_values.isEmpty())
            return T{};
        if (!m_enabled)
            return m_values.constBegin()->value; // frozen at the first key

        auto it = m_values.lowerBound(time);
        if (it == m_values.end())
            return std::prev(it)->value;

        if (it.key() == time || it == m_values.begin())
            return it->value;

        const auto prev = std::prev(it);
        if (prev->hold)
            return prev->value;

        const double t0 = static_cast<double>(prev.key());
        const double t1 = static_cast<double>(it.key());
        const double span = t1 - t0;
        if (span <= 0.0)
            return it->value;

        const double v0 = static_cast<double>(prev->value);
        const double v1 = static_cast<double>(it->value);

        // Straight segment: skip the solve entirely. This is the common case.
        if (qFuzzyIsNull(prev->outDx) && qFuzzyIsNull(prev->outDy) && qFuzzyIsNull(it->inDx)
            && qFuzzyIsNull(it->inDy)) {
            return lerp(prev->value, it->value, (static_cast<double>(time) - t0) / span);
        }

        // Clamp the handles inside the segment so the curve stays single-valued in time and
        // the bisection below has something monotonic to search.
        const double x1 = qBound(t0, t0 + prev->outDx, t1);
        const double x2 = qBound(t0, t1 + it->inDx, t1);
        const double s = bezierParameterForX(t0, x1, x2, t1, static_cast<double>(time));
        const double y = cubicBezier(v0, v0 + prev->outDy, v1 + it->inDy, v1, s);
        return static_cast<T>(y);
    }

    // Break Qt implicit sharing so a cross-thread reader cannot race a detach on the live copy.
    void detachSharedData() { m_values.detach(); }

private:
    static double lerp(double a, double b, double t) { return a + (b - a) * t; }

    static float lerp(float a, float b, double t)
    {
        return static_cast<float>(a + (b - a) * static_cast<float>(t));
    }

    QMap<TimeUs, Keyframe<T>> m_values;
    bool m_enabled = true;
};

} // namespace drift
