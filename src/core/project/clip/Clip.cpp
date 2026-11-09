#include "core/project/clip/Clip.h"

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
    case ClipType::Vector:
        return QStringLiteral("vector");
    case ClipType::Model3d:
        return QStringLiteral("model3d");
    case ClipType::Pdf:
        return QStringLiteral("pdf");
    case ClipType::Composite:
        return QStringLiteral("composite");
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
    if (type == QStringLiteral("vector"))
        return ClipType::Vector;
    if (type == QStringLiteral("model3d"))
        return ClipType::Model3d;
    if (type == QStringLiteral("pdf"))
        return ClipType::Pdf;
    if (type == QStringLiteral("composite"))
        return ClipType::Composite;
    return ClipType::Video;
}

QString adjustmentKindToString(AdjustmentKind kind)
{
    switch (kind) {
    case AdjustmentKind::VideoEffects:
        return QStringLiteral("videoEffects");
    case AdjustmentKind::AudioEffects:
        return QStringLiteral("audioEffects");
    case AdjustmentKind::Mask:
        return QStringLiteral("mask");
    case AdjustmentKind::Transform:
        return QStringLiteral("transform");
    case AdjustmentKind::Camera:
        return QStringLiteral("camera");
    }
    return QStringLiteral("videoEffects");
}

AdjustmentKind adjustmentKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("audioEffects"))
        return AdjustmentKind::AudioEffects;
    if (kind == QStringLiteral("mask"))
        return AdjustmentKind::Mask;
    if (kind == QStringLiteral("transform"))
        return AdjustmentKind::Transform;
    if (kind == QStringLiteral("camera"))
        return AdjustmentKind::Camera;
    return AdjustmentKind::VideoEffects;
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
    case BlendMode::ColorBurn:
        return QStringLiteral("colorBurn");
    case BlendMode::LinearBurn:
        return QStringLiteral("linearBurn");
    case BlendMode::DarkerColor:
        return QStringLiteral("darkerColor");
    case BlendMode::ColorDodge:
        return QStringLiteral("colorDodge");
    case BlendMode::LighterColor:
        return QStringLiteral("lighterColor");
    case BlendMode::SoftLight:
        return QStringLiteral("softLight");
    case BlendMode::HardLight:
        return QStringLiteral("hardLight");
    case BlendMode::VividLight:
        return QStringLiteral("vividLight");
    case BlendMode::LinearLight:
        return QStringLiteral("linearLight");
    case BlendMode::PinLight:
        return QStringLiteral("pinLight");
    case BlendMode::HardMix:
        return QStringLiteral("hardMix");
    case BlendMode::Difference:
        return QStringLiteral("difference");
    case BlendMode::Exclusion:
        return QStringLiteral("exclusion");
    case BlendMode::Subtract:
        return QStringLiteral("subtract");
    case BlendMode::Divide:
        return QStringLiteral("divide");
    case BlendMode::Hue:
        return QStringLiteral("hue");
    case BlendMode::Saturation:
        return QStringLiteral("saturation");
    case BlendMode::Color:
        return QStringLiteral("color");
    case BlendMode::Luminosity:
        return QStringLiteral("luminosity");
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
    if (mode == QStringLiteral("colorBurn"))
        return BlendMode::ColorBurn;
    if (mode == QStringLiteral("linearBurn"))
        return BlendMode::LinearBurn;
    if (mode == QStringLiteral("darkerColor"))
        return BlendMode::DarkerColor;
    if (mode == QStringLiteral("colorDodge"))
        return BlendMode::ColorDodge;
    if (mode == QStringLiteral("lighterColor"))
        return BlendMode::LighterColor;
    if (mode == QStringLiteral("softLight"))
        return BlendMode::SoftLight;
    if (mode == QStringLiteral("hardLight"))
        return BlendMode::HardLight;
    if (mode == QStringLiteral("vividLight"))
        return BlendMode::VividLight;
    if (mode == QStringLiteral("linearLight"))
        return BlendMode::LinearLight;
    if (mode == QStringLiteral("pinLight"))
        return BlendMode::PinLight;
    if (mode == QStringLiteral("hardMix"))
        return BlendMode::HardMix;
    if (mode == QStringLiteral("difference"))
        return BlendMode::Difference;
    if (mode == QStringLiteral("exclusion"))
        return BlendMode::Exclusion;
    if (mode == QStringLiteral("subtract"))
        return BlendMode::Subtract;
    if (mode == QStringLiteral("divide"))
        return BlendMode::Divide;
    if (mode == QStringLiteral("hue"))
        return BlendMode::Hue;
    if (mode == QStringLiteral("saturation"))
        return BlendMode::Saturation;
    if (mode == QStringLiteral("color"))
        return BlendMode::Color;
    if (mode == QStringLiteral("luminosity"))
        return BlendMode::Luminosity;
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
