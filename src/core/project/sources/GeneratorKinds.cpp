#include "core/project/sources/GeneratorKinds.h"

#include <QCoreApplication>

#define GTR(text) QCoreApplication::translate("GeneratorKinds", text)

namespace drift {

namespace {

EffectParamSpec number(const QString &key, const QString &label, double min, double max, double def,
                       double step = 0.0)
{
    EffectParamSpec p;
    p.key = key;
    p.label = label;
    p.min = min;
    p.max = max;
    p.defaultValue = def;
    p.step = step;
    return p;
}

EffectParamSpec integer(const QString &key, const QString &label, int min, int max, int def)
{
    EffectParamSpec p = number(key, label, min, max, def, 1.0);
    p.type = EffectParamType::Int;
    return p;
}

EffectParamSpec color(const QString &key, const QString &label, const QString &hex, bool alpha = false)
{
    EffectParamSpec p;
    p.key = key;
    p.label = label;
    p.type = EffectParamType::Color;
    p.defaultColorHex = alpha ? hex + QStringLiteral("ff") : hex;
    p.alpha = alpha;
    return p;
}

EffectParamSpec toggle(const QString &key, const QString &label, bool def)
{
    EffectParamSpec p;
    p.key = key;
    p.label = label;
    p.type = EffectParamType::Bool;
    p.defaultValue = def ? 1.0 : 0.0;
    return p;
}

EffectParamSpec choice(const QString &key, const QString &label, const QStringList &options, int def = 0)
{
    EffectParamSpec p;
    p.key = key;
    p.label = label;
    p.type = EffectParamType::Enum;
    p.options = options;
    p.min = 0;
    p.max = options.size() - 1;
    p.defaultValue = def;
    return p;
}

EffectParamSpec point(const QString &key, const QString &label, double x, double y, double min = -0.5,
                      double max = 1.5)
{
    EffectParamSpec p;
    p.key = key;
    p.label = label;
    p.type = EffectParamType::Vec2;
    p.min = min;
    p.max = max;
    p.defaultX = x;
    p.defaultY = y;
    return p;
}

EffectParamSpec text(const QString &key, const QString &label)
{
    EffectParamSpec p;
    p.key = key;
    p.label = label;
    p.type = EffectParamType::FilePath;
    return p;
}

EffectParamSpec grouped(EffectParamSpec p, const QString &group)
{
    p.group = group;
    return p;
}

QList<GeneratorKind> buildKinds()
{
    QList<GeneratorKind> kinds;

    GeneratorKind solid;
    solid.id = QStringLiteral("gen.solid");
    solid.label = GTR("Solid colour");
    solid.category = QStringLiteral("background");
    solid.description = GTR("A flat colour that fills the frame.");
    solid.parameters = {color(QStringLiteral("color"), GTR("Colour"), QStringLiteral("#3b82f6"), true)};
    kinds.append(solid);

    GeneratorKind gradient;
    gradient.id = QStringLiteral("gen.gradient");
    gradient.label = GTR("Gradient");
    gradient.category = QStringLiteral("background");
    gradient.description = GTR("A linear, radial or sweep gradient with up to five colour stops.");
    gradient.parameters = {
        choice(QStringLiteral("type"), GTR("Type"), {GTR("Linear"), GTR("Radial"), GTR("Sweep")}),
        number(QStringLiteral("angle"), GTR("Angle"), 0, 360, 90, 1),
        integer(QStringLiteral("stops"), GTR("Colour stops"), 2, 5, 3),
        color(QStringLiteral("color1"), GTR("Colour 1"), QStringLiteral("#ff512f")),
        color(QStringLiteral("color2"), GTR("Colour 2"), QStringLiteral("#dd2476")),
        color(QStringLiteral("color3"), GTR("Colour 3"), QStringLiteral("#7f00ff")),
        color(QStringLiteral("color4"), GTR("Colour 4"), QStringLiteral("#00c6ff")),
        color(QStringLiteral("color5"), GTR("Colour 5"), QStringLiteral("#00ff95")),
        number(QStringLiteral("pos2"), GTR("Stop 2 position"), 0, 1, 0.5),
        number(QStringLiteral("pos3"), GTR("Stop 3 position"), 0, 1, 0.75),
        number(QStringLiteral("pos4"), GTR("Stop 4 position"), 0, 1, 0.9),
        point(QStringLiteral("center"), GTR("Centre"), 0.5, 0.5, -0.5, 1.5),
        number(QStringLiteral("offset"), GTR("Offset"), -4, 4, 0),
        number(QStringLiteral("offsetSpeed"), GTR("Scroll speed"), -4, 4, 0),
        number(QStringLiteral("scale"), GTR("Scale"), 0.1, 8, 1),
        toggle(QStringLiteral("repeat"), GTR("Repeat"), false),
        toggle(QStringLiteral("oklab"), GTR("Blend in OKLab"), false),
    };
    gradient.timeDriven = false;
    kinds.append(gradient);

    GeneratorKind noise;
    noise.id = QStringLiteral("gen.noise");
    noise.label = GTR("Noise");
    noise.category = QStringLiteral("background");
    noise.description = GTR("A value, Perlin, fractal or cellular noise background that can drift over time.");
    noise.timeDriven = true;
    noise.parameters = {
        choice(QStringLiteral("type"), GTR("Type"), {GTR("Value"), GTR("Perlin"), GTR("Fractal"), GTR("Cells")}, 2),
        number(QStringLiteral("scale"), GTR("Scale"), 0.5, 120, 5),
        integer(QStringLiteral("octaves"), GTR("Octaves"), 1, 8, 4),
        number(QStringLiteral("gain"), GTR("Detail gain"), 0.1, 0.9, 0.5),
        number(QStringLiteral("speed"), GTR("Speed"), -4, 4, 0.25),
        point(QStringLiteral("offset"), GTR("Offset"), 0, 0, -100, 100),
        number(QStringLiteral("contrast"), GTR("Contrast"), 0.2, 4, 1),
        color(QStringLiteral("colorA"), GTR("Shadow colour"), QStringLiteral("#0b132b"), true),
        color(QStringLiteral("colorB"), GTR("Highlight colour"), QStringLiteral("#5bc0be"), true),
    };
    kinds.append(noise);

    GeneratorKind bars;
    bars.id = QStringLiteral("gen.bars");
    bars.label = GTR("Colour bars");
    bars.category = QStringLiteral("pattern");
    bars.description = GTR("SMPTE-style colour bars, 100% bars or a grey ramp for checking a display.");
    bars.parameters = {
        choice(QStringLiteral("style"), GTR("Style"), {GTR("SMPTE"), GTR("100% bars"), GTR("Grey ramp")}),
        number(QStringLiteral("brightness"), GTR("Brightness"), 0.2, 1.5, 1),
    };
    kinds.append(bars);

    GeneratorKind grid;
    grid.id = QStringLiteral("gen.grid");
    grid.label = GTR("Grid");
    grid.category = QStringLiteral("pattern");
    grid.description = GTR("A line grid with an accented major line.");
    grid.parameters = {
        number(QStringLiteral("size"), GTR("Cell size"), 8, 600, 120, 1),
        number(QStringLiteral("lineWidth"), GTR("Line width"), 0.5, 24, 2),
        integer(QStringLiteral("major"), GTR("Major line every"), 1, 16, 4),
        color(QStringLiteral("background"), GTR("Background"), QStringLiteral("#14161c")),
        color(QStringLiteral("lineColor"), GTR("Line colour"), QStringLiteral("#3a3f4b")),
        color(QStringLiteral("accent"), GTR("Major colour"), QStringLiteral("#7c8aa5")),
        number(QStringLiteral("scroll"), GTR("Scroll speed"), -600, 600, 0),
    };
    grid.timeDriven = true;
    kinds.append(grid);

    GeneratorKind checker;
    checker.id = QStringLiteral("gen.checker");
    checker.label = GTR("Checkerboard");
    checker.category = QStringLiteral("pattern");
    checker.description = GTR("Alternating squares, optionally scrolling.");
    checker.parameters = {
        number(QStringLiteral("size"), GTR("Square size"), 8, 800, 120, 1),
        color(QStringLiteral("colorA"), GTR("Colour A"), QStringLiteral("#ffffff")),
        color(QStringLiteral("colorB"), GTR("Colour B"), QStringLiteral("#1b1b1b")),
        number(QStringLiteral("scrollX"), GTR("Scroll X"), -800, 800, 0),
        number(QStringLiteral("scrollY"), GTR("Scroll Y"), -800, 800, 0),
    };
    checker.timeDriven = true;
    kinds.append(checker);

    GeneratorKind countdown;
    countdown.id = QStringLiteral("gen.countdown");
    countdown.label = GTR("Countdown");
    countdown.category = QStringLiteral("leader");
    countdown.description = GTR("A film-leader countdown with a clock sweep. The number uses the clip's text style.");
    countdown.defaultDurationSec = 10.0;
    countdown.timeDriven = true;
    countdown.parameters = {
        integer(QStringLiteral("from"), GTR("Count from"), 1, 99, 10),
        number(QStringLiteral("interval"), GTR("Seconds per count"), 0.2, 5, 1),
        color(QStringLiteral("background"), GTR("Background"), QStringLiteral("#111111")),
        color(QStringLiteral("line"), GTR("Ring and cross colour"), QStringLiteral("#d9d9d9")),
        color(QStringLiteral("sweepColor"), GTR("Sweep colour"), QStringLiteral("#ffffff"), true),
        toggle(QStringLiteral("sweep"), GTR("Clock sweep"), true),
        toggle(QStringLiteral("cross"), GTR("Cross hairs and ring"), true),
    };
    kinds.append(countdown);

    GeneratorKind particles;
    particles.id = QStringLiteral("gen.particles");
    particles.label = GTR("Particle emitter");
    particles.category = QStringLiteral("particles");
    particles.description = GTR("A particle system. It is simulated from the clip start, so scrubbing and export agree.");
    particles.defaultDurationSec = 6.0;
    particles.timeDriven = true;
    const QString emitter = GTR("Emitter");
    const QString motion = GTR("Motion");
    const QString look = GTR("Look");
    particles.parameters = {
        grouped(choice(QStringLiteral("emitterShape"), GTR("Shape"), {GTR("Point"), GTR("Line"), GTR("Rectangle"), GTR("Circle")}), emitter),
        grouped(point(QStringLiteral("emitter"), GTR("Position"), 0.5, 0.5), emitter),
        grouped(number(QStringLiteral("emitterW"), GTR("Width"), 0, 1.5, 0.3), emitter),
        grouped(number(QStringLiteral("emitterH"), GTR("Height"), 0, 1.5, 0.3), emitter),
        grouped(number(QStringLiteral("rate"), GTR("Rate per second"), 0, 2000, 60), emitter),
        grouped(integer(QStringLiteral("burst"), GTR("Burst count"), 0, 2000, 0), emitter),
        grouped(number(QStringLiteral("burstEvery"), GTR("Repeat burst every (s)"), 0, 30, 0), emitter),
        grouped(toggle(QStringLiteral("burstAtPoint"), GTR("Burst from one random point"), false), emitter),
        grouped(number(QStringLiteral("prewarm"), GTR("Pre-warm (s)"), 0, 30, 0), emitter),
        grouped(number(QStringLiteral("life"), GTR("Lifetime (s)"), 0.1, 30, 2), motion),
        grouped(number(QStringLiteral("lifeVar"), GTR("Lifetime variation"), 0, 1, 0.3), motion),
        grouped(number(QStringLiteral("speed"), GTR("Initial speed"), 0, 3000, 300), motion),
        grouped(number(QStringLiteral("speedVar"), GTR("Speed variation"), 0, 1, 0.3), motion),
        grouped(number(QStringLiteral("angle"), GTR("Direction"), 0, 360, 270, 1), motion),
        grouped(number(QStringLiteral("spread"), GTR("Spread"), 0, 360, 30, 1), motion),
        grouped(number(QStringLiteral("gravity"), GTR("Gravity"), -2000, 2000, 0), motion),
        grouped(number(QStringLiteral("wind"), GTR("Wind"), -2000, 2000, 0), motion),
        grouped(number(QStringLiteral("drag"), GTR("Drag"), 0, 6, 0), motion),
        grouped(number(QStringLiteral("turbulence"), GTR("Turbulence"), 0, 1500, 0), motion),
        grouped(number(QStringLiteral("turbulenceScale"), GTR("Turbulence scale"), 0.1, 10, 1), motion),
        grouped(number(QStringLiteral("spin"), GTR("Spin (deg/s)"), -1440, 1440, 0), motion),
        grouped(number(QStringLiteral("spinVar"), GTR("Spin variation"), 0, 1440, 0), motion),
        grouped(toggle(QStringLiteral("randomRotation"), GTR("Random start rotation"), true), motion),
        grouped(toggle(QStringLiteral("alignVelocity"), GTR("Point along motion"), false), motion),
        grouped(number(QStringLiteral("stretch"), GTR("Stretch with speed"), 0, 5, 0), motion),
        grouped(choice(QStringLiteral("sprite"), GTR("Sprite"),
                       {GTR("Circle"), GTR("Soft dot"), GTR("Ring"), GTR("Square"), GTR("Star"), GTR("Spark"),
                        GTR("Confetti"), GTR("Image or emoji")}),
                look),
        grouped(text(QStringLiteral("spriteImage"), GTR("Image path or emoji")), look),
        grouped(choice(QStringLiteral("blend"), GTR("Blend"), {GTR("Normal"), GTR("Additive")}), look),
        grouped(number(QStringLiteral("size"), GTR("Size"), 1, 400, 16), look),
        grouped(number(QStringLiteral("sizeVar"), GTR("Size variation"), 0, 1, 0.3), look),
        grouped(number(QStringLiteral("sizeEnd"), GTR("Size at end of life"), 0, 5, 1), look),
        grouped(number(QStringLiteral("opacity"), GTR("Opacity at birth"), 0, 1, 1), look),
        grouped(number(QStringLiteral("opacityEnd"), GTR("Opacity at end of life"), 0, 1, 0), look),
        grouped(color(QStringLiteral("colorStart"), GTR("Colour at birth"), QStringLiteral("#ffffff")), look),
        grouped(color(QStringLiteral("colorMid"), GTR("Colour at mid-life"), QStringLiteral("#ffffff")), look),
        grouped(color(QStringLiteral("colorEnd"), GTR("Colour at end of life"), QStringLiteral("#ffffff")), look),
        grouped(toggle(QStringLiteral("rainbow"), GTR("Random colour per particle"), false), look),
    };
    kinds.append(particles);

    return kinds;
}

using Params = QMap<QString, QVariant>;

GeneratorPreset preset(const QString &id, const QString &label, double duration, Params params)
{
    GeneratorPreset p;
    p.id = QStringLiteral("particles.") + id;
    p.label = label;
    p.kindId = QStringLiteral("gen.particles");
    p.category = QStringLiteral("particles");
    p.durationSec = duration;
    p.params = std::move(params);
    return p;
}

QList<GeneratorPreset> buildPresets()
{
    const auto at = [](Params &p, const QString &key, double x, double y) {
        p.insert(key + QStringLiteral(".x"), x);
        p.insert(key + QStringLiteral(".y"), y);
    };
    QList<GeneratorPreset> out;

    Params confetti{{"rate", 0.0}, {"burst", 160}, {"emitterShape", 0}, {"life", 3.5}, {"lifeVar", 0.3},
                    {"speed", 950.0}, {"speedVar", 0.5}, {"angle", 270.0}, {"spread", 80.0},
                    {"gravity", 1300.0}, {"drag", 0.7}, {"turbulence", 140.0}, {"spin", 0.0},
                    {"spinVar", 720.0}, {"sprite", 6}, {"size", 20.0}, {"sizeVar", 0.35}, {"opacityEnd", 1.0},
                    {"rainbow", true}, {"stretch", 0.0}};
    at(confetti, QStringLiteral("emitter"), 0.5, 0.75);
    out.append(preset(QStringLiteral("confetti"), GTR("Confetti burst"), 5.0, confetti));

    Params fireworks{{"rate", 0.0}, {"burst", 110}, {"burstEvery", 1.1}, {"burstAtPoint", true},
                     {"emitterShape", 2}, {"emitterW", 0.7}, {"emitterH", 0.35}, {"life", 1.7},
                     {"lifeVar", 0.25}, {"speed", 360.0}, {"speedVar", 0.35}, {"angle", 0.0},
                     {"spread", 360.0}, {"gravity", 260.0}, {"drag", 1.1}, {"sprite", 1}, {"blend", 1},
                     {"size", 11.0}, {"sizeVar", 0.3}, {"sizeEnd", 0.3}, {"opacityEnd", 0.0},
                     {"rainbow", true}, {"randomRotation", false}};
    at(fireworks, QStringLiteral("emitter"), 0.5, 0.35);
    out.append(preset(QStringLiteral("fireworks"), GTR("Fireworks"), 8.0, fireworks));

    Params sparks{{"rate", 140.0}, {"emitterShape", 0}, {"life", 0.9}, {"lifeVar", 0.4}, {"speed", 700.0},
                  {"speedVar", 0.5}, {"angle", 270.0}, {"spread", 55.0}, {"gravity", 1100.0}, {"drag", 0.4},
                  {"sprite", 5}, {"blend", 1}, {"size", 9.0}, {"sizeVar", 0.4}, {"sizeEnd", 0.4},
                  {"alignVelocity", true}, {"stretch", 1.2}, {"opacityEnd", 0.0},
                  {"colorStart", "#fff3b0"}, {"colorMid", "#ffb23e"}, {"colorEnd", "#ff3d00"},
                  {"randomRotation", false}};
    at(sparks, QStringLiteral("emitter"), 0.5, 0.8);
    out.append(preset(QStringLiteral("sparks"), GTR("Sparks"), 6.0, sparks));

    Params snow{{"rate", 90.0}, {"emitterShape", 1}, {"emitterW", 1.3}, {"life", 11.0}, {"lifeVar", 0.2},
                {"speed", 90.0}, {"speedVar", 0.5}, {"angle", 90.0}, {"spread", 20.0}, {"wind", 25.0},
                {"turbulence", 110.0}, {"turbulenceScale", 1.5}, {"sprite", 1}, {"size", 10.0},
                {"sizeVar", 0.7}, {"opacity", 0.9}, {"opacityEnd", 0.9}, {"prewarm", 11.0},
                {"colorStart", "#ffffff"}, {"colorMid", "#ffffff"}, {"colorEnd", "#ffffff"}};
    at(snow, QStringLiteral("emitter"), 0.5, -0.05);
    out.append(preset(QStringLiteral("snow"), GTR("Snow"), 10.0, snow));

    Params rain{{"rate", 520.0}, {"emitterShape", 1}, {"emitterW", 1.4}, {"life", 1.1}, {"lifeVar", 0.1},
                {"speed", 1500.0}, {"speedVar", 0.2}, {"angle", 98.0}, {"spread", 2.0}, {"sprite", 3},
                {"alignVelocity", true}, {"stretch", 4.0}, {"size", 3.0}, {"sizeVar", 0.3},
                {"opacity", 0.55}, {"opacityEnd", 0.55}, {"prewarm", 1.1}, {"randomRotation", false},
                {"colorStart", "#bcd6ff"}, {"colorMid", "#bcd6ff"}, {"colorEnd", "#bcd6ff"}};
    at(rain, QStringLiteral("emitter"), 0.55, -0.1);
    out.append(preset(QStringLiteral("rain"), GTR("Rain"), 10.0, rain));

    Params smoke{{"rate", 28.0}, {"emitterShape", 0}, {"life", 4.5}, {"lifeVar", 0.3}, {"speed", 130.0},
                 {"speedVar", 0.4}, {"angle", 270.0}, {"spread", 28.0}, {"drag", 0.7}, {"wind", 35.0},
                 {"turbulence", 140.0}, {"turbulenceScale", 0.8}, {"spin", 12.0}, {"spinVar", 40.0},
                 {"sprite", 1}, {"size", 110.0}, {"sizeVar", 0.4}, {"sizeEnd", 3.0}, {"opacity", 0.35},
                 {"opacityEnd", 0.0}, {"prewarm", 4.5}, {"colorStart", "#d9d9d9"}, {"colorMid", "#9a9a9a"},
                 {"colorEnd", "#5a5a5a"}};
    at(smoke, QStringLiteral("emitter"), 0.5, 0.85);
    out.append(preset(QStringLiteral("smoke"), GTR("Smoke puffs"), 8.0, smoke));

    Params dust{{"rate", 75.0}, {"emitterShape", 3}, {"emitterW", 0.25}, {"emitterH", 0.25}, {"life", 2.6},
                {"lifeVar", 0.4}, {"speed", 45.0}, {"speedVar", 0.8}, {"angle", 270.0}, {"spread", 360.0},
                {"gravity", -50.0}, {"drag", 0.3}, {"turbulence", 240.0}, {"turbulenceScale", 1.8},
                {"sprite", 1}, {"blend", 1}, {"size", 12.0}, {"sizeVar", 0.6}, {"sizeEnd", 0.2},
                {"opacityEnd", 0.0}, {"prewarm", 2.6}, {"colorStart", "#fff3a0"}, {"colorMid", "#a0e0ff"},
                {"colorEnd", "#c080ff"}};
    at(dust, QStringLiteral("emitter"), 0.5, 0.5);
    out.append(preset(QStringLiteral("magicdust"), GTR("Magic dust"), 8.0, dust));

    Params bubbles{{"rate", 11.0}, {"emitterShape", 1}, {"emitterW", 0.8}, {"life", 7.5}, {"lifeVar", 0.3},
                   {"speed", 140.0}, {"speedVar", 0.4}, {"angle", 270.0}, {"spread", 14.0},
                   {"gravity", -25.0}, {"wind", 12.0}, {"turbulence", 90.0}, {"turbulenceScale", 1.2},
                   {"sprite", 2}, {"size", 46.0}, {"sizeVar", 0.7}, {"opacity", 0.85}, {"opacityEnd", 0.7},
                   {"prewarm", 7.5}, {"colorStart", "#cdeeff"}, {"colorMid", "#cdeeff"},
                   {"colorEnd", "#e8f8ff"}, {"randomRotation", false}};
    at(bubbles, QStringLiteral("emitter"), 0.5, 1.05);
    out.append(preset(QStringLiteral("bubbles"), GTR("Bubbles"), 10.0, bubbles));

    return out;
}

} // namespace

const QList<GeneratorKind> &generatorKinds()
{
    static const QList<GeneratorKind> kinds = buildKinds();
    return kinds;
}

const GeneratorKind *generatorKind(const QString &id)
{
    for (const GeneratorKind &kind : generatorKinds()) {
        if (kind.id == id)
            return &kind;
    }
    return nullptr;
}

const QList<GeneratorPreset> &generatorPresets()
{
    static const QList<GeneratorPreset> presets = buildPresets();
    return presets;
}

const GeneratorPreset *generatorPreset(const QString &id)
{
    for (const GeneratorPreset &p : generatorPresets()) {
        if (p.id == id)
            return &p;
    }
    return nullptr;
}

QMap<QString, QVariant> generatorDefaultParameters(const GeneratorKind &kind, const QMap<QString, QVariant> &overrides)
{
    QMap<QString, QVariant> out;
    for (const EffectParamSpec &spec : kind.parameters)
        spec.insertDefault(out);
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

} // namespace drift
