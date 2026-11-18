#include "app/forge/nodes/NodeBuild.h"

namespace drift::forge {

void registerOutputNodes()
{
    const auto output = [](const char *type, const char *description, Kind kind, QJsonValue fallback) {
        NodeDef d;
        d.type = QLatin1String(type);
        d.label = QStringLiteral("Output");
        d.category = QStringLiteral("output");
        d.description = QLatin1String(description);
        d.kinds = {kind};
        d.output = true;
        d.inputs = {{QStringLiteral("image"), QStringLiteral("Image"), SocketType::Color, fallback}};
        d.emitFn = [](EmitCtx &c, const StageReader &) -> build::Out {
            return {{"image", "return " + c.in("image") + ";"}};
        };
        return d;
    };
    registerNodeDefs({
        output("effect_output", "What the effect finally shows. Every effect has exactly one.", Kind::Effect,
               QJsonArray{0, 0, 0, 1}),
        output("transition_output", "What the transition finally shows. Every transition has exactly one.",
               Kind::Transition, QJsonArray{0, 0, 0, 1}),
        output("animation_output", "What the clip shows while the animation plays. Every animation has exactly one.",
               Kind::Animation, QJsonArray{0, 0, 0, 0}),
    });
}

}
