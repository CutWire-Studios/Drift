#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

QString blendExpr(const QString &mode)
{
    if (mode == QLatin1String("add"))
        return QStringLiteral("min(b.rgb + l.rgb, 1.0)");
    if (mode == QLatin1String("screen"))
        return QStringLiteral("1.0 - (1.0 - b.rgb) * (1.0 - l.rgb)");
    if (mode == QLatin1String("multiply"))
        return QStringLiteral("b.rgb * l.rgb");
    if (mode == QLatin1String("overlay"))
        return QStringLiteral("blendOverlay(b.rgb, l.rgb)");
    if (mode == QLatin1String("softlight"))
        return QStringLiteral("blendSoftLight(b.rgb, l.rgb)");
    if (mode == QLatin1String("difference"))
        return QStringLiteral("abs(b.rgb - l.rgb)");
    if (mode == QLatin1String("lighten"))
        return QStringLiteral("max(b.rgb, l.rgb)");
    if (mode == QLatin1String("darken"))
        return QStringLiteral("min(b.rgb, l.rgb)");
    return QStringLiteral("l.rgb");
}

}

void registerMixNodes()
{
    using namespace build;
    using Stage = const StageReader &;

    NodeDef mix = node("mix", "Mix", "mix", "Blend between A and B. Plug a mask into Amount to choose per pixel.");
    mix.inputs = {colorIn("a", "A", rgba(0, 0, 0, 1)), colorIn("b", "B", rgba(1, 1, 1, 1)), floatIn("amount", "Amount", 0.5, 0, 1)};
    mix.outputs = {out("image", "Image", SocketType::Color)};
    mix.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString a = c.in("a");
        const QString b = c.in("b");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral("return mix(%1, %2, clamp(%3, 0.0, 1.0));").arg(a, b, amount)}};
    };

    NodeDef blend = node("blend", "Blend", "mix", "Lay one image on top of another with a blend mode, like layers in a photo editor.");
    blend.inputs = {colorIn("base", "Base", rgba(0, 0, 0, 1)), colorIn("layer", "Layer", rgba(1, 1, 1, 1)),
                    floatIn("opacity", "Opacity", 1, 0, 1)};
    blend.options = {selectOpt("mode", "Mode",
                               {choice("normal", "Normal"), choice("add", "Add (lighten)"), choice("screen", "Screen"),
                                choice("multiply", "Multiply"), choice("overlay", "Overlay"), choice("softlight", "Soft light"),
                                choice("difference", "Difference"), choice("lighten", "Lighten"), choice("darken", "Darken")},
                               "screen")};
    blend.outputs = {out("image", "Image", SocketType::Color)};
    blend.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString mode = c.optString("mode");
        if (mode == QLatin1String("overlay") || mode == QLatin1String("softlight"))
            c.helper("blend");
        const QString base = c.in("base");
        const QString layer = c.in("layer");
        const QString opacity = c.in("opacity");
        return {{"image", QStringLiteral(R"(vec4 b = %1;
    vec4 l = %2;
    vec3 rgb = %3;
    return vec4(mix(b.rgb, rgb, clamp(%4 * l.a, 0.0, 1.0)), b.a);)").arg(base, layer, blendExpr(mode), opacity)}};
    };

    NodeDef over = node("over", "Layer over", "mix", "Put Top over Bottom, respecting transparency.");
    over.inputs = {colorIn("top", "Top", rgba(0, 0, 0, 0)), colorIn("bottom", "Bottom", rgba(0, 0, 0, 1))};
    over.outputs = {out("image", "Image", SocketType::Color)};
    over.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("over");
        const QString top = c.in("top");
        const QString bottom = c.in("bottom");
        return {{"image", QStringLiteral("return over(%1, %2);").arg(top, bottom)}};
    };

    NodeDef lumaMask = node("luma_mask", "Brightness mask", "mix", "A mask from how bright each pixel is: dark is 0, bright is 1.");
    lumaMask.inputs = {imageIn(), floatIn("low", "Low", 0, 0, 1), floatIn("high", "High", 1, 0, 1)};
    lumaMask.outputs = {out("mask", "Mask", SocketType::Float)};
    lumaMask.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const QString low = c.in("low");
        const QString high = c.in("high");
        const QString image = c.in("image");
        return {{"mask", QStringLiteral("return smoothstep(%1, max(%2, %1 + 0.0001), luma(%3));").arg(low, high, image)}};
    };

    NodeDef setAlpha = node("set_alpha", "Cut out with mask", "mix", "Make parts of the image transparent where the mask is 0.");
    setAlpha.inputs = {imageIn(), floatIn("mask", "Mask", 1, 0, 1)};
    setAlpha.outputs = {out("image", "Image", SocketType::Color)};
    setAlpha.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QString mask = c.in("mask");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    return vec4(col.rgb, col.a * clamp(%2, 0.0, 1.0));)").arg(image, mask)}};
    };

    NodeDef maskOps = node("mask_ops", "Combine masks", "mix", "Add, subtract, intersect or invert masks.");
    maskOps.inputs = {floatIn("a", "A", 0, 0, 1), floatIn("b", "B", 0, 0, 1)};
    maskOps.options = {selectOpt("op", "Operation",
                                 {choice("union", "A or B"), choice("intersect", "A and B"), choice("subtract", "A minus B"),
                                  choice("xor", "A or B, not both"), choice("invert", "Invert A")},
                                 "union")};
    maskOps.outputs = {out("mask", "Mask", SocketType::Float)};
    maskOps.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString a = "clamp(" + c.in("a") + ", 0.0, 1.0)";
        const QString b = "clamp(" + c.in("b") + ", 0.0, 1.0)";
        const QMap<QString, QString> e = {{"union", "max(" + a + ", " + b + ")"},
                                          {"intersect", "min(" + a + ", " + b + ")"},
                                          {"subtract", "clamp(" + a + " - " + b + ", 0.0, 1.0)"},
                                          {"xor", "abs(" + a + " - " + b + ")"},
                                          {"invert", "1.0 - " + a}};
        return {{"mask", "return " + e.value(c.optString("op"), e["union"]) + ";"}};
    };

    NodeDef switchImage = node("switch_image", "Pick image", "mix",
        "Show A when the switch is off and B when it's on. Pair with an exposed toggle.");
    switchImage.inputs = {colorIn("a", "Off", rgba(0, 0, 0, 1)), colorIn("b", "On", rgba(1, 1, 1, 1)),
                          widget(floatIn("on", "Switch", 0, 0, 1), "toggle")};
    switchImage.outputs = {out("image", "Image", SocketType::Color)};
    switchImage.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString on = c.in("on");
        const QString b = c.in("b");
        const QString a = c.in("a");
        return {{"image", QStringLiteral("return %1 > 0.5 ? %2 : %3;").arg(on, b, a)}};
    };

    registerNodeDefs({mix, blend, over, lumaMask, setAlpha, maskOps, switchImage});
}

}
