#include "app/forge/nodes/Registry.h"

#include "app/forge/doc/Util.h"

#include <deque>

namespace drift::forge {

namespace {

struct Store
{
    std::deque<NodeDef> defs;
    QList<const NodeDef *> list;
    QMap<QString, const NodeDef *> byType;
};

Store &store()
{
    static Store s;
    return s;
}

void ensureRegistered()
{
    static const bool done = [] {
        registerInputNodes();
        registerControlNodes();
        registerColorNodes();
        registerGradeNodes();
        registerDistortNodes();
        registerKitDistortNodes();
        registerThreeNodes();
        registerBlurNodes();
        registerKitBlurNodes();
        registerStylizeNodes();
        registerGenerateNodes();
        registerKitOverlayNodes();
        registerMixNodes();
        registerTransitionNodes();
        registerKitTransitionNodes();
        registerAnimateNodes();
        registerMathNodes();
        registerAdvancedNodes();
        registerOutputNodes();
        return true;
    }();
    Q_UNUSED(done);
}

}

void registerNodeDefs(QList<NodeDef> defs)
{
    Store &s = store();
    for (NodeDef &d : defs) {
        if (s.byType.contains(d.type))
            continue;
        s.defs.push_back(std::move(d));
        const NodeDef *p = &s.defs.back();
        s.list.append(p);
        s.byType.insert(p->type, p);
    }
}

const NodeDef *nodeDef(const QString &type)
{
    ensureRegistered();
    return store().byType.value(type, nullptr);
}

const QList<const NodeDef *> &allNodeDefs()
{
    ensureRegistered();
    return store().list;
}

bool availableFor(const NodeDef &def, Kind kind)
{
    return def.kinds.isEmpty() || def.kinds.contains(kind);
}

ForgeNode createNode(const QString &type, double x, double y)
{
    const NodeDef *def = nodeDef(type);
    ForgeNode n;
    n.id = uid(QStringLiteral("n"));
    n.type = type;
    n.x = x;
    n.y = y;
    if (!def)
        return n;
    for (const InputDef &i : def->inputs)
        n.inputs.insert(i.id, i.defaultValue);
    for (const OptionDef &o : def->options)
        if (o.kind != OptionKind::Asset)
            n.data.insert(o.id, o.defaultValue);
    return n;
}

QString outputType(Kind kind)
{
    return kindInfo(kind).outputNode;
}

}
