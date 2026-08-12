#include "Mask.h"

#include <algorithm>

namespace drift {

QString maskShapeToString(MaskShape shape)
{
    switch (shape) {
    case MaskShape::Rectangle:
        return QStringLiteral("rectangle");
    case MaskShape::Ellipse:
        return QStringLiteral("ellipse");
    case MaskShape::Star:
        return QStringLiteral("star");
    case MaskShape::Heart:
        return QStringLiteral("heart");
    case MaskShape::Bars:
        return QStringLiteral("bars");
    case MaskShape::Freeform:
        return QStringLiteral("freeform");
    case MaskShape::Media:
        return QStringLiteral("media");
    case MaskShape::None:
        break;
    }
    return QStringLiteral("none");
}

MaskShape maskShapeFromString(const QString &shape)
{
    if (shape == QStringLiteral("rectangle"))
        return MaskShape::Rectangle;
    if (shape == QStringLiteral("ellipse"))
        return MaskShape::Ellipse;
    if (shape == QStringLiteral("star"))
        return MaskShape::Star;
    if (shape == QStringLiteral("heart"))
        return MaskShape::Heart;
    if (shape == QStringLiteral("bars"))
        return MaskShape::Bars;
    if (shape == QStringLiteral("freeform"))
        return MaskShape::Freeform;
    // "matte" is what this was called before user-supplied media joined segmentation output.
    if (shape == QStringLiteral("media") || shape == QStringLiteral("matte"))
        return MaskShape::Media;
    return MaskShape::None;
}

QString maskMediaFitToString(MaskMediaFit fit)
{
    switch (fit) {
    case MaskMediaFit::Fit:
        return QStringLiteral("fit");
    case MaskMediaFit::Fill:
        return QStringLiteral("fill");
    case MaskMediaFit::Stretch:
        break;
    }
    return QStringLiteral("stretch");
}

MaskMediaFit maskMediaFitFromString(const QString &fit)
{
    if (fit == QStringLiteral("fit"))
        return MaskMediaFit::Fit;
    if (fit == QStringLiteral("fill"))
        return MaskMediaFit::Fill;
    return MaskMediaFit::Stretch;
}

QString maskMediaChannelToString(MaskMediaChannel channel)
{
    return channel == MaskMediaChannel::Alpha ? QStringLiteral("alpha") : QStringLiteral("luma");
}

MaskMediaChannel maskMediaChannelFromString(const QString &channel)
{
    return channel == QStringLiteral("alpha") ? MaskMediaChannel::Alpha : MaskMediaChannel::Luma;
}

QString maskOpToString(MaskOp op)
{
    switch (op) {
    case MaskOp::Subtract:
        return QStringLiteral("subtract");
    case MaskOp::Intersect:
        return QStringLiteral("intersect");
    case MaskOp::Add:
        break;
    }
    return QStringLiteral("add");
}

MaskOp maskOpFromString(const QString &op)
{
    if (op == QStringLiteral("subtract"))
        return MaskOp::Subtract;
    if (op == QStringLiteral("intersect"))
        return MaskOp::Intersect;
    return MaskOp::Add;
}

bool Mask::isAnimated() const
{
    if (pathKeys.size() > 1)
        return true;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty())
            return true;
    }
    return false;
}

double Mask::valueAt(const QString &key, TimeUs clipTimeUs) const
{
    const auto it = keyframes.constFind(key);
    if (it == keyframes.constEnd() || it->isEmpty()) {
        if (key == QStringLiteral("x"))
            return x;
        if (key == QStringLiteral("y"))
            return y;
        if (key == QStringLiteral("w"))
            return w;
        if (key == QStringLiteral("h"))
            return h;
        if (key == QStringLiteral("rotation"))
            return rotation;
        if (key == QStringLiteral("feather"))
            return feather;
        return 0.0;
    }
    return it->evaluateAt(clipTimeUs);
}

// Positions between the two bracketing keys. A vertex-count mismatch means the polygon was
// reshaped rather than moved, and there is no honest correspondence between the two lists, so
// the earlier key holds until the later one arrives.
QVector<MaskPoint> pointsAt(const QMap<TimeUs, QVector<MaskPoint>> &pathKeys, TimeUs clipTimeUs,
                            const QVector<MaskPoint> &fallback)
{
    if (pathKeys.isEmpty())
        return fallback;
    if (pathKeys.size() == 1)
        return pathKeys.first();

    auto after = pathKeys.lowerBound(clipTimeUs);
    if (after == pathKeys.constEnd())
        return std::prev(after).value();
    if (after.key() == clipTimeUs || after == pathKeys.constBegin())
        return after.value();

    const auto before = std::prev(after);
    const QVector<MaskPoint> &a = before.value();
    const QVector<MaskPoint> &b = after.value();
    if (a.size() != b.size())
        return a;

    const double span = double(after.key() - before.key());
    const double t = span > 0.0 ? double(clipTimeUs - before.key()) / span : 0.0;

    QVector<MaskPoint> out;
    out.reserve(a.size());
    for (int i = 0; i < a.size(); ++i) {
        MaskPoint p = a.at(i);
        p.pos = a.at(i).pos + (b.at(i).pos - a.at(i).pos) * t;
        p.inTan = a.at(i).inTan + (b.at(i).inTan - a.at(i).inTan) * t;
        p.outTan = a.at(i).outTan + (b.at(i).outTan - a.at(i).outTan) * t;
        out.append(p);
    }
    return out;
}

Mask Mask::resolvedAt(TimeUs clipTimeUs) const
{
    Mask out = *this;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (it->isEmpty())
            continue;
        const double value = it->evaluateAt(clipTimeUs);
        const QString &key = it.key();
        if (key == QStringLiteral("x"))
            out.x = value;
        else if (key == QStringLiteral("y"))
            out.y = value;
        else if (key == QStringLiteral("w"))
            out.w = value;
        else if (key == QStringLiteral("h"))
            out.h = value;
        else if (key == QStringLiteral("rotation"))
            out.rotation = value;
        else if (key == QStringLiteral("feather"))
            out.feather = value;
    }
    if (!pathKeys.isEmpty())
        out.points = pointsAt(pathKeys, clipTimeUs, points);
    return out;
}

void Mask::detachSharedData()
{
    points.detach();
    for (auto it = keyframes.begin(); it != keyframes.end(); ++it)
        it->detachSharedData();
    pathKeys.detach();
    for (auto it = pathKeys.begin(); it != pathKeys.end(); ++it)
        it->detach();
}

Mask fullFrameMediaMask(const QString &path, TimeUs offsetUs)
{
    Mask mask;
    mask.shape = MaskShape::Media;
    mask.mediaPath = path;
    mask.mediaOffsetUs = offsetUs;
    mask.x = 0.5;
    mask.y = 0.5;
    mask.w = 1.0;
    mask.h = 1.0;
    return mask;
}

bool masksAreInert(const QList<Mask> &masks)
{
    for (const Mask &mask : masks) {
        if (mask.contributes())
            return false;
    }
    return true;
}

QList<Mask> masksActiveAt(const QList<Mask> &masks, TimeUs timelineUs)
{
    QList<Mask> active;
    for (const Mask &mask : masks) {
        if (mask.contributes() && mask.containsTime(timelineUs))
            active.append(mask);
    }
    // Lane order decides the composite. Subtract and Intersect read against everything folded
    // before them, so a stable, visible ordering is what keeps the result predictable; ties fall
    // back to list order so a reorder within a lane still means something.
    std::stable_sort(active.begin(), active.end(),
                     [](const Mask &a, const Mask &b) { return a.lane < b.lane; });
    return active;
}

int maskLaneCount(const QList<Mask> &masks)
{
    int highest = -1;
    for (const Mask &mask : masks)
        highest = qMax(highest, mask.lane);
    return highest + 1;
}

int firstFreeMaskLane(const QList<Mask> &masks, TimeUs start, TimeUs duration, int ignoreIndex)
{
    const TimeUs end = start + duration;
    for (int lane = 0;; ++lane) {
        bool clash = false;
        for (int i = 0; i < masks.size() && !clash; ++i) {
            if (i == ignoreIndex)
                continue;
            const Mask &other = masks.at(i);
            if (other.lane != lane)
                continue;
            // Touching end to end is not an overlap.
            clash = start < other.timelineEnd() && other.timelineStart < end;
        }
        if (!clash)
            return lane;
    }
}

} // namespace drift
