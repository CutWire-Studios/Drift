#include "app/forge/edit/GraphOps.h"

#include "app/forge/doc/ParamTypes.h"
#include "app/forge/doc/Util.h"
#include "app/forge/edit/ParamOps.h"
#include "app/forge/nodes/Registry.h"

#include <QSet>

#include <algorithm>

namespace drift::forge {

namespace {

constexpr const char *kFaceParam = "faceIndex";

bool reachable(const ForgeDoc &doc, const QString &from, const QString &to)
{
    QMap<QString, QStringList> out;
    for (const ForgeEdge &e : doc.edges)
        out[e.from] << e.to;
    QStringList stack{from};
    QSet<QString> seen;
    while (!stack.isEmpty()) {
        const QString id = stack.takeLast();
        if (id == to)
            return true;
        if (seen.contains(id))
            continue;
        seen.insert(id);
        stack << out.value(id);
    }
    return false;
}

bool isOutputNode(const QString &type)
{
    const NodeDef *d = nodeDef(type);
    return d && d->output;
}

}

const SamplerNode *samplerNode(const QString &nodeType)
{
    static const QMap<QString, SamplerNode> table{
        {"other_clip", {"clip", "Other clip", "Other track", ParamType::Track}},
        {"track_mask", {"mask", "Track mask", "Track mask", ParamType::TrackMask}},
        {"audio_track", {"audio", "Audio", "Audio", ParamType::Audio}},
    };
    const auto it = table.constFind(nodeType);
    return it == table.constEnd() ? nullptr : &it.value();
}

OpResult addNode(const ForgeDoc &doc, const QString &type, double x, double y)
{
    if (!nodeDef(type))
        return OpResult::fail(QStringLiteral("Unknown block type \"%1\".").arg(type));
    ForgeNode n = createNode(type, x, y);
    QList<ParamDef> created;
    if (const SamplerNode *sampler = samplerNode(type)) {
        ParamDef p;
        p.identifier = uniqueParamName(doc, sampler->base);
        p.displayName = sampler->label;
        p.type = sampler->type;
        p.min = 0;
        p.max = 1;
        p.defaultValue = QString();
        n.data.insert(sampler->key, makeParamRef(p.identifier));
        created << p;
    }
    if (type == QLatin1String("face") && !doc.param(QLatin1String(kFaceParam))) {
        ParamDef p;
        p.identifier = QLatin1String(kFaceParam);
        p.displayName = QStringLiteral("Face");
        p.type = ParamType::Float;
        p.min = 0;
        p.max = 3;
        p.defaultValue = 0;
        p.ui = QJsonObject{{"step", 1}};
        created << p;
    }
    ForgeDoc d = doc;
    d.params << created;
    d.nodes << n;
    return {d, {}, {n.id}};
}

ForgeDoc removeNodes(const ForgeDoc &doc, const QStringList &ids)
{
    QSet<QString> del;
    for (const QString &id : ids) {
        const ForgeNode *n = doc.node(id);
        if (!isOutputNode(n ? n->type : QString()))
            del.insert(id);
    }
    ForgeDoc d = doc;
    d.nodes.removeIf([&](const ForgeNode &n) { return del.contains(n.id); });
    d.edges.removeIf([&](const ForgeEdge &e) { return del.contains(e.from) || del.contains(e.to); });
    const bool hasFace = std::any_of(d.nodes.begin(), d.nodes.end(), [](const ForgeNode &n) { return n.type == QLatin1String("face"); });
    const ForgeDoc snapshot = d;
    d.params.removeIf([&](const ParamDef &p) {
        const bool keepTrack = !isTrackParam(p) || paramUsers(snapshot, p.identifier) > 0;
        const bool keepFace = p.identifier != QLatin1String(kFaceParam) || hasFace;
        return !(keepTrack && keepFace);
    });
    return d;
}

OpResult duplicateNodes(const ForgeDoc &doc, const QStringList &ids)
{
    QMap<QString, QString> map;
    QStringList order;
    QList<ForgeNode> copies;
    for (const ForgeNode &n : doc.nodes) {
        if (!ids.contains(n.id) || isOutputNode(n.type))
            continue;
        ForgeNode c = n;
        c.id = uid(QStringLiteral("n"));
        c.x += 40;
        c.y += 40;
        map.insert(n.id, c.id);
        order << c.id;
        copies << c;
    }
    ForgeDoc d = doc;
    d.nodes << copies;
    for (const ForgeEdge &e : doc.edges) {
        if (map.contains(e.from) && map.contains(e.to)) {
            ForgeEdge c = e;
            c.id = uid(QStringLiteral("e"));
            c.from = map.value(e.from);
            c.to = map.value(e.to);
            d.edges << c;
        }
    }
    return {d, {}, order};
}

ForgeDoc moveNode(const ForgeDoc &doc, const QString &id, double x, double y)
{
    ForgeDoc d = doc;
    if (ForgeNode *n = d.node(id)) {
        n->x = x;
        n->y = y;
    }
    return d;
}

OpResult connect(const ForgeDoc &doc, const QString &from, const QString &fromSocket, const QString &to,
                 const QString &toSocket)
{
    const ForgeNode *a = doc.node(from);
    const ForgeNode *b = doc.node(to);
    if (!a || !b)
        return OpResult::fail(QStringLiteral("One of those blocks doesn't exist."));
    const NodeDef *da = nodeDef(a->type);
    const NodeDef *db = nodeDef(b->type);
    if (!da || !da->outputDef(fromSocket))
        return OpResult::fail(QStringLiteral("\"%1\" has no output \"%2\".").arg(a->type, fromSocket));
    if (!db || !db->input(toSocket))
        return OpResult::fail(QStringLiteral("\"%1\" has no input \"%2\".").arg(b->type, toSocket));
    if (from == to || reachable(doc, to, from))
        return OpResult::fail(QStringLiteral("That connection would make a loop."));
    ForgeDoc d = doc;
    d.edges.removeIf([&](const ForgeEdge &e) { return e.to == to && e.toSocket == toSocket; });
    d.edges.append({uid(QStringLiteral("e")), from, fromSocket, to, toSocket, {}});
    return {d, {}, {}};
}

ForgeDoc disconnect(const ForgeDoc &doc, const QStringList &edgeIds)
{
    ForgeDoc d = doc;
    d.edges.removeIf([&](const ForgeEdge &e) { return edgeIds.contains(e.id); });
    return d;
}

ForgeDoc setInput(const ForgeDoc &doc, const QString &node, const QString &input, const InputValue &value)
{
    ForgeDoc d = doc;
    if (ForgeNode *n = d.node(node))
        n->inputs.insert(input, value);
    return d;
}

ForgeDoc setData(const ForgeDoc &doc, const QString &node, const QString &key, const QJsonValue &value)
{
    ForgeDoc d = doc;
    if (ForgeNode *n = d.node(node)) {
        if (value.isUndefined())
            n->data.remove(key);
        else
            n->data.insert(key, value);
    }
    return d;
}

ForgeDoc addAsset(const ForgeDoc &doc, const ForgeAsset &asset)
{
    ForgeDoc d = doc;
    d.assets << asset;
    return d;
}

}
