#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

using namespace build;

InputDef f(const char *id, const char *label, double def = 0, double min = -10, double max = 10)
{
    return floatIn(id, label, def, min, max);
}

InputDef v(const char *id, const char *label, double x, double y, double min = -2, double max = 2)
{
    return vecIn(id, label, x, y, min, max);
}

InputDef spacing()
{
    InputDef i = floatIn("spacing", "Step (px)", 1, 0.5, 20);
    i.noExpose = true;
    i.hint = S("How far apart the samples are. Larger smooths out noise and fine detail.");
    return i;
}

const char *fieldHint = "Unconnected: the pixel's own position.";

// The input's value, or the pixel's position when nothing is wired in.
QString field(EmitCtx &c, const QString &id, const QString &at = QStringLiteral("uv"))
{
    return c.connected(id) ? c.in(id, at) : at;
}

// Statements declaring `h`, a step of `spacing` pixels in uv units.
QString stepUv(EmitCtx &c)
{
    const QString s = c.in("spacing");
    return QStringLiteral("vec2 h = max(%1, 0.01) / %2;").arg(s, c.res());
}

QString pick(const QMap<QString, QString> &e, const QString &key, const char *fallback)
{
    return e.contains(key) ? e.value(key) : e.value(QString::fromLatin1(fallback));
}

}

void registerMathNodes()
{
    using Stage = const StageReader &;
    using Q = QString;

    NodeDef math = node("math", "Math", "math", "Combine two numbers.");
    math.inputs = {f("a", "A", 0), f("b", "B", 1)};
    math.options = {selectOpt("op", "Operation",
                              {choice("add", "A + B"), choice("sub", "A \xe2\x88\x92 B"), choice("mul", "A \xc3\x97 B"),
                               choice("div", "A \xc3\xb7 B"), choice("min", "Smaller"), choice("max", "Larger"),
                               choice("pow", "A to the power B"), choice("mod", "Remainder"),
                               choice("step", "1 if A \xe2\x89\xa5 B"), choice("atan2", "Angle of (A, B)"),
                               choice("hypot", "Length of (A, B)")},
                              "add")};
    math.outputs = {out("value", "Result", SocketType::Float)};
    math.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q a = c.in("a");
        const Q b = c.in("b");
        const QMap<Q, Q> e = {{"add", a + " + " + b},
                              {"sub", a + " - " + b},
                              {"mul", a + " * " + b},
                              {"div", a + " / (abs(" + b + ") < 1e-6 ? 1e-6 : " + b + ")"},
                              {"min", "min(" + a + ", " + b + ")"},
                              {"max", "max(" + a + ", " + b + ")"},
                              {"pow", "pow(abs(" + a + "), " + b + ")"},
                              {"mod", "mod(" + a + ", " + b + ")"},
                              {"step", "step(" + b + ", " + a + ")"},
                              {"atan2", "atan(" + b + ", " + a + ")"},
                              {"hypot", "length(vec2(" + a + ", " + b + "))"}};
        return {{"value", "return " + pick(e, c.optString("op"), "add") + ";"}};
    };

    NodeDef func = node("func", "Function", "math", "Apply sine, absolute value, one-minus and similar to a number.");
    func.inputs = {f("x", "X", 0)};
    func.options = {selectOpt("fn", "Function",
                              {choice("oneminus", "1 \xe2\x88\x92 X"), choice("neg", "\xe2\x88\x92X"), choice("abs", "Absolute"),
                               choice("sin", "Sine"), choice("cos", "Cosine"), choice("fract", "Fraction"),
                               choice("floor", "Round down"), choice("sqrt", "Square root"), choice("saturate", "Clamp 0\xe2\x80\x93" "1"),
                               choice("square", "X squared"), choice("tan", "Tangent"), choice("atan", "Arctangent"),
                               choice("tanh", "Tanh (soft clip)"), choice("exp", "e to the X"), choice("log", "Natural log"),
                               choice("sign", "Sign (\xe2\x88\x92" "1, 0, 1)"), choice("round", "Round"), choice("ceil", "Round up")},
                              "oneminus")};
    func.outputs = {out("value", "Result", SocketType::Float)};
    func.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q x = c.in("x");
        const QMap<Q, Q> e = {{"oneminus", "1.0 - " + x},
                              {"neg", "-" + x},
                              {"abs", "abs(" + x + ")"},
                              {"sin", "sin(" + x + ")"},
                              {"cos", "cos(" + x + ")"},
                              {"fract", "fract(" + x + ")"},
                              {"floor", "floor(" + x + ")"},
                              {"sqrt", "sqrt(max(" + x + ", 0.0))"},
                              {"saturate", "clamp(" + x + ", 0.0, 1.0)"},
                              {"square", x + " * " + x},
                              {"tan", "tan(" + x + ")"},
                              {"atan", "atan(" + x + ")"},
                              {"tanh", "tanh(" + x + ")"},
                              {"exp", "exp(min(" + x + ", 80.0))"},
                              {"log", "log(max(" + x + ", 1e-6))"},
                              {"sign", "sign(" + x + ")"},
                              {"round", "floor(" + x + " + 0.5)"},
                              {"ceil", "ceil(" + x + ")"}};
        return {{"value", "return " + pick(e, c.optString("fn"), "oneminus") + ";"}};
    };

    NodeDef remap = node("remap", "Remap", "math",
        "Convert a number from one range to another, e.g. 0\xe2\x80\x93" "1 into 10\xe2\x80\x93" "50.");
    remap.inputs = {f("x", "X", 0.5), f("inMin", "From low", 0), f("inMax", "From high", 1), f("outMin", "To low", 0),
                    f("outMax", "To high", 1)};
    remap.options = {toggleOpt("clamp", "Stay inside the range", true)};
    remap.outputs = {out("value", "Result", SocketType::Float)};
    remap.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q x = c.in("x");
        const Q inMin = c.in("inMin");
        const Q inMax = c.in("inMax");
        const Q t = "(" + x + " - " + inMin + ") / (" + inMax + " - " + inMin + " + 1e-6)";
        const Q tt = c.optBool("clamp") ? "clamp(" + t + ", 0.0, 1.0)" : t;
        const Q outMin = c.in("outMin");
        const Q outMax = c.in("outMax");
        return {{"value", QStringLiteral("float t = %1;\n    return mix(%2, %3, t);").arg(tt, outMin, outMax)}};
    };

    NodeDef smoothstepN = node("smoothstep", "Smooth step", "math", "0 below Low, 1 above High, smooth in between.");
    smoothstepN.inputs = {f("x", "X", 0.5), f("low", "Low", 0), f("high", "High", 1)};
    smoothstepN.outputs = {out("value", "Result", SocketType::Float)};
    smoothstepN.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q low = c.in("low");
        const Q high = c.in("high");
        const Q x = c.in("x");
        return {{"value", QStringLiteral("return smoothstep(%1, max(%2, %1 + 1e-5), %3);").arg(low, high, x)}};
    };

    NodeDef splitColor = node("split_color", "Split color", "math", "Pull out the red, green, blue, alpha and brightness of an image.");
    splitColor.inputs = {colorIn("color", "Image", rgba(0, 0, 0, 1))};
    splitColor.outputs = {out("r", "Red", SocketType::Float), out("g", "Green", SocketType::Float),
                          out("b", "Blue", SocketType::Float), out("a", "Alpha", SocketType::Float),
                          out("l", "Brightness", SocketType::Float)};
    splitColor.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("luma");
        const Q v = c.in("color");
        return {{"r", "return " + v + ".r;"}, {"g", "return " + v + ".g;"}, {"b", "return " + v + ".b;"},
                {"a", "return " + v + ".a;"}, {"l", "return luma(" + v + ");"}};
    };

    NodeDef combineColor = node("combine_color", "Combine color", "math", "Build a colour from separate red, green, blue and alpha values.");
    combineColor.inputs = {f("r", "Red", 0, 0, 1), f("g", "Green", 0, 0, 1), f("b", "Blue", 0, 0, 1), f("a", "Alpha", 1, 0, 1)};
    combineColor.outputs = {out("color", "Color", SocketType::Color)};
    combineColor.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q r = c.in("r");
        const Q g = c.in("g");
        const Q b = c.in("b");
        const Q a = c.in("a");
        return {{"color", QStringLiteral("return vec4(%1, %2, %3, %4);").arg(r, g, b, a)}};
    };

    NodeDef splitPoint = node("split_point", "Split point", "math", "Separate a point into X and Y.");
    splitPoint.inputs = {vecIn("p", "Point", 0, 0, 0, 0)};
    splitPoint.inputs[0].min.reset();
    splitPoint.inputs[0].max.reset();
    splitPoint.outputs = {out("x", "X", SocketType::Float), out("y", "Y", SocketType::Float)};
    splitPoint.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q p = c.in("p");
        return {{"x", "return " + p + ".x;"}, {"y", "return " + p + ".y;"}};
    };

    NodeDef combinePoint = node("combine_point", "Combine point", "math", "Make a point from X and Y.");
    combinePoint.inputs = {f("x", "X", 0.5, 0, 1), f("y", "Y", 0.5, 0, 1)};
    combinePoint.outputs = {out("p", "Point", SocketType::Vec2)};
    combinePoint.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q x = c.in("x");
        const Q y = c.in("y");
        return {{"p", "return vec2(" + x + ", " + y + ");"}};
    };

    NodeDef mixValue = node("mix_value", "Mix numbers", "math", "Blend between A and B: A at 0, B at 1, halfway at 0.5.");
    mixValue.inputs = {f("a", "A", 0), f("b", "B", 1), f("t", "Amount", 0.5, 0, 1)};
    mixValue.outputs = {out("value", "Result", SocketType::Float)};
    mixValue.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q a = c.in("a");
        const Q b = c.in("b");
        const Q t = c.in("t");
        return {{"value", "return mix(" + a + ", " + b + ", " + t + ");"}};
    };

    NodeDef vectorMath = node("vector_math", "Vector math", "math", "Add, subtract, scale, rotate, reflect or project points and directions.");
    vectorMath.inputs = {v("a", "A", 0, 0), v("b", "B", 0, 0), f("s", "Amount", 1)};
    vectorMath.options = {selectOpt("op", "Operation",
                                    {choice("add", "A + B"), choice("sub", "A \xe2\x88\x92 B"),
                                     choice("mul", "A \xc3\x97 B (each axis)"), choice("div", "A \xc3\xb7 B (each axis)"),
                                     choice("scale", "A \xc3\x97 Amount"), choice("rotate", "Rotate A by Amount (degrees)"),
                                     choice("normalize", "Direction of A (length 1)"), choice("perp", "A turned 90\xc2\xb0"),
                                     choice("reflect", "Reflect A off a surface facing B"), choice("project", "Project A onto B"),
                                     choice("lerp", "Blend A to B by Amount"), choice("min", "Smaller of each axis"),
                                     choice("max", "Larger of each axis")},
                                    "add")};
    vectorMath.outputs = {out("p", "Result", SocketType::Vec2)};
    vectorMath.emitFn = [](EmitCtx &c, Stage) -> Out {
        c.helper("rot2");
        const Q a = c.in("a");
        const Q b = c.in("b");
        const Q s = c.in("s");
        const QMap<Q, Q> e = {
            {"add", a + " + " + b},
            {"sub", a + " - " + b},
            {"mul", a + " * " + b},
            {"div", a + " / mix(" + b + ", vec2(1e-6), vec2(lessThan(abs(" + b + "), vec2(1e-6))))"},
            {"scale", a + " * " + s},
            {"rotate", "rot2(radians(" + s + ")) * " + a},
            {"normalize", a + " / max(length(" + a + "), 1e-6)"},
            {"perp", "vec2(-" + a + ".y, " + a + ".x)"},
            {"reflect", "reflect(" + a + ", " + b + " / max(length(" + b + "), 1e-6))"},
            {"project", b + " * dot(" + a + ", " + b + ") / max(dot(" + b + ", " + b + "), 1e-6)"},
            {"lerp", "mix(" + a + ", " + b + ", " + s + ")"},
            {"min", "min(" + a + ", " + b + ")"},
            {"max", "max(" + a + ", " + b + ")"},
        };
        return {{"p", "return " + pick(e, c.optString("op"), "add") + ";"}};
    };

    NodeDef vectorMeasure = node("vector_measure", "Vector measure", "math", "Lengths, distances, angles and dot / cross products of two vectors.");
    vectorMeasure.inputs = {v("a", "A", 1, 0), v("b", "B", 0, 1)};
    vectorMeasure.outputs = {out("length", "Length of A", SocketType::Float), out("distance", "Distance A to B", SocketType::Float),
                             out("dot", "Dot product", SocketType::Float), out("cross", "Cross product", SocketType::Float),
                             out("angle", "Angle of A (\xc2\xb0)", SocketType::Float),
                             out("between", "Angle A to B (\xc2\xb0)", SocketType::Float)};
    vectorMeasure.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q a = c.in("a");
        const Q b = c.in("b");
        return {{"length", "return length(" + a + ");"},
                {"distance", "return distance(" + a + ", " + b + ");"},
                {"dot", "return dot(" + a + ", " + b + ");"},
                {"cross", "vec2 a = " + a + ";\n    vec2 b = " + b + ";\n    return a.x * b.y - a.y * b.x;"},
                {"angle", "vec2 a = " + a + ";\n    return degrees(atan(a.y, a.x));"},
                {"between", "vec2 a = " + a + ";\n    vec2 b = " + b +
                                ";\n    return degrees(atan(a.x * b.y - a.y * b.x, dot(a, b)));"}};
    };

    NodeDef linear = node("linear_transform", "Linear transform", "math",
        "Multiply a point by a 2\xc3\x97" "2 matrix [[a, b], [c, d]] around an origin, then move it.");
    linear.inputs = {hint(v("p", "Point", 0.5, 0.5, 0, 1), fieldHint),
                     f("m00", "a (x from x)", 1, -4, 4),
                     f("m01", "b (x from y)", 0, -4, 4),
                     f("m10", "c (y from x)", 0, -4, 4),
                     f("m11", "d (y from y)", 1, -4, 4),
                     v("offset", "Then move by", 0, 0, -1, 1),
                     widget(v("origin", "Origin", 0.5, 0.5, 0, 1), "point")};
    linear.options = {toggleOpt("inverse", "Apply the inverse matrix", false),
                      toggleOpt("square", "Square pixels (correct for aspect)", true)};
    linear.outputs = {out("p", "Point", SocketType::Vec2), out("det", "Determinant", SocketType::Float)};
    linear.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q m00 = c.in("m00");
        const Q m10 = c.in("m10");
        const Q m01 = c.in("m01");
        const Q m11 = c.in("m11");
        const Q m = "mat2(" + m00 + ", " + m10 + ", " + m01 + ", " + m11 + ")";
        const bool sq = c.optBool("square");
        const bool inv = c.optBool("inverse");
        const Q invText = inv ? QStringLiteral("float det = determinant(M);\n    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);")
                              : QString();
        const Q asp = sq ? c.aspect() : QStringLiteral("1.0");
        const Q origin = c.in("origin");
        const Q p = field(c, "p");
        const Q offset = c.in("offset");
        return {{"p", QStringLiteral(R"(mat2 M = %1;
    %2
    float asp = %3;
    vec2 o = %4;
    vec2 q = (%5 - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + %6;)").arg(m, invText, asp, origin, p, offset)},
                {"det", "return determinant(" + m + ");"}};
    };

    NodeDef warp = node("matrix_warp", "Matrix warp", "distort",
        "Shear, stretch, rotate or flip the picture with a 2\xc3\x97" "2 matrix [[a, b], [c, d]].");
    warp.inputs = {imageIn(),
                   f("m00", "a (x from x)", 1, -4, 4),
                   f("m01", "b (x from y)", 0.3, -4, 4),
                   f("m10", "c (y from x)", 0, -4, 4),
                   f("m11", "d (y from y)", 1, -4, 4),
                   v("offset", "Move", 0, 0, -1, 1),
                   widget(v("origin", "Origin", 0.5, 0.5, 0, 1), "point")};
    warp.options = {toggleOpt("transparent", "Transparent outside the frame", true)};
    warp.outputs = {out("image", "Image", SocketType::Color)};
    warp.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q outside = c.optBool("transparent")
            ? QStringLiteral("if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);\n    ")
            : QString();
        const Q m00 = c.in("m00");
        const Q m10 = c.in("m10");
        const Q m01 = c.in("m01");
        const Q m11 = c.in("m11");
        const Q asp = c.aspect();
        const Q origin = c.in("origin");
        const Q offset = c.in("offset");
        const Q image = c.in("image", "clamp(p, 0.0, 1.0)");
        return {{"image", QStringLiteral(R"(mat2 M = mat2(%1, %2, %3, %4);
    float det = determinant(M);
    mat2 inv = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = %5;
    vec2 o = %6;
    vec2 p = (inv * ((uv - o - %7) * vec2(asp, 1.0))) / vec2(asp, 1.0) + o;
    %8return %9;)").arg(m00, m10, m01, m11, asp, origin, offset, outside, image)}};
    };

    NodeDef sampleAt = node("sample_at", "Sample at position", "math",
        "Read an image at any position, e.g. one computed by Linear transform, Vector math or Complex math.");
    sampleAt.inputs = {imageIn(), hint(v("p", "Position", 0.5, 0.5, 0, 1), fieldHint)};
    sampleAt.options = {selectOpt("edge", "Outside the frame",
                                  {choice("clamp", "Stretch edges"), choice("transparent", "Transparent"),
                                   choice("repeat", "Repeat"), choice("mirror", "Mirror")},
                                  "clamp")};
    sampleAt.outputs = {out("image", "Image", SocketType::Color)};
    sampleAt.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q clampIn = c.in("image", "clamp(p, 0.0, 1.0)");
        const Q transparentIn = c.in("image", "p");
        const Q repeatIn = c.in("image", "fract(p)");
        const Q mirrorIn = c.in("image", "1.0 - abs(mod(p, 2.0) - 1.0)");
        const QMap<Q, Q> at = {
            {"clamp", "return " + clampIn + ";"},
            {"transparent", "if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);\n    return " + transparentIn + ";"},
            {"repeat", "return " + repeatIn + ";"},
            {"mirror", "return " + mirrorIn + ";"},
        };
        const Q p = field(c, "p");
        return {{"image", "vec2 p = " + p + ";\n    " + pick(at, c.optString("edge"), "clamp")}};
    };

    NodeDef complex = node("complex_math", "Complex math", "math",
        "Treat points as complex numbers (x + iy) around the centre of the frame. Squaring, inverting and exponentials make conformal warps when fed into Sample at position.");
    complex.inputs = {hint(v("z", "Z", 0.5, 0.5, 0, 1), fieldHint), v("w", "W", 1, 0), f("n", "Power", 2, -8, 8),
                      f("zoom", "Zoom", 2, 0.1, 10)};
    complex.options = {selectOpt("op", "Operation",
                                 {choice("mul", "Z \xc3\x97 W"), choice("div", "Z \xc3\xb7 W"), choice("pow", "Z to the Power"),
                                  choice("inv", "1 \xc3\xb7 Z"), choice("conj", "Conjugate"), choice("exp", "e to the Z"),
                                  choice("log", "Natural log"), choice("sqrt", "Square root"), choice("sin", "Sine"),
                                  choice("mobius", "(Z \xe2\x88\x92 W) \xc3\xb7 (1 \xe2\x88\x92 conj(W)\xc2\xb7Z)")},
                                 "pow")};
    complex.outputs = {out("p", "Position", SocketType::Vec2), out("abs", "Magnitude", SocketType::Float),
                       out("arg", "Angle (\xc2\xb0)", SocketType::Float)};
    complex.emitFn = [](EmitCtx &c, Stage) -> Out {
        const Q pre = c.prefix;
        c.declare("vec2 " + pre + "cmul(vec2 a, vec2 b) { return vec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x); }\n"
                  "vec2 " + pre + "cdiv(vec2 a, vec2 b) { float d = max(dot(b, b), 1e-8); return vec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / d; }");
        const QMap<Q, Q> e = {
            {"mul", pre + "cmul(z, w)"},
            {"div", pre + "cdiv(z, w)"},
            {"pow", "pow(max(length(z), 1e-8), n) * vec2(cos(n * atan(z.y, z.x)), sin(n * atan(z.y, z.x)))"},
            {"inv", pre + "cdiv(vec2(1.0, 0.0), z)"},
            {"conj", "vec2(z.x, -z.y)"},
            {"exp", "exp(min(z.x, 80.0)) * vec2(cos(z.y), sin(z.y))"},
            {"log", "vec2(log(max(length(z), 1e-8)), atan(z.y, z.x))"},
            {"sqrt", "sqrt(length(z)) * vec2(cos(0.5 * atan(z.y, z.x)), sin(0.5 * atan(z.y, z.x)))"},
            {"sin", "vec2(sin(z.x) * cosh(clamp(z.y, -80.0, 80.0)), cos(z.x) * sinh(clamp(z.y, -80.0, 80.0)))"},
            {"mobius", pre + "cdiv(z - w, vec2(1.0, 0.0) - " + pre + "cmul(vec2(w.x, -w.y), z))"},
        };
        const Q asp = c.aspect();
        const Q zoom = c.in("zoom");
        const Q z = field(c, "z");
        const Q w = c.in("w");
        const Q n = c.in("n");
        const Q body = QStringLiteral(R"(float asp = %1;
    vec2 sc = vec2(asp, -1.0) * %2;
    vec2 z = (%3 - 0.5) * sc;
    vec2 w = %4;
    float n = %5;
    vec2 r = %6;)").arg(asp, zoom, z, w, n, pick(e, c.optString("op"), "pow"));
        return {{"p", body + "\n    return r / sc + 0.5;"},
                {"abs", body + "\n    return length(r);"},
                {"arg", body + "\n    return degrees(atan(r.y, r.x));"}};
    };

    NodeDef derivative = node("derivative", "Derivative", "math",
        "How fast a value changes across the frame: its slope along X and Y, the gradient direction, and the Laplacian (curvature). Measured per frame width / height.");
    derivative.inputs = {f("x", "Value", 0), spacing()};
    derivative.outputs = {out("dx", "\xe2\x88\x82/\xe2\x88\x82x", SocketType::Float), out("dy", "\xe2\x88\x82/\xe2\x88\x82y", SocketType::Float),
                          out("grad", "Gradient", SocketType::Vec2), out("slope", "Steepness", SocketType::Float),
                          out("lap", "Laplacian", SocketType::Float)};
    derivative.emitFn = [](EmitCtx &c, Stage) -> Out {
        const auto at = [&c](const char *o) { return c.in("x", "uv + " + QString::fromLatin1(o)); };
        const Q step = stepUv(c);
        const Q xp = at("vec2(h.x, 0.0)");
        const Q xm = at("vec2(-h.x, 0.0)");
        const Q yp = at("vec2(0.0, h.y)");
        const Q ym = at("vec2(0.0, -h.y)");
        const Q grad = step + "\n    vec2 g = vec2(" + xp + " - " + xm + ", " + yp + " - " + ym + ") / (2.0 * h);";
        const Q c0 = c.in("x");
        const Q lap = step + "\n    float c0 = " + c0 + ";\n    return (" + xp + " + " + xm + " - 2.0 * c0) / (h.x * h.x)\n         + (" +
                      yp + " + " + ym + " - 2.0 * c0) / (h.y * h.y);";
        return {{"dx", grad + "\n    return g.x;"},
                {"dy", grad + "\n    return g.y;"},
                {"grad", grad + "\n    return g;"},
                {"slope", grad + "\n    return length(g);"},
                {"lap", lap}};
    };

    NodeDef jacobian = node("jacobian", "Jacobian", "math",
        "For a position field (e.g. a warp), how it stretches and turns space at each pixel: the partial derivatives, determinant (area change), divergence and curl.");
    jacobian.inputs = {hint(v("p", "Field", 0.5, 0.5, 0, 1), fieldHint), spacing()};
    jacobian.outputs = {out("ddx", "\xe2\x88\x82/\xe2\x88\x82x", SocketType::Vec2), out("ddy", "\xe2\x88\x82/\xe2\x88\x82y", SocketType::Vec2),
                        out("det", "Determinant", SocketType::Float), out("div", "Divergence", SocketType::Float),
                        out("curl", "Curl", SocketType::Float)};
    jacobian.emitFn = [](EmitCtx &c, Stage) -> Out {
        const auto at = [&c](const char *o) { return field(c, "p", "(uv + " + QString::fromLatin1(o) + ")"); };
        const Q step = stepUv(c);
        const Q xp = at("vec2(h.x, 0.0)");
        const Q xm = at("vec2(-h.x, 0.0)");
        const Q yp = at("vec2(0.0, h.y)");
        const Q ym = at("vec2(0.0, -h.y)");
        const Q J = step + "\n    vec2 jx = (" + xp + " - " + xm + ") / (2.0 * h.x);\n    vec2 jy = (" + yp + " - " + ym + ") / (2.0 * h.y);";
        return {{"ddx", J + "\n    return jx;"},
                {"ddy", J + "\n    return jy;"},
                {"det", J + "\n    return jx.x * jy.y - jy.x * jx.y;"},
                {"div", J + "\n    return jx.x + jy.y;"},
                {"curl", J + "\n    return jx.y - jy.x;"}};
    };

    registerNodeDefs({math, func, remap, smoothstepN, splitColor, combineColor, splitPoint, combinePoint, mixValue, vectorMath,
                      vectorMeasure, linear, warp, sampleAt, complex, derivative, jacobian});
}

}
