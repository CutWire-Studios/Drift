#include "app/forge/audio/RackGraph.h"

namespace drift::forge {

namespace {

QJsonValue graphKnob(const KnobValue &v)
{
    if (v.bound())
        return QJsonObject{{"param", v.param}};
    return v.number;
}

QJsonObject graphKnobs(const QMap<QString, KnobValue> &knobs)
{
    QJsonObject o;
    for (auto it = knobs.begin(); it != knobs.end(); ++it)
        o[it.key()] = graphKnob(it.value());
    return o;
}

QJsonObject graphNode(const ForgeDoc &doc, const Rack &rack, const RackItem &item)
{
    QJsonObject out;
    out["id"] = item.id;
    if (item.isSplit()) {
        out["type"] = QStringLiteral("split");
        out["mode"] = item.mode;
        if (item.crossfade)
            out["crossfade"] = true;
        if (item.blend)
            out["blend"] = graphKnob(*item.blend);
        if (item.mode == QLatin1String("bands") && item.crossovers) {
            QJsonArray c;
            for (const KnobValue &v : *item.crossovers)
                c.append(graphKnob(v));
            out["crossovers"] = c;
        }
        QJsonArray lanes;
        for (const SplitLane &l : item.lanes) {
            QJsonArray chain;
            for (const RackItem &child : l.chain)
                chain.append(graphNode(doc, rack, child));
            lanes.append(QJsonObject{{"gain", l.gain}, {"chain", chain}});
        }
        out["lanes"] = lanes;
        return out;
    }
    out["type"] = item.type;
    out["knobs"] = graphKnobs(item.knobs);
    if (item.bypass && !(item.bypass->isBool && !item.bypass->bound() && !item.bypass->truthy()))
        out["bypass"] = graphKnob(*item.bypass);
    if (!item.ir.isEmpty())
        out["ir"] = irPath(doc, item.ir);
    QJsonObject mod;
    for (const ModRoute &r : rack.routes) {
        if (r.to != item.id)
            continue;
        QJsonArray list = mod.value(r.knob).toArray();
        list.append(QJsonObject{{"from", r.from}, {"depth", graphKnob(r.depth)}});
        mod[r.knob] = list;
    }
    if (!mod.isEmpty())
        out["mod"] = mod;
    return out;
}

}

QString irPath(const ForgeDoc &doc, const QString &ir)
{
    const QString builtin = builtinIr(ir);
    if (!builtin.isNull())
        return QStringLiteral("ir/%1.wav").arg(builtin);
    for (const ForgeAsset &a : doc.assets)
        if (a.id == ir)
            return QStringLiteral("ir/%1.wav").arg(a.id);
    return QStringLiteral("ir/%1.wav").arg(ir);
}

QJsonObject rackGraphJson(const ForgeDoc &doc)
{
    const Rack rack = rackOf(doc);
    QJsonObject graph{{"version", 1}};
    if (!rack.modulators.isEmpty()) {
        QJsonArray mods;
        for (const Modulator &m : rack.modulators) {
            QJsonObject j{{"id", m.id}, {"type", m.type}, {"knobs", graphKnobs(m.knobs)}};
            if (m.type == QLatin1String("envelope")) {
                if (!m.source.isEmpty() && m.source != QLatin1String("input"))
                    j["source"] = m.source;
            } else if (m.type == QLatin1String("steps")) {
                QJsonArray s;
                for (double v : m.steps.value_or(QList<double>{1}))
                    s.append(v);
                j["steps"] = s;
            }
            mods.append(j);
        }
        graph["modulators"] = mods;
    }
    QJsonArray chain;
    for (const RackItem &item : rack.chain)
        chain.append(graphNode(doc, rack, item));
    graph["chain"] = chain;
    return graph;
}

}
