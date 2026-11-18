#include "app/forge/audio/Rack.h"

namespace drift::forge {

namespace {

QJsonObject without(const QJsonObject &o, std::initializer_list<const char *> keys)
{
    QJsonObject out = o;
    for (const char *k : keys)
        out.remove(QLatin1String(k));
    return out;
}

QMap<QString, KnobValue> knobsFromJson(const QJsonObject &o)
{
    QMap<QString, KnobValue> out;
    for (auto it = o.begin(); it != o.end(); ++it)
        out.insert(it.key(), KnobValue::fromJson(it.value()));
    return out;
}

QJsonObject knobsToJson(const QMap<QString, KnobValue> &knobs)
{
    QJsonObject o;
    for (auto it = knobs.begin(); it != knobs.end(); ++it)
        o[it.key()] = it.value().toJson();
    return o;
}

RackItem itemFromJson(const QJsonObject &o);

std::vector<RackItem> chainFromJson(const QJsonArray &a)
{
    std::vector<RackItem> out;
    for (const QJsonValue &v : a)
        out.push_back(itemFromJson(v.toObject()));
    return out;
}

QJsonArray chainToJson(const std::vector<RackItem> &chain);

RackItem itemFromJson(const QJsonObject &o)
{
    RackItem item;
    item.id = o.value("id").toString();
    item.type = o.value("type").toString();
    if (item.isSplit()) {
        item.mode = o.value("mode").toString();
        item.crossfade = o.value("crossfade").toBool(false);
        if (o.contains("blend"))
            item.blend = KnobValue::fromJson(o.value("blend"));
        if (o.contains("crossovers")) {
            QList<KnobValue> c;
            for (const QJsonValue &v : o.value("crossovers").toArray())
                c.append(KnobValue::fromJson(v));
            item.crossovers = c;
        }
        for (const QJsonValue &lv : o.value("lanes").toArray()) {
            const QJsonObject l = lv.toObject();
            SplitLane lane;
            lane.id = l.value("id").toString();
            lane.gain = l.value("gain").toDouble(1);
            lane.chain = chainFromJson(l.value("chain").toArray());
            lane.extra = without(l, {"id", "gain", "chain"});
            item.lanes.push_back(std::move(lane));
        }
        item.extra = without(o, {"id", "type", "mode", "crossfade", "blend", "crossovers", "lanes"});
    } else {
        item.knobs = knobsFromJson(o.value("knobs").toObject());
        if (o.contains("bypass"))
            item.bypass = KnobValue::fromJson(o.value("bypass"));
        item.ir = o.value("ir").toString();
        item.extra = without(o, {"id", "type", "knobs", "bypass", "ir"});
    }
    return item;
}

QJsonObject itemToJson(const RackItem &item)
{
    QJsonObject o = item.extra;
    o["id"] = item.id;
    o["type"] = item.type;
    if (item.isSplit()) {
        o["mode"] = item.mode;
        if (item.crossfade)
            o["crossfade"] = true;
        if (item.blend)
            o["blend"] = item.blend->toJson();
        if (item.crossovers) {
            QJsonArray c;
            for (const KnobValue &v : *item.crossovers)
                c.append(v.toJson());
            o["crossovers"] = c;
        }
        QJsonArray lanes;
        for (const SplitLane &lane : item.lanes) {
            QJsonObject l = lane.extra;
            l["id"] = lane.id;
            l["gain"] = lane.gain;
            l["chain"] = chainToJson(lane.chain);
            lanes.append(l);
        }
        o["lanes"] = lanes;
    } else {
        o["knobs"] = knobsToJson(item.knobs);
        if (item.bypass)
            o["bypass"] = item.bypass->toJson();
        if (!item.ir.isEmpty())
            o["ir"] = item.ir;
    }
    return o;
}

QJsonArray chainToJson(const std::vector<RackItem> &chain)
{
    QJsonArray a;
    for (const RackItem &item : chain)
        a.append(itemToJson(item));
    return a;
}

}

QJsonValue KnobValue::toJson() const
{
    if (bound())
        return QJsonObject{{"param", param}};
    if (isBool)
        return number != 0;
    return number;
}

KnobValue KnobValue::fromJson(const QJsonValue &v)
{
    if (v.isObject())
        return ref(v.toObject().value("param").toString());
    if (v.isBool())
        return flag(v.toBool());
    return num(v.toDouble());
}

Rack Rack::fromJson(const QJsonObject &o)
{
    Rack r;
    r.chain = chainFromJson(o.value("chain").toArray());
    for (const QJsonValue &mv : o.value("modulators").toArray()) {
        const QJsonObject m = mv.toObject();
        Modulator mod;
        mod.id = m.value("id").toString();
        mod.type = m.value("type").toString();
        mod.knobs = knobsFromJson(m.value("knobs").toObject());
        if (m.contains("steps")) {
            QList<double> steps;
            for (const QJsonValue &s : m.value("steps").toArray())
                steps.append(s.toDouble());
            mod.steps = steps;
        }
        mod.source = m.value("source").toString();
        mod.extra = without(m, {"id", "type", "knobs", "steps", "source"});
        r.modulators.append(mod);
    }
    for (const QJsonValue &rv : o.value("routes").toArray()) {
        const QJsonObject q = rv.toObject();
        ModRoute route;
        route.id = q.value("id").toString();
        route.from = q.value("from").toString();
        route.to = q.value("to").toString();
        route.knob = q.value("knob").toString();
        route.depth = KnobValue::fromJson(q.value("depth"));
        route.extra = without(q, {"id", "from", "to", "knob", "depth"});
        r.routes.append(route);
    }
    r.extra = without(o, {"chain", "modulators", "routes"});
    return r;
}

QJsonObject Rack::toJson() const
{
    QJsonObject o = extra;
    o["chain"] = chainToJson(chain);
    QJsonArray mods;
    for (const Modulator &m : modulators) {
        QJsonObject j = m.extra;
        j["id"] = m.id;
        j["type"] = m.type;
        j["knobs"] = knobsToJson(m.knobs);
        if (m.steps) {
            QJsonArray s;
            for (double v : *m.steps)
                s.append(v);
            j["steps"] = s;
        }
        if (!m.source.isEmpty())
            j["source"] = m.source;
        mods.append(j);
    }
    o["modulators"] = mods;
    QJsonArray routesJson;
    for (const ModRoute &r : routes) {
        QJsonObject j = r.extra;
        j["id"] = r.id;
        j["from"] = r.from;
        j["to"] = r.to;
        j["knob"] = r.knob;
        j["depth"] = r.depth.toJson();
        routesJson.append(j);
    }
    o["routes"] = routesJson;
    return o;
}

Rack rackOf(const ForgeDoc &doc)
{
    return doc.audio ? Rack::fromJson(doc.audio->value("rack").toObject()) : Rack{};
}

ForgeDoc withRack(const ForgeDoc &doc, const Rack &rack)
{
    ForgeDoc d = doc;
    QJsonObject audio = d.audio.value_or(QJsonObject());
    audio["rack"] = rack.toJson();
    d.audio = audio;
    return d;
}

QString builtinIr(const QString &ir)
{
    static const QString prefix = QStringLiteral("builtin:");
    return ir.startsWith(prefix) ? ir.mid(prefix.size()) : QString();
}

void walkRack(std::vector<RackItem> &chain, const std::function<void(const Located &)> &fn, int depth, SplitLane *lane)
{
    for (size_t i = 0; i < chain.size(); ++i) {
        fn({&chain[i], &chain, int(i), depth, lane});
        if (chain[i].isSplit())
            for (SplitLane &l : chain[i].lanes)
                walkRack(l.chain, fn, depth + 1, &l);
    }
}

void walkRack(const std::vector<RackItem> &chain, const std::function<void(const RackItem &, int)> &fn, int depth)
{
    for (const RackItem &item : chain) {
        fn(item, depth);
        for (const SplitLane &l : item.lanes)
            walkRack(l.chain, fn, depth + 1);
    }
}

Located findItem(Rack &rack, const QString &id)
{
    Located found;
    walkRack(rack.chain, [&](const Located &at) {
        if (!found.item && at.item->id == id)
            found = at;
    });
    return found;
}

RackItem *pedalAt(Rack &rack, const QString &id)
{
    RackItem *item = findItem(rack, id).item;
    return item && !item->isSplit() ? item : nullptr;
}

RackItem *splitAt(Rack &rack, const QString &id)
{
    RackItem *item = findItem(rack, id).item;
    return item && item->isSplit() ? item : nullptr;
}

int itemCount(const Rack &rack)
{
    int n = 0;
    walkRack(rack.chain, [&](const RackItem &, int) { ++n; });
    return n;
}

SlotList slotList(Rack &rack, const Slot &slot)
{
    if (slot.lane.isEmpty())
        return {&rack.chain, 0};
    SlotList found;
    walkRack(rack.chain, [&](const Located &at) {
        if (found.list || !at.item->isSplit())
            return;
        for (SplitLane &l : at.item->lanes)
            if (l.id == slot.lane)
                found = {&l.chain, at.depth + 1};
    });
    return found;
}

}
