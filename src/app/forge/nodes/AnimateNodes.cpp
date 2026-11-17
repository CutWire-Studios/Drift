#include "app/forge/glsl/Literals.h"
#include "app/forge/nodes/NodeBuild.h"

#include <QJsonObject>

#include <algorithm>

namespace drift::forge {

namespace {

using namespace build;
using Q = QString;

Q easeGlsl(const Q &ease, const Q &k)
{
    if (ease == QLatin1String("smooth"))
        return k + " * " + k + " * (3.0 - 2.0 * " + k + ")";
    if (ease == QLatin1String("in"))
        return k + " * " + k + " * " + k;
    if (ease == QLatin1String("out"))
        return "1.0 - pow(1.0 - " + k + ", 3.0)";
    if (ease == QLatin1String("hold"))
        return QStringLiteral("0.0");
    return k;
}

QJsonObject curveKey(double x, double y, const char *ease)
{
    return {{"x", x}, {"y", y}, {"ease", QLatin1String(ease)}};
}

}

void registerAnimateNodes()
{
    using Stage = const StageReader &;

    const QList<OutputDef> valueOut = {out("value", "Value", SocketType::Float)};
    const auto seedIn = [] { return widget(floatIn("seed", "Seed", 0, 0, 1000), "seed"); };

    NodeDef lfo = node("lfo", "Oscillator", "animate", "A value that swings back and forth over time: pulses, flickers, bounces.");
    lfo.inputs = {floatIn("rate", "Cycles per second", 1, 0, 20), floatIn("min", "Lowest", 0, -10, 10),
                  floatIn("max", "Highest", 1, -10, 10), floatIn("phase", "Offset", 0, 0, 1), clockIn("t", "Time"), seedIn()};
    lfo.options = {selectOpt("shape", "Shape",
                             {choice("sine", "Smooth (sine)"), choice("triangle", "Triangle"), choice("saw", "Ramp up"),
                              choice("square", "On / off"), choice("pulse", "Pulse (beat)"), choice("random", "Random steps")},
                             "sine")};
    lfo.outputs = valueOut;
    lfo.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q shape = c.optString("shape");
        if (shape == QLatin1String("random"))
            c.helper("hash11");
        const Q seed = c.in("seed");
        Q w;
        if (shape == QLatin1String("triangle"))
            w = "1.0 - abs(fract(x) * 2.0 - 1.0)";
        else if (shape == QLatin1String("saw"))
            w = "fract(x)";
        else if (shape == QLatin1String("square"))
            w = "step(0.5, fract(x))";
        else if (shape == QLatin1String("pulse"))
            w = "exp(-fract(x) * 6.0)";
        else if (shape == QLatin1String("random"))
            w = "hash11(floor(x) + 0.5 + " + seed + " * 13.1)";
        else
            w = "0.5 - 0.5 * cos(x * 6.2832)";
        const Q t = c.in("t");
        const Q rate = c.in("rate");
        const Q phase = c.in("phase");
        const Q lo = c.in("min");
        const Q hi = c.in("max");
        return {{"value", "float x = " + t + " * " + rate + " + " + phase + ";\n    return mix(" + lo + ", " + hi + ", " + w + ");"}};
    };

    NodeDef curve = node("curve", "Keyframe curve", "animate",
        "Draw exactly how a value changes over time. Add points, drag them, pick how each one eases.");
    curve.inputs = {floatIn("min", "Bottom value", 0, -10, 10), floatIn("max", "Top value", 1, -10, 10),
                    floatIn("length", "Length (s)", 2, 0.1, 30), clockIn("t", "Time")};
    curve.inputs[2].noExpose = true;
    {
        OptionDef points;
        points.id = QStringLiteral("points");
        points.label = QStringLiteral("Curve");
        points.kind = OptionKind::Curve;
        points.defaultValue = QJsonArray{curveKey(0, 0, "smooth"), curveKey(1, 1, "linear")};
        curve.options = {points, toggleOpt("loop", "Loop", true)};
    }
    curve.outputs = valueOut;
    curve.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QJsonValue raw = c.opt("points");
        QList<QJsonObject> pts;
        if (raw.isArray())
            for (const QJsonValue &v : raw.toArray())
                pts.append(v.toObject());
        std::stable_sort(pts.begin(), pts.end(), [](const QJsonObject &a, const QJsonObject &b) {
            return a.value("x").toDouble() < b.value("x").toDouble();
        });
        const bool effect = c.kind == Kind::Effect;
        Q timeIn;
        if (!effect) {
            timeIn = c.in("t");
        } else {
            const Q t = c.in("t");
            const Q length = c.in("length");
            timeIn = t + " / max(" + length + ", 0.0001)";
        }
        const Q t = effect && c.optBool("loop") ? "fract(" + timeIn + ")" : "clamp(" + timeIn + ", 0.0, 1.0)";
        QStringList lines = {"float t = " + t + ";", QStringLiteral("float y;")};
        const std::optional<QString> exposed = c.optParam("points");
        const auto num = [](const QJsonObject &o, const char *k) { return glslFloat(o.value(QLatin1String(k)).toDouble()); };
        if (exposed) {
            lines.append("y = texture(" + *exposed + ", vec2(t * (255.0 / 256.0) + 0.5 / 256.0, 0.5)).r;");
        } else if (pts.isEmpty()) {
            lines.append(QStringLiteral("y = 0.0;"));
        } else {
            lines.append("if (t <= " + num(pts.first(), "x") + ") y = " + num(pts.first(), "y") + ";");
            for (int i = 0; i + 1 < pts.size(); ++i) {
                const QJsonObject &a = pts.at(i);
                const QJsonObject &b = pts.at(i + 1);
                const double span = std::max(b.value("x").toDouble() - a.value("x").toDouble(), 1e-6);
                lines.append("else if (t < " + num(b, "x") + ") { float k = (t - " + num(a, "x") + ") / " + glslFloat(span)
                             + "; y = mix(" + num(a, "y") + ", " + num(b, "y") + ", " + easeGlsl(a.value("ease").toString(), "k")
                             + "); }");
            }
            lines.append("else y = " + num(pts.last(), "y") + ";");
        }
        const Q lo = c.in("min");
        const Q hi = c.in("max");
        lines.append("return mix(" + lo + ", " + hi + ", y);");
        return {{"value", lines.join("\n    ")}};
    };

    NodeDef ease = node("ease", "Ease", "animate", "Reshape a 0\xE2\x86\x92" "1 value so movement speeds up, slows down, overshoots or bounces.");
    ease.inputs = {clockIn("t", "Input")};
    ease.options = {selectOpt("curve", "Curve",
                              {choice("inout", "Slow, fast, slow"), choice("in", "Speed up"), choice("out", "Slow down"),
                               choice("expo", "Snappy"), choice("back", "Overshoot"), choice("elastic", "Elastic"),
                               choice("bounce", "Bounce"), choice("peak", "Up and back (0\xE2\x86\x92" "1\xE2\x86\x92" "0)"),
                               choice("peaksmooth", "Swell (smooth 0\xE2\x86\x92" "1\xE2\x86\x92" "0)")},
                              "inout")};
    ease.outputs = valueOut;
    ease.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q curve = c.optString("curve");
        if (curve == QLatin1String("bounce"))
            c.helper("ease");
        Q e;
        if (curve == QLatin1String("in"))
            e = "t * t * t";
        else if (curve == QLatin1String("out"))
            e = "1.0 - pow(1.0 - t, 3.0)";
        else if (curve == QLatin1String("expo"))
            e = "t < 0.5 ? pow(2.0, 20.0 * t - 10.0) / 2.0 : (2.0 - pow(2.0, -20.0 * t + 10.0)) / 2.0";
        else if (curve == QLatin1String("back"))
            e = "1.0 + 2.70158 * pow(t - 1.0, 3.0) + 1.70158 * pow(t - 1.0, 2.0)";
        else if (curve == QLatin1String("elastic"))
            e = "t <= 0.0 ? 0.0 : t >= 1.0 ? 1.0 : pow(2.0, -10.0 * t) * sin((t * 10.0 - 0.75) * 2.0944) + 1.0";
        else if (curve == QLatin1String("bounce"))
            e = "easeOutBounce(t)";
        else if (curve == QLatin1String("peak"))
            e = "1.0 - abs(t * 2.0 - 1.0)";
        else if (curve == QLatin1String("peaksmooth"))
            e = "sin(t * 3.14159265)";
        else
            e = "t < 0.5 ? 4.0 * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0";
        const Q t = c.in("t");
        return {{"value", "float t = clamp(" + t + ", 0.0, 1.0);\n    return " + e + ";"}};
    };

    NodeDef flicker = node("flicker", "Flicker", "animate", "Random brightness jumps, like a faulty bulb or an old projector.");
    flicker.inputs = {floatIn("rate", "Changes per second", 12, 0, 60), floatIn("amount", "Amount", 0.4, 0, 1), clockIn("t", "Time"),
                      seedIn()};
    flicker.outputs = valueOut;
    flicker.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("hash11");
        const Q amount = c.in("amount");
        const Q t = c.in("t");
        const Q rate = c.in("rate");
        const Q seed = c.in("seed");
        return {{"value", "return 1.0 - " + amount + " * hash11(floor(" + t + " * " + rate + ") + 0.5 + " + seed + " * 13.1);"}};
    };

    NodeDef steps = node("steps", "Stepped", "animate", "Make smooth movement jump in steps, like stop-motion.");
    steps.inputs = {clockIn("x", "Input"), integer(floatIn("steps", "Steps per unit", 8, 1, 60))};
    steps.outputs = valueOut;
    steps.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q x = c.in("x");
        const Q s1 = c.in("steps");
        const Q s2 = c.in("steps");
        return {{"value", "return floor(" + x + " * " + s1 + ") / max(" + s2 + ", 1.0);"}};
    };

    registerNodeDefs({lfo, curve, ease, flicker, steps});
}

}
