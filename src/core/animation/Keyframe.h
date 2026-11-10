#pragma once

#include "core/animation/Easing.h"
#include "core/base/Bezier.h"
#include "core/base/Time.h"

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
    // Function easing shapes the outgoing segment the way `hold` does: it replaces the handles.
    EasingFn fn = EasingFn::None;
    double fnAmplitude = 1.0;
    double fnPeriod = 0.3;

    bool operator==(const Keyframe &other) const
    {
        return value == other.value && qFuzzyCompare(inDx + 1.0, other.inDx + 1.0)
            && qFuzzyCompare(inDy + 1.0, other.inDy + 1.0)
            && qFuzzyCompare(outDx + 1.0, other.outDx + 1.0)
            && qFuzzyCompare(outDy + 1.0, other.outDy + 1.0) && corner == other.corner
            && hold == other.hold && fn == other.fn
            && qFuzzyCompare(fnAmplitude + 1.0, other.fnAmplitude + 1.0)
            && qFuzzyCompare(fnPeriod + 1.0, other.fnPeriod + 1.0);
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

    void clearKeyframes() { m_values.clear(); }

    // Pin whatever the curve already evaluates to at `time` as a real key there. Used when a clip
    // is cut: the new half has to start from the value the animation had at the cut, not from
    // whichever neighbouring key happens to survive.
    void pinValueAt(TimeUs time)
    {
        if (m_values.isEmpty() || m_values.contains(time))
            return;
        setKeyframe(time, evaluateAt(time));
    }

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
        it->fn = EasingFn::None;
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

    // Catmull-Rom auto tangents: the slope through a key is the chord between its neighbours, and
    // each handle reaches a third of the way along its own segment, so velocity is continuous
    // across the key. The ends use their single neighbour's chord. Replaces whatever shaped the
    // key before, hold and function easing included.
    void setSmooth(TimeUs time)
    {
        auto it = m_values.find(time);
        if (it == m_values.end())
            return;

        const bool hasPrev = it != m_values.begin();
        auto next = std::next(it);
        const bool hasNext = next != m_values.end();
        it->hold = false;
        it->fn = EasingFn::None;
        it->corner = false;
        it->inDx = it->inDy = it->outDx = it->outDy = 0.0;
        if (!hasPrev && !hasNext)
            return;

        const auto lo = hasPrev ? std::prev(it) : it;
        const auto hi = hasNext ? next : it;
        const double dt = static_cast<double>(hi.key() - lo.key());
        const double slope =
            dt > 0.0 ? (static_cast<double>(hi->value) - static_cast<double>(lo->value)) / dt : 0.0;
        if (hasPrev) {
            it->inDx = -static_cast<double>(time - lo.key()) / 3.0;
            it->inDy = slope * it->inDx;
        }
        if (hasNext) {
            it->outDx = static_cast<double>(hi.key() - time) / 3.0;
            it->outDy = slope * it->outDx;
        }
    }

    void setSmoothAll()
    {
        const QList<TimeUs> times = m_values.keys();
        for (TimeUs time : times)
            setSmooth(time);
    }

    // Writes an easing onto the segment that starts at `keyTime`. The last key has no outgoing
    // segment, so its incoming one is targeted instead. Inserting a key inside a function segment
    // (a cut through pinValueAt, say) compresses the shape, as it already does for beziers.
    void setSegmentEasing(TimeUs keyTime, const EasingSpec &spec)
    {
        auto a = m_values.find(keyTime);
        if (a == m_values.end())
            return;
        auto b = std::next(a);
        if (b == m_values.end()) {
            if (a == m_values.begin())
                return;
            b = a;
            a = std::prev(a);
        }

        const double span = static_cast<double>(b.key() - a.key());
        const double dv = static_cast<double>(b->value) - static_cast<double>(a->value);
        switch (spec.kind) {
        case EasingSpec::Kind::Linear:
            a->hold = false;
            a->fn = EasingFn::None;
            a->outDx = 0.0;
            a->outDy = 0.0;
            b->inDx = 0.0;
            b->inDy = 0.0;
            break;
        case EasingSpec::Kind::Hold:
            a->hold = true;
            a->fn = EasingFn::None;
            break;
        case EasingSpec::Kind::Bezier:
            a->hold = false;
            a->fn = EasingFn::None;
            a->outDx = spec.x1 * span;
            a->outDy = spec.y1 * dv;
            b->inDx = (spec.x2 - 1.0) * span;
            b->inDy = (spec.y2 - 1.0) * dv;
            a->corner = true;
            b->corner = true;
            break;
        case EasingSpec::Kind::Function:
            a->hold = false;
            a->fn = spec.fn;
            a->fnAmplitude = spec.amplitude;
            a->fnPeriod = spec.period;
            a->outDx = 0.0;
            a->outDy = 0.0;
            b->inDx = 0.0;
            b->inDy = 0.0;
            break;
        }
    }

    // Reads back what setSegmentEasing wrote, or the nearest equivalent for hand-dragged handles.
    EasingSpec segmentEasing(TimeUs keyTime) const
    {
        auto a = m_values.constFind(keyTime);
        if (a == m_values.constEnd())
            return {};
        auto b = std::next(a);
        if (b == m_values.constEnd()) {
            if (a == m_values.constBegin())
                return {};
            b = a;
            a = std::prev(a);
        }

        EasingSpec spec;
        if (a->hold) {
            spec.kind = EasingSpec::Kind::Hold;
            return spec;
        }
        if (a->fn != EasingFn::None) {
            spec.kind = EasingSpec::Kind::Function;
            spec.fn = a->fn;
            spec.amplitude = a->fnAmplitude;
            spec.period = a->fnPeriod;
            return spec;
        }
        if (qFuzzyIsNull(a->outDx) && qFuzzyIsNull(a->outDy) && qFuzzyIsNull(b->inDx)
            && qFuzzyIsNull(b->inDy)) {
            return spec;
        }

        const double span = static_cast<double>(b.key() - a.key());
        const double dv = static_cast<double>(b->value) - static_cast<double>(a->value);
        spec.kind = EasingSpec::Kind::Bezier;
        spec.x1 = qBound(0.0, a->outDx / span, 1.0);
        spec.x2 = qBound(0.0, 1.0 + b->inDx / span, 1.0);
        if (qAbs(dv) < 1e-12) {
            spec.y1 = 0.0;
            spec.y2 = 1.0;
        } else {
            spec.y1 = a->outDy / dv;
            spec.y2 = 1.0 + b->inDy / dv;
        }
        return spec;
    }

    void setSegmentEasingAll(const EasingSpec &spec)
    {
        const QList<TimeUs> times = m_values.keys();
        for (qsizetype i = 0; i + 1 < times.size(); ++i)
            setSegmentEasing(times[i], spec);
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

        if (prev->fn != EasingFn::None) {
            const double u = (static_cast<double>(time) - t0) / span;
            return static_cast<T>(
                v0 + (v1 - v0) * evaluateEasingFn(prev->fn, u, prev->fnAmplitude, prev->fnPeriod));
        }

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
