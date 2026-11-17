#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

namespace {

NodeDef input(const char *type, const char *label, const char *description, QList<Kind> kinds = {})
{
    NodeDef d;
    d.type = QLatin1String(type);
    d.label = QLatin1String(label);
    d.category = QStringLiteral("input");
    d.description = QLatin1String(description);
    d.kinds = std::move(kinds);
    d.cheap = true;
    return d;
}

OutputDef out(const char *id, const char *label, SocketType t)
{
    return {QLatin1String(id), QLatin1String(label), t};
}

InputDef in(const char *id, const char *label, SocketType t, QJsonValue def, double min, double max, const char *widget = "")
{
    InputDef i;
    i.id = QLatin1String(id);
    i.label = QLatin1String(label);
    i.type = t;
    i.defaultValue = def;
    i.min = min;
    i.max = max;
    i.widget = QLatin1String(widget);
    return i;
}

}

void registerInputNodes()
{
    using K = Kind;
    NodeDef video = input("video", "Video", "The clip this effect or animation is applied to.", {K::Effect, K::Animation});
    video.outputs = {out("image", "Image", SocketType::Color)};

    NodeDef from = input("from", "From clip", "The outgoing clip.", {K::Transition});
    from.outputs = {out("image", "Image", SocketType::Color)};

    NodeDef to = input("to", "To clip", "The incoming clip.", {K::Transition});
    to.outputs = {out("image", "Image", SocketType::Color)};

    NodeDef progress = input("progress", "Progress",
        "How far through the transition or animation we are: 0 at the start, 1 at the end.", {K::Transition, K::Animation});
    progress.outputs = {out("value", "Progress", SocketType::Float)};

    NodeDef direction = input("direction", "Direction",
        "Whether the animation plays in (0) or out (1). Settled is 1 when the clip is fully shown, whichever way it plays. Inputs that follow progress already use it.",
        {K::Animation});
    direction.outputs = {out("value", "In 0 / Out 1", SocketType::Float), out("settled", "Settled", SocketType::Float)};

    NodeDef time = input("time", "Time", "Seconds since the start of the clip.", {K::Effect});
    time.inputs = {in("speed", "Speed", SocketType::Float, 1, 0, 10)};
    time.outputs = {out("value", "Seconds", SocketType::Float)};

    NodeDef uv = input("uv", "Position",
        "Where on screen the current pixel is. 0,0 is the top-left corner, 1,1 the bottom-right.");
    uv.outputs = {out("uv", "Position", SocketType::Vec2), out("x", "X", SocketType::Float), out("y", "Y", SocketType::Float),
                  out("dist", "Distance from centre", SocketType::Float)};

    NodeDef resolution = input("resolution", "Frame size", "Size of the frame in pixels, and its aspect ratio.");
    resolution.outputs = {out("size", "Size (px)", SocketType::Vec2), out("aspect", "Aspect", SocketType::Float)};

    NodeDef number = input("number", "Number",
        "A single value. Expose it as a slider to reuse one control across many nodes.");
    number.inputs = {in("value", "Value", SocketType::Float, 0.5, 0, 1)};
    number.outputs = {out("value", "Value", SocketType::Float)};

    NodeDef sw = input("switch_value", "Switch", "An on/off value (1 or 0). Expose it to give users a toggle.");
    sw.inputs = {in("on", "On", SocketType::Float, 1, 0, 1, "toggle")};
    sw.outputs = {out("value", "Value", SocketType::Float)};

    NodeDef color = input("color_value", "Color", "A solid colour.");
    InputDef c = in("color", "Color", SocketType::Color, QJsonArray{1, 0.76, 0.03, 1}, 0, 1, "swatch");
    c.min.reset();
    c.max.reset();
    color.inputs = {c};
    color.outputs = {out("color", "Color", SocketType::Color)};

    NodeDef point = input("point_value", "Point", "A position on screen.");
    point.inputs = {in("point", "Point", SocketType::Vec2, QJsonArray{0.5, 0.5}, 0, 1, "point")};
    point.outputs = {out("point", "Point", SocketType::Vec2)};

    NodeDef image = input("image", "Image", "A picture you upload: an overlay, a texture, a logo.");
    image.inputs = {in("scale", "Repeat", SocketType::Float, 1, 0.1, 20)};
    OptionDef asset;
    asset.id = QStringLiteral("asset");
    asset.label = QStringLiteral("Picture");
    asset.kind = OptionKind::Asset;
    image.options = {asset};
    image.outputs = {out("image", "Image", SocketType::Color), out("alpha", "Alpha", SocketType::Float)};

    using build::Out;
    using Stage = const StageReader &;
    video.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"image", "return " + c.source(0, "uv") + ";"}}; };
    from.emitFn = video.emitFn;
    to.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"image", "return " + c.source(1, "uv") + ";"}}; };
    progress.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"value", "return " + c.progress() + ";"}}; };
    direction.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString d = c.engine("u_direction");
        const QString a = c.progress();
        const QString b = c.progress();
        return {{"value", "return " + d + ";"}, {"settled", "return mix(" + a + ", 1.0 - " + b + ", " + d + ");"}};
    };
    time.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString t = c.time();
        return {{"value", "return " + t + " * " + c.in("speed") + ";"}};
    };
    uv.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString r = c.res();
        return {{"uv", "return uv;"},
                {"x", "return uv.x;"},
                {"y", "return uv.y;"},
                {"dist", "vec2 d = (uv - 0.5) * vec2(" + r + ".x / max(" + r + ".y, 1.0), 1.0);\n    return length(d) * 2.0;"}};
    };
    resolution.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString size = "return " + c.res() + ";";
        return {{"size", size}, {"aspect", "return " + c.aspect() + ";"}};
    };
    number.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"value", "return " + c.in("value") + ";"}}; };
    sw.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"value", "return " + c.in("on") + " > 0.5 ? 1.0 : 0.0;"}}; };
    color.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"color", "return " + c.in("color") + ";"}}; };
    point.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"point", "return " + c.in("point") + ";"}}; };
    image.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString scale = c.in("scale");
        const QString a = c.asset("uv * " + scale);
        const QString b = c.asset("uv * " + scale);
        return {{"image", "return " + a + ";"}, {"alpha", "return " + b + ".a;"}};
    };

    registerNodeDefs({video, from, to, progress, direction, time, uv, resolution, number, sw, color, point, image});
}

}
