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
    case MaskShape::Matte:
        return QStringLiteral("matte");
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
    if (shape == QStringLiteral("matte"))
        return MaskShape::Matte;
    return MaskShape::None;
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

} // namespace drift
