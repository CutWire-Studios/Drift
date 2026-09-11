#include "Mask.h"

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
    // "matte" is what Media was called before v5.
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
    switch (channel) {
    case MaskMediaChannel::Alpha:
        return QStringLiteral("alpha");
    case MaskMediaChannel::Luma:
        break;
    }
    return QStringLiteral("luma");
}

MaskMediaChannel maskMediaChannelFromString(const QString &channel)
{
    if (channel == QStringLiteral("alpha"))
        return MaskMediaChannel::Alpha;
    return MaskMediaChannel::Luma;
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

const QStringList &maskKeyframeProperties()
{
    static const QStringList props{QStringLiteral("x"),        QStringLiteral("y"),
                                   QStringLiteral("w"),        QStringLiteral("h"),
                                   QStringLiteral("rotation"), QStringLiteral("feather")};
    return props;
}

bool Mask::isAnimated() const
{
    // One path key is a static shape, not an animation — two are needed before anything moves.
    if (pathKeys.size() > 1)
        return true;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (!it->isEmpty() && it->enabled())
            return true;
    }
    return false;
}

double Mask::valueAt(const QString &key, TimeUs maskTimeUs) const
{
    const auto it = keyframes.constFind(key);
    if (it != keyframes.constEnd() && !it->isEmpty())
        return it->evaluateAt(maskTimeUs);

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

namespace {

// Positions between the two bracketing keys. A vertex-count mismatch means the polygon was
// reshaped rather than moved, and there is no honest correspondence between the two lists, so the
// earlier key holds until the later one arrives.
QVector<QPointF> pointsAt(const QMap<TimeUs, QVector<QPointF>> &pathKeys, TimeUs maskTimeUs,
                          const QVector<QPointF> &fallback)
{
    if (pathKeys.isEmpty())
        return fallback;
    if (pathKeys.size() == 1)
        return pathKeys.first();

    auto after = pathKeys.lowerBound(maskTimeUs);
    if (after == pathKeys.constEnd())
        return std::prev(after).value();
    if (after.key() == maskTimeUs || after == pathKeys.constBegin())
        return after.value();

    const auto before = std::prev(after);
    const QVector<QPointF> &a = before.value();
    const QVector<QPointF> &b = after.value();
    if (a.size() != b.size())
        return a;

    const double span = double(after.key() - before.key());
    const double t = span > 0.0 ? double(maskTimeUs - before.key()) / span : 0.0;

    QVector<QPointF> out;
    out.reserve(a.size());
    for (int i = 0; i < a.size(); ++i)
        out.append(a.at(i) + (b.at(i) - a.at(i)) * t);
    return out;
}

} // namespace

Mask Mask::resolvedAt(TimeUs maskTimeUs) const
{
    Mask out = *this;
    for (auto it = keyframes.constBegin(); it != keyframes.constEnd(); ++it) {
        if (it->isEmpty())
            continue;
        const double value = it->evaluateAt(maskTimeUs);
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
        out.points = pointsAt(pathKeys, maskTimeUs, points);
    return out;
}

bool masksAreInert(const QList<Mask> &masks)
{
    for (const Mask &mask : masks) {
        if (mask.contributes())
            return false;
    }
    return true;
}

Mask fullFrameMediaMask(const QString &path, TimeUs srcOffsetUs)
{
    Mask mask;
    mask.shape = MaskShape::Media;
    mask.mediaPath = path;
    mask.mediaSrcOffsetUs = srcOffsetUs;
    mask.w = 1.0;
    mask.h = 1.0;
    return mask;
}

} // namespace drift
