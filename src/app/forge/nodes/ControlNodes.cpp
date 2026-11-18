#include "app/forge/glsl/Literals.h"
#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

void registerControlNodes()
{
    using namespace build;
    using Stage = const StageReader &;

    NodeDef choiceNode = node("choice", "Dropdown", "input",
        "A list of named choices. Outputs 0 for the first, 1 for the second\xe2\x80\xa6 Wire it into Pick by choice.");
    choiceNode.cheap = true;
    choiceNode.inputs = {integer(widget(floatIn("value", "Choice", 0, 0, 7), "choice"))};
    {
        OptionDef labels;
        labels.id = QStringLiteral("labels");
        labels.label = QStringLiteral("Choices");
        labels.kind = OptionKind::Labels;
        labels.defaultValue = QJsonArray{QStringLiteral("First"), QStringLiteral("Second"), QStringLiteral("Third")};
        choiceNode.options = {labels};
    }
    choiceNode.outputs = {out("value", "Index", SocketType::Float)};
    choiceNode.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"value", "return floor(" + c.in("value") + " + 0.5);"}}; };

    NodeDef pick = node("pick", "Pick by choice", "mix",
        "Shows one of up to four images, chosen by a number: 0 shows the first, 1 the second\xe2\x80\xa6");
    pick.inputs = {integer(floatIn("which", "Choice", 0, 0, 3)), colorIn("a", "Choice 0", rgba(0, 0, 0, 1)),
                   colorIn("b", "Choice 1", rgba(0, 0, 0, 1)), colorIn("c", "Choice 2", rgba(0, 0, 0, 1)),
                   colorIn("d", "Choice 3", rgba(0, 0, 0, 1))};
    pick.outputs = {out("image", "Image", SocketType::Color)};
    pick.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString which = c.in("which");
        const QString a = c.in("a");
        const QString b = c.in("b");
        const QString cc = c.in("c");
        const QString d = c.in("d");
        return {{"image", QStringLiteral(R"(float w = floor(%1 + 0.5);
    if (w < 0.5) return %2;
    if (w < 1.5) return %3;
    if (w < 2.5) return %4;
    return %5;)").arg(which, a, b, cc, d)}};
    };

    NodeDef region = node("region", "Region", "input",
        "A box or oval on the frame. Use the mask to blur a face, spotlight something, or limit any effect to one area.");
    region.inputs = {floatIn("softness", "Edge softness", 0.02, 0, 0.3)};
    {
        OptionDef rect;
        rect.id = QStringLiteral("rect");
        rect.label = QStringLiteral("Area");
        rect.kind = OptionKind::Region;
        rect.defaultValue = QJsonArray{0.3, 0.25, 0.4, 0.5};
        region.options = {rect, selectOpt("shape", "Shape", {choice("rect", "Box"), choice("ellipse", "Oval")}, "ellipse"),
                          toggleOpt("invert", "Everything outside", false)};
    }
    region.outputs = {out("mask", "Mask", SocketType::Float), out("center", "Centre", SocketType::Vec2),
                      out("size", "Size", SocketType::Vec2)};
    region.emitFn = [](EmitCtx &c, Stage) -> Out {
        const std::optional<QString> exposed = c.optParam("rect");
        const QJsonValue r = c.opt("rect");
        QString rect;
        if (exposed) {
            rect = QStringLiteral("pv_%1()").arg(*exposed);
        } else {
            const QJsonArray lit = r.isArray() ? r.toArray() : QJsonArray{0.3, 0.25, 0.4, 0.5};
            QStringList parts;
            for (const QJsonValue &v : lit)
                parts << glslFloat(v.toDouble());
            rect = QStringLiteral("vec4(%1)").arg(parts.join(QStringLiteral(", ")));
        }
        const bool ellipse = c.optString("shape") != QLatin1String("rect");
        const QString sd = ellipse ? QStringLiteral("float sd = (length((uv - ctr) / half2) - 1.0) * min(half2.x, half2.y);")
                                   : QStringLiteral("vec2 q = abs(uv - ctr) - half2;\n    float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);");
        const QString soft = c.in("softness");
        const QString ret = c.optBool("invert") ? QStringLiteral("1.0 - m") : QStringLiteral("m");
        return {{"mask", QStringLiteral(R"(vec4 rg = %1;
    vec2 half2 = max(rg.zw * 0.5, vec2(0.0001));
    vec2 ctr = rg.xy + half2;
    %2
    float soft = max(%3, 0.0005);
    float m = 1.0 - smoothstep(-soft, soft, sd);
    return %4;)").arg(rect, sd, soft, ret)},
                {"center", QStringLiteral("vec4 rg = %1;\n    return rg.xy + rg.zw * 0.5;").arg(rect)},
                {"size", QStringLiteral("vec4 rg = %1;\n    return rg.zw;").arg(rect)}};
    };

    NodeDef seed = node("seed", "Random seed", "input",
        "A number that reshuffles randomness. Expose it so each clip can get its own random pattern.");
    seed.cheap = true;
    seed.inputs = {widget(floatIn("seed", "Seed", 0, 0, 1000), "seed")};
    seed.outputs = {out("value", "Seed", SocketType::Float)};
    seed.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"value", "return " + c.in("seed") + ";"}}; };

    NodeDef otherClip = node("other_clip", "Other track", "input",
        "Another track from the timeline, chosen in Drift. Shows what that track plays at this moment, so the effect can use a whole track. For track mattes, luma mattes and displacing one track with another.");
    otherClip.next = true;
    otherClip.cheap = true;
    otherClip.outputs = {out("image", "Image", SocketType::Color)};
    otherClip.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"image", "return " + c.paramSampler("clip", "uv") + ";"}}; };

    NodeDef trackMask = node("track_mask", "Track mask", "input",
        "The masks drawn on a track in Drift, read at this moment. 1 inside, 0 outside, as a matte for any effect.");
    trackMask.kinds = {Kind::Effect};
    trackMask.next = true;
    trackMask.cheap = true;
    trackMask.outputs = {out("mask", "Mask", SocketType::Float)};
    trackMask.emitFn = [](EmitCtx &c, Stage) -> Out { return {{"mask", "return " + c.paramSampler("mask", "uv") + ".r;"}}; };

    NodeDef audioTrack = node("audio_track", "Audio track", "input",
        "The sound of an audio track in Drift, or the whole mix. Spectrum runs from low to high pitch across Position, 0 to 1. Wave is the waveform across Position, -1 to 1. Level, bass and beat follow the loudness and the beats, 0 to 1.");
    audioTrack.kinds = {Kind::Effect};
    audioTrack.next = true;
    audioTrack.cheap = true;
    audioTrack.inputs = {floatIn("x", "Position", 0.5, 0, 1)};
    audioTrack.outputs = {out("spectrum", "Spectrum", SocketType::Float), out("wave", "Wave", SocketType::Float),
                          out("level", "Loudness", SocketType::Float), out("bass", "Bass", SocketType::Float),
                          out("beat", "Beat pulse", SocketType::Float)};
    audioTrack.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString audio = c.paramName("audio");
        const QString x = c.in("x");
        const QString spectrum = "return " + c.paramSampler("audio", "vec2(" + x + ", 0.25)") + ".r;";
        const QString wave = "return " + c.paramSampler("audio", "vec2(" + x + ", 0.75)") + ".r * 2.0 - 1.0;";
        const QString level = "return " + c.engine(audio + "_level") + ";";
        const QString bass = "return " + c.engine(audio + "_bass") + ";";
        const QString beat = "return " + c.engine(audio + "_beat") + ";";
        return {{"spectrum", spectrum}, {"wave", wave}, {"level", level}, {"bass", bass}, {"beat", beat}};
    };

    NodeDef depth = node("depth", "Depth", "input",
        "How near each part of the clip is to the camera, from the depth estimate in Drift. Depth runs 0 far to 1 near. Normal is the surface direction as a colour (x, y, z times 0.5 plus 0.5). Has depth is 1 when the clip has an estimate.");
    depth.kinds = {Kind::Effect};
    depth.next = true;
    depth.inputs = {floatIn("strength", "Relief", 1, 0, 4)};
    depth.outputs = {out("depth", "Depth", SocketType::Float), out("normal", "Normal", SocketType::Color),
                     out("has", "Has depth", SocketType::Float)};
    depth.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString d = "return " + c.depth("uv") + ";";
        const QString strength = c.in("strength");
        const QString n = "return vec4(" + c.depthNormal("uv", strength) + " * 0.5 + 0.5, 1.0);";
        return {{"depth", d}, {"normal", n}, {"has", "return " + c.hasDepth() + ";"}};
    };

    NodeDef face = node("face", "Face", "input",
        "Where the tracked face sits on the clip, from Drift's face tracking. Positions are on the frame, 0 to 1; Face picks which person when there are several. Where there is no face, the frame passes through unchanged.");
    face.kinds = {Kind::Effect};
    face.next = true;
    face.cheap = true;
    face.outputs = {out("leftEye", "Left eye", SocketType::Vec2), out("rightEye", "Right eye", SocketType::Vec2),
                    out("nose", "Nose", SocketType::Vec2), out("mouth", "Mouth", SocketType::Vec2),
                    out("chin", "Chin", SocketType::Vec2), out("forehead", "Forehead", SocketType::Vec2),
                    out("center", "Centre", SocketType::Vec2), out("angle", "Tilt", SocketType::Float),
                    out("rx", "Half width", SocketType::Float), out("ry", "Half height", SocketType::Float),
                    out("valid", "Has a face", SocketType::Float)};
    face.emitFn = [](EmitCtx &c, Stage) -> Out {
        const auto at = [&c](const QString &name) {
            const QString x = c.face(name + "X");
            const QString y = c.face(name + "Y");
            return "vec2(" + x + ", " + y + ")";
        };
        Out o;
        o["leftEye"] = "return " + at("u_faceLeftEye") + ";";
        o["rightEye"] = "return " + at("u_faceRightEye") + ";";
        o["nose"] = "return " + at("u_faceNose") + ";";
        o["mouth"] = "return " + at("u_faceMouth") + ";";
        o["chin"] = "return " + at("u_faceChin") + ";";
        o["forehead"] = "return " + at("u_faceForehead") + ";";
        o["center"] = "return " + at("u_faceCenter") + ";";
        o["angle"] = "return " + c.face("u_faceAngle") + ";";
        o["rx"] = "return " + c.face("u_faceRx") + ";";
        o["ry"] = "return " + c.face("u_faceRy") + ";";
        o["valid"] = "return " + c.face("u_faceValid") + ";";
        return o;
    };

    NodeDef clipMask = node("clip_mask", "Clip mask", "input",
        "The masks on the clip in Drift, such as Cut out subject: 1 inside, 0 outside. The effect takes the mask over, so Drift stops cutting the clip out with it. In a transition, the outgoing clip's mask.");
    clipMask.next = true;
    clipMask.cheap = true;
    clipMask.outputs = {out("mask", "Mask", SocketType::Float), out("has", "Has a mask", SocketType::Float)};
    clipMask.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString m = "return " + c.mask("uv") + ";";
        return {{"mask", m}, {"has", "return " + c.hasMask() + ";"}};
    };

    NodeDef audio = node("audio", "Music", "input",
        "Follows the music: overall loudness, bass, and a pulse on every beat. Each runs 0 to 1.");
    audio.next = true;
    audio.cheap = true;
    audio.outputs = {out("level", "Loudness", SocketType::Float), out("bass", "Bass", SocketType::Float),
                     out("beat", "Beat pulse", SocketType::Float)};
    audio.emitFn = [](EmitCtx &c, Stage) -> Out {
        const QString level = "return " + c.engine("u_audioLevel") + ";";
        const QString bass = "return " + c.engine("u_audioBass") + ";";
        const QString beat = "return " + c.engine("u_audioBeat") + ";";
        return {{"level", level}, {"bass", bass}, {"beat", beat}};
    };

    registerNodeDefs({choiceNode, pick, region, seed, otherClip, trackMask, audioTrack, depth, face, clipMask, audio});
}

}
