#include "Clip.h"

namespace drift {

QString clipTypeToString(ClipType type)
{
    switch (type) {
    case ClipType::Video:
        return QStringLiteral("video");
    case ClipType::Audio:
        return QStringLiteral("audio");
    case ClipType::Image:
        return QStringLiteral("image");
    case ClipType::Text:
        return QStringLiteral("text");
    case ClipType::Subtitle:
        return QStringLiteral("subtitle");
    case ClipType::Shape:
        return QStringLiteral("shape");
    case ClipType::Adjustment:
        return QStringLiteral("adjustment");
    }
    return QStringLiteral("video");
}

ClipType clipTypeFromString(const QString &type)
{
    if (type == QStringLiteral("audio"))
        return ClipType::Audio;
    if (type == QStringLiteral("image"))
        return ClipType::Image;
    if (type == QStringLiteral("text"))
        return ClipType::Text;
    if (type == QStringLiteral("subtitle"))
        return ClipType::Subtitle;
    if (type == QStringLiteral("shape"))
        return ClipType::Shape;
    if (type == QStringLiteral("adjustment"))
        return ClipType::Adjustment;
    return ClipType::Video;
}

QString blendModeToString(BlendMode mode)
{
    switch (mode) {
    case BlendMode::Normal:
        return QStringLiteral("normal");
    case BlendMode::Multiply:
        return QStringLiteral("multiply");
    case BlendMode::Screen:
        return QStringLiteral("screen");
    case BlendMode::Overlay:
        return QStringLiteral("overlay");
    case BlendMode::Add:
        return QStringLiteral("add");
    case BlendMode::Darken:
        return QStringLiteral("darken");
    case BlendMode::Lighten:
        return QStringLiteral("lighten");
    }
    return QStringLiteral("normal");
}

BlendMode blendModeFromString(const QString &mode)
{
    if (mode == QStringLiteral("multiply"))
        return BlendMode::Multiply;
    if (mode == QStringLiteral("screen"))
        return BlendMode::Screen;
    if (mode == QStringLiteral("overlay"))
        return BlendMode::Overlay;
    if (mode == QStringLiteral("add"))
        return BlendMode::Add;
    if (mode == QStringLiteral("darken"))
        return BlendMode::Darken;
    if (mode == QStringLiteral("lighten"))
        return BlendMode::Lighten;
    return BlendMode::Normal;
}

QString stabilizeModeToString(StabilizeMode mode)
{
    if (mode == StabilizeMode::Keyframes)
        return QStringLiteral("keyframes");
    return QStringLiteral("bake");
}

StabilizeMode stabilizeModeFromString(const QString &mode)
{
    if (mode == QLatin1String("keyframes"))
        return StabilizeMode::Keyframes;
    return StabilizeMode::Bake;
}

} // namespace drift
