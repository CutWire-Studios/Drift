#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

void registerAdvancedNodes()
{
    using namespace build;
    using Q = QString;

    NodeDef custom = node("custom", "Custom GLSL", "advanced", "Write your own shader code. For people who know GLSL.");
    custom.inputs = {colorIn("in1", "Image 1", rgba(0, 0, 0, 1)), colorIn("in2", "Image 2", rgba(0, 0, 0, 1)),
                     floatIn("a", "a", 0.5, 0, 1), floatIn("b", "b", 0, 0, 1), floatIn("c", "c", 0, 0, 1),
                     floatIn("d", "d", 0, 0, 1)};
    {
        OptionDef code;
        code.id = QStringLiteral("code");
        code.label = QStringLiteral("Code");
        code.kind = OptionKind::Code;
        code.defaultValue = QStringLiteral(R"(// Write GLSL that returns a vec4 colour.
// Available: uv (vec2, 0..1, top-left origin), IN1(p) and IN2(p) sample the
// two image inputs at any position, a b c d are the number inputs,
// RESOLUTION is the frame size in pixels, and TIME (effects) or
// PROGRESS (transitions) is the clock.
vec4 col = IN1(uv);
col.rgb = mix(col.rgb, col.bgr, a);
return col;)");
        custom.options = {code, toggleOpt("heavy", "Samples the inputs many times (render them to a buffer first)", false)};
    }
    custom.heavyInputs = [](const std::function<QJsonValue(const QString &)> &opt) -> QStringList {
        return opt(QStringLiteral("heavy")).toBool() ? QStringList{QStringLiteral("in1"), QStringLiteral("in2")} : QStringList{};
    };
    custom.outputs = {out("image", "Image", SocketType::Color)};
    custom.emitFn = [](EmitCtx &c, const StageReader &) -> Out {
        const Q p = c.prefix;
        const Q in1 = c.in("in1", "p");
        const Q in2 = c.in("in2", "p");
        c.declare("vec4 " + p + "_in1(vec2 p) { return " + in1 + "; }\nvec4 " + p + "_in2(vec2 p) { return " + in2 + "; }");
        const bool effect = c.kind == Kind::Effect;
        const Q clock = effect ? "#define TIME " + c.time() : "#define PROGRESS " + c.progress();
        const Q a = c.in("a");
        const Q b = c.in("b");
        const Q cc = c.in("c");
        const Q d = c.in("d");
        const Q res = c.res();
        const Q code = c.optString("code");
        return {{"image", "float a = " + a + ";\n    float b = " + b + ";\n    float c = " + cc + ";\n    float d = " + d
                              + ";\n#define IN1 " + p + "_in1\n#define IN2 " + p + "_in2\n#define RESOLUTION " + res + "\n"
                              + clock + "\n" + code + "\n#undef IN1\n#undef IN2\n#undef RESOLUTION\n#undef "
                              + (effect ? "TIME" : "PROGRESS")}};
    };

    registerNodeDefs({custom});
}

}
