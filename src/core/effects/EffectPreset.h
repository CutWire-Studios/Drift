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
    // A float seed, shown with a shuffle button. Bound as a float.
    Seed,
    // Stored as four numbers, "<key>.x/.y/.w/.h", each keyframable; bound as a vec4.
    Region,
    // A picture file in the package, stored as an absolute path; bound as a sampler2D.
    Image,
    // Gradient stops stored as a JSON string; baked to a 256x1 sampler2D.
    Gradient,
    // Curve keys stored as a JSON string; baked to a 256x1 sampler2D.
    Curve,
    // A colour look id (not a path) resolved to a .cube file; baked to a tiled 1089x33 sampler2D.
    Lut,
    // Another track's picture, by track id ("" = none); bound as a sampler2D.
    Track,
    // Another track's folded mask lanes, by track id; bound as a sampler2D.
    TrackMask,
    // Another track's audio, by track id ("" = Timeline mix); a sampler2D plus scalars.
    Audio,
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
    // Region params: the default width and height, and the shape the preview draws.
    double defaultW = 1.0;
    double defaultH = 1.0;
    QString regionShape = QStringLiteral("rect");
    // Colour params: carries opacity, so the stored hex is #rrggbbaa and the uniform is a vec4.
    bool alpha = false;
    // Curve params: "hue" for a curve over hue, which wraps at red and rests flat at y = 0.5.
    QString curveMode;
    // Curve params: the channel colour the inspector draws it in ("master", "r", "g", "b", "hue", "sat", "lum").
    QString curveChannel;
    // Vec2 params: "colorWheel" shows a colour wheel instead of the XY pad.
    QString widget;
    // Float params: a coloured slider track ("temperature", "tint", "luma", or "hue:<deg>", "sat:<deg>", "lum:<deg>" for an HSL band).
    QString track;

    bool isBoolean() const { return type == EffectParamType::Bool; }
    bool isColor() const { return type == EffectParamType::Color; }
    bool isFilePath() const { return type == EffectParamType::FilePath; }
    bool isClip() const { return type == EffectParamType::Clip; }
    bool isVec2() const { return type == EffectParamType::Vec2; }
    bool isRegion() const { return type == EffectParamType::Region; }
    bool isEnum() const { return type == EffectParamType::Enum; }
    bool isInt() const { return type == EffectParamType::Int; }
    bool isSampler() const
    {
        return type == EffectParamType::Image || type == EffectParamType::Gradient
               || type == EffectParamType::Curve || type == EffectParamType::Lut
               || type == EffectParamType::Track
               || type == EffectParamType::TrackMask || type == EffectParamType::Audio;
    }
    // Strings on the parameter map rather than numbers: never keyframed, never bound as uniforms.
    bool isText() const { return isColor() || isFilePath() || isClip() || isSampler(); }

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
        case EffectParamType::Track:
        case EffectParamType::TrackMask:
        case EffectParamType::Audio:
            return QVariant(QString());
        case EffectParamType::Image:
        case EffectParamType::Gradient:
        case EffectParamType::Curve:
        case EffectParamType::Lut:
            return QVariant(defaultString);
        case EffectParamType::Vec2:
        case EffectParamType::Region:
            return QVariant(defaultX);
        case EffectParamType::Float:
        case EffectParamType::Enum:
        case EffectParamType::Int:
        case EffectParamType::Seed:
            break;
        }
        return QVariant(defaultValue);
    }

    // Seeds a parameter map with this param's default. A vec2 or region is several entries, so
    // callers should not insert defaultVariant() under `key` themselves.
    template<typename Map>
    void insertDefault(Map &params) const
    {
        if (type == EffectParamType::Vec2) {
            params.insert(key + QStringLiteral(".x"), QVariant(defaultX));
            params.insert(key + QStringLiteral(".y"), QVariant(defaultY));
            return;
        }
        if (type == EffectParamType::Region) {
            params.insert(key + QStringLiteral(".x"), QVariant(defaultX));
            params.insert(key + QStringLiteral(".y"), QVariant(defaultY));
            params.insert(key + QStringLiteral(".w"), QVariant(defaultW));
            params.insert(key + QStringLiteral(".h"), QVariant(defaultH));
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
        case EffectParamType::Seed:
            return QStringLiteral("seed");
        case EffectParamType::Region:
            return QStringLiteral("region");
        case EffectParamType::Image:
            return QStringLiteral("image");
        case EffectParamType::Gradient:
            return QStringLiteral("gradient");
        case EffectParamType::Curve:
            return QStringLiteral("curve");
        case EffectParamType::Lut:
            return QStringLiteral("lut");
        case EffectParamType::Track:
            return QStringLiteral("track");
        case EffectParamType::TrackMask:
            return QStringLiteral("trackMask");
        case EffectParamType::Audio:
            return QStringLiteral("audio");
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
