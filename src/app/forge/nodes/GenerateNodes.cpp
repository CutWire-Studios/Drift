#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

using namespace build;
using Q = QString;

InputDef center()
{
    return widget(vecIn("center", "Centre", 0.5, 0.5, 0, 1), "point");
}

Q shapeSdf(const Q &shape, const Q &s)
{
    if (shape == QLatin1String("box"))
        return QStringLiteral(R"(vec2 q = abs(d) - vec2(%1);
    float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);)").arg(s);
    if (shape == QLatin1String("ring"))
        return QStringLiteral("float sd = abs(length(d) - %1) - %1 * 0.15;").arg(s);
    if (shape == QLatin1String("diamond"))
        return QStringLiteral("float sd = (abs(d.x) + abs(d.y) - %1) * 0.7071;").arg(s);
    if (shape == QLatin1String("star"))
        return QStringLiteral(R"(float a = atan(d.x, -d.y);
    float seg = 6.2832 / 5.0;
    float k = abs(mod(a, seg) - seg * 0.5) / (seg * 0.5);
    float sd = length(d) - %1 * mix(0.45, 1.0, k * k);)").arg(s);
    if (shape == QLatin1String("heart"))
        return QStringLiteral(R"(vec2 h = vec2(abs(d.x), -d.y + %1 * 0.25) / max(%1, 0.0001);
    float sd = (length(h - vec2(0.0, 0.25 * (1.0 + h.x))) - 0.75 + h.x * 0.35) * %1;)").arg(s);
    return QStringLiteral("float sd = length(d) - %1;").arg(s);
}

}

void registerGenerateNodes()
{
    using Stage = const StageReader &;

    NodeDef noise = node("noise", "Noise", "generate", "Smooth random clouds. Great for dissolves, flicker and organic movement.");
    noise.inputs = {floatIn("scale", "Scale", 6, 0.5, 60), floatIn("speed", "Speed", 0.3, 0, 5), clockIn("t", "Time"),
                    widget(floatIn("seed", "Seed", 0, 0, 1000), "seed")};
    noise.options = {selectOpt("style", "Style",
                               {choice("cloud", "Clouds (detailed)"), choice("smooth", "Smooth"), choice("blocky", "Blocky")}, "cloud")};
    noise.outputs = {out("value", "Value", SocketType::Float)};
    noise.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q style = c.optString("style");
        const Q fn = style == QLatin1String("cloud") ? Q("fbm") : style == QLatin1String("smooth") ? Q("valueNoise") : Q("hash21");
        c.helper(fn);
        const Q asp = c.aspect();
        const Q scale = c.in("scale");
        const Q t1 = c.in("t");
        const Q speed1 = c.in("speed");
        const Q t2 = c.in("t");
        const Q speed2 = c.in("speed");
        const Q seed1 = c.in("seed");
        const Q seed2 = c.in("seed");
        const Q p = "uv * vec2(" + asp + ", 1.0) * " + scale + " + vec2(" + t1 + " * " + speed1 + ", " + t2 + " * " + speed2
            + " * 0.7) + vec2(" + seed1 + " * 17.31, " + seed2 + " * 7.77)";
        return {{"value", style == QLatin1String("blocky") ? "return hash21(floor(" + p + "));" : "return " + fn + "(" + p + ");"}};
    };

    NodeDef linear = node("linear_gradient", "Linear gradient", "generate", "A smooth blend between two colours across the frame.");
    linear.inputs = {swatch("a", "Start", rgba(0, 0, 0, 1)), swatch("b", "End", rgba(1, 1, 1, 1)),
                     widget(floatIn("angle", "Angle", 90, 0, 360), "angle")};
    linear.outputs = {out("image", "Image", SocketType::Color), out("t", "Amount", SocketType::Float)};
    linear.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q angle = c.in("angle");
        const Q t = QStringLiteral(R"(float ang = radians(%1);
    vec2 d = vec2(cos(ang), sin(ang));
    float t = clamp(dot(uv - 0.5, d) / max(abs(d.x) + abs(d.y), 0.0001) + 0.5, 0.0, 1.0);)").arg(angle);
        const Q a = c.in("a");
        const Q b = c.in("b");
        return {{"image", t + "\n    return mix(" + a + ", " + b + ", t);"}, {"t", t + "\n    return t;"}};
    };

    NodeDef radial = node("radial_gradient", "Radial gradient", "generate", "A soft circle of colour fading outwards.");
    radial.inputs = {swatch("a", "Inside", rgba(1, 1, 1, 1)), swatch("b", "Outside", rgba(0, 0, 0, 1)), center(),
                     floatIn("radius", "Radius", 0.5, 0, 2)};
    radial.outputs = {out("image", "Image", SocketType::Color), out("t", "Amount", SocketType::Float)};
    radial.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q radius = c.in("radius");
        const Q t = QStringLiteral(R"(float asp = %1;
    float t = clamp(length((uv - %2) * vec2(asp, 1.0)) / max(%3, 0.0001), 0.0, 1.0);)").arg(asp, center, radius);
        const Q a = c.in("a");
        const Q b = c.in("b");
        return {{"image", t + "\n    return mix(" + a + ", " + b + ", t);"}, {"t", t + "\n    return t;"}};
    };

    NodeDef shape = node("shape", "Shape", "generate", "A circle, box, ring, diamond or star. Outputs a mask: 1 inside, 0 outside.");
    shape.inputs = {center(), floatIn("size", "Size", 0.3, 0, 2), floatIn("softness", "Softness", 0.01, 0, 0.5),
                    widget(floatIn("rotation", "Rotation", 0, -360, 360), "angle")};
    shape.options = {selectOpt("shape", "Shape",
                               {choice("circle", "Circle"), choice("box", "Box"), choice("ring", "Ring"), choice("diamond", "Diamond"),
                                choice("star", "Star"), choice("heart", "Heart")},
                               "circle")};
    shape.outputs = {out("mask", "Mask", SocketType::Float)};
    shape.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("rot2");
        const Q s = c.in("size");
        const Q sd = shapeSdf(c.optString("shape"), s);
        const Q asp = c.aspect();
        const Q rotation = c.in("rotation");
        const Q center = c.in("center");
        const Q softness = c.in("softness");
        return {{"mask", QStringLiteral(R"(float asp = %1;
    vec2 d = rot2(-radians(%2)) * ((uv - %3) * vec2(asp, 1.0));
    %4
    float soft = max(%5, 0.0005);
    return 1.0 - smoothstep(-soft, soft, sd);)").arg(asp, rotation, center, sd, softness)}};
    };

    NodeDef stripes = node("stripes", "Stripes", "generate", "Repeating bands. Use as a mask for blinds and scanline-style looks.");
    stripes.inputs = {integer(floatIn("count", "Stripes", 10, 1, 100)), widget(floatIn("angle", "Angle", 0, 0, 360), "angle"),
                      floatIn("width", "Width", 0.5, 0, 1), floatIn("softness", "Softness", 0.02, 0, 0.5),
                      floatIn("speed", "Speed", 0, -10, 10), clockIn("t", "Time")};
    stripes.outputs = {out("mask", "Mask", SocketType::Float)};
    stripes.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q angle = c.in("angle");
        const Q count = c.in("count");
        const Q t = c.in("t");
        const Q speed = c.in("speed");
        const Q softness = c.in("softness");
        const Q width1 = c.in("width");
        const Q width2 = c.in("width");
        return {{"mask", QStringLiteral(R"(float ang = radians(%1);
    float x = dot(uv - 0.5, vec2(cos(ang), sin(ang))) * %2 + %3 * %4;
    float f = abs(fract(x) - 0.5) * 2.0;
    float soft = max(%5, 0.0005);
    return 1.0 - smoothstep(%6 - soft, %7 + soft, f);)").arg(angle, count, t, speed, softness, width1, width2)}};
    };

    NodeDef checker = node("checker", "Checkerboard", "generate", "Alternating squares.");
    checker.inputs = {integer(floatIn("count", "Squares across", 8, 1, 100))};
    checker.outputs = {out("mask", "Mask", SocketType::Float)};
    checker.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q count = c.in("count");
        return {{"mask", QStringLiteral(R"(float asp = %1;
    vec2 q = floor(uv * vec2(asp, 1.0) * %2 / asp);
    return mod(q.x + q.y, 2.0);)").arg(asp, count)}};
    };

    registerNodeDefs({noise, linear, radial, shape, stripes, checker});
}

}
