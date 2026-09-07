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
