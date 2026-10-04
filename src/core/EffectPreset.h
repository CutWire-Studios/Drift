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
    // Another clip on the timeline, by id. Empty means the effect picks one itself.
    Clip,
    // Stored as two numbers, "<key>.x" and "<key>.y", so each axis keyframes as an ordinary float
    // track; bound as a vec2.
    Vec2,
    // The chosen option's index, stored and bound as a float.
    Enum,
    // A float rounded to a whole number at bind time.
    Int,
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
    // Controls a shader stage GLES cannot compile. The inspector drops these on an ES context,
    // where the renderer ignores them anyway.
    bool desktopGlOnly = false;
    // Inspector section this parameter folds into ("Light 2"); empty for the effect's own
    // controls. Presentation only: it never reaches the shader.
    QString group;
    // The group starts folded even when it is the first one ("groupCollapsed": true).
    bool groupCollapsed = false;
    // Colour params: preset shades the inspector shows as a grid beside the picker, normalized
    // to #rrggbb at parse time.
    QStringList swatches;
    // Colour params: a bool param that editing this colour switches on, for packages where the
    // colour only applies once a "custom colour" toggle is set.
    QString enables;
    // Enum params: the labels the inspector offers; the stored value is the index.
    QStringList options;
    // Float/Int params: the inspector snaps to multiples of this when positive.
    double step = 0.0;
    // Vec2 params: default position and per-axis range (min/max above are the shared range).
    double defaultX = 0.0;
    double defaultY = 0.0;
    // Colour params: carries opacity, so the stored hex is #rrggbbaa and the uniform is a vec4.
    bool alpha = false;

    bool isBoolean() const { return type == EffectParamType::Bool; }
    bool isColor() const { return type == EffectParamType::Color; }
    bool isFilePath() const { return type == EffectParamType::FilePath; }
    bool isClip() const { return type == EffectParamType::Clip; }
    bool isVec2() const { return type == EffectParamType::Vec2; }
    bool isEnum() const { return type == EffectParamType::Enum; }
    bool isInt() const { return type == EffectParamType::Int; }
    // Strings on the parameter map rather than numbers: never keyframed, never bound as uniforms.
    bool isText() const { return isColor() || isFilePath() || isClip(); }

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
        case EffectParamType::Clip:
            return QVariant(QString());
        case EffectParamType::Vec2:
            return QVariant(defaultX);
        case EffectParamType::Float:
        case EffectParamType::Enum:
        case EffectParamType::Int:
            break;
        }
        return QVariant(defaultValue);
    }

    // Seeds a parameter map with this param's default. A vec2 is two entries, so callers should
    // not insert defaultVariant() under `key` themselves.
    template<typename Map>
    void insertDefault(Map &params) const
    {
        if (type == EffectParamType::Vec2) {
            params.insert(key + QStringLiteral(".x"), QVariant(defaultX));
            params.insert(key + QStringLiteral(".y"), QVariant(defaultY));
            return;
        }
        params.insert(key, defaultVariant());
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
        case EffectParamType::Clip:
            return QStringLiteral("clip");
        case EffectParamType::Vec2:
            return QStringLiteral("vec2");
        case EffectParamType::Enum:
            return QStringLiteral("enum");
        case EffectParamType::Int:
            return QStringLiteral("int");
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
