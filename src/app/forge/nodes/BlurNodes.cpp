#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

using namespace build;
using Q = QString;

Q gaussian(EmitCtx &c, const std::function<Q(const Q &)> &read, const Q &dir, const Q &radius)
{
    const Q res = c.res();
    const Q tap = read("uv + texel * o");
    return QStringLiteral(R"(float r = max(%1, 0.0);
    float sigma = max(r * 0.5, 0.0001);
    float stepPx = max(r / 16.0, 1.0);
    vec2 texel = %2 / %3;
    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int k = -16; k <= 16; ++k) {
        float o = float(k) * stepPx;
        if (abs(o) > r + 0.001) continue;
        float w = exp(-0.5 * o * o / (sigma * sigma));
        sum += %4 * w;
        wsum += w;
    }
    return sum / wsum;)").arg(radius, dir, res, tap);
}

}

void registerBlurNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};

    NodeDef blur = node("blur", "Blur", "blur", "Soft, even gaussian blur.");
    blur.cheap = true;
    blur.inputs = {imageIn(), floatIn("radius", "Radius (px)", 8, 0, 64)};
    blur.outputs = imageOut;
    blur.heavyInputsStatic = {S("image")};
    blur.stages = {
        {[](EmitCtx &c, Stage) {
            const Q radius = c.in("radius");
            return gaussian(c, [&c](const Q &p) { return c.in("image", p); }, "vec2(1.0, 0.0)", radius);
        }},
        {[](EmitCtx &c, Stage prev) {
            const Q radius = c.in("radius");
            return gaussian(c, prev, "vec2(0.0, 1.0)", radius);
        }},
    };
    blur.emitFn = [](EmitCtx &, Stage stage) -> Out { return {{"image", "return " + stage("uv") + ";"}}; };

    NodeDef glow = node("glow", "Glow", "blur", "Bright parts of the picture bleed soft light around them.");
    glow.inputs = {imageIn(), floatIn("threshold", "Threshold", 0.6, 0, 1), floatIn("radius", "Radius (px)", 16, 1, 64),
                   floatIn("intensity", "Intensity", 1, 0, 4), swatch("tint", "Tint", rgba(1, 1, 1, 1))};
    glow.outputs = {out("image", "Image", SocketType::Color), out("glow", "Glow only", SocketType::Color)};
    glow.stages = {
        {[](EmitCtx &c, Stage) {
            const Q image = c.in("image");
            const Q t1 = c.in("threshold");
            const Q t2 = c.in("threshold");
            return QStringLiteral(R"(vec4 col = %1;
    return vec4(max(col.rgb - vec3(%2), 0.0) / max(1.0 - %3, 0.0001), 1.0);)").arg(image, t1, t2);
        }},
        {[](EmitCtx &c, Stage prev) {
            const Q radius = c.in("radius");
            return gaussian(c, prev, "vec2(1.0, 0.0)", radius);
        }},
        {[](EmitCtx &c, Stage prev) {
            const Q radius = c.in("radius");
            return gaussian(c, prev, "vec2(0.0, 1.0)", radius);
        }},
    };
    glow.emitFn = [](EmitCtx &c, Stage stage) -> Out {
        const Q image = c.in("image");
        const Q s1 = stage("uv");
        const Q tint1 = c.in("tint");
        const Q intensity1 = c.in("intensity");
        const Q s2 = stage("uv");
        const Q tint2 = c.in("tint");
        const Q intensity2 = c.in("intensity");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 g = %2.rgb * %3.rgb * %4;
    return vec4(clamp(col.rgb + g, 0.0, 1.0), col.a);)").arg(image, s1, tint1, intensity1)},
                {"glow", QStringLiteral("return vec4(clamp(%1.rgb * %2.rgb * %3, 0.0, 1.0), 1.0);").arg(s2, tint2, intensity2)}};
    };

    NodeDef directional = node("directional_blur", "Motion blur", "blur", "Streak the picture in one direction, like fast movement.");
    directional.inputs = {imageIn(), widget(floatIn("angle", "Angle", 0, 0, 360), "angle"), floatIn("length", "Length (px)", 30, 0, 300)};
    directional.outputs = imageOut;
    directional.heavyInputsStatic = {S("image")};
    directional.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q angle = c.in("angle");
        const Q length = c.in("length");
        const Q res = c.res();
        const Q image = c.in("image", "uv + dir * t");
        return {{"image", QStringLiteral(R"(float a = radians(%1);
    vec2 dir = vec2(cos(a), sin(a)) * %2 / %3;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float t = float(k) / 23.0 - 0.5;
        sum += %4;
    }
    return sum / 24.0;)").arg(angle, length, res, image)}};
    };

    NodeDef zoomBlur = node("zoom_blur", "Zoom blur", "blur", "Streaks rushing out from a point, like zooming the lens mid-shot.");
    zoomBlur.inputs = {imageIn(), floatIn("strength", "Strength", 0.15, 0, 1),
                       widget(vecIn("center", "Centre", 0.5, 0.5, 0, 1), "point")};
    zoomBlur.outputs = imageOut;
    zoomBlur.heavyInputsStatic = {S("image")};
    zoomBlur.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q center = c.in("center");
        const Q strength = c.in("strength");
        const Q image = c.in("image", "ctr + (uv - ctr) * s");
        return {{"image", QStringLiteral(R"(vec2 ctr = %1;
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float s = 1.0 - %2 * float(k) / 23.0;
        sum += %3;
    }
    return sum / 24.0;)").arg(center, strength, image)}};
    };

    NodeDef chromatic = node("chromatic", "RGB split", "blur", "Pull the red, green and blue channels apart, like a cheap lens or a glitch.");
    chromatic.inputs = {imageIn(), floatIn("amount", "Distance (px)", 8, 0, 80), widget(floatIn("angle", "Angle", 0, 0, 360), "angle"),
                        widget(vecIn("center", "Centre (from the centre style)", 0.5, 0.5, 0, 1), "point")};
    chromatic.options = {selectOpt("mode", "Style", {choice("linear", "Sideways"), choice("radial", "From the centre")}, "linear")};
    chromatic.outputs = imageOut;
    chromatic.emitFn = [](EmitCtx &c, Stage) -> Out {
        const bool radial = c.optString("mode") == QLatin1String("radial");
        Q head;
        if (radial) {
            const Q center = c.in("center");
            const Q amount = c.in("amount");
            const Q res = c.res();
            head = "vec2 delta = (uv - " + center + ") * 2.0 * " + amount + " / " + res + ";";
        } else {
            const Q angle = c.in("angle");
            const Q amount = c.in("amount");
            const Q res = c.res();
            head = "float a = radians(" + angle + ");\n    vec2 delta = vec2(cos(a), sin(a)) * " + amount + " / " + res + ";";
        }
        const Q g = c.in("image");
        const Q r = c.in("image", "clamp(uv + delta, 0.0, 1.0)");
        const Q b = c.in("image", "clamp(uv - delta, 0.0, 1.0)");
        return {{"image", head + "\n    vec4 g = " + g + ";\n    float r = " + r + ".r;\n    float b = " + b
                                + ".b;\n    return vec4(r, g.g, b, g.a);"}};
    };

    NodeDef vignette = node("vignette", "Vignette", "blur", "Darken (or colour) the corners to pull focus to the middle.");
    vignette.inputs = {imageIn(), floatIn("amount", "Amount", 0.6, 0, 1), floatIn("radius", "Size", 0.75, 0, 1.5),
                       floatIn("softness", "Softness", 0.45, 0.01, 1), swatch("color", "Color", rgba(0, 0, 0, 1))};
    vignette.outputs = imageOut;
    vignette.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q image = c.in("image");
        const Q asp = c.aspect();
        const Q radius1 = c.in("radius");
        const Q softness = c.in("softness");
        const Q radius2 = c.in("radius");
        const Q color = c.in("color");
        const Q amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec2 d = (uv - 0.5) * vec2(%2, 1.0);
    float v = smoothstep(%3 - %4, %5, length(d) * 1.2);
    return vec4(mix(col.rgb, %6.rgb, v * %7), col.a);)").arg(image, asp, radius1, softness, radius2, color, amount)}};
    };

    NodeDef sharpen = node("sharpen", "Sharpen", "blur", "Crisp up edges and fine detail.");
    sharpen.inputs = {imageIn(), floatIn("amount", "Amount", 0.8, 0, 4)};
    sharpen.outputs = imageOut;
    sharpen.heavyInputsStatic = {S("image")};
    sharpen.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q res = c.res();
        const Q col = c.in("image");
        const Q n1 = c.in("image", "uv + vec2(px.x, 0.0)");
        const Q n2 = c.in("image", "uv - vec2(px.x, 0.0)");
        const Q n3 = c.in("image", "uv + vec2(0.0, px.y)");
        const Q n4 = c.in("image", "uv - vec2(0.0, px.y)");
        const Q amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec2 px = 1.0 / %1;
    vec4 col = %2;
    vec4 n = %3 + %4
           + %5 + %6;
    vec3 rgb = col.rgb + (col.rgb * 4.0 - n.rgb) * %7;
    return vec4(clamp(rgb, 0.0, 1.0), col.a);)").arg(res, col, n1, n2, n3, n4, amount)}};
    };

    NodeDef edges = node("edges", "Edge detect", "blur", "Find the outlines in the picture.");
    edges.inputs = {imageIn(), floatIn("strength", "Strength", 2, 0, 10)};
    edges.outputs = {out("image", "Edges", SocketType::Color), out("mask", "Mask", SocketType::Float)};
    edges.heavyInputsStatic = {S("image")};
    edges.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const auto s = [&c](int dx, int dy) {
            return "luma(" + c.in("image", QStringLiteral("uv + vec2(%1.0, %2.0) * px").arg(dx).arg(dy)) + ")";
        };
        const Q res = c.res();
        const Q a1 = s(-1, -1);
        const Q a2 = s(-1, 0);
        const Q a3 = s(-1, 1);
        const Q a4 = s(1, -1);
        const Q a5 = s(1, 0);
        const Q a6 = s(1, 1);
        const Q b1 = s(-1, -1);
        const Q b2 = s(0, -1);
        const Q b3 = s(1, -1);
        const Q b4 = s(-1, 1);
        const Q b5 = s(0, 1);
        const Q b6 = s(1, 1);
        const Q strength = c.in("strength");
        const Q body = QStringLiteral(R"(vec2 px = 1.0 / %1;
    float gx = -%2 - 2.0 * %3 - %4 + %5 + 2.0 * %6 + %7;
    float gy = -%8 - 2.0 * %9 - %10 + %11 + 2.0 * %12 + %13;
    float e = clamp(length(vec2(gx, gy)) * %14, 0.0, 1.0);)")
                           .arg(res, a1, a2, a3, a4, a5, a6)
                           .arg(b1, b2, b3, b4, b5, b6)
                           .arg(strength);
        const Q alpha = c.in("image");
        return {{"image", body + "\n    return vec4(vec3(e), " + alpha + ".a);"}, {"mask", body + "\n    return e;"}};
    };

    registerNodeDefs({blur, glow, directional, zoomBlur, chromatic, vignette, sharpen, edges});
}

}
