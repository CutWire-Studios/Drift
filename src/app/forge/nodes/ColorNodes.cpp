#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

void registerColorNodes()
{
    using namespace build;
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};

    NodeDef bc = node("brightness_contrast", "Brightness / Contrast", "color", "Make the picture lighter, darker, flatter or punchier.");
    bc.inputs = {imageIn(), floatIn("brightness", "Brightness", 0, -1, 1), floatIn("contrast", "Contrast", 1, 0, 3)};
    bc.outputs = imageOut;
    bc.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QString contrast = c.in("contrast");
        const QString brightness = c.in("brightness");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 rgb = (col.rgb - 0.5) * %2 + 0.5 + %3;
    return vec4(clamp(rgb, 0.0, 1.0), col.a);)").arg(image, contrast, brightness)}};
    };

    NodeDef saturation = node("saturation", "Saturation", "color", "0 is black and white, 1 is unchanged, above 1 is more colourful.");
    saturation.inputs = {imageIn(), floatIn("amount", "Saturation", 1, 0, 3)};
    saturation.outputs = imageOut;
    saturation.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const QString image = c.in("image");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 rgb = mix(vec3(luma(col)), col.rgb, %2);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);)").arg(image, amount)}};
    };

    NodeDef hue = node("hue_shift", "Hue shift", "color", "Rotate every colour around the colour wheel.");
    hue.inputs = {imageIn(), widget(floatIn("degrees", "Shift", 90, -180, 180), "angle")};
    hue.outputs = imageOut;
    hue.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hsv");
        const QString image = c.in("image");
        const QString degrees = c.in("degrees");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 hsv = rgb2hsv(col.rgb);
    hsv.x = fract(hsv.x + %2 / 360.0);
    return vec4(hsv2rgb(hsv), col.a);)").arg(image, degrees)}};
    };

    NodeDef levels = node("levels", "Levels", "color", "Set the black point, white point and mid-tone gamma.");
    levels.inputs = {imageIn(), floatIn("black", "Black point", 0, 0, 1), floatIn("white", "White point", 1, 0, 1),
                     floatIn("gamma", "Gamma", 1, 0.1, 4), floatIn("outBlack", "Output black", 0, 0, 1),
                     floatIn("outWhite", "Output white", 1, 0, 1)};
    levels.outputs = imageOut;
    levels.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QString black = c.in("black");
        const QString white = c.in("white");
        const QString gamma = c.in("gamma");
        const QString outBlack = c.in("outBlack");
        const QString outWhite = c.in("outWhite");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 rgb = clamp((col.rgb - %2) / max(%3 - %2, 0.0001), 0.0, 1.0);
    rgb = pow(rgb, vec3(1.0 / max(%4, 0.0001)));
    return vec4(mix(vec3(%5), vec3(%6), rgb), col.a);)").arg(image, black, white, gamma, outBlack, outWhite)}};
    };

    NodeDef temperature = node("temperature", "Temperature", "color", "Warm the picture towards orange or cool it towards blue.");
    temperature.inputs = {imageIn(), floatIn("amount", "Warmth", 0.3, -1, 1)};
    temperature.outputs = imageOut;
    temperature.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    float t = %2;
    vec3 rgb = col.rgb * vec3(1.0 + 0.25 * t, 1.0 + 0.05 * t, 1.0 - 0.25 * t);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);)").arg(image, amount)}};
    };

    NodeDef tint = node("tint", "Tint", "color", "Wash the picture with a colour.");
    tint.inputs = {imageIn(), swatch("color", "Color", rgba(1, 0.55, 0.2, 1)), floatIn("amount", "Amount", 0.4, 0, 1)};
    tint.outputs = imageOut;
    tint.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const QString image = c.in("image");
        const QString color = c.in("color");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 tinted = luma(col) * %2.rgb * 1.6;
    return vec4(mix(col.rgb, clamp(tinted, 0.0, 1.0), %3), col.a);)").arg(image, color, amount)}};
    };

    NodeDef duotone = node("gradient_map", "Duotone", "color", "Replace dark areas with one colour and bright areas with another.");
    duotone.inputs = {imageIn(), swatch("shadow", "Shadows", rgba(0.04, 0.09, 0.16, 1)),
                      swatch("highlight", "Highlights", rgba(1, 0.76, 0.03, 1)), floatIn("amount", "Amount", 1, 0, 1)};
    duotone.outputs = imageOut;
    duotone.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const QString image = c.in("image");
        const QString shadow = c.in("shadow");
        const QString highlight = c.in("highlight");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 duo = mix(%2.rgb, %3.rgb, smoothstep(0.0, 1.0, luma(col)));
    return vec4(mix(col.rgb, duo, %4), col.a);)").arg(image, shadow, highlight, amount)}};
    };

    NodeDef invert = node("invert", "Invert", "color", "Turn the picture into its negative.");
    invert.inputs = {imageIn(), floatIn("amount", "Amount", 1, 0, 1)};
    invert.outputs = imageOut;
    invert.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    return vec4(mix(col.rgb, 1.0 - col.rgb, %2), col.a);)").arg(image, amount)}};
    };

    NodeDef posterize = node("posterize", "Posterize", "color", "Reduce the number of colours for a flat, poster look.");
    posterize.inputs = {imageIn(), integer(floatIn("levels", "Levels", 5, 2, 32))};
    posterize.outputs = imageOut;
    posterize.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QString levels = c.in("levels");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    float n = max(floor(%2), 2.0) - 1.0;
    return vec4(floor(col.rgb * n + 0.5) / n, col.a);)").arg(image, levels)}};
    };

    NodeDef threshold = node("threshold", "Threshold", "color", "Pure black and white: everything brighter than the level turns white.");
    threshold.inputs = {imageIn(), floatIn("level", "Level", 0.5, 0, 1), floatIn("softness", "Softness", 0.02, 0, 0.5)};
    threshold.outputs = {out("image", "Image", SocketType::Color), out("mask", "Mask", SocketType::Float)};
    threshold.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const QString level = c.in("level");
        const QString softness = c.in("softness");
        const QString image = c.in("image");
        const QString m = QStringLiteral("smoothstep(%1 - %2, %1 + %2 + 0.0001, luma(%3))").arg(level, softness, image);
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    float m = smoothstep(%2 - %3, %2 + %3 + 0.0001, luma(col));
    return vec4(vec3(m), col.a);)").arg(image, level, softness)},
                {"mask", "return " + m + ";"}};
    };

    NodeDef lut = node("lut", "Color LUT", "color",
        "Apply a colour grade from a 512\xc3\x97" "512 LUT image (8\xc3\x97" "8 grid of 64\xc3\x97" "64 tiles).");
    lut.inputs = {imageIn(), floatIn("amount", "Amount", 1, 0, 1)};
    {
        OptionDef asset;
        asset.id = QStringLiteral("asset");
        asset.label = QStringLiteral("LUT image");
        asset.kind = OptionKind::Asset;
        lut.options = {asset};
    }
    lut.outputs = imageOut;
    lut.emitFn = [](EmitCtx &c, Stage) -> Out {
        const auto tile = [](const QString &slice) {
            return QStringLiteral("vec2(mod(%1, 8.0), floor(%1 / 8.0)) / 8.0 + (0.5 / 512.0) + rgb.rg * (63.0 / 512.0)").arg(slice);
        };
        const QString image = c.in("image");
        const QString a0 = c.asset(tile("s0"));
        const QString a1 = c.asset(tile("s1"));
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 rgb = clamp(col.rgb, 0.0, 1.0);
    float b = rgb.b * 63.0;
    float s0 = floor(b);
    float s1 = min(s0 + 1.0, 63.0);
    vec3 a0 = %2.rgb;
    vec3 a1 = %3.rgb;
    vec3 graded = mix(a0, a1, b - s0);
    return vec4(mix(col.rgb, graded, %4), col.a);)").arg(image, a0, a1, amount)}};
    };

    registerNodeDefs({bc, saturation, hue, levels, temperature, tint, duotone, invert, posterize, threshold, lut});
}

}
