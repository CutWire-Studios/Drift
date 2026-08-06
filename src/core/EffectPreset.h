#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace drift {

// Colour is a distinct type rather than three float sliders because a shade is picked, not dialled,
// and because the GPU runtime already binds a "#rrggbb" string as a vec3.
// FilePath is for user-supplied assets (face-prop .glb); it is never keyframed and never bound as
// a uniform — the engine reads the string out of the parameter map before draw.
enum class EffectParamType {
    Float,
    Bool,
    Color,
    FilePath,
};

// User-adjustable parameter metadata for an effect preset (GUI-free).
struct EffectParamSpec
{
    QString key;
    QString label;
    EffectParamType type = EffectParamType::Float;
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;
    QString defaultColorHex = QStringLiteral("#ffffff"); // normalized to 6 digits at parse time
    QString defaultString;                              // FilePath default (absolute after resolve)
    QStringList fileFilters;                            // QFileDialog name filters

    bool isBoolean() const { return type == EffectParamType::Bool; }
    bool isColor() const { return type == EffectParamType::Color; }
    bool isFilePath() const { return type == EffectParamType::FilePath; }

    // The catalog default as the QVariant a parameter map wants. Every caller used to spell this
    // out as a ternary, and each one was a place to forget a new type.
    QVariant defaultVariant() const
    {
        switch (type) {
        case EffectParamType::Bool:
            return QVariant(defaultValue > 0.5);
        case EffectParamType::Color:
            return QVariant(defaultColorHex);
        case EffectParamType::FilePath:
            return QVariant(defaultString);
        case EffectParamType::Float:
            break;
        }
        return QVariant(defaultValue);
    }

    // What effectToMap and the QML inspectors switch on.
    QString typeName() const
    {
        switch (type) {
        case EffectParamType::Bool:
            return QStringLiteral("bool");
        case EffectParamType::Color:
            return QStringLiteral("color");
        case EffectParamType::FilePath:
            return QStringLiteral("file");
        case EffectParamType::Float:
            break;
        }
        return QStringLiteral("float");
    }
};

// Stable catalog entry describing an effect preset without FFmpeg details.
struct EffectPresetMeta
{
    QString id;           // e.g. "rgb_split", "stylize.bloom"
    QString displayName;  // e.g. "RGB Split"
    QString category;     // stable slug, e.g. "glitch", "retro", "dreamy", "impact"
    QList<EffectParamSpec> parameters;
    bool compositorOnly = false; // true when not expressible via libavfilter alone
};

} // namespace drift
