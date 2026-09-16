#include "TextAnimator.h"

#include "Bezier.h"

#include <QEasingCurve>
#include <QHash>
#include <QJsonDocument>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace drift {

QString textAnimOrderToString(TextAnimOrder order)
{
    switch (order) {
    case TextAnimOrder::Backward:
        return QStringLiteral("backward");
    case TextAnimOrder::CenterOut:
        return QStringLiteral("centerOut");
    case TextAnimOrder::Random:
        return QStringLiteral("random");
    case TextAnimOrder::Forward:
        return QStringLiteral("forward");
    }
    return QStringLiteral("forward");
}

TextAnimOrder textAnimOrderFromString(const QString &order)
{
    if (order == QStringLiteral("backward"))
        return TextAnimOrder::Backward;
    if (order == QStringLiteral("centerOut"))
        return TextAnimOrder::CenterOut;
    if (order == QStringLiteral("random"))
        return TextAnimOrder::Random;
    return TextAnimOrder::Forward;
}

QString textEaseKindToString(TextEaseKind kind)
{
    switch (kind) {
    case TextEaseKind::Linear:
        return QStringLiteral("linear");
    case TextEaseKind::EaseIn:
        return QStringLiteral("easeIn");
    case TextEaseKind::EaseOut:
        return QStringLiteral("easeOut");
    case TextEaseKind::EaseInOut:
        return QStringLiteral("easeInOut");
    case TextEaseKind::Back:
        return QStringLiteral("back");
    case TextEaseKind::Bounce:
        return QStringLiteral("bounce");
    case TextEaseKind::Smooth:
        return QStringLiteral("smooth");
    case TextEaseKind::Bezier:
        return QStringLiteral("bezier");
    }
    return QStringLiteral("easeOut");
}

TextEaseKind textEaseKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("linear"))
        return TextEaseKind::Linear;
    if (kind == QStringLiteral("easeIn"))
        return TextEaseKind::EaseIn;
    if (kind == QStringLiteral("easeInOut"))
        return TextEaseKind::EaseInOut;
    if (kind == QStringLiteral("back"))
        return TextEaseKind::Back;
    if (kind == QStringLiteral("bounce"))
        return TextEaseKind::Bounce;
    if (kind == QStringLiteral("smooth"))
        return TextEaseKind::Smooth;
    if (kind == QStringLiteral("bezier"))
        return TextEaseKind::Bezier;
    return TextEaseKind::EaseOut;
}

namespace {

double clamp01(double v)
{
    return std::min(1.0, std::max(0.0, v));
}

} // namespace

double TextEaseSpec::value(double t) const
{
    t = clamp01(t);
    switch (kind) {
    case TextEaseKind::Linear:
        return t;
    case TextEaseKind::EaseIn:
        return QEasingCurve(QEasingCurve::InCubic).valueForProgress(t);
    case TextEaseKind::EaseOut:
        return QEasingCurve(QEasingCurve::OutCubic).valueForProgress(t);
    case TextEaseKind::EaseInOut:
        return QEasingCurve(QEasingCurve::InOutQuad).valueForProgress(t);
    case TextEaseKind::Back:
        return QEasingCurve(QEasingCurve::OutBack).valueForProgress(t);
    case TextEaseKind::Bounce:
        return QEasingCurve(QEasingCurve::OutBounce).valueForProgress(t);
    case TextEaseKind::Smooth:
        return t * t * (3.0 - 2.0 * t);
    case TextEaseKind::Bezier:
        return textanim::unitCubic(c1, c2, t);
    }
    return t;
}

double TextAnimParam::at(double progress) const
{
    if (curve.isEmpty())
        return value;
    return curve.evaluateAt(static_cast<TimeUs>(clamp01(progress) * kProgressScale));
}

double TextAnimParam::maxAbs() const
{
    if (curve.isEmpty())
        return std::abs(value);
    double m = 0.0;
    for (const auto &key : curve.keyframes())
        m = std::max(m, std::abs(key.value));
    return m;
}

namespace textanim {

double unitCubic(const QPointF &c1, const QPointF &c2, double x)
{
    x = clamp01(x);
    if (qFuzzyIsNull(c1.x()) && qFuzzyIsNull(c1.y()) && qFuzzyCompare(c2.x(), 1.0)
        && qFuzzyCompare(c2.y(), 1.0))
        return x;
    // Control x is pinned inside the unit interval so the curve stays single-valued in x.
    const double x1 = clamp01(c1.x());
    const double x2 = clamp01(c2.x());
    const double s = bezierParameterForX(0.0, x1, x2, 1.0, x);
    return cubicBezier(0.0, c1.y(), c2.y(), 1.0, s);
}

namespace {

// Skottie's selector shapes as a signal generator: edges e0/e1 and a cubic ramp of size crs,
// with an optional nonlinear ramp mapper (Round / Smooth).
struct ShapeInfo
{
    QPointF ctrl0;
    QPointF ctrl1;
    double e0;
    double e1;
    double crs;
};

const ShapeInfo &shapeInfo(TextSelectorShape shape)
{
    static const ShapeInfo table[] = {
        {{0, 0}, {1, 1}, 0.0, 1.0, 0.0},                                          // Square
        {{0, 0}, {1, 1}, 0.0, std::numeric_limits<double>::infinity(), 1.0},      // RampUp
        {{0, 0}, {1, 1}, -std::numeric_limits<double>::infinity(), 1.0, 1.0},     // RampDown
        {{0, 0}, {1, 1}, 0.0, 1.0, 0.5},                                          // Triangle
        {{0, 0.5}, {0.5, 1}, 0.0, 1.0, 0.5},                                      // Round
        {{0.5, 0}, {0.5, 1}, 0.0, 1.0, 0.5},                                      // Smooth
    };
    return table[static_cast<int>(shape)];
}

QPointF easeVec(double ease)
{
    return ease < 0 ? QPointF(0.0, -ease) : QPointF(ease, 0.0);
}

double pin(double v, double lo, double hi)
{
    return std::min(hi, std::max(lo, v));
}

double combineCoverage(TextSelectorMode mode, double current, double amount)
{
    switch (mode) {
    case TextSelectorMode::Add:
        return pin(current + amount, -1.0, 1.0);
    case TextSelectorMode::Subtract:
        return pin(current - amount, -1.0, 1.0);
    case TextSelectorMode::Intersect:
        return current * amount;
    case TextSelectorMode::Min:
        return std::min(current, amount);
    case TextSelectorMode::Max:
        return std::max(current, amount);
    case TextSelectorMode::Difference:
        return std::abs(current - amount);
    }
    return current;
}

// Deterministic 0..1 hash for noise and random order.
double hash01(quint32 a, quint32 b)
{
    quint32 h = a * 2654435761u;
    h ^= b + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= h >> 15;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    return (h & 0xffffffu) / double(0xffffff);
}

double waveValue(TextWiggleWave wave, double phase, double slot, quint32 seed)
{
    switch (wave) {
    case TextWiggleWave::Sine:
        return std::sin(phase);
    case TextWiggleWave::Triangle:
        return 2.0 / M_PI * std::asin(std::sin(phase));
    case TextWiggleWave::Noise: {
        // Value noise along the phase axis, one lane per fragment slot.
        const double x = phase / (2.0 * M_PI);
        const double f = std::floor(x);
        const double u = x - f;
        const quint32 lane = static_cast<quint32>(std::lround(slot * 1000.0)) ^ seed;
        const double a = hash01(static_cast<quint32>(static_cast<qint64>(f)), lane);
        const double b = hash01(static_cast<quint32>(static_cast<qint64>(f) + 1), lane);
        const double s = u * u * (3.0 - 2.0 * u);
        return (a + (b - a) * s) * 2.0 - 1.0;
    }
    }
    return 0.0;
}

double wiggleCoverage(const TextRangeSelector &sel, double slot, double elapsedSec)
{
    const double phase = 2.0 * M_PI * (sel.frequencyHz * elapsedSec + sel.temporalPhaseDeg / 360.0)
                       + qDegreesToRadians(sel.spatialPhaseDeg) * slot;
    const double w = waveValue(sel.wave, phase, slot, sel.seed);
    const double lo = sel.minAmount / 100.0;
    const double hi = sel.maxAmount / 100.0;
    return lo + (hi - lo) * (0.5 + 0.5 * w);
}

struct DomainRef
{
    int index = 0;
    int size = 1;
};

DomainRef domainRef(TextSelectorDomain domain, const FragmentInfo &frag, const Domains &domains)
{
    switch (domain) {
    case TextSelectorDomain::All:
        return {0, 1};
    case TextSelectorDomain::Chars:
        return {frag.charIndex, domains.chars};
    case TextSelectorDomain::CharsExcludingSpaces:
        return {frag.nonSpaceIndex, domains.nonSpaceChars};
    case TextSelectorDomain::Words:
        return {frag.wordIndex, domains.words};
    case TextSelectorDomain::Lines:
        return {frag.lineIndex, domains.lines};
    }
    return {0, 1};
}

int domainSize(TextSelectorDomain domain, const Domains &domains)
{
    switch (domain) {
    case TextSelectorDomain::All:
        return 1;
    case TextSelectorDomain::Chars:
        return domains.chars;
    case TextSelectorDomain::CharsExcludingSpaces:
        return domains.nonSpaceChars;
    case TextSelectorDomain::Words:
        return domains.words;
    case TextSelectorDomain::Lines:
        return domains.lines;
    }
    return 1;
}

bool isBlockAnimator(const TextAnimator &animator)
{
    for (const TextRangeSelector &sel : animator.selectors)
        if (sel.domain != TextSelectorDomain::All)
            return false;
    return true;
}

// Time-side state of one slot for this frame.
struct SlotTime
{
    bool active = false;
    bool isOut = false;
    bool timeDriven = false; // loops never settle
    double progress = 0.0;
    TimeUs staggerElapsedUs = 0; // In: since the origin; Out: until the end
    double elapsedSec = 0.0;     // for wiggles
    TimeUs durationUs = 0;
};

TimeUs derivedSlotDuration(const TextAnimationSlot &slot, const QList<TextAnimator> &animators,
                           const Domains &domains)
{
    if (slot.durationUs > 0)
        return slot.durationUs;
    TimeUs longest = 0;
    for (const TextAnimator &a : animators) {
        if (!a.enabled)
            continue;
        for (const TextRangeSelector &sel : a.selectors) {
            if (sel.driver != TextSelectorDriver::Stagger)
                continue;
            const double maxSlot = maxReindex(domainSize(sel.domain, domains), sel.order);
            longest = std::max(longest, static_cast<TimeUs>(maxSlot * sel.staggerUs) + sel.durationUs);
        }
    }
    return longest > 0 ? longest : 400000;
}

SlotTime slotTimeFor(TextAnimSlotKind kind, const TextAnimationSlot &slot,
                     const QList<TextAnimator> &animators, const Domains &domains,
                     const EvalContext &ctx)
{
    SlotTime st;
    if (!slot.enabled || animators.isEmpty())
        return st;
    st.active = true;
    const TimeUs start = ctx.windowStartUs;
    const TimeUs end = ctx.windowStartUs + ctx.windowDurationUs;
    const TimeUs t = ctx.timelineUs;
    st.durationUs = derivedSlotDuration(slot, animators, domains);
    switch (kind) {
    case TextAnimSlotKind::In: {
        const TimeUs elapsed = t - start - slot.delayUs;
        st.staggerElapsedUs = elapsed;
        st.progress = clamp01(static_cast<double>(elapsed) / static_cast<double>(st.durationUs));
        st.elapsedSec = usToSeconds(std::max<TimeUs>(0, t - start));
        break;
    }
    case TextAnimSlotKind::Out: {
        const TimeUs remaining = end - slot.delayUs - t;
        st.isOut = true;
        st.staggerElapsedUs = remaining;
        st.progress = clamp01(1.0 - static_cast<double>(remaining) / static_cast<double>(st.durationUs));
        st.elapsedSec = usToSeconds(std::max<TimeUs>(0, t - start));
        break;
    }
    case TextAnimSlotKind::Loop: {
        const TimeUs elapsed = std::max<TimeUs>(0, t - start);
        st.timeDriven = true;
        st.staggerElapsedUs = elapsed;
        st.elapsedSec = usToSeconds(elapsed);
        if (slot.periodUs > 0)
            st.progress = static_cast<double>(elapsed % slot.periodUs) / static_cast<double>(slot.periodUs);
        else if (ctx.windowDurationUs > 0)
            st.progress = clamp01(static_cast<double>(elapsed) / static_cast<double>(ctx.windowDurationUs));
        break;
    }
    }
    return st;
}

double selectorCoverage(const TextRangeSelector &sel, const FragmentInfo &frag, const Domains &domains,
                        const SlotTime &st, const EvalContext &ctx)
{
    const DomainRef d = domainRef(sel.domain, frag, domains);
    switch (sel.driver) {
    case TextSelectorDriver::Curves:
        return rangeSelectorCoverage(sel, d.index, d.size, st.progress);
    case TextSelectorDriver::Stagger: {
        double slot = reindexForOrder(d.index, d.size, sel.order, sel.seed);
        if (st.isOut)
            slot = maxReindex(d.size, sel.order) - slot;
        return staggerCoverage(sel, slot, st.staggerElapsedUs);
    }
    case TextSelectorDriver::Wiggle:
        return wiggleCoverage(sel, d.index, st.elapsedSec);
    case TextSelectorDriver::Karaoke:
        return frag.wordIndex == ctx.activeWordIndex ? 1.0 : 0.0;
    }
    return 0.0;
}

double resolveLength(double v, TextLengthUnit unit, bool horizontal, double maxPx, const EvalContext &ctx)
{
    double r = 0.0;
    switch (unit) {
    case TextLengthUnit::Px:
        r = v * ctx.renderScale;
        break;
    case TextLengthUnit::Em:
        r = v * ctx.emPx;
        break;
    case TextLengthUnit::Box:
        r = v * (horizontal ? ctx.boxWidthPx : ctx.boxHeightPx);
        break;
    }
    if (maxPx > 0.0) {
        const double cap = maxPx * ctx.renderScale;
        r = std::max(-cap, std::min(cap, r));
    }
    return r;
}

double lerp(double a, double b, double t)
{
    return a + (b - a) * t;
}

QColor lerpColor(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(lerp(a.redF(), b.redF(), t), lerp(a.greenF(), b.greenF(), t),
                            lerp(a.blueF(), b.blueF(), t), lerp(a.alphaF(), b.alphaF(), t));
}

// The props that stay per fragment even for a whole-block animator.
void applyFragmentProps(const TextAnimatorProps &p, double a, double progress, FragmentProps &out,
                        const EvalContext &ctx, bool includeMotion)
{
    const double ap = std::max(a, 0.0);
    if (includeMotion) {
        out.dx += resolveLength(p.position.x.at(progress), p.position.unit, true, p.position.maxPx, ctx) * a;
        out.dy += resolveLength(p.position.y.at(progress), p.position.unit, false, p.position.maxPx, ctx) * a;
        out.rotation += p.rotation.at(progress) * a;
        out.scaleX *= 1.0 + (p.scaleX.at(progress) / 100.0 - 1.0) * a;
        out.scaleY *= 1.0 + (p.scaleY.at(progress) / 100.0 - 1.0) * a;
        out.blurPx += p.blur.at(progress) * ctx.renderScale * a;
        if (p.hasOpacity)
            out.opacity = lerp(out.opacity, p.opacity.at(progress) / 100.0, ap);
        if (p.wipe.enabled) {
            const double revealed = 1.0 - clamp01(a);
            out.wipeProgress = out.wipeProgress < 0.0 ? revealed : std::min(out.wipeProgress, revealed);
            out.wipeAngle = p.wipe.angleDeg;
            out.wipeSoftness = p.wipe.softness;
        }
    }
    out.skew += p.skew.at(progress) * a;
    out.tracking += resolveLength(p.tracking.at(progress), p.trackingUnit, true, 0.0, ctx) * a;
    out.lineSpacing += p.lineSpacing.at(progress) * ctx.renderScale * a;
    out.strokeWidthDelta += p.strokeWidth.at(progress) * ctx.renderScale * a;
    if (p.hasFillOpacity)
        out.fillOpacity = lerp(out.fillOpacity, p.fillOpacity.at(progress) / 100.0, ap);
    if (p.hasStrokeOpacity)
        out.strokeOpacity = lerp(out.strokeOpacity, p.strokeOpacity.at(progress) / 100.0, ap);
    if (p.hasFillColor && p.fillColor.isValid())
        out.fillColor = lerpColor(out.fillColor.value_or(ctx.baseFillColor), p.fillColor, ap);
    if (p.hasStrokeColor && p.strokeColor.isValid())
        out.strokeColor = lerpColor(out.strokeColor.value_or(ctx.baseStrokeColor), p.strokeColor, ap);
}

void applyBlockProps(const TextAnimatorProps &p, double a, double progress, BlockProps &out,
                     const EvalContext &ctx)
{
    const double ap = std::max(a, 0.0);
    out.dx += resolveLength(p.position.x.at(progress), p.position.unit, true, p.position.maxPx, ctx) * a;
    out.dy += resolveLength(p.position.y.at(progress), p.position.unit, false, p.position.maxPx, ctx) * a;
    out.rotation += p.rotation.at(progress) * a;
    out.scale *= 1.0 + (p.scaleX.at(progress) / 100.0 - 1.0) * a;
    out.blurPx += p.blur.at(progress) * ctx.renderScale * a;
    if (p.hasOpacity)
        out.opacity = lerp(out.opacity, p.opacity.at(progress) / 100.0, ap);
    if (p.wipe.enabled) {
        const double revealed = 1.0 - clamp01(a);
        out.wipeProgress = out.wipeProgress < 0.0 ? revealed : std::min(out.wipeProgress, revealed);
        out.wipeAngle = p.wipe.angleDeg;
        out.wipeSoftness = p.wipe.softness;
    }
}

// Skottie's line adjustment: tracking is split into a |before| and |after| half per fragment,
// the line's total shifts everything by the alignment factor so a centred line grows both ways,
// and line spacing accumulates downwards as a per-line average. Collapsed (hidden) fragments
// give back their advance so the visible run re-centres.
void applyLineAdjustments(const QList<FragmentInfo> &fragments, bool collapseHidden, double alignFactor,
                          QList<FragmentProps> &props)
{
    const int n = fragments.size();
    int first = 0;
    double lineOffsetY = 0.0;
    bool firstLine = true;
    while (first < n) {
        int last = first;
        while (last + 1 < n && fragments[last + 1].lineIndex == fragments[first].lineIndex)
            ++last;
        const int count = last - first + 1;

        double totalTracking = 0.0;
        double totalSpacing = 0.0;
        QList<double> before(count), after(count);
        for (int i = first; i <= last; ++i) {
            const FragmentProps &p = props[i];
            const int k = i - first;
            if (collapseHidden && p.hidden) {
                before[k] = 0.0;
                after[k] = -fragments[i].advance;
            } else {
                before[k] = i > first ? p.tracking * 0.5 : 0.0;
                after[k] = i < last ? p.tracking * 0.5 : 0.0;
            }
            totalTracking += before[k] + after[k];
            totalSpacing += p.lineSpacing;
        }
        const double alignOffset = -totalTracking * alignFactor;
        if (!firstLine && count > 0)
            lineOffsetY += totalSpacing / count;
        firstLine = false;

        double acc = 0.0;
        for (int i = first; i <= last; ++i) {
            const int k = i - first;
            props[i].dx += alignOffset + acc + before[k];
            props[i].dy += lineOffsetY;
            acc += before[k] + after[k];
        }
        first = last + 1;
    }
}

bool isIdentity(const FragmentProps &p)
{
    return qFuzzyIsNull(p.dx) && qFuzzyIsNull(p.dy) && qFuzzyCompare(p.scaleX, 1.0)
        && qFuzzyCompare(p.scaleY, 1.0) && qFuzzyIsNull(p.rotation) && qFuzzyIsNull(p.skew)
        && qFuzzyCompare(p.opacity, 1.0) && qFuzzyCompare(p.fillOpacity, 1.0)
        && qFuzzyCompare(p.strokeOpacity, 1.0) && qFuzzyIsNull(p.blurPx)
        && qFuzzyIsNull(p.strokeWidthDelta) && !p.fillColor && !p.strokeColor && p.wipeProgress < 0.0;
}

bool isIdentity(const BlockProps &b)
{
    return qFuzzyIsNull(b.dx) && qFuzzyIsNull(b.dy) && qFuzzyCompare(b.scale, 1.0)
        && qFuzzyIsNull(b.rotation) && qFuzzyCompare(b.opacity, 1.0) && qFuzzyIsNull(b.blurPx)
        && b.wipeProgress < 0.0;
}

qint64 q(double v)
{
    return std::llround(v * 1000.0);
}

quint64 poseHashOf(const Frame &frame)
{
    quint64 h = qHashMulti(0, q(frame.block.dx), q(frame.block.dy), q(frame.block.scale),
                           q(frame.block.rotation), q(frame.block.opacity), q(frame.block.blurPx),
                           q(frame.block.wipeProgress));
    for (const FragmentProps &p : frame.props) {
        h = qHashMulti(h, q(p.dx), q(p.dy), q(p.scaleX), q(p.scaleY), q(p.rotation), q(p.skew),
                       q(p.opacity), q(p.fillOpacity), q(p.strokeOpacity), q(p.blurPx),
                       q(p.strokeWidthDelta), q(p.wipeProgress),
                       p.fillColor ? p.fillColor->rgba() : 0u, p.strokeColor ? p.strokeColor->rgba() : 0u);
    }
    return h;
}

bool hasStepStagger(const QList<TextAnimator> &animators)
{
    for (const TextAnimator &a : animators)
        for (const TextRangeSelector &sel : a.selectors)
            if (sel.driver == TextSelectorDriver::Stagger && sel.durationUs <= 0)
                return true;
    return false;
}

CaretState caretFor(const TextAnimationSet &set, const ResolvedSlots &resolved, const SlotTime &in,
                    const SlotTime &out, const QList<FragmentInfo> &fragments,
                    const QList<FragmentProps> &props, const EvalContext &ctx)
{
    CaretState caret;
    const TextCaret &c = set.caret;
    if (!c.enabled)
        return caret;

    const TimeUs elapsed = std::max<TimeUs>(0, ctx.timelineUs - ctx.windowStartUs);
    const TimeUs end = ctx.windowStartUs + ctx.windowDurationUs;

    bool show = false;
    if (in.active) {
        const TimeUs revealEnd = set.in.delayUs + in.durationUs;
        show = c.holdAfterUs < 0 || elapsed <= revealEnd + c.holdAfterUs;
    }
    if (out.active && hasStepStagger(resolved.out)) {
        const TimeUs outStart = end - set.out.delayUs - out.durationUs;
        show = show || ctx.timelineUs >= outStart;
    }
    if (!in.active && !out.active)
        show = true;
    if (!show)
        return caret;

    const TimeUs period = c.blinkOnUs + c.blinkOffUs;
    caret.visible = period <= 0 || (elapsed % period) < c.blinkOnUs;

    caret.afterFragment = -1;
    for (int i = 0; i < props.size(); ++i)
        if (props[i].opacity > 0.5)
            caret.afterFragment = i;
    caret.line = caret.afterFragment >= 0 ? fragments[caret.afterFragment].lineIndex : 0;
    caret.widthPx = c.widthEm * ctx.emPx;
    caret.heightPx = c.heightEm * ctx.emPx;
    caret.color = c.color.isValid() ? c.color : ctx.baseFillColor;
    return caret;
}

} // namespace

double rangeSelectorCoverage(const TextRangeSelector &sel, int domainIndex, int domainSize,
                             double progress)
{
    if (domainSize <= 0)
        return 0.0;
    const double s = sel.start.at(progress);
    const double e = sel.end.at(progress);
    const double o = sel.offset.at(progress);
    double r0, r1;
    if (sel.units == TextSelectorUnits::Percent) {
        r0 = domainSize * (s + o) / 100.0;
        r1 = domainSize * (e + o) / 100.0;
    } else {
        r0 = s + o;
        r1 = e + o;
    }
    if (r0 > r1)
        std::swap(r0, r1);
    double len = std::max(r1 - r0, std::numeric_limits<double>::epsilon());

    const double amount = pin(sel.amount.at(progress) / 100.0, -1.0, 1.0);
    const double easeLo = pin(sel.easeLo.at(progress) / 100.0, -1.0, 1.0);
    const double easeHi = pin(sel.easeHi.at(progress) / 100.0, -1.0, 1.0);

    const ShapeInfo &info = shapeInfo(sel.shape);
    double crs = info.crs;
    if (sel.shape == TextSelectorShape::Square) {
        // AE squares have a "smoothness" that widens the edges into a ramp: move the range
        // outward by half of it and grow the cubic ramp to match.
        const double sm = pin(sel.smoothness.at(progress) / 100.0, 0.0, 1.0);
        r0 -= sm / 2.0;
        len += sm;
        crs += sm / len;
    }

    // Mid-unit sampling, like Skottie: the unit sits at index + 0.5.
    const double t = (domainIndex + 0.5 - r0) / len;
    double u = std::min(t - info.e0, info.e1 - t);
    if (crs > 0.0)
        u /= crs;
    else
        u = u > 0.0 ? 1.0 : (u < 0.0 ? 0.0 : 1.0);
    const double y = unitCubic(info.ctrl0, info.ctrl1, clamp01(u));
    const double coverage = unitCubic(easeVec(easeLo), QPointF(1.0, 1.0) - easeVec(easeHi), y);
    return amount * coverage;
}

double reindexForOrder(int index, int count, TextAnimOrder order, quint32 seed)
{
    if (count <= 1)
        return 0.0;
    switch (order) {
    case TextAnimOrder::Forward:
        return index;
    case TextAnimOrder::Backward:
        return count - 1 - index;
    case TextAnimOrder::CenterOut:
        return std::abs(index - (count - 1) / 2.0);
    case TextAnimOrder::Random: {
        // Stable per-index pseudo-random slot so the shuffle holds still across frames. Seed 0
        // is the legacy table; other seeds give a different, equally stable shuffle.
        const quint32 h = qHash(static_cast<quint32>(index) * 2654435761u ^ seed) ^ 0x9e3779b9u;
        return (h & 0xffffu) / 65535.0 * (count - 1);
    }
    }
    return index;
}

double maxReindex(int count, TextAnimOrder order)
{
    if (count <= 1)
        return 0.0;
    if (order == TextAnimOrder::CenterOut)
        return (count - 1) / 2.0;
    return count - 1;
}

double staggerCoverage(const TextRangeSelector &sel, double slot, TimeUs elapsedUs)
{
    const double settledNum = static_cast<double>(elapsedUs) - slot * static_cast<double>(sel.staggerUs);
    if (sel.durationUs <= 0)
        return settledNum >= 0.0 ? 0.0 : 1.0;
    const double settled = settledNum / static_cast<double>(sel.durationUs);
    return 1.0 - sel.ease.value(settled);
}

Frame evaluateTextAnimation(const TextAnimationSet &set, const ResolvedSlots &resolved,
                            const QList<FragmentInfo> &fragments, const Domains &domains,
                            const EvalContext &ctx)
{
    Frame frame;
    const int n = fragments.size();
    frame.props.resize(n);

    const SlotTime inTime = slotTimeFor(TextAnimSlotKind::In, set.in, resolved.in, domains, ctx);
    const SlotTime outTime = slotTimeFor(TextAnimSlotKind::Out, set.out, resolved.out, domains, ctx);
    const SlotTime loopTime = slotTimeFor(TextAnimSlotKind::Loop, set.loop, resolved.loop, domains, ctx);

    bool timeDriven = false;
    QList<double> coverage(n);
    const auto runSlot = [&](const QList<TextAnimator> &animators, const SlotTime &st) {
        if (!st.active)
            return;
        for (const TextAnimator &animator : animators) {
            if (!animator.enabled)
                continue;
            if (st.timeDriven)
                timeDriven = true;
            for (const TextRangeSelector &sel : animator.selectors)
                if (sel.driver == TextSelectorDriver::Wiggle)
                    timeDriven = true;
            frame.collapseHidden = frame.collapseHidden || animator.collapseHidden;

            const bool block = isBlockAnimator(animator);
            const double initial = animator.selectors.isEmpty() ? 1.0 : 0.0;
            if (block) {
                // One coverage for the whole block; the geometry-independent selectors only need
                // a representative fragment.
                double a = initial;
                const FragmentInfo probe = n > 0 ? fragments[0] : FragmentInfo{};
                for (const TextRangeSelector &sel : animator.selectors)
                    a = combineCoverage(sel.mode, a, selectorCoverage(sel, probe, domains, st, ctx));
                applyBlockProps(animator.props, a, st.progress, frame.block, ctx);
                for (FragmentProps &p : frame.props)
                    applyFragmentProps(animator.props, a, st.progress, p, ctx, false);
                continue;
            }
            for (int i = 0; i < n; ++i) {
                double a = initial;
                for (const TextRangeSelector &sel : animator.selectors)
                    a = combineCoverage(sel.mode, a, selectorCoverage(sel, fragments[i], domains, st, ctx));
                coverage[i] = a;
            }
            for (int i = 0; i < n; ++i)
                applyFragmentProps(animator.props, coverage[i], st.progress, frame.props[i], ctx, true);
        }
    };
    runSlot(resolved.in, inTime);
    runSlot(resolved.out, outTime);
    runSlot(resolved.loop, loopTime);

    for (FragmentProps &p : frame.props) {
        p.opacity = clamp01(p.opacity);
        p.hidden = p.opacity <= 0.001;
    }
    frame.block.opacity = clamp01(frame.block.opacity);

    applyLineAdjustments(fragments, frame.collapseHidden, ctx.alignFactor, frame.props);

    frame.caret = caretFor(set, resolved, inTime, outTime, fragments, frame.props, ctx);

    bool identity = isIdentity(frame.block);
    for (const FragmentProps &p : frame.props)
        identity = identity && isIdentity(p);
    frame.isStatic = identity && !timeDriven && !set.caret.enabled;
    frame.poseHash = frame.isStatic ? 0 : poseHashOf(frame);
    return frame;
}

Bounds animationBounds(const ResolvedSlots &resolved, const EvalContext &ctx)
{
    Bounds b;
    // Back / Bounce easings overshoot the resting pose a little; reserve for it.
    constexpr double kOvershoot = 1.15;
    const auto visit = [&](const QList<TextAnimator> &animators) {
        for (const TextAnimator &a : animators) {
            if (!a.enabled)
                continue;
            const TextAnimatorProps &p = a.props;
            b.maxDx += std::abs(resolveLength(p.position.x.maxAbs(), p.position.unit, true, p.position.maxPx, ctx)) * kOvershoot;
            b.maxDy += std::abs(resolveLength(p.position.y.maxAbs(), p.position.unit, false, p.position.maxPx, ctx)) * kOvershoot;
            b.maxBlurPx += p.blur.maxAbs() * ctx.renderScale;
            const double scale = std::max(p.scaleX.maxAbs(), p.scaleY.maxAbs()) / 100.0;
            if (scale > 1.0)
                b.maxScale *= scale * kOvershoot;
            b.maxRotationDeg += p.rotation.maxAbs();
            b.maxTrackingPx += std::abs(resolveLength(p.tracking.maxAbs(), p.trackingUnit, true, 0.0, ctx));
        }
    };
    visit(resolved.in);
    visit(resolved.out);
    visit(resolved.loop);
    return b;
}

} // namespace textanim
} // namespace drift

// ---------------------------------------------------------------------------------------------
// JSON and enum spellings

#include "Effect.h"

#include <QJsonArray>

namespace drift {

namespace {

template<typename E, size_t N>
QString enumToString(E value, const char *const (&names)[N])
{
    const size_t i = static_cast<size_t>(value);
    return QLatin1String(i < N ? names[i] : names[0]);
}

template<typename E, size_t N>
E enumFromString(const QString &s, const char *const (&names)[N], E fallback)
{
    for (size_t i = 0; i < N; ++i)
        if (s == QLatin1String(names[i]))
            return static_cast<E>(i);
    return fallback;
}

const char *const kDriverNames[] = {"curves", "stagger", "wiggle", "karaoke"};
// "character" means the drawn characters (spaces were never emitted by the layout); the
// with-spaces domain exists for AE parity.
const char *const kDomainNames[] = {"block", "characterWithSpaces", "character", "word", "line"};
const char *const kUnitsNames[] = {"percent", "index"};
const char *const kShapeNames[] = {"square", "rampUp", "rampDown", "triangle", "round", "smooth"};
const char *const kModeNames[] = {"add", "subtract", "intersect", "min", "max", "difference"};
const char *const kWaveNames[] = {"sine", "triangle", "noise"};
const char *const kLengthNames[] = {"px", "em", "box"};
const char *const kGroupingNames[] = {"character", "word", "line", "all"};
const char *const kSlotNames[] = {"in", "out", "loop"};
const char *const kCaretNames[] = {"bar", "underscore", "block"};

QJsonArray pointToJson(const QPointF &p)
{
    return QJsonArray{p.x(), p.y()};
}

QPointF pointFromJson(const QJsonValue &v, const QPointF &fallback)
{
    const QJsonArray a = v.toArray();
    return a.size() == 2 ? QPointF(a.at(0).toDouble(), a.at(1).toDouble()) : fallback;
}

// Preset params substitute booleans as 0 / 1, so a flag may arrive as a number.
bool jsonBool(const QJsonValue &v, bool fallback)
{
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble() > 0.5;
    return fallback;
}

void putParam(QJsonObject &o, const QString &key, const TextAnimParam &p, double def)
{
    if (!p.isConstant() || !qFuzzyCompare(p.value + 1.0, def + 1.0))
        o.insert(key, textAnimParamToJson(p));
}

void putDouble(QJsonObject &o, const QString &key, double v, double def)
{
    if (!qFuzzyCompare(v + 1.0, def + 1.0))
        o.insert(key, v);
}

} // namespace

QString textSelectorDriverToString(TextSelectorDriver d) { return enumToString(d, kDriverNames); }
TextSelectorDriver textSelectorDriverFromString(const QString &s) { return enumFromString(s, kDriverNames, TextSelectorDriver::Stagger); }
QString textSelectorDomainToString(TextSelectorDomain d) { return enumToString(d, kDomainNames); }
TextSelectorDomain textSelectorDomainFromString(const QString &s)
{
    if (s == QLatin1String("all"))
        return TextSelectorDomain::All;
    if (s == QLatin1String("chars") || s == QLatin1String("characters") || s == QLatin1String("characterNoSpaces"))
        return TextSelectorDomain::CharsExcludingSpaces;
    if (s == QLatin1String("words"))
        return TextSelectorDomain::Words;
    if (s == QLatin1String("lines"))
        return TextSelectorDomain::Lines;
    return enumFromString(s, kDomainNames, TextSelectorDomain::Words);
}
QString textSelectorUnitsToString(TextSelectorUnits u) { return enumToString(u, kUnitsNames); }
TextSelectorUnits textSelectorUnitsFromString(const QString &s) { return enumFromString(s, kUnitsNames, TextSelectorUnits::Percent); }
QString textSelectorShapeToString(TextSelectorShape v) { return enumToString(v, kShapeNames); }
TextSelectorShape textSelectorShapeFromString(const QString &s) { return enumFromString(s, kShapeNames, TextSelectorShape::Square); }
QString textSelectorModeToString(TextSelectorMode v) { return enumToString(v, kModeNames); }
TextSelectorMode textSelectorModeFromString(const QString &s) { return enumFromString(s, kModeNames, TextSelectorMode::Add); }
QString textWiggleWaveToString(TextWiggleWave v) { return enumToString(v, kWaveNames); }
TextWiggleWave textWiggleWaveFromString(const QString &s) { return enumFromString(s, kWaveNames, TextWiggleWave::Sine); }
QString textLengthUnitToString(TextLengthUnit v) { return enumToString(v, kLengthNames); }
TextLengthUnit textLengthUnitFromString(const QString &s) { return enumFromString(s, kLengthNames, TextLengthUnit::Px); }
QString textAnchorGroupingToString(TextAnchorGrouping v) { return enumToString(v, kGroupingNames); }
TextAnchorGrouping textAnchorGroupingFromString(const QString &s) { return enumFromString(s, kGroupingNames, TextAnchorGrouping::Character); }
QString textAnimSlotKindToString(TextAnimSlotKind v) { return enumToString(v, kSlotNames); }
TextAnimSlotKind textAnimSlotKindFromString(const QString &s) { return enumFromString(s, kSlotNames, TextAnimSlotKind::In); }
QString textCaretShapeToString(TextCaret::Shape v) { return enumToString(v, kCaretNames); }
TextCaret::Shape textCaretShapeFromString(const QString &s) { return enumFromString(s, kCaretNames, TextCaret::Shape::Bar); }

QJsonValue textAnimParamToJson(const TextAnimParam &p)
{
    if (p.isConstant())
        return p.value;
    return QJsonObject{{QStringLiteral("value"), p.value}, {QStringLiteral("curve"), keyframesToJson(p.curve)}};
}

TextAnimParam textAnimParamFromJson(const QJsonValue &v, double fallback)
{
    TextAnimParam p(fallback);
    if (v.isDouble()) {
        p.value = v.toDouble();
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        p.value = o.value(QStringLiteral("value")).toDouble(fallback);
        p.curve = keyframesFromJson(o.value(QStringLiteral("curve")).toObject());
    }
    return p;
}

QJsonValue textEaseSpecToJson(const TextEaseSpec &e)
{
    if (e.kind != TextEaseKind::Bezier)
        return textEaseKindToString(e.kind);
    return QJsonObject{{QStringLiteral("kind"), QStringLiteral("bezier")},
                       {QStringLiteral("c1"), pointToJson(e.c1)},
                       {QStringLiteral("c2"), pointToJson(e.c2)}};
}

TextEaseSpec textEaseSpecFromJson(const QJsonValue &v)
{
    TextEaseSpec e;
    if (v.isString()) {
        e.kind = textEaseKindFromString(v.toString());
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        e.kind = textEaseKindFromString(o.value(QStringLiteral("kind")).toString());
        e.c1 = pointFromJson(o.value(QStringLiteral("c1")), e.c1);
        e.c2 = pointFromJson(o.value(QStringLiteral("c2")), e.c2);
    }
    return e;
}

QJsonObject textRangeSelectorToJson(const TextRangeSelector &s)
{
    const TextRangeSelector def;
    QJsonObject o{{QStringLiteral("driver"), textSelectorDriverToString(s.driver)},
                  {QStringLiteral("domain"), textSelectorDomainToString(s.domain)}};
    if (s.mode != def.mode)
        o.insert(QStringLiteral("mode"), textSelectorModeToString(s.mode));
    switch (s.driver) {
    case TextSelectorDriver::Curves:
        o.insert(QStringLiteral("units"), textSelectorUnitsToString(s.units));
        o.insert(QStringLiteral("shape"), textSelectorShapeToString(s.shape));
        putParam(o, QStringLiteral("start"), s.start, 0.0);
        putParam(o, QStringLiteral("end"), s.end, s.units == TextSelectorUnits::Percent ? 100.0 : kTextSelectorIndexEnd);
        putParam(o, QStringLiteral("offset"), s.offset, 0.0);
        putParam(o, QStringLiteral("amount"), s.amount, 100.0);
        putParam(o, QStringLiteral("easeLo"), s.easeLo, 0.0);
        putParam(o, QStringLiteral("easeHi"), s.easeHi, 0.0);
        putParam(o, QStringLiteral("smoothness"), s.smoothness, 100.0);
        break;
    case TextSelectorDriver::Stagger:
        o.insert(QStringLiteral("staggerUs"), static_cast<qint64>(s.staggerUs));
        o.insert(QStringLiteral("durationUs"), static_cast<qint64>(s.durationUs));
        o.insert(QStringLiteral("ease"), textEaseSpecToJson(s.ease));
        o.insert(QStringLiteral("order"), textAnimOrderToString(s.order));
        break;
    case TextSelectorDriver::Wiggle:
        o.insert(QStringLiteral("wave"), textWiggleWaveToString(s.wave));
        putDouble(o, QStringLiteral("frequencyHz"), s.frequencyHz, def.frequencyHz);
        putDouble(o, QStringLiteral("spatialPhaseDeg"), s.spatialPhaseDeg, def.spatialPhaseDeg);
        putDouble(o, QStringLiteral("temporalPhaseDeg"), s.temporalPhaseDeg, def.temporalPhaseDeg);
        putDouble(o, QStringLiteral("minAmount"), s.minAmount, def.minAmount);
        putDouble(o, QStringLiteral("maxAmount"), s.maxAmount, def.maxAmount);
        break;
    case TextSelectorDriver::Karaoke:
        break;
    }
    if (s.seed != 0)
        o.insert(QStringLiteral("seed"), static_cast<qint64>(s.seed));
    return o;
}

TextRangeSelector textRangeSelectorFromJson(const QJsonObject &o)
{
    TextRangeSelector s;
    s.driver = textSelectorDriverFromString(o.value(QStringLiteral("driver")).toString());
    s.domain = textSelectorDomainFromString(o.value(QStringLiteral("domain")).toString(QStringLiteral("word")));
    s.mode = textSelectorModeFromString(o.value(QStringLiteral("mode")).toString());
    s.units = textSelectorUnitsFromString(o.value(QStringLiteral("units")).toString());
    s.shape = textSelectorShapeFromString(o.value(QStringLiteral("shape")).toString());
    s.start = textAnimParamFromJson(o.value(QStringLiteral("start")), 0.0);
    s.end = textAnimParamFromJson(o.value(QStringLiteral("end")),
                                  s.units == TextSelectorUnits::Percent ? 100.0 : kTextSelectorIndexEnd);
    s.offset = textAnimParamFromJson(o.value(QStringLiteral("offset")), 0.0);
    s.amount = textAnimParamFromJson(o.value(QStringLiteral("amount")), 100.0);
    s.easeLo = textAnimParamFromJson(o.value(QStringLiteral("easeLo")), 0.0);
    s.easeHi = textAnimParamFromJson(o.value(QStringLiteral("easeHi")), 0.0);
    s.smoothness = textAnimParamFromJson(o.value(QStringLiteral("smoothness")), 100.0);
    s.staggerUs = o.value(QStringLiteral("staggerUs")).toInteger(s.staggerUs);
    s.durationUs = o.value(QStringLiteral("durationUs")).toInteger(s.durationUs);
    if (o.contains(QStringLiteral("ease")))
        s.ease = textEaseSpecFromJson(o.value(QStringLiteral("ease")));
    s.order = textAnimOrderFromString(o.value(QStringLiteral("order")).toString());
    s.wave = textWiggleWaveFromString(o.value(QStringLiteral("wave")).toString());
    s.frequencyHz = o.value(QStringLiteral("frequencyHz")).toDouble(s.frequencyHz);
    s.spatialPhaseDeg = o.value(QStringLiteral("spatialPhaseDeg")).toDouble(s.spatialPhaseDeg);
    s.temporalPhaseDeg = o.value(QStringLiteral("temporalPhaseDeg")).toDouble(s.temporalPhaseDeg);
    s.minAmount = o.value(QStringLiteral("minAmount")).toDouble(s.minAmount);
    s.maxAmount = o.value(QStringLiteral("maxAmount")).toDouble(s.maxAmount);
    s.seed = static_cast<quint32>(o.value(QStringLiteral("seed")).toInteger(0));
    return s;
}

QJsonObject textAnimatorToJson(const TextAnimator &a)
{
    const TextAnimatorProps &p = a.props;
    QJsonObject props;
    if (!p.position.x.isConstant() || !p.position.y.isConstant() || !qFuzzyIsNull(p.position.x.value)
        || !qFuzzyIsNull(p.position.y.value)) {
        QJsonObject pos{{QStringLiteral("x"), textAnimParamToJson(p.position.x)},
                        {QStringLiteral("y"), textAnimParamToJson(p.position.y)},
                        {QStringLiteral("unit"), textLengthUnitToString(p.position.unit)}};
        if (p.position.maxPx > 0.0)
            pos.insert(QStringLiteral("maxPx"), p.position.maxPx);
        props.insert(QStringLiteral("position"), pos);
    }
    putParam(props, QStringLiteral("scaleX"), p.scaleX, 100.0);
    putParam(props, QStringLiteral("scaleY"), p.scaleY, 100.0);
    putParam(props, QStringLiteral("rotation"), p.rotation, 0.0);
    putParam(props, QStringLiteral("skew"), p.skew, 0.0);
    if (p.hasOpacity)
        props.insert(QStringLiteral("opacity"), textAnimParamToJson(p.opacity));
    if (p.hasFillOpacity)
        props.insert(QStringLiteral("fillOpacity"), textAnimParamToJson(p.fillOpacity));
    if (p.hasStrokeOpacity)
        props.insert(QStringLiteral("strokeOpacity"), textAnimParamToJson(p.strokeOpacity));
    if (p.hasFillColor)
        props.insert(QStringLiteral("fillColor"), p.fillColor.name(QColor::HexArgb));
    if (p.hasStrokeColor)
        props.insert(QStringLiteral("strokeColor"), p.strokeColor.name(QColor::HexArgb));
    putParam(props, QStringLiteral("blur"), p.blur, 0.0);
    putParam(props, QStringLiteral("tracking"), p.tracking, 0.0);
    if (p.trackingUnit != TextLengthUnit::Px)
        props.insert(QStringLiteral("trackingUnit"), textLengthUnitToString(p.trackingUnit));
    putParam(props, QStringLiteral("lineSpacing"), p.lineSpacing, 0.0);
    putParam(props, QStringLiteral("strokeWidth"), p.strokeWidth, 0.0);
    if (p.wipe.enabled)
        props.insert(QStringLiteral("wipe"), QJsonObject{{QStringLiteral("enabled"), true},
                                                         {QStringLiteral("angleDeg"), p.wipe.angleDeg},
                                                         {QStringLiteral("softness"), p.wipe.softness}});
    QJsonArray selectors;
    for (const TextRangeSelector &s : a.selectors)
        selectors.append(textRangeSelectorToJson(s));
    QJsonObject o{{QStringLiteral("selectors"), selectors}, {QStringLiteral("props"), props}};
    if (!a.name.isEmpty())
        o.insert(QStringLiteral("name"), a.name);
    if (!a.enabled)
        o.insert(QStringLiteral("enabled"), false);
    if (a.collapseHidden)
        o.insert(QStringLiteral("collapseHidden"), true);
    return o;
}

TextAnimator textAnimatorFromJson(const QJsonObject &o)
{
    TextAnimator a;
    a.name = o.value(QStringLiteral("name")).toString();
    a.enabled = jsonBool(o.value(QStringLiteral("enabled")), true);
    a.collapseHidden = jsonBool(o.value(QStringLiteral("collapseHidden")), false);
    for (const QJsonValue &v : o.value(QStringLiteral("selectors")).toArray())
        a.selectors.append(textRangeSelectorFromJson(v.toObject()));
    const QJsonObject props = o.value(QStringLiteral("props")).toObject();
    TextAnimatorProps &p = a.props;
    const QJsonObject pos = props.value(QStringLiteral("position")).toObject();
    if (!pos.isEmpty()) {
        p.position.x = textAnimParamFromJson(pos.value(QStringLiteral("x")), 0.0);
        p.position.y = textAnimParamFromJson(pos.value(QStringLiteral("y")), 0.0);
        p.position.unit = textLengthUnitFromString(pos.value(QStringLiteral("unit")).toString());
        p.position.maxPx = pos.value(QStringLiteral("maxPx")).toDouble(0.0);
    }
    p.scaleX = textAnimParamFromJson(props.value(QStringLiteral("scaleX")), 100.0);
    p.scaleY = textAnimParamFromJson(props.value(QStringLiteral("scaleY")), 100.0);
    if (props.contains(QStringLiteral("scale"))) { // shorthand for a uniform scale
        p.scaleX = textAnimParamFromJson(props.value(QStringLiteral("scale")), 100.0);
        p.scaleY = p.scaleX;
    }
    p.rotation = textAnimParamFromJson(props.value(QStringLiteral("rotation")), 0.0);
    p.skew = textAnimParamFromJson(props.value(QStringLiteral("skew")), 0.0);
    p.hasOpacity = props.contains(QStringLiteral("opacity"));
    p.opacity = textAnimParamFromJson(props.value(QStringLiteral("opacity")), 100.0);
    p.hasFillOpacity = props.contains(QStringLiteral("fillOpacity"));
    p.fillOpacity = textAnimParamFromJson(props.value(QStringLiteral("fillOpacity")), 100.0);
    p.hasStrokeOpacity = props.contains(QStringLiteral("strokeOpacity"));
    p.strokeOpacity = textAnimParamFromJson(props.value(QStringLiteral("strokeOpacity")), 100.0);
    p.hasFillColor = props.contains(QStringLiteral("fillColor"));
    p.fillColor = QColor(props.value(QStringLiteral("fillColor")).toString());
    p.hasStrokeColor = props.contains(QStringLiteral("strokeColor"));
    p.strokeColor = QColor(props.value(QStringLiteral("strokeColor")).toString());
    p.blur = textAnimParamFromJson(props.value(QStringLiteral("blur")), 0.0);
    p.tracking = textAnimParamFromJson(props.value(QStringLiteral("tracking")), 0.0);
    p.trackingUnit = textLengthUnitFromString(props.value(QStringLiteral("trackingUnit")).toString());
    p.lineSpacing = textAnimParamFromJson(props.value(QStringLiteral("lineSpacing")), 0.0);
    p.strokeWidth = textAnimParamFromJson(props.value(QStringLiteral("strokeWidth")), 0.0);
    const QJsonObject wipe = props.value(QStringLiteral("wipe")).toObject();
    if (!wipe.isEmpty()) {
        p.wipe.enabled = jsonBool(wipe.value(QStringLiteral("enabled")), true);
        p.wipe.angleDeg = wipe.value(QStringLiteral("angleDeg")).toDouble(p.wipe.angleDeg);
        p.wipe.softness = wipe.value(QStringLiteral("softness")).toDouble(p.wipe.softness);
    }
    return a;
}

QJsonObject textCaretToJson(const TextCaret &c)
{
    return QJsonObject{
        {QStringLiteral("enabled"), c.enabled},
        {QStringLiteral("leadUs"), static_cast<qint64>(c.leadUs)},
        {QStringLiteral("blinkOnUs"), static_cast<qint64>(c.blinkOnUs)},
        {QStringLiteral("blinkOffUs"), static_cast<qint64>(c.blinkOffUs)},
        {QStringLiteral("holdAfterUs"), static_cast<qint64>(c.holdAfterUs)},
        {QStringLiteral("widthEm"), c.widthEm},
        {QStringLiteral("heightEm"), c.heightEm},
        {QStringLiteral("shape"), textCaretShapeToString(c.shape)},
        {QStringLiteral("color"), c.color.isValid() ? c.color.name(QColor::HexArgb) : QString()},
    };
}

TextCaret textCaretFromJson(const QJsonObject &o)
{
    TextCaret c;
    if (o.isEmpty())
        return c;
    c.enabled = jsonBool(o.value(QStringLiteral("enabled")), c.enabled);
    c.leadUs = o.value(QStringLiteral("leadUs")).toInteger(c.leadUs);
    c.blinkOnUs = o.value(QStringLiteral("blinkOnUs")).toInteger(c.blinkOnUs);
    c.blinkOffUs = o.value(QStringLiteral("blinkOffUs")).toInteger(c.blinkOffUs);
    c.holdAfterUs = o.value(QStringLiteral("holdAfterUs")).toInteger(c.holdAfterUs);
    c.widthEm = o.value(QStringLiteral("widthEm")).toDouble(c.widthEm);
    c.heightEm = o.value(QStringLiteral("heightEm")).toDouble(c.heightEm);
    c.shape = textCaretShapeFromString(o.value(QStringLiteral("shape")).toString());
    const QString color = o.value(QStringLiteral("color")).toString();
    c.color = color.isEmpty() ? QColor() : QColor(color);
    return c;
}

QJsonObject textAnimationSlotToJson(const TextAnimationSlot &s)
{
    QJsonObject o;
    if (!s.presetId.isEmpty())
        o.insert(QStringLiteral("preset"), s.presetId);
    if (!s.params.isEmpty()) {
        QJsonObject params;
        for (auto it = s.params.constBegin(); it != s.params.constEnd(); ++it)
            params.insert(it.key(), it->toJson());
        o.insert(QStringLiteral("params"), params);
    }
    if (s.presetId.isEmpty() && !s.animators.isEmpty()) {
        QJsonArray animators;
        for (const TextAnimator &a : s.animators)
            animators.append(textAnimatorToJson(a));
        o.insert(QStringLiteral("animators"), animators);
    }
    if (s.delayUs != 0)
        o.insert(QStringLiteral("delayUs"), static_cast<qint64>(s.delayUs));
    if (s.durationUs != 0)
        o.insert(QStringLiteral("durationUs"), static_cast<qint64>(s.durationUs));
    if (s.periodUs != 0)
        o.insert(QStringLiteral("periodUs"), static_cast<qint64>(s.periodUs));
    if (!s.enabled)
        o.insert(QStringLiteral("enabled"), false);
    return o;
}

TextAnimationSlot textAnimationSlotFromJson(const QJsonObject &o)
{
    TextAnimationSlot s;
    s.presetId = o.value(QStringLiteral("preset")).toString();
    const QJsonObject params = o.value(QStringLiteral("params")).toObject();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it)
        s.params.insert(it.key(), VectorSlotValue::fromJson(it->toObject()));
    for (const QJsonValue &v : o.value(QStringLiteral("animators")).toArray())
        s.animators.append(textAnimatorFromJson(v.toObject()));
    s.delayUs = o.value(QStringLiteral("delayUs")).toInteger(0);
    s.durationUs = o.value(QStringLiteral("durationUs")).toInteger(0);
    s.periodUs = o.value(QStringLiteral("periodUs")).toInteger(0);
    s.enabled = jsonBool(o.value(QStringLiteral("enabled")), true);
    return s;
}

QJsonObject textAnimationSetToJson(const TextAnimationSet &set)
{
    QJsonObject o;
    if (set.in.isActive() || !set.in.enabled)
        o.insert(QStringLiteral("in"), textAnimationSlotToJson(set.in));
    if (set.out.isActive() || !set.out.enabled)
        o.insert(QStringLiteral("out"), textAnimationSlotToJson(set.out));
    if (set.loop.isActive() || !set.loop.enabled)
        o.insert(QStringLiteral("loop"), textAnimationSlotToJson(set.loop));
    if (set.caret.enabled)
        o.insert(QStringLiteral("caret"), textCaretToJson(set.caret));
    if (set.anchorGrouping != TextAnchorGrouping::Character)
        o.insert(QStringLiteral("anchorGrouping"), textAnchorGroupingToString(set.anchorGrouping));
    if (!set.anchorAlignment.isNull())
        o.insert(QStringLiteral("anchorAlignment"), pointToJson(set.anchorAlignment));
    return o;
}

TextAnimationSet textAnimationSetFromJson(const QJsonObject &o)
{
    TextAnimationSet set;
    set.in = textAnimationSlotFromJson(o.value(QStringLiteral("in")).toObject());
    set.out = textAnimationSlotFromJson(o.value(QStringLiteral("out")).toObject());
    set.loop = textAnimationSlotFromJson(o.value(QStringLiteral("loop")).toObject());
    set.caret = textCaretFromJson(o.value(QStringLiteral("caret")).toObject());
    set.anchorGrouping = textAnchorGroupingFromString(o.value(QStringLiteral("anchorGrouping")).toString());
    set.anchorAlignment = pointFromJson(o.value(QStringLiteral("anchorAlignment")), QPointF());
    return set;
}

quint64 textAnimationSetHash(const TextAnimationSet &set)
{
    // The JSON is the canonical spelling of everything that matters; hashing it is far simpler
    // than a field-by-field hash that every new member would have to remember to join.
    return qHash(QJsonDocument(textAnimationSetToJson(set)).toJson(QJsonDocument::Compact));
}

} // namespace drift
