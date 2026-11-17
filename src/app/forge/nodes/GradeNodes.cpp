#include "app/forge/glsl/Literals.h"
#include "app/forge/nodes/NodeBuild.h"

#include <algorithm>

namespace drift::forge {

namespace {

QJsonArray defaultGradient()
{
    const auto stop = [](double pos, const QJsonArray &color) { return QJsonObject{{"pos", pos}, {"color", color}}; };
    return QJsonArray{stop(0, build::rgba(0.05, 0.02, 0.15, 1)), stop(0.5, build::rgba(0.85, 0.2, 0.35, 1)),
                      stop(1, build::rgba(1, 0.85, 0.4, 1))};
}

struct Stop
{
    double pos;
    QJsonArray color;
};

QList<Stop> readStops(const QJsonArray &a)
{
    QList<Stop> stops;
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        stops.append({o.value("pos").toDouble(), o.value("color").toArray()});
    }
    return stops;
}

QString vec4Of(const Stop &s)
{
    QStringList parts;
    for (const QJsonValue &c : s.color)
        parts << glslFloat(c.toDouble());
    return QStringLiteral("vec4(%1)").arg(parts.join(QStringLiteral(", ")));
}

}

void registerGradeNodes()
{
    using namespace build;
    using Stage = const StageReader &;

    const QList<OutputDef> imageOut = {out("image", "Image", SocketType::Color)};

    NodeDef exposure = node("exposure", "Exposure", "color", "Brighten or darken like opening or closing the camera's aperture, in stops.");
    exposure.inputs = {imageIn(), unit(floatIn("stops", "Stops", 0.5, -4, 4), "EV")};
    exposure.outputs = imageOut;
    exposure.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QString stops = c.in("stops");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    return vec4(clamp(col.rgb * exp2(%2), 0.0, 1.0), col.a);)").arg(image, stops)}};
    };

    NodeDef tone = node("tone", "Shadows / Highlights", "color",
        "Lift or crush the dark parts and the bright parts separately, plus the extreme blacks and whites.");
    tone.inputs = {imageIn(), floatIn("shadows", "Shadows", 0.2, -1, 1), floatIn("highlights", "Highlights", -0.1, -1, 1),
                   floatIn("blacks", "Blacks", 0, -1, 1), floatIn("whites", "Whites", 0, -1, 1)};
    tone.outputs = imageOut;
    tone.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const QString image = c.in("image");
        const QString shadows = c.in("shadows");
        const QString highlights = c.in("highlights");
        const QString blacks = c.in("blacks");
        const QString whites = c.in("whites");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    float l = luma(col);
    float wS = 1.0 - smoothstep(0.0, 0.55, l);
    float wH = smoothstep(0.45, 1.0, l);
    float wB = 1.0 - smoothstep(0.0, 0.25, l);
    float wW = smoothstep(0.75, 1.0, l);
    float d = %2 * 0.5 * wS + %3 * 0.5 * wH + %4 * 0.3 * wB + %5 * 0.3 * wW;
    return vec4(clamp(col.rgb + d, 0.0, 1.0), col.a);)").arg(image, shadows, highlights, blacks, whites)}};
    };

    NodeDef selective = node("selective_color", "Selective colour", "color",
        "Change just one colour range: make the sky bluer, the skin warmer, or keep only the reds.");
    selective.inputs = {imageIn(), widget(floatIn("hue", "Target hue", 0, 0, 360), "angle"),
                        unit(floatIn("range", "Range", 30, 1, 180), "\xc2\xb0"), unit(floatIn("softness", "Softness", 20, 0, 90), "\xc2\xb0"),
                        widget(floatIn("shift", "Hue shift", 0, -180, 180), "angle"), floatIn("saturation", "Saturation", 0, -1, 1),
                        floatIn("lightness", "Lightness", 0, -1, 1), floatIn("others", "Desaturate the rest", 0, 0, 1)};
    selective.outputs = {out("image", "Image", SocketType::Color), out("mask", "Selection", SocketType::Float)};
    selective.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hsv");
        const QString hue = c.in("hue");
        const QString range = c.in("range");
        const QString softness = c.in("softness");
        const QString sel = QStringLiteral(R"(vec3 hsv = rgb2hsv(col.rgb);
    float dh = abs(fract(hsv.x - %1 / 360.0 + 0.5) - 0.5) * 360.0;
    float m = (1.0 - smoothstep(%2, %2 + %3 + 0.001, dh)) * smoothstep(0.05, 0.2, hsv.y);)").arg(hue, range, softness);
        const QString image = c.in("image");
        const QString shift = c.in("shift");
        const QString saturation = c.in("saturation");
        const QString lightness = c.in("lightness");
        const QString others = c.in("others");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    %2
    vec3 adj = vec3(fract(hsv.x + %3 / 360.0), clamp(hsv.y * (1.0 + %4), 0.0, 1.0), clamp(hsv.z * (1.0 + %5), 0.0, 1.0));
    vec3 rest = mix(col.rgb, vec3(dot(col.rgb, vec3(0.2126, 0.7152, 0.0722))), %6);
    return vec4(mix(rest, hsv2rgb(adj), m), col.a);)").arg(image, sel, shift, saturation, lightness, others)},
                {"mask", QStringLiteral("vec4 col = %1;\n    %2\n    return m;").arg(image, sel)}};
    };

    NodeDef mixer = node("channel_mixer", "Channel mixer", "color",
        "Rebuild red, green and blue from any mix of the originals, for false-colour and film looks.");
    mixer.inputs = {imageIn(),
                    floatIn("rr", "Red \xe2\x86\x90 red", 1, -2, 2),   floatIn("rg", "Red \xe2\x86\x90 green", 0, -2, 2),
                    floatIn("rb", "Red \xe2\x86\x90 blue", 0, -2, 2),  floatIn("gr", "Green \xe2\x86\x90 red", 0, -2, 2),
                    floatIn("gg", "Green \xe2\x86\x90 green", 1, -2, 2), floatIn("gb", "Green \xe2\x86\x90 blue", 0, -2, 2),
                    floatIn("br", "Blue \xe2\x86\x90 red", 0, -2, 2),  floatIn("bg", "Blue \xe2\x86\x90 green", 0, -2, 2),
                    floatIn("bb", "Blue \xe2\x86\x90 blue", 1, -2, 2)};
    mixer.outputs = imageOut;
    mixer.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QStringList ids = {"rr", "rg", "rb", "gr", "gg", "gb", "br", "bg", "bb"};
        QStringList v;
        for (const QString &id : ids)
            v << c.in(id);
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 rgb = vec3(
        dot(col.rgb, vec3(%2, %3, %4)),
        dot(col.rgb, vec3(%5, %6, %7)),
        dot(col.rgb, vec3(%8, %9, %10)));
    return vec4(clamp(rgb, 0.0, 1.0), col.a);)").arg(image).arg(v[0]).arg(v[1]).arg(v[2]).arg(v[3]).arg(v[4]).arg(v[5]).arg(v[6]).arg(v[7]).arg(v[8])}};
    };

    NodeDef wheels = node("lift_gamma_gain", "Colour wheels", "color",
        "Tint the shadows, mid-tones and highlights separately (lift, gamma, gain). Grey means no change.");
    wheels.inputs = {imageIn(), swatch("lift", "Shadows (lift)", rgba(0.45, 0.5, 0.56, 1)),
                     swatch("gamma", "Mid-tones (gamma)", rgba(0.5, 0.5, 0.5, 1)),
                     swatch("gain", "Highlights (gain)", rgba(0.56, 0.52, 0.46, 1)), floatIn("amount", "Amount", 1, 0, 1)};
    wheels.outputs = imageOut;
    wheels.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString image = c.in("image");
        const QString lift = c.in("lift");
        const QString gain = c.in("gain");
        const QString gamma = c.in("gamma");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    vec3 lift = (%2.rgb - 0.5) * 0.5;
    vec3 gain = 1.0 + (%3.rgb - 0.5);
    vec3 gam = exp2(-(%4.rgb - 0.5) * 2.0);
    vec3 rgb = col.rgb * gain;
    rgb = rgb + lift * (1.0 - rgb);
    rgb = pow(clamp(rgb, 0.0, 1.0), gam);
    return vec4(mix(col.rgb, rgb, %5), col.a);)").arg(image, lift, gain, gamma, amount)}};
    };

    NodeDef vibrance = node("vibrance", "Vibrance", "color", "Boost dull colours more than already-vivid ones, so skin doesn't go orange.");
    vibrance.inputs = {imageIn(), floatIn("amount", "Vibrance", 0.5, -1, 1)};
    vibrance.outputs = imageOut;
    vibrance.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const QString image = c.in("image");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    float mx = max(col.r, max(col.g, col.b));
    float mn = min(col.r, min(col.g, col.b));
    float sat = mx - mn;
    float k = %2 * (1.0 - sat);
    vec3 rgb = mix(vec3(luma(col)), col.rgb, 1.0 + k);
    return vec4(clamp(rgb, 0.0, 1.0), col.a);)").arg(image, amount)}};
    };

    NodeDef replace = node("color_replace", "Replace colour", "color", "Swap one colour for another, keeping the light and shade.");
    replace.inputs = {imageIn(), swatch("from", "Find", rgba(0.85, 0.15, 0.15, 1)), swatch("to", "Replace with", rgba(0.15, 0.45, 0.95, 1)),
                      floatIn("tolerance", "Tolerance", 0.25, 0, 1), floatIn("softness", "Softness", 0.1, 0, 0.5)};
    replace.outputs = {out("image", "Image", SocketType::Color), out("mask", "Selection", SocketType::Float)};
    replace.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString from = c.in("from");
        const QString tolerance = c.in("tolerance");
        const QString softness = c.in("softness");
        const QString sel = QStringLiteral(R"(float dist = distance(col.rgb, %1.rgb);
    float m = 1.0 - smoothstep(%2, %2 + %3 + 0.0001, dist);)").arg(from, tolerance, softness);
        const QString image = c.in("image");
        const QString to = c.in("to");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    %2
    vec3 moved = clamp(col.rgb - %3.rgb + %4.rgb, 0.0, 1.0);
    return vec4(mix(col.rgb, moved, m), col.a);)").arg(image, sel, from, to)},
                {"mask", QStringLiteral("vec4 col = %1;\n    %2\n    return m;").arg(image, sel)}};
    };

    NodeDef chroma = node("chroma_key", "Green screen", "color",
        "Cut out a green (or any colour) screen, leaving the subject on a transparent background.");
    chroma.inputs = {imageIn(), swatch("key", "Screen colour", rgba(0.1, 0.8, 0.2, 1)), floatIn("tolerance", "Tolerance", 0.12, 0, 0.5),
                     floatIn("softness", "Edge softness", 0.08, 0, 0.5), floatIn("spill", "Remove green spill", 0.5, 0, 1)};
    chroma.outputs = {out("image", "Cut out", SocketType::Color), out("mask", "Subject mask", SocketType::Float)};
    chroma.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("ycc");
        const QString key = c.in("key");
        const QString tolerance = c.in("tolerance");
        const QString softness = c.in("softness");
        const QString m = QStringLiteral(R"(vec3 k = rgb2ycc(%1.rgb);
    vec3 y = rgb2ycc(col.rgb);
    float dist = distance(y.yz, k.yz);
    float m = smoothstep(%2, %2 + %3 + 0.0001, dist);)").arg(key, tolerance, softness);
        const QString image = c.in("image");
        const QString spill = c.in("spill");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    %2
    vec3 key = %3.rgb;
    vec3 rgb = col.rgb;
    float dom = key.g >= max(key.r, key.b) ? rgb.g - max(rgb.r, rgb.b) : key.b >= key.r ? rgb.b - max(rgb.r, rgb.g) : rgb.r - max(rgb.g, rgb.b);
    vec3 spillDir = key / max(max(key.r, max(key.g, key.b)), 0.0001);
    rgb -= spillDir * max(dom, 0.0) * %4;
    return vec4(clamp(rgb, 0.0, 1.0), col.a * m);)").arg(image, m, key, spill)},
                {"mask", QStringLiteral("vec4 col = %1;\n    %2\n    return m;").arg(image, m)}};
    };

    NodeDef ramp = node("gradient_ramp", "Gradient map", "color", "Recolour by brightness through a colour gradient: fire, thermal, neon, film looks.");
    ramp.inputs = {imageIn(), floatIn("amount", "Amount", 1, 0, 1)};
    {
        OptionDef stops;
        stops.id = QStringLiteral("stops");
        stops.label = QStringLiteral("Gradient");
        stops.kind = OptionKind::Gradient;
        stops.defaultValue = defaultGradient();
        ramp.options = {stops};
    }
    ramp.outputs = imageOut;
    ramp.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const std::optional<QString> exposed = c.optParam("stops");
        QString lookup;
        if (exposed) {
            lookup = QStringLiteral("texture(%1, vec2(t * (255.0 / 256.0) + 0.5 / 256.0, 0.5))").arg(*exposed);
        } else {
            const QJsonValue raw = c.opt("stops");
            QList<Stop> stops = readStops(raw.isArray() && !raw.toArray().isEmpty() ? raw.toArray() : defaultGradient());
            std::stable_sort(stops.begin(), stops.end(), [](const Stop &a, const Stop &b) { return a.pos < b.pos; });
            const QString fn = c.prefix + QStringLiteral("_grad");
            QStringList lines = {QStringLiteral("vec4 %1(float t) {").arg(fn),
                                 QStringLiteral("    if (t <= %1) return %2;").arg(glslFloat(stops.first().pos), vec4Of(stops.first()))};
            for (int i = 0; i + 1 < stops.size(); ++i) {
                const Stop &a = stops[i];
                const Stop &b = stops[i + 1];
                lines << QStringLiteral("    if (t < %1) return mix(%2, %3, (t - %4) / %5);")
                             .arg(glslFloat(b.pos), vec4Of(a), vec4Of(b), glslFloat(a.pos), glslFloat(std::max(b.pos - a.pos, 1e-6)));
            }
            lines << QStringLiteral("    return %1;").arg(vec4Of(stops.last())) << QStringLiteral("}");
            c.declare(lines.join(QLatin1Char('\n')));
            lookup = fn + QStringLiteral("(t)");
        }
        const QString image = c.in("image");
        const QString amount = c.in("amount");
        return {{"image", QStringLiteral(R"(vec4 col = %1;
    float t = clamp(luma(col), 0.0, 1.0);
    vec4 g = %2;
    return vec4(mix(col.rgb, g.rgb, %3 * g.a), col.a);)").arg(image, lookup, amount)}};
    };

    registerNodeDefs({exposure, tone, selective, mixer, wheels, vibrance, replace, chroma, ramp});
}

}
