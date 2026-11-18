#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

using namespace build;
using Q = QString;

InputDef progress()
{
    return clockIn("p", "Progress");
}

InputDef softness(double def = 0.05, double max = 0.5)
{
    return floatIn("softness", "Softness", def, 0, max);
}

Q sweep(const Q &x, const Q &p, const Q &soft)
{
    return QStringLiteral(R"(float s = max(%1, 0.0001);
    float front = mix(-s, 1.0 + s, %2);
    return 1.0 - smoothstep(front - s, front + s, %3);)").arg(soft, p, x);
}

InputDef point(const char *id, const char *label, double x, double y)
{
    return widget(vecIn(id, label, x, y, 0, 1), "point");
}

}

void registerTransitionNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> maskOut = {out("mask", "Mask", SocketType::Float)};
    const QList<Kind> transAnim = {Kind::Transition, Kind::Animation};

    NodeDef wipe = node("wipe_mask", "Wipe mask", "transition",
        "A straight edge sweeping across the frame. Plug into Mix \xE2\x86\x92 Amount between From and To.");
    wipe.kinds = transAnim;
    wipe.inputs = {widget(floatIn("angle", "Direction", 0, 0, 360), "angle"), softness(), progress()};
    wipe.outputs = maskOut;
    wipe.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q angle = c.in("angle");
        const Q p = c.in("p");
        const Q soft = c.in("softness");
        return {{"mask", QStringLiteral(R"(float ang = radians(%1);
    vec2 d = vec2(cos(ang), sin(ang));
    float x = dot(uv - 0.5, d) / max(abs(d.x) + abs(d.y), 0.0001) + 0.5;
    %2)").arg(angle, sweep("x", p, soft))}};
    };

    NodeDef iris = node("iris_mask", "Iris mask", "transition", "A circle growing from a point until it fills the frame.");
    iris.kinds = transAnim;
    iris.inputs = {point("center", "Centre", 0.5, 0.5), softness(), progress()};
    iris.outputs = maskOut;
    iris.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q p = c.in("p");
        const Q soft = c.in("softness");
        return {{"mask", QStringLiteral(R"(float asp = %1;
    vec2 ctr = %2;
    vec2 far = max(ctr, 1.0 - ctr) * vec2(asp, 1.0);
    float x = length((uv - ctr) * vec2(asp, 1.0)) / max(length(far), 0.0001);
    %3)").arg(asp, center, sweep("x", p, soft))}};
    };

    NodeDef dissolve = node("dissolve_mask", "Noise dissolve mask", "transition",
        "The new clip eats through the old one in organic, random patches.");
    dissolve.kinds = transAnim;
    dissolve.inputs = {floatIn("scale", "Patch size", 5, 0.5, 40), softness(), progress(),
                       widget(floatIn("seed", "Seed", 0, 0, 1000), "seed")};
    dissolve.outputs = maskOut;
    dissolve.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("fbm");
        const Q asp = c.aspect();
        const Q scale = c.in("scale");
        const Q seed = c.in("seed");
        const Q p = c.in("p");
        const Q soft = c.in("softness");
        return {{"mask", QStringLiteral(R"(float x = clamp((fbm(uv * vec2(%1, 1.0) * %2 + %3 * 7.31) - 0.2) / 0.6, 0.0, 1.0);
    %4)").arg(asp, scale, seed, sweep("x", p, soft))}};
    };

    NodeDef blinds = node("blinds_mask", "Blinds mask", "transition", "Many thin strips each opening at once, like venetian blinds.");
    blinds.kinds = transAnim;
    blinds.inputs = {integer(floatIn("count", "Strips", 10, 1, 60)), widget(floatIn("angle", "Direction", 0, 0, 360), "angle"),
                     softness(), progress()};
    blinds.outputs = maskOut;
    blinds.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q angle = c.in("angle");
        const Q count = c.in("count");
        const Q p = c.in("p");
        const Q soft = c.in("softness");
        return {{"mask", QStringLiteral(R"(float ang = radians(%1);
    float x = fract(dot(uv, vec2(cos(ang), sin(ang))) * %2);
    %3)").arg(angle, count, sweep("x", p, soft))}};
    };

    NodeDef glow = node("edge_glow", "Edge glow", "transition",
        "A glowing line along the soft edge of a mask. Blend it on top with Add or Screen.");
    glow.inputs = {floatIn("mask", "Mask", 0, 0, 1), swatch("color", "Color", rgba(1, 0.6, 0.1, 1)),
                   floatIn("intensity", "Intensity", 1.5, 0, 5), floatIn("sharpness", "Sharpness", 2, 0.5, 10)};
    glow.outputs = {out("image", "Glow", SocketType::Color)};
    glow.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q mask = c.in("mask");
        const Q sharp = c.in("sharpness");
        const Q color = c.in("color");
        const Q intensity = c.in("intensity");
        return {{"image", QStringLiteral(R"(float m = clamp(%1, 0.0, 1.0);
    float band = pow(1.0 - abs(m * 2.0 - 1.0), %2);
    return vec4(%3.rgb * band * %4, band);)").arg(mask, sharp, color, intensity)}};
    };

    NodeDef push = node("push", "Push / Slide", "transition",
        "The new clip slides in and shoves the old one out (or slides over it).");
    push.kinds = {Kind::Transition};
    push.inputs = {colorIn("from", "From", rgba(0, 0, 0, 1)), colorIn("to", "To", rgba(1, 1, 1, 1)), progress()};
    push.options = {selectOpt("direction", "Direction",
                              {choice("left", "Towards the left"), choice("right", "Towards the right"),
                               choice("up", "Upwards"), choice("down", "Downwards")}, "left"),
                    toggleOpt("cover", "Slide over instead of pushing", false)};
    push.outputs = {out("image", "Image", SocketType::Color)};
    push.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q dir = c.optString("direction");
        const Q d = dir == QLatin1String("right") ? Q("vec2(1.0, 0.0)")
            : dir == QLatin1String("up")          ? Q("vec2(0.0, -1.0)")
            : dir == QLatin1String("down")        ? Q("vec2(0.0, 1.0)")
                                                  : Q("vec2(-1.0, 0.0)");
        const bool cover = c.optBool("cover");
        const Q p = c.in("p");
        const Q to = c.in("to", "pt");
        const Q from = c.in("from", "clamp(pf, 0.0, 1.0)");
        return {{"image", QStringLiteral(R"(vec2 d = %1;
    float p = clamp(%2, 0.0, 1.0);
    vec2 pt = uv - d * (p - 1.0);
    if (pt.x >= 0.0 && pt.x <= 1.0 && pt.y >= 0.0 && pt.y <= 1.0) return %3;
    vec2 pf = %4;
    return %5;)").arg(d, p, to, cover ? Q("uv") : Q("uv - d * p"), from)}};
    };

    registerNodeDefs({wipe, iris, dissolve, blinds, glow, push});
}

void registerKitTransitionNodes()
{
    using Stage = const StageReader &;

    NodeDef reveal = node("reveal", "Reveal by pattern", "transition",
        "Turns any pattern into a transition: dark parts of the pattern switch first, bright parts last. Plug in a gradient, noise, shape, brightness or grid tiles.");
    reveal.inputs = {floatIn("pattern", "Pattern", 0.5, 0, 1), softness(), progress()};
    reveal.options = {toggleOpt("invert", "Bright parts first", false)};
    reveal.outputs = {out("mask", "Mask", SocketType::Float)};
    reveal.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q pattern = c.in("pattern");
        const bool invert = c.optBool("invert");
        const Q p = c.in("p");
        const Q soft = c.in("softness");
        return {{"mask", QStringLiteral(R"(float x = clamp(%1, 0.0, 1.0);
    %2
    %3)").arg(pattern, invert ? Q("x = 1.0 - x;") : Q(), sweep("x", p, soft))}};
    };

    NodeDef clock = node("clock_wipe", "Clock wipe", "transition", "A hand sweeps around like a clock, revealing the new clip.");
    clock.kinds = {Kind::Transition, Kind::Animation};
    clock.inputs = {point("center", "Centre", 0.5, 0.5), widget(floatIn("start", "Start angle", 0, 0, 360), "angle"),
                    softness(0.01, 0.2), progress()};
    clock.options = {toggleOpt("ccw", "Anticlockwise", false)};
    clock.outputs = {out("mask", "Mask", SocketType::Float)};
    clock.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q start = c.in("start");
        const bool ccw = c.optBool("ccw");
        const Q p = c.in("p");
        const Q soft = c.in("softness");
        return {{"mask", QStringLiteral(R"(float asp = %1;
    vec2 d = (uv - %2) * vec2(asp, 1.0);
    float a = fract((atan(d.x, -d.y) - radians(%3)) / 6.2831853);
    %4
    %5)").arg(asp, center, start, ccw ? Q("a = 1.0 - a;") : Q(), sweep("a", p, soft))}};
    };

    NodeDef dip = node("dip", "Dip to colour", "transition", "Fade out to a colour (black, white, anything), then fade in the new clip.");
    dip.kinds = {Kind::Transition};
    dip.inputs = {colorIn("from", "From", rgba(0, 0, 0, 1)), colorIn("to", "To", rgba(1, 1, 1, 1)),
                  swatch("color", "Colour", rgba(0, 0, 0, 1)), floatIn("hold", "Hold", 0.1, 0, 0.8), progress()};
    dip.outputs = {out("image", "Image", SocketType::Color)};
    dip.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q p = c.in("p");
        const Q hold = c.in("hold");
        const Q color = c.in("color");
        const Q from = c.in("from");
        const Q to = c.in("to");
        return {{"image", QStringLiteral(R"(float p = clamp(%1, 0.0, 1.0);
    float h = %2 * 0.5;
    vec4 dip = vec4(%3.rgb, 1.0);
    if (p < 0.5 - h) return mix(%4, dip, smoothstep(0.0, 0.5 - h, p));
    if (p > 0.5 + h) return mix(dip, %5, smoothstep(0.5 + h, 1.0, p));
    return dip;)").arg(p, hold, color, from, to)}};
    };

    registerNodeDefs({reveal, clock, dip});
}

}
