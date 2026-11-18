#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

using namespace build;
using Q = QString;

}

void registerStylizeNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};
    const auto seedIn = [] { return widget(floatIn("seed", "Seed", 0, 0, 1000), "seed"); };

    NodeDef grain = node("grain", "Film grain", "stylize", "Fine moving noise, like film stock.");
    grain.inputs = {imageIn(), floatIn("amount", "Amount", 0.12, 0, 1), floatIn("size", "Grain size (px)", 1.5, 1, 8),
                    clockIn("t", "Time"), seedIn()};
    grain.outputs = imageOut;
    grain.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hash21");
        const Q image = c.in("image");
        const Q res = c.res();
        const Q size = c.in("size");
        const Q t = c.in("t");
        const Q seed = c.in("seed");
        const Q amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec2 cell = floor(uv * %2 / max(%3, 1.0));
    float n = hash21(cell + floor(%4 * 24.0) * 17.31 + %5 * 3.17) - 0.5;
    return vec4(clamp(col.rgb + n * %6, 0.0, 1.0), col.a);)").arg(image, res, size, t, seed, amount)}};
    };

    NodeDef scanlines = node("scanlines", "Scanlines", "stylize", "Horizontal lines like an old TV or monitor.");
    scanlines.inputs = {imageIn(), floatIn("count", "Lines", 240, 10, 1000), floatIn("amount", "Darkness", 0.35, 0, 1),
                        floatIn("speed", "Roll speed", 0, -5, 5), clockIn("t", "Time")};
    scanlines.outputs = imageOut;
    scanlines.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q image = c.in("image");
        const Q t = c.in("t");
        const Q speed = c.in("speed");
        const Q count = c.in("count");
        const Q amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    float l = 0.5 + 0.5 * sin((uv.y + %2 * %3 * 0.05) * %4 * 3.14159);
    return vec4(col.rgb * (1.0 - %5 * (1.0 - l)), col.a);)").arg(image, t, speed, count, amount)}};
    };

    NodeDef halftone = node("halftone", "Halftone", "stylize", "Comic-book printed dots.");
    halftone.inputs = {imageIn(), floatIn("size", "Dot spacing (px)", 10, 3, 60), widget(floatIn("angle", "Angle", 45, 0, 90), "angle"),
                       swatch("ink", "Ink", rgba(0.08, 0.08, 0.1, 1)), swatch("paper", "Paper", rgba(0.98, 0.96, 0.9, 1))};
    halftone.options = {toggleOpt("colored", "Keep colours", false)};
    halftone.outputs = imageOut;
    halftone.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("rot2");
        c.helper("luma");
        const Q res1 = c.res();
        const Q angle = c.in("angle");
        const Q size1 = c.in("size");
        const Q size2 = c.in("size");
        const Q res2 = c.res();
        const Q image1 = c.in("image", "clamp(sampleUv, 0.0, 1.0)");
        const Q ink = c.optBool("colored") ? Q("src.rgb") : c.in("ink") + ".rgb";
        const Q paper = c.in("paper");
        const Q image2 = c.in("image");
        return {{"image", QStringLiteral(R"(vec2 px = uv * %1;
    mat2 m = rot2(radians(%2));
    vec2 q = m * px / max(%3, 1.0);
    vec2 cellCenter = (floor(q) + 0.5);
    vec2 sampleUv = (transpose(m) * (cellCenter * max(%4, 1.0))) / %5;
    vec4 src = %6;
    float r = sqrt(1.0 - luma(src)) * 0.75;
    float d = length(q - cellCenter);
    float dotMask = 1.0 - smoothstep(r - 0.08, r + 0.08, d);
    vec3 ink = %7;
    return vec4(mix(%8.rgb, ink, dotMask), %9.a);)").arg(res1, angle, size1, size2, res2, image1, ink, paper, image2)}};
    };

    NodeDef glitch = node("glitch_blocks", "Glitch blocks", "stylize",
        "Random slices of the frame jump sideways and split colour, like a broken signal.");
    glitch.inputs = {imageIn(), floatIn("amount", "Strength", 0.5, 0, 1), floatIn("blocks", "Slices", 24, 2, 120),
                     floatIn("rate", "Changes per second", 12, 0, 60), clockIn("t", "Time"), seedIn()};
    glitch.outputs = imageOut;
    glitch.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hash21");
        const Q t = c.in("t");
        const Q rate = c.in("rate");
        const Q seed = c.in("seed");
        const Q blocks = c.in("blocks");
        const Q amount1 = c.in("amount");
        const Q amount2 = c.in("amount");
        const Q g = c.in("image", "p");
        const Q r = c.in("image", "vec2(fract(p.x + shift * 0.3), p.y)");
        const Q b = c.in("image", "vec2(fract(p.x - shift * 0.3), p.y)");
        return {{"image", QStringLiteral(R"(float tick = floor(%1 * %2) + %3 * 31.7;
    float row = floor(uv.y * %4);
    float h = hash21(vec2(row, tick));
    float on = step(1.0 - %5 * 0.6, h);
    float shift = (hash21(vec2(tick, row * 1.7)) - 0.5) * 0.25 * %6 * on;
    vec2 p = vec2(fract(uv.x + shift), uv.y);
    vec4 g = %7;
    float r = %8.r;
    float b = %9.b;
    return vec4(r, g.g, b, g.a);)").arg(t, rate, seed, blocks, amount1, amount2, g, r, b)}};
    };

    NodeDef vhs = node("vhs", "VHS", "stylize", "Tape wobble, colour bleed and noise bands.");
    vhs.inputs = {imageIn(), floatIn("amount", "Strength", 0.6, 0, 1), clockIn("t", "Time")};
    vhs.outputs = imageOut;
    vhs.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("valueNoise");
        c.helper("hash21");
        const Q amount = c.in("amount");
        const Q t = c.in("t");
        const Q g = c.in("image", "clamp(p, 0.0, 1.0)");
        const Q r = c.in("image", "clamp(p + vec2(0.004 * a, 0.0), 0.0, 1.0)");
        const Q b = c.in("image", "clamp(p - vec2(0.004 * a, 0.0), 0.0, 1.0)");
        const Q res = c.res();
        return {{"image", QStringLiteral(R"(float a = %1;
    float t = %2;
    float wob = (valueNoise(vec2(uv.y * 8.0, t * 4.0)) - 0.5) * 0.01 * a;
    float band = smoothstep(0.96, 1.0, sin(uv.y * 3.0 - t * 1.7) * 0.5 + 0.5);
    vec2 p = vec2(uv.x + wob + band * 0.02 * a, uv.y);
    vec4 g = %3;
    float r = %4.r;
    float b = %5.b;
    vec3 col = vec3(r, g.g, b);
    float n = hash21(floor(uv * %6 * 0.5) + floor(t * 30.0)) - 0.5;
    col += n * 0.08 * a + band * 0.15 * a;
    return vec4(clamp(col, 0.0, 1.0), g.a);)").arg(amount, t, g, r, b, res)}};
    };

    registerNodeDefs({grain, scanlines, halftone, glitch, vhs});
}

}
