#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

using namespace build;
using Q = QString;

InputDef point(const char *id, const char *label, double x, double y)
{
    return widget(vecIn(id, label, x, y, 0, 1), "point");
}

}

void registerKitDistortNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};

    NodeDef cornerPin = node("corner_pin", "Corner pin", "distort",
        "Drag the four corners anywhere to fit the picture onto a screen, sign or poster.");
    cornerPin.inputs = {imageIn(), point("tl", "Top-left", 0.1, 0.1), point("tr", "Top-right", 0.9, 0.05),
                        point("br", "Bottom-right", 0.95, 0.9), point("bl", "Bottom-left", 0.05, 0.95)};
    cornerPin.outputs = imageOut;
    cornerPin.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("invBilinear");
        const Q tl = c.in("tl");
        const Q tr = c.in("tr");
        const Q br = c.in("br");
        const Q bl = c.in("bl");
        const Q image = c.in("image", "q");
        return {{"image", QStringLiteral(R"(vec2 q = invBilinear(uv, %1, %2, %3, %4);
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return vec4(0.0);
    return %5;)").arg(tl, tr, br, bl, image)}};
    };

    NodeDef polar = node("polar", "Polar coordinates", "distort",
        "Bend the picture into a circle (tiny planet), or unroll a circle into a strip. Great for tunnels.");
    polar.inputs = {imageIn(), point("center", "Centre", 0.5, 0.5),
                    widget(floatIn("rotation", "Rotation", 0, -360, 360), "angle"), floatIn("zoom", "Zoom", 1, 0.1, 4),
                    integer(floatIn("repeat", "Repeat", 1, 1, 12))};
    polar.options = {selectOpt("mode", "Direction",
                               {choice("wrap", "Wrap into a circle"), choice("unwrap", "Unroll a circle")}, "wrap")};
    polar.outputs = imageOut;
    polar.emitFn = [](EmitCtx &c, Stage) -> Out {
        const bool wrap = c.optString("mode") != QLatin1String("unwrap");
        if (wrap) {
            const Q asp = c.aspect();
            const Q center = c.in("center");
            const Q zoom = c.in("zoom");
            const Q rotation = c.in("rotation");
            const Q repeat = c.in("repeat");
            const Q image = c.in("image", "vec2(fract(a * max(floor(" + repeat + "), 1.0)), clamp(1.0 - r, 0.0, 1.0))");
            return {{"image", QStringLiteral(R"(float asp = %1;
    vec2 d = (uv - %2) * vec2(asp, 1.0);
    float r = length(d) * 2.0 / max(%3, 0.0001);
    float a = atan(d.y, d.x) / 6.2831853 + 0.5 + %4 / 360.0;
    return %5;)").arg(asp, center, zoom, rotation, image)}};
        }
        const Q asp = c.aspect();
        const Q repeat = c.in("repeat");
        const Q rotation = c.in("rotation");
        const Q zoom = c.in("zoom");
        const Q center = c.in("center");
        const Q image = c.in("image", "clamp(p, 0.0, 1.0)");
        return {{"image", QStringLiteral(R"(float asp = %1;
    float a = (uv.x * max(floor(%2), 1.0) - 0.5 - %3 / 360.0) * 6.2831853;
    float r = (1.0 - uv.y) * %4 * 0.5;
    vec2 p = %5 + vec2(cos(a), sin(a)) * r / vec2(asp, 1.0);
    return %6;)").arg(asp, repeat, rotation, zoom, center, image)}};
    };

    NodeDef hex = node("hex_pixelate", "Hex mosaic", "distort", "Turn the picture into honeycomb tiles.");
    hex.inputs = {imageIn(), unit(floatIn("size", "Tile size (px)", 24, 3, 200), "px")};
    hex.outputs = imageOut;
    hex.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q size = c.in("size");
        const Q res = c.res();
        const Q image = c.in("image", "clamp(q, 0.0, 1.0)");
        return {{"image", QStringLiteral(R"(float asp = %1;
    float cs = max(%2, 1.0) / %3.y;
    vec2 p = uv * vec2(asp, 1.0) / cs;
    vec2 s = vec2(1.0, 1.7320508);
    vec2 a = mod(p, s) - s * 0.5;
    vec2 b = mod(p - s * 0.5, s) - s * 0.5;
    vec2 g = dot(a, a) < dot(b, b) ? a : b;
    vec2 q = (p - g) * cs / vec2(asp, 1.0);
    return %4;)").arg(asp, size, res, image)}};
    };

    registerNodeDefs({cornerPin, polar, hex});
}

}
