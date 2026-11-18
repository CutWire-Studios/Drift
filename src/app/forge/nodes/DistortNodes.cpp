#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

using namespace build;
using Q = QString;

InputDef center()
{
    return widget(vecIn("center", "Centre", 0.5, 0.5, 0, 1), "point");
}

OptionDef edgeOpt()
{
    return selectOpt("edge", "Outside the frame",
                     {choice("clamp", "Stretch edges"), choice("transparent", "Transparent"), choice("repeat", "Repeat"),
                      choice("mirror", "Mirror")},
                     "clamp");
}

Q sampleAt(EmitCtx &c, const Q &p = QStringLiteral("p"))
{
    const Q edge = c.optString("edge");
    if (edge == QLatin1String("transparent")) {
        const Q image = c.in("image", p);
        return QStringLiteral("if (%1.x < 0.0 || %1.x > 1.0 || %1.y < 0.0 || %1.y > 1.0) return vec4(0.0);\n    return %2;")
            .arg(p, image);
    }
    if (edge == QLatin1String("repeat")) {
        const Q image = c.in("image", QStringLiteral("fract(%1)").arg(p));
        return QStringLiteral("return %1;").arg(image);
    }
    if (edge == QLatin1String("mirror")) {
        const Q image = c.in("image", QStringLiteral("1.0 - abs(mod(%1, 2.0) - 1.0)").arg(p));
        return QStringLiteral("return %1;").arg(image);
    }
    const Q image = c.in("image", QStringLiteral("clamp(%1, 0.0, 1.0)").arg(p));
    return QStringLiteral("return %1;").arg(image);
}

}

void registerDistortNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};

    NodeDef transform = node("transform", "Move / Scale / Rotate", "distort", "Move, resize and spin the picture.");
    transform.inputs = {imageIn(), vecIn("offset", "Move", 0, 0, -1, 1), floatIn("scale", "Scale", 1, 0.05, 5),
                        widget(floatIn("rotation", "Rotation", 0, -360, 360), "angle"),
                        [] { InputDef i = center(); i.id = S("pivot"); i.label = S("Pivot"); return i; }()};
    transform.options = {edgeOpt()};
    transform.outputs = imageOut;
    transform.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("rot2");
        const Q asp = c.aspect();
        const Q offset = c.in("offset");
        const Q pivot = c.in("pivot");
        const Q rotation = c.in("rotation");
        const Q scale = c.in("scale");
        const Q pivot2 = c.in("pivot");
        const Q body = QStringLiteral(R"(float asp = %1;
    vec2 p = uv - %2 - %3;
    p.x *= asp;
    p = rot2(-radians(%4)) * p;
    p /= max(%5, 0.0001);
    p.x /= asp;
    p += %6;
    )").arg(asp, offset, pivot, rotation, scale, pivot2);
        return {{"image", body + sampleAt(c)}};
    };

    NodeDef zoom = node("zoom", "Zoom", "distort", "Zoom in or out around a point.");
    zoom.inputs = {imageIn(), floatIn("amount", "Zoom", 1.5, 0.1, 8), center()};
    zoom.options = {edgeOpt()};
    zoom.outputs = imageOut;
    zoom.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q center = c.in("center");
        const Q amount = c.in("amount");
        const Q center2 = c.in("center");
        const Q body = QStringLiteral("vec2 p = (uv - %1) / max(%2, 0.0001) + %3;\n    ").arg(center, amount, center2);
        return {{"image", body + sampleAt(c)}};
    };

    NodeDef twirl = node("twirl", "Twirl", "distort", "Swirl the picture around a point like water down a drain.");
    twirl.inputs = {imageIn(), floatIn("angle", "Twist", 2, -6.28, 6.28), floatIn("radius", "Radius", 0.45, 0, 1.5), center()};
    twirl.options = {edgeOpt()};
    twirl.outputs = imageOut;
    twirl.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q radius = c.in("radius");
        const Q angle = c.in("angle");
        const Q body = QStringLiteral(R"(float asp = %1;
    vec2 ctr = %2;
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    float r = length(d);
    float fall = 1.0 - smoothstep(0.0, max(%3, 0.0001), r);
    float a = atan(d.y, d.x) + %4 * fall * fall;
    vec2 nd = r > 1e-6 ? vec2(cos(a), sin(a)) * r : d;
    vec2 p = ctr + vec2(nd.x / asp, nd.y);
    )").arg(asp, center, radius, angle);
        return {{"image", body + sampleAt(c)}};
    };

    NodeDef wave = node("wave", "Wave", "distort", "Ripple the picture side to side or up and down.");
    wave.inputs = {imageIn(), floatIn("amplitude", "Strength", 0.02, 0, 0.2), floatIn("frequency", "Waves", 10, 0, 60),
                   floatIn("speed", "Speed", 2, -20, 20), clockIn("t", "Time")};
    wave.options = {selectOpt("direction", "Direction",
                              {choice("x", "Side to side"), choice("y", "Up and down"), choice("xy", "Both")}, "x"),
                    edgeOpt()};
    wave.outputs = imageOut;
    wave.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q dir = c.optString("direction");
        const Q t = c.in("t");
        const Q speed = c.in("speed");
        const Q ph = t + " * " + speed;
        const Q f1 = c.in("frequency");
        const Q a1 = c.in("amplitude");
        const Q dx = "sin(uv.y * " + f1 + " * 6.2832 + " + ph + ") * " + a1;
        const Q f2 = c.in("frequency");
        const Q a2 = c.in("amplitude");
        const Q dy = "sin(uv.x * " + f2 + " * 6.2832 + " + ph + ") * " + a2;
        Q off;
        if (dir == QLatin1String("x"))
            off = "vec2(" + dx + ", 0.0)";
        else if (dir == QLatin1String("y"))
            off = "vec2(0.0, " + dy + ")";
        else
            off = "vec2(" + dx + ", " + dy + ")";
        return {{"image", "vec2 p = uv + " + off + ";\n    " + sampleAt(c)}};
    };

    NodeDef ripple = node("ripple", "Ripple", "distort", "Rings spreading out from a point, like a stone dropped in a pond.");
    ripple.inputs = {imageIn(), floatIn("amplitude", "Strength", 0.015, 0, 0.1), floatIn("frequency", "Rings", 30, 1, 100),
                     floatIn("speed", "Speed", 4, -20, 20), center(),
                     clockIn("t", "Time")};
    ripple.options = {edgeOpt()};
    ripple.outputs = imageOut;
    ripple.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q frequency = c.in("frequency");
        const Q t = c.in("t");
        const Q speed = c.in("speed");
        const Q amplitude = c.in("amplitude");
        const Q body = QStringLiteral(R"(float asp = %1;
    vec2 d = (uv - %2) * vec2(asp, 1.0);
    float r = length(d);
    vec2 dir = r > 1e-5 ? d / r : vec2(0.0);
    float w = sin(r * %3 - %4 * %5) * %6;
    vec2 p = uv + vec2(dir.x / asp, dir.y) * w;
    )").arg(asp, center, frequency, t, speed, amplitude);
        return {{"image", body + sampleAt(c)}};
    };

    NodeDef pixelate = node("pixelate", "Pixelate", "distort", "Big chunky pixels.");
    pixelate.inputs = {imageIn(), floatIn("size", "Pixel size", 16, 1, 200)};
    pixelate.outputs = imageOut;
    pixelate.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q size = c.in("size");
        const Q res = c.res();
        const Q image = c.in("image", "clamp(p, 0.0, 1.0)");
        return {{"image", QStringLiteral(R"(vec2 cell = max(%1, 1.0) / %2;
    vec2 p = (floor(uv / cell) + 0.5) * cell;
    return %3;)").arg(size, res, image)}};
    };

    NodeDef mirror = node("mirror", "Mirror", "distort", "Reflect one half of the picture onto the other.");
    mirror.inputs = {imageIn()};
    mirror.options = {selectOpt("mode", "Reflect",
                                {choice("lr", "Left onto right"), choice("rl", "Right onto left"), choice("tb", "Top onto bottom"),
                                 choice("bt", "Bottom onto top"), choice("quad", "Four ways")},
                                "lr")};
    mirror.outputs = imageOut;
    mirror.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QMap<Q, Q> uvs = {{"lr", "vec2(0.5 - abs(uv.x - 0.5), uv.y)"},
                                {"rl", "vec2(0.5 + abs(uv.x - 0.5), uv.y)"},
                                {"tb", "vec2(uv.x, 0.5 - abs(uv.y - 0.5))"},
                                {"bt", "vec2(uv.x, 0.5 + abs(uv.y - 0.5))"},
                                {"quad", "0.5 - abs(uv - 0.5)"}};
        const Q m = c.optString("mode");
        const Q p = uvs.contains(m) ? uvs.value(m) : uvs.value("quad");
        return {{"image", "return " + c.in("image", p) + ";"}};
    };

    NodeDef kaleido = node("kaleidoscope", "Kaleidoscope", "distort", "Fold the picture into mirrored slices around a point.");
    kaleido.inputs = {imageIn(), integer(floatIn("segments", "Slices", 6, 2, 24)),
                      widget(floatIn("rotation", "Rotation", 0, -360, 360), "angle"), center()};
    kaleido.outputs = imageOut;
    kaleido.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q segments = c.in("segments");
        const Q rotation = c.in("rotation");
        const Q image = c.in("image", "1.0 - abs(mod(p, 2.0) - 1.0)");
        return {{"image", QStringLiteral(R"(float asp = %1;
    vec2 ctr = %2;
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    float seg = 6.2832 / max(floor(%3), 2.0);
    float a = atan(d.y, d.x) + radians(%4);
    a = mod(a, seg);
    a = abs(a - seg * 0.5);
    vec2 nd = vec2(cos(a), sin(a)) * length(d);
    vec2 p = ctr + vec2(nd.x / asp, nd.y);
    return %5;)").arg(asp, center, segments, rotation, image)}};
    };

    NodeDef lens = node("lens", "Fisheye / Lens", "distort", "Bulge the middle out (fisheye) or pinch it in.");
    lens.inputs = {imageIn(), floatIn("amount", "Bulge", 0.5, -1, 2), center(),
                   hint(floatIn("area", "Area", 2, 0.05, 2), "Below 1 limits the bulge to a circle (spherize).")};
    lens.options = {edgeOpt()};
    lens.outputs = imageOut;
    lens.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q area = c.in("area");
        const Q amount1 = c.in("amount");
        const Q amount2 = c.in("amount");
        const Q body = QStringLiteral(R"(float asp = %1;
    vec2 ctr = %2;
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    float ar = max(%3, 0.0001);
    float inside = 1.0 - smoothstep(ar * 0.8, ar, length(d));
    float k = min(ar, 1.0);
    vec2 dn = d / k;
    d = mix(d, dn * (1.0 - %4 * 0.5 + %5 * dot(dn, dn)) * k, inside);
    vec2 p = ctr + vec2(d.x / asp, d.y);
    )").arg(asp, center, area, amount1, amount2);
        return {{"image", body + sampleAt(c)}};
    };

    NodeDef displace = node("displace", "Displace", "distort",
        "Push pixels around using another image or noise. Red moves sideways, green moves up/down.");
    displace.inputs = {imageIn(), colorIn("map", "Map", rgba(0.5, 0.5, 0.5, 1)), floatIn("strength", "Strength", 0.05, 0, 0.5)};
    displace.options = {edgeOpt()};
    displace.outputs = imageOut;
    displace.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q map = c.in("map");
        const Q strength = c.in("strength");
        const Q body = QStringLiteral("vec2 p = uv + (%1.rg - 0.5) * 2.0 * %2;\n    ").arg(map, strength);
        return {{"image", body + sampleAt(c)}};
    };

    NodeDef shake = node("shake", "Camera shake", "distort", "Jitter the frame like a handheld camera or an impact.");
    shake.inputs = {imageIn(), floatIn("amount", "Strength", 0.02, 0, 0.2), floatIn("speed", "Speed", 12, 0, 60),
                    widget(floatIn("rotation", "Twist", 0, 0, 20), "angle"),
                    clockIn("t", "Time"),
                    widget(floatIn("seed", "Seed", 0, 0, 1000), "seed")};
    shake.options = {edgeOpt()};
    shake.outputs = imageOut;
    shake.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("valueNoise");
        c.helper("rot2");
        const Q t = c.in("t");
        const Q speed = c.in("speed");
        const Q seed = c.in("seed");
        const Q rotation = c.in("rotation");
        const Q asp = c.aspect();
        const Q amount1 = c.in("amount");
        const Q amount2 = c.in("amount");
        const Q body = QStringLiteral(R"(float ts = %1 * %2 + %3 * 11.3;
    vec2 j = vec2(valueNoise(vec2(ts, 3.7)), valueNoise(vec2(9.1, ts))) - 0.5;
    float tw = (valueNoise(vec2(ts * 0.9, 5.3)) - 0.5) * 2.0 * radians(%4);
    float asp = %5;
    vec2 q = (uv - 0.5) * vec2(asp, 1.0);
    q = rot2(tw) * q;
    vec2 p = vec2(q.x / asp, q.y) / (1.0 + %6 * 2.0 + abs(tw) * 0.5) + 0.5 + j * 2.0 * %7;
    )").arg(t, speed, seed, rotation, asp, amount1, amount2);
        return {{"image", body + sampleAt(c)}};
    };

    NodeDef tile = node("tile", "Tile", "distort", "Repeat the picture in a grid.");
    tile.inputs = {imageIn(), vecIn("count", "Tiles", 2, 2, 1, 12)};
    tile.options = {toggleOpt("mirror", "Mirror alternate tiles", false)};
    tile.outputs = imageOut;
    tile.emitFn = [](EmitCtx &c, Stage) -> Out {
        if (c.optBool("mirror")) {
            const Q count = c.in("count");
            const Q image = c.in("image", "1.0 - abs(mod(p, 2.0) - 1.0)");
            return {{"image", QStringLiteral("vec2 p = uv * %1;\n    return %2;").arg(count, image)}};
        }
        const Q count = c.in("count");
        const Q image = c.in("image", "fract(uv * " + count + ")");
        return {{"image", "return " + image + ";"}};
    };

    registerNodeDefs({transform, zoom, twirl, wave, ripple, pixelate, mirror, kaleido, lens, displace, shake, tile});
}

}
