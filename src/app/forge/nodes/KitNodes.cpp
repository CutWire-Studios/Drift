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

void registerKitBlurNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};
    const auto f = [](const char *id, const char *label, double def, double min, double max) { return floatIn(id, label, def, min, max); };

    NodeDef spin = node("spin_blur", "Spin blur", "blur", "Blur in a circle around a point, like the camera spinning.");
    spin.inputs = {imageIn(), widget(f("angle", "Spin", 10, 0, 90), "angle"), point("center", "Centre", 0.5, 0.5)};
    spin.outputs = imageOut;
    spin.heavyInputsStatic = {S("image")};
    spin.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("rot2");
        const Q asp = c.aspect();
        const Q center = c.in("center");
        const Q angle = c.in("angle");
        const Q image = c.in("image", "ctr + vec2(q.x / asp, q.y)");
        return {{"image", QStringLiteral(R"(float asp = %1;
    vec2 ctr = %2;
    vec2 d = (uv - ctr) * vec2(asp, 1.0);
    vec4 sum = vec4(0.0);
    for (int k = 0; k < 24; ++k) {
        float a = radians(%3) * (float(k) / 23.0 - 0.5);
        vec2 q = rot2(a) * d;
        sum += %4;
    }
    return sum / 24.0;)").arg(asp, center, angle, image)}};
    };

    NodeDef lensBlur = node("lens_blur", "Lens blur (bokeh)", "blur", "Out-of-focus blur like a real lens, where bright lights become soft discs.");
    lensBlur.inputs = {imageIn(), unit(f("radius", "Radius (px)", 12, 0, 60), "px"), f("boost", "Highlight discs", 1.5, 0, 6)};
    lensBlur.outputs = imageOut;
    lensBlur.heavyInputsStatic = {S("image")};
    lensBlur.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const Q radius = c.in("radius");
        const Q res = c.res();
        const Q image = c.in("image", "uv + vec2(cos(a), sin(a)) * rr / " + res);
        const Q boost = c.in("boost");
        return {{"image", QStringLiteral(R"(float r = max(%1, 0.0);
    vec4 acc = vec4(0.0);
    float wsum = 0.0;
    for (int i = 0; i < 64; ++i) {
        float fi = float(i);
        float rr = sqrt((fi + 0.5) / 64.0) * r;
        float a = fi * 2.39996323;
        vec4 s = %2;
        float w = 1.0 + %3 * pow(luma(s), 4.0) * 8.0;
        acc += s * w;
        wsum += w;
    }
    return acc / wsum;)").arg(radius, image, boost)}};
    };

    NodeDef streaks = node("streaks", "Star streaks", "blur", "Bright points flare into star-shaped spikes, like a star filter or sparkle.");
    streaks.inputs = {imageIn(), f("threshold", "Threshold", 0.7, 0, 1), unit(f("length", "Length (px)", 60, 1, 300), "px"),
                      integer(f("points", "Spikes", 4, 1, 8)), widget(f("rotation", "Rotation", 45, 0, 180), "angle"),
                      f("intensity", "Intensity", 1.5, 0, 5), swatch("tint", "Tint", rgba(1, 0.95, 0.85, 1))};
    streaks.outputs = imageOut;
    streaks.heavyInputsStatic = {S("image")};
    streaks.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q image0 = c.in("image");
        const Q points = c.in("points");
        const Q threshold = c.in("threshold");
        const Q rotation = c.in("rotation");
        const Q length = c.in("length");
        const Q res = c.res();
        const Q imageA = c.in("image", "uv + dir * t");
        const Q imageB = c.in("image", "uv - dir * t");
        const Q intensity = c.in("intensity");
        const Q tint = c.in("tint");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 sum = vec3(0.0);
    float n = clamp(floor(%2), 1.0, 8.0);
    float thr = %3;
    for (int i = 0; i < 8; ++i) {
        if (float(i) >= n) break;
        float a = radians(%4) + float(i) * 3.14159265 / n;
        vec2 dir = vec2(cos(a), sin(a)) * %5 / %6;
        for (int k = 1; k <= 12; ++k) {
            float t = float(k) / 12.0;
            float w = (1.0 - t) * (1.0 - t);
            sum += (max(%7.rgb - thr, 0.0) + max(%8.rgb - thr, 0.0)) * w;
        }
    }
    sum /= n * 4.0 * max(1.0 - thr, 0.05);
    return vec4(clamp(col.rgb + sum * %9 * %10.rgb, 0.0, 1.0), col.a);)")
                           .arg(image0, points, threshold, rotation, length, res, imageA, imageB)
                           .arg(intensity, tint)}};
    };

    NodeDef emboss = node("emboss", "Emboss", "stylize", "Raised, stamped-metal look from the picture's edges.");
    emboss.inputs = {imageIn(), f("strength", "Strength", 2, 0, 10), widget(f("angle", "Light angle", 135, 0, 360), "angle")};
    emboss.options = {toggleOpt("keep", "Keep colours", false)};
    emboss.outputs = imageOut;
    emboss.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const Q image0 = c.in("image");
        const Q angle = c.in("angle");
        const Q res = c.res();
        const Q imageA = c.in("image", "uv + d");
        const Q imageB = c.in("image", "uv - d");
        const Q strength = c.in("strength");
        const Q expr = c.optBool("keep") ? Q("col.rgb + e") : Q("vec3(0.5 + e)");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    float a = radians(%2);
    vec2 d = vec2(cos(a), sin(a)) * 1.5 / %3;
    float e = (luma(%4) - luma(%5)) * %6;
    return vec4(clamp(%7, 0.0, 1.0), col.a);)").arg(image0, angle, res, imageA, imageB, strength, expr)}};
    };

    NodeDef oil = node("oil_paint", "Oil paint", "stylize", "Smooth flat areas with crisp edges, like brush strokes.");
    oil.inputs = {imageIn(), unit(f("radius", "Brush size (px)", 4, 1, 6), "px")};
    oil.outputs = imageOut;
    oil.heavyInputsStatic = {S("image")};
    oil.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q radius = c.in("radius");
        const Q sample = c.in("image", "uv + vec2(float(i), float(j)) / " + c.res());
        const Q alpha = c.in("image");
        return {{"image", QStringLiteral(R"(float r = clamp(%1, 1.0, 6.0);
    vec3 m0 = vec3(0.0); vec3 m1 = vec3(0.0); vec3 m2 = vec3(0.0); vec3 m3 = vec3(0.0);
    vec3 q0 = vec3(0.0); vec3 q1 = vec3(0.0); vec3 q2 = vec3(0.0); vec3 q3 = vec3(0.0);
    float n0 = 0.0; float n1 = 0.0; float n2 = 0.0; float n3 = 0.0;
    for (int j = -6; j <= 6; ++j) {
        for (int i = -6; i <= 6; ++i) {
            if (abs(float(i)) > r || abs(float(j)) > r) continue;
            vec3 s = %2.rgb;
            if (i <= 0 && j <= 0) { m0 += s; q0 += s * s; n0 += 1.0; }
            if (i >= 0 && j <= 0) { m1 += s; q1 += s * s; n1 += 1.0; }
            if (i <= 0 && j >= 0) { m2 += s; q2 += s * s; n2 += 1.0; }
            if (i >= 0 && j >= 0) { m3 += s; q3 += s * s; n3 += 1.0; }
        }
    }
    m0 /= n0; m1 /= n1; m2 /= n2; m3 /= n3;
    vec3 v0 = q0 / n0 - m0 * m0; vec3 v1 = q1 / n1 - m1 * m1;
    vec3 v2 = q2 / n2 - m2 * m2; vec3 v3 = q3 / n3 - m3 * m3;
    float s0 = v0.r + v0.g + v0.b; float s1 = v1.r + v1.g + v1.b;
    float s2 = v2.r + v2.g + v2.b; float s3 = v3.r + v3.g + v3.b;
    vec3 best = m0; float bv = s0;
    if (s1 < bv) { best = m1; bv = s1; }
    if (s2 < bv) { best = m2; bv = s2; }
    if (s3 < bv) { best = m3; }
    return vec4(best, %3.a);)").arg(radius, sample, alpha)}};
    };

    registerNodeDefs({spin, lensBlur, streaks, emboss, oil});
}

void registerKitOverlayNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};
    const auto f = [](const char *id, const char *label, double def, double min, double max) { return floatIn(id, label, def, min, max); };
    const auto seedIn = [] { return widget(floatIn("seed", "Seed", 0, 0, 1000), "seed"); };

    NodeDef rain = node("rain", "Rain", "generate",
        "Falling rain streaks. Outputs a mask; blend it over the picture with Screen or Add.");
    rain.inputs = {f("amount", "Amount", 0.5, 0, 1), f("speed", "Speed", 1.5, 0, 5), f("length", "Streak length", 0.08, 0.01, 0.3),
                   widget(f("angle", "Slant", 10, -45, 45), "angle"), clockIn("t", "Time"), seedIn()};
    rain.outputs = {out("mask", "Rain", SocketType::Float)};
    rain.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hash11");
        c.helper("rot2");
        const Q asp = c.aspect();
        const Q angle = c.in("angle");
        const Q seed = c.in("seed");
        const Q amount = c.in("amount");
        const Q t = c.in("t");
        const Q speed = c.in("speed");
        const Q length = c.in("length");
        return {{"mask", QStringLiteral(R"(float asp = %1;
    vec2 q = rot2(radians(%2)) * ((uv - 0.5) * vec2(asp, 1.0));
    float acc = 0.0;
    for (int l = 0; l < 3; ++l) {
        float fl = float(l);
        float cols = 80.0 + fl * 60.0;
        float x = q.x * cols;
        float col = floor(x);
        float h = hash11(col * 1.37 + fl * 91.7 + %3);
        if (h > %4) continue;
        float y = q.y - %5 * %6 * (1.2 + fl * 0.5 + h) + hash11(col + 5.1) * 7.0;
        float seg = fract(y / (%7 * (3.0 + fl)));
        float streak = smoothstep(0.0, 0.2, seg) * (1.0 - smoothstep(0.25, 0.3, seg));
        float thin = 1.0 - smoothstep(0.05, 0.25, abs(fract(x) - 0.5));
        acc += streak * thin * (0.5 + fl * 0.25);
    }
    return clamp(acc, 0.0, 1.0);)").arg(asp, angle, seed, amount, t, speed, length)}};
    };

    NodeDef snow = node("snow", "Snow", "generate", "Drifting snowflakes. Outputs a mask; blend it over the picture with Screen.");
    snow.inputs = {f("amount", "Amount", 0.5, 0, 1), f("size", "Flake size", 1, 0.2, 4), f("speed", "Speed", 0.3, 0, 2),
                   clockIn("t", "Time"), seedIn()};
    snow.outputs = {out("mask", "Snow", SocketType::Float)};
    snow.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hash22");
        const Q asp = c.aspect();
        const Q t1 = c.in("t");
        const Q speed = c.in("speed");
        const Q t2 = c.in("t");
        const Q seed = c.in("seed");
        const Q amount = c.in("amount");
        const Q size = c.in("size");
        return {{"mask", QStringLiteral(R"(float asp = %1;
    vec2 base = uv * vec2(asp, 1.0);
    float acc = 0.0;
    for (int l = 0; l < 4; ++l) {
        float fl = float(l);
        float scale = 6.0 + fl * 5.0;
        vec2 p = base * scale;
        p.y -= %2 * %3 * scale * (0.6 + fl * 0.15);
        p.x += sin(p.y * 0.4 + fl * 3.0 + %4) * 0.4;
        vec2 cell = floor(p);
        vec2 h = hash22(cell + fl * 17.0 + %5);
        if (h.x > %6) continue;
        vec2 ctr = cell + 0.2 + h * 0.6;
        float r = (0.04 + h.y * 0.06) * %7 * (1.0 + fl * 0.3);
        acc += 1.0 - smoothstep(r * 0.4, r, length(p - ctr));
    }
    return clamp(acc, 0.0, 1.0);)").arg(asp, t1, speed, t2, seed, amount, size)}};
    };

    NodeDef film = node("film_damage", "Old film", "stylize", "Scratches, dust, gate weave and flicker, like worn projector film.");
    film.inputs = {imageIn(), f("scratches", "Scratches", 0.5, 0, 1), f("dust", "Dust", 0.5, 0, 1), f("flicker", "Flicker", 0.3, 0, 1),
                   f("weave", "Gate weave", 0.3, 0, 1), clockIn("t", "Time"), seedIn()};
    film.outputs = imageOut;
    film.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hash11");
        c.helper("hash21");
        const Q t = c.in("t");
        const Q seed = c.in("seed");
        const Q weave = c.in("weave");
        const Q image = c.in("image", "clamp(uv + jitter, 0.0, 1.0)");
        const Q flicker = c.in("flicker");
        const Q scratches = c.in("scratches");
        const Q asp = c.aspect();
        const Q dust = c.in("dust");
        return {{"image", QStringLiteral(R"(float frame = floor(%1 * 18.0) + %2 * 7.0;
    vec2 jitter = (vec2(hash11(frame), hash11(frame + 3.3)) - 0.5) * 0.006 * %3;
    vec4 col = %4;
    float fl = 1.0 + (hash11(frame + 9.1) - 0.5) * 0.3 * %5;
    vec3 rgb = col.rgb * fl;
    for (int i = 0; i < 3; ++i) {
        float fi = float(i);
        float on = step(1.0 - %6 * 0.6, hash11(frame * 1.7 + fi * 11.0));
        float x = hash11(frame + fi * 5.7);
        float line = 1.0 - smoothstep(0.0, 0.0015, abs(uv.x - x - sin(uv.y * 9.0 + fi) * 0.003));
        rgb = mix(rgb, vec3(0.9), line * on * 0.7);
    }
    vec2 cell = floor(uv * vec2(%7, 1.0) * 60.0);
    float speck = step(1.0 - %8 * 0.02, hash21(cell + frame * 13.0));
    rgb = mix(rgb, vec3(0.05), speck * 0.85);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);)").arg(t, seed, weave, image, flicker, scratches, asp, dust)}};
    };

    NodeDef grid = node("grid_cells", "Grid cells", "generate",
        "Splits the frame into tiles and gives each one a random number, its position and a local 0\xe2\x80\x93" "1 position. The base for tile, puzzle and random-square transitions.");
    grid.inputs = {vecIn("count", "Tiles", 8, 5, 1, 64), seedIn()};
    grid.outputs = {out("random", "Random per tile", SocketType::Float), out("order", "Diagonal order", SocketType::Float),
                    out("checker", "Checker", SocketType::Float), out("cell", "Tile position", SocketType::Vec2),
                    out("local", "Inside tile", SocketType::Vec2)};
    grid.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hash21");
        const Q count = c.in("count");
        const Q g = QStringLiteral("vec2 n = max(floor(%1), vec2(1.0));\n    vec2 id = floor(uv * n);").arg(count);
        const Q seed = c.in("seed");
        return {{"random", g + "\n    return hash21(id + " + seed + " * 1.31);"},
                {"order", g + "\n    return (id.x + id.y) / max(n.x + n.y - 2.0, 1.0);"},
                {"checker", g + "\n    return mod(id.x + id.y, 2.0);"},
                {"cell", g + "\n    return (id + 0.5) / n;"},
                {"local", g + "\n    return fract(uv * n);"}};
    };

    registerNodeDefs({rain, snow, film, grid});
}

}
