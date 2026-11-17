#include "app/forge/audio/RackOps.h"

#include "app/forge/doc/Util.h"
#include "app/forge/edit/ParamOps.h"
#include "engine/audio/dsp/graph/AudioGraph.h"
#include "engine/audio/dsp/graph/PedalCatalog.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace drift::forge {

using audiofx::KnobSpec;
using audiofx::PedalSpec;

namespace {

using Scale = KnobSpec::Scale;

const KnobSpec kBypassKnob{"bypass", "Bypass", 0, 1, 0, Scale::Toggle, ""};
const KnobSpec kBlendKnob{"blend", "Blend", 0, 1, 0.5f, Scale::Linear, ""};
const KnobSpec kCrossoverKnob{"crossover", "Crossover", 20, 20000, 1000, Scale::Log, "Hz"};
const KnobSpec kDepthKnob{"depth", "Depth", -1, 1, 0.3f, Scale::Linear, ""};

const QString kTooDeep = QStringLiteral("Splits nest at most two deep.");
const QString kNotSplit = QStringLiteral("That isn't a split.");
const QString kNoLane = QStringLiteral("That lane doesn't exist.");

QString tooManyPedals()
{
    return QStringLiteral("A pedalboard holds at most %1 pedals.").arg(audiofx::kMaxGraphNodes);
}

bool hasImpulseResponse(const QString &type)
{
    return type == QLatin1String("convolution");
}

KnobValue knobDefault(const KnobSpec &k)
{
    return k.scale == Scale::Toggle ? KnobValue::flag(k.defaultValue > 0.5f) : KnobValue::num(k.defaultValue);
}

QMap<QString, KnobValue> knobDefaults(const PedalSpec &spec)
{
    QMap<QString, KnobValue> out;
    for (const KnobSpec &k : spec.knobs)
        out.insert(QString::fromLatin1(k.id), knobDefault(k));
    return out;
}

void insert(std::vector<RackItem> &list, RackItem item, int index)
{
    const int at = index < 0 ? int(list.size()) : std::clamp(index, 0, int(list.size()));
    list.insert(list.begin() + at, std::move(item));
}

QSet<QString> idsUnder(const RackItem &item)
{
    QSet<QString> ids;
    const std::vector<RackItem> one{item};
    walkRack(one, [&](const RackItem &x, int) { ids.insert(x.id); });
    return ids;
}

QList<KnobValue> defaultCrossovers(int lanes)
{
    QList<KnobValue> out;
    for (int i = 0; i < lanes - 1; ++i)
        out.append(KnobValue::num(jsRound(200 * std::pow(25.0, lanes == 2 ? 0.5 : double(i) / (lanes - 2)))));
    return out;
}

SplitLane newLane()
{
    SplitLane l;
    l.id = uid(QStringLiteral("l"));
    return l;
}

int splitDepthBelow(const RackItem &item)
{
    if (!item.isSplit())
        return 0;
    int deepest = 0;
    for (const SplitLane &lane : item.lanes)
        for (const RackItem &child : lane.chain)
            deepest = std::max(deepest, splitDepthBelow(child));
    return deepest + 1;
}

void dropRoutesTo(Rack &r, const QSet<QString> &gone)
{
    r.routes.removeIf([&](const ModRoute &route) { return gone.contains(route.to); });
    for (Modulator &m : r.modulators)
        if (!m.source.isEmpty() && gone.contains(m.source))
            m.source = QStringLiteral("input");
}

bool followsSource(const QString &type)
{
    return type == QLatin1String("envelope");
}

bool hasSteps(const QString &type)
{
    return type == QLatin1String("steps");
}

Modulator *modulatorOf(Rack &r, const QString &id)
{
    for (Modulator &m : r.modulators)
        if (m.id == id)
            return &m;
    return nullptr;
}

ModRoute *routeOf(Rack &r, const QString &id)
{
    for (ModRoute &q : r.routes)
        if (q.id == id)
            return &q;
    return nullptr;
}

const KnobSpec *valueSpec(Rack &rack, const ValuePath &path)
{
    switch (path.kind) {
    case ValuePath::Knob: {
        const RackItem *pedal = pedalAt(rack, path.item);
        const PedalSpec *spec = pedal ? audiofx::pedalSpec(pedal->type.toStdString()) : nullptr;
        return spec ? audiofx::findKnob(*spec, path.knob.toStdString()) : nullptr;
    }
    case ValuePath::Bypass: return &kBypassKnob;
    case ValuePath::Blend: return &kBlendKnob;
    case ValuePath::Crossover: return &kCrossoverKnob;
    case ValuePath::ModKnob: {
        const Modulator *m = modulatorOf(rack, path.item);
        const PedalSpec *spec = m ? audiofx::modulatorSpec(m->type.toStdString()) : nullptr;
        return spec ? audiofx::findKnob(*spec, path.knob.toStdString()) : nullptr;
    }
    case ValuePath::Depth: return &kDepthKnob;
    }
    return nullptr;
}

std::optional<KnobValue> readValue(Rack &rack, const ValuePath &path)
{
    switch (path.kind) {
    case ValuePath::Knob: {
        const RackItem *pedal = pedalAt(rack, path.item);
        if (pedal && pedal->knobs.contains(path.knob))
            return pedal->knobs.value(path.knob);
        return std::nullopt;
    }
    case ValuePath::Bypass: {
        const RackItem *pedal = pedalAt(rack, path.item);
        if (!pedal)
            return std::nullopt;
        return pedal->bypass.value_or(KnobValue::flag(false));
    }
    case ValuePath::Blend: {
        const RackItem *split = splitAt(rack, path.item);
        if (!split)
            return std::nullopt;
        return split->blend.value_or(KnobValue::num(kBlendKnob.defaultValue));
    }
    case ValuePath::Crossover: {
        const RackItem *split = splitAt(rack, path.item);
        if (split && split->crossovers && path.index >= 0 && path.index < split->crossovers->size())
            return split->crossovers->at(path.index);
        return std::nullopt;
    }
    case ValuePath::ModKnob: {
        const Modulator *m = modulatorOf(rack, path.item);
        if (m && m->knobs.contains(path.knob))
            return m->knobs.value(path.knob);
        return std::nullopt;
    }
    case ValuePath::Depth: {
        const ModRoute *r = routeOf(rack, path.item);
        if (r)
            return r->depth;
        return std::nullopt;
    }
    }
    return std::nullopt;
}

void writeValue(Rack &rack, const ValuePath &path, const KnobValue &v)
{
    switch (path.kind) {
    case ValuePath::Knob:
        if (RackItem *pedal = pedalAt(rack, path.item))
            pedal->knobs[path.knob] = v;
        break;
    case ValuePath::Bypass:
        if (RackItem *pedal = pedalAt(rack, path.item))
            pedal->bypass = v;
        break;
    case ValuePath::Blend:
        if (RackItem *split = splitAt(rack, path.item))
            split->blend = v;
        break;
    case ValuePath::Crossover:
        if (RackItem *split = splitAt(rack, path.item))
            if (split->crossovers && path.index >= 0 && path.index < split->crossovers->size())
                (*split->crossovers)[path.index] = v;
        break;
    case ValuePath::ModKnob:
        if (Modulator *m = modulatorOf(rack, path.item))
            m->knobs[path.knob] = v;
        break;
    case ValuePath::Depth:
        if (ModRoute *r = routeOf(rack, path.item))
            r->depth = v;
        break;
    }
}

QString sliderLabel(Rack &rack, const ValuePath &path, const KnobSpec &spec)
{
    switch (path.kind) {
    case ValuePath::Bypass: return QStringLiteral("Off");
    case ValuePath::ModKnob: {
        const Modulator *m = modulatorOf(rack, path.item);
        const PedalSpec *ms = m ? audiofx::modulatorSpec(m->type.toStdString()) : nullptr;
        return QStringLiteral("%1 %2").arg(ms ? QString::fromLatin1(ms->label) : QStringLiteral("Modulator"), QString::fromLatin1(spec.label));
    }
    case ValuePath::Depth: {
        const ModRoute *r = routeOf(rack, path.item);
        const RackItem *target = r ? pedalAt(rack, r->to) : nullptr;
        const PedalSpec *ts = target ? audiofx::pedalSpec(target->type.toStdString()) : nullptr;
        const KnobSpec *k = ts && r ? audiofx::findKnob(*ts, r->knob.toStdString()) : nullptr;
        const QString text = QStringLiteral("%1%2 Movement")
                                 .arg(ts ? QString::fromLatin1(ts->label) + QLatin1Char(' ') : QString(),
                                      k ? QString::fromLatin1(k->label) : QString());
        return text.trimmed();
    }
    default: return QString::fromLatin1(spec.label);
    }
}

}

OpResult addPedal(const ForgeDoc &doc, const QString &type, const Slot &slot)
{
    const PedalSpec *spec = audiofx::pedalSpec(type.toStdString());
    if (!spec)
        return OpResult::fail(QStringLiteral("There is no pedal called \"%1\".").arg(type));
    Rack rack = rackOf(doc);
    if (itemCount(rack) >= audiofx::kMaxGraphNodes)
        return OpResult::fail(tooManyPedals());
    const SlotList target = slotList(rack, slot);
    if (!target.list)
        return OpResult::fail(kNoLane);
    RackItem pedal;
    pedal.id = uid(QStringLiteral("p"));
    pedal.type = type;
    pedal.knobs = knobDefaults(*spec);
    if (hasImpulseResponse(type))
        pedal.ir = QString::fromLatin1(kDefaultIr);
    const QString id = pedal.id;
    insert(*target.list, std::move(pedal), slot.index);
    return {withRack(doc, rack), {}, {id}};
}

OpResult addSplit(const ForgeDoc &doc, const QString &mode, int lanes, const Slot &slot)
{
    Rack rack = rackOf(doc);
    const SlotList target = slotList(rack, slot);
    if (!target.list)
        return OpResult::fail(kNoLane);
    if (target.depth >= audiofx::kMaxSplitDepth)
        return OpResult::fail(kTooDeep);
    if (itemCount(rack) >= audiofx::kMaxGraphNodes)
        return OpResult::fail(tooManyPedals());
    const int count = std::clamp(lanes, 2, audiofx::kMaxSplitLanes);
    RackItem split;
    split.id = uid(QStringLiteral("s"));
    split.type = QStringLiteral("split");
    split.mode = mode;
    for (int i = 0; i < count; ++i)
        split.lanes.push_back(newLane());
    if (mode == QLatin1String("bands"))
        split.crossovers = defaultCrossovers(count);
    const QString id = split.id;
    insert(*target.list, std::move(split), slot.index);
    return {withRack(doc, rack), {}, {id}};
}

OpResult moveItem(const ForgeDoc &doc, const QString &id, const Slot &to)
{
    Rack rack = rackOf(doc);
    const Located from = findItem(rack, id);
    const SlotList target = slotList(rack, to);
    if (!from.item || !target.list)
        return OpResult::fail(QStringLiteral("Nothing to move there."));
    if (!to.lane.isEmpty()) {
        const std::vector<RackItem> one{*from.item};
        bool inside = false;
        walkRack(one, [&](const RackItem &x, int) {
            for (const SplitLane &l : x.lanes)
                inside = inside || l.id == to.lane;
        });
        if (inside)
            return OpResult::fail(QStringLiteral("A split can't go inside itself."));
    }
    if (target.depth + splitDepthBelow(*from.item) > audiofx::kMaxSplitDepth)
        return OpResult::fail(kTooDeep);

    RackItem moved = std::move(*from.item);
    std::vector<RackItem> *fromList = from.list;
    const int fromIndex = from.index;
    fromList->erase(fromList->begin() + fromIndex);
    // The removal shifted a later target index in the same list down by one.
    std::vector<RackItem> *list = slotList(rack, to).list;
    const int index = to.index >= 0 && list == fromList && to.index > fromIndex ? to.index - 1 : to.index;
    insert(*list, std::move(moved), index);
    return {withRack(doc, rack), {}, {}};
}

ForgeDoc removeItem(const ForgeDoc &doc, const QString &id)
{
    Rack rack = rackOf(doc);
    const Located at = findItem(rack, id);
    if (!at.item)
        return doc;
    const QSet<QString> gone = idsUnder(*at.item);
    at.list->erase(at.list->begin() + at.index);
    dropRoutesTo(rack, gone);
    return withRack(doc, rack);
}

OpResult addLane(const ForgeDoc &doc, const QString &splitId)
{
    Rack rack = rackOf(doc);
    RackItem *split = splitAt(rack, splitId);
    if (!split)
        return OpResult::fail(kNotSplit);
    if (int(split->lanes.size()) >= audiofx::kMaxSplitLanes)
        return OpResult::fail(QStringLiteral("A split has at most %1 lanes.").arg(audiofx::kMaxSplitLanes));
    split->lanes.push_back(newLane());
    if (split->mode == QLatin1String("bands"))
        split->crossovers = defaultCrossovers(int(split->lanes.size()));
    if (split->lanes.size() > 2)
        split->crossfade = false;
    return {withRack(doc, rack), {}, {}};
}

OpResult removeLane(const ForgeDoc &doc, const QString &splitId, const QString &laneId)
{
    Rack rack = rackOf(doc);
    RackItem *split = splitAt(rack, splitId);
    if (!split)
        return OpResult::fail(kNotSplit);
    if (split->lanes.size() <= 2)
        return OpResult::fail(QStringLiteral("A split needs at least two lanes."));
    const auto lane = std::find_if(split->lanes.begin(), split->lanes.end(), [&](const SplitLane &l) { return l.id == laneId; });
    if (lane == split->lanes.end())
        return OpResult::fail(kNoLane);
    QSet<QString> gone;
    walkRack(lane->chain, [&](const RackItem &x, int) { gone.insert(x.id); });
    split->lanes.erase(lane);
    if (split->mode == QLatin1String("bands")) {
        QList<KnobValue> c = split->crossovers.value_or(QList<KnobValue>());
        c.resize(std::min(int(c.size()), int(split->lanes.size()) - 1));
        split->crossovers = c;
    }
    dropRoutesTo(rack, gone);
    return {withRack(doc, rack), {}, {}};
}

ForgeDoc setLaneGain(const ForgeDoc &doc, const QString &splitId, const QString &laneId, double gain)
{
    Rack rack = rackOf(doc);
    RackItem *split = splitAt(rack, splitId);
    if (!split)
        return doc;
    for (SplitLane &l : split->lanes)
        if (l.id == laneId)
            l.gain = std::clamp(gain, 0.0, 4.0);
    return withRack(doc, rack);
}

ForgeDoc setSplitMode(const ForgeDoc &doc, const QString &splitId, const QString &mode)
{
    Rack rack = rackOf(doc);
    RackItem *split = splitAt(rack, splitId);
    if (!split || split->mode == mode)
        return doc;
    split->mode = mode;
    if (mode == QLatin1String("bands")) {
        split->crossovers = defaultCrossovers(int(split->lanes.size()));
        split->crossfade = false;
    } else {
        split->crossovers.reset();
    }
    return withRack(doc, rack);
}

OpResult setCrossfade(const ForgeDoc &doc, const QString &splitId, bool on)
{
    Rack rack = rackOf(doc);
    RackItem *split = splitAt(rack, splitId);
    if (!split)
        return OpResult::fail(kNotSplit);
    if (on && (split->mode != QLatin1String("parallel") || split->lanes.size() != 2))
        return OpResult::fail(QStringLiteral("Blending needs a parallel split with two lanes."));
    if (on) {
        split->crossfade = true;
        if (!split->blend)
            split->blend = KnobValue::num(kBlendKnob.defaultValue);
    } else {
        split->crossfade = false;
    }
    return {withRack(doc, rack), {}, {}};
}

ForgeDoc setIr(const ForgeDoc &doc, const QString &pedalId, const QString &ir)
{
    Rack rack = rackOf(doc);
    RackItem *pedal = pedalAt(rack, pedalId);
    if (!pedal || !hasImpulseResponse(pedal->type))
        return doc;
    pedal->ir = ir;
    return withRack(doc, rack);
}

OpResult addModulator(const ForgeDoc &doc, const QString &type)
{
    const PedalSpec *spec = audiofx::modulatorSpec(type.toStdString());
    if (!spec)
        return OpResult::fail(QStringLiteral("There is no modulator called \"%1\".").arg(type));
    Modulator m;
    m.id = uid(QStringLiteral("m"));
    m.type = type;
    m.knobs = knobDefaults(*spec);
    if (followsSource(type))
        m.source = QStringLiteral("input");
    if (hasSteps(type))
        m.steps = QList<double>{1, 0.25, 0.75, 0, 0.5, 1, 0.25, 0.5};
    Rack rack = rackOf(doc);
    const QString id = m.id;
    rack.modulators.append(m);
    return {withRack(doc, rack), {}, {id}};
}

ForgeDoc removeModulator(const ForgeDoc &doc, const QString &id)
{
    Rack rack = rackOf(doc);
    rack.modulators.removeIf([&](const Modulator &m) { return m.id == id; });
    rack.routes.removeIf([&](const ModRoute &r) { return r.from == id; });
    return withRack(doc, rack);
}

ForgeDoc setModulatorSource(const ForgeDoc &doc, const QString &id, const QString &source)
{
    Rack rack = rackOf(doc);
    if (source != QLatin1String("input") && !findItem(rack, source).item)
        return doc;
    Modulator *m = modulatorOf(rack, id);
    if (!m || !followsSource(m->type))
        return doc;
    m->source = source;
    return withRack(doc, rack);
}

ForgeDoc setSteps(const ForgeDoc &doc, const QString &id, const QList<double> &steps)
{
    QList<double> clean;
    for (int i = 0; i < std::min(int(steps.size()), audiofx::kMaxSteps); ++i)
        clean.append(std::clamp(steps[i], 0.0, 1.0));
    if (clean.isEmpty())
        return doc;
    Rack rack = rackOf(doc);
    Modulator *m = modulatorOf(rack, id);
    if (!m || !hasSteps(m->type))
        return doc;
    m->steps = clean;
    return withRack(doc, rack);
}

OpResult addRoute(const ForgeDoc &doc, const QString &from, const QString &to, const QString &knob, double depth)
{
    Rack rack = rackOf(doc);
    if (!modulatorOf(rack, from))
        return OpResult::fail(QStringLiteral("That modulator doesn't exist."));
    const RackItem *pedal = pedalAt(rack, to);
    const PedalSpec *spec = pedal ? audiofx::pedalSpec(pedal->type.toStdString()) : nullptr;
    const KnobSpec *k = spec ? audiofx::findKnob(*spec, knob.toStdString()) : nullptr;
    if (!k)
        return OpResult::fail(QStringLiteral("That knob doesn't exist."));
    if (!k->continuous())
        return OpResult::fail(QStringLiteral("Switches can't be modulated; pick a knob that turns."));
    const QString knobId = QString::fromLatin1(k->id);
    for (const ModRoute &r : std::as_const(rack.routes))
        if (r.from == from && r.to == to && r.knob == knobId)
            return {setValue(doc, ValuePath::depthOf(r.id), depth), {}, {r.id}};
    ModRoute route;
    route.id = uid(QStringLiteral("r"));
    route.from = from;
    route.to = to;
    route.knob = knobId;
    route.depth = KnobValue::num(std::clamp(depth, -1.0, 1.0));
    const QString id = route.id;
    rack.routes.append(route);
    return {withRack(doc, rack), {}, {id}};
}

ForgeDoc removeRoute(const ForgeDoc &doc, const QString &id)
{
    Rack rack = rackOf(doc);
    rack.routes.removeIf([&](const ModRoute &r) { return r.id == id; });
    return withRack(doc, rack);
}

ForgeDoc setRouteDepth(const ForgeDoc &doc, const QString &routeId, double depth)
{
    return setValue(doc, ValuePath::depthOf(routeId), depth);
}

std::optional<KnobValue> getValue(const ForgeDoc &doc, const ValuePath &path)
{
    Rack rack = rackOf(doc);
    return readValue(rack, path);
}

ForgeDoc setValue(const ForgeDoc &doc, const ValuePath &path, double v)
{
    Rack rack = rackOf(doc);
    const KnobSpec *spec = valueSpec(rack, path);
    const std::optional<KnobValue> cur = readValue(rack, path);
    if (!spec || (cur && cur->bound()))
        return doc;
    KnobValue next;
    if (spec->scale == Scale::Toggle) {
        next = KnobValue::flag(v >= 0.5);
    } else {
        const double n = std::clamp(v, double(spec->min), double(spec->max));
        next = KnobValue::num(spec->scale == Scale::Choice ? jsRound(n) : n);
    }
    writeValue(rack, path, next);
    return withRack(doc, rack);
}

OpResult exposeValue(const ForgeDoc &doc, const ValuePath &path)
{
    Rack rack = rackOf(doc);
    const KnobSpec *spec = valueSpec(rack, path);
    const std::optional<KnobValue> cur = readValue(rack, path);
    if (!spec || !cur)
        return OpResult::fail(QStringLiteral("That control doesn't exist."));
    if (cur->bound())
        return {doc, {}, {cur->param}};
    const QString label = sliderLabel(rack, path, *spec);
    ParamDef p;
    p.identifier = uniqueParamName(doc, label);
    p.displayName = label;
    p.min = spec->min;
    p.max = spec->max;
    switch (spec->scale) {
    case Scale::Toggle:
        p.type = ParamType::Bool;
        p.min = 0;
        p.max = 1;
        p.defaultValue = cur->truthy();
        break;
    case Scale::Choice:
        p.type = ParamType::Choice;
        p.options = QStringList();
        for (const char *o : spec->options)
            p.options->append(QString::fromLatin1(o));
        p.defaultValue = jsRound(cur->asNumber());
        break;
    default:
        p.type = ParamType::Float;
        p.defaultValue = cur->asNumber();
        if (spec->unit && *spec->unit)
            p.ui = QJsonObject{{"unit", QString::fromLatin1(spec->unit)}};
        break;
    }
    writeValue(rack, path, KnobValue::ref(p.identifier));
    ForgeDoc out = withRack(doc, rack);
    out.params.append(p);
    return {out, {}, {p.identifier}};
}

ForgeDoc unexposeValue(const ForgeDoc &doc, const ValuePath &path)
{
    Rack rack = rackOf(doc);
    const std::optional<KnobValue> cur = readValue(rack, path);
    if (!cur || !cur->bound())
        return doc;
    const QString name = cur->param;
    const ParamDef *p = doc.param(name);
    const KnobValue literal = p && p->defaultValue.isBool() ? KnobValue::flag(p->defaultValue.toBool())
                                                            : KnobValue::num(p ? p->defaultValue.toDouble() : 0);
    writeValue(rack, path, literal);
    ForgeDoc out = withRack(doc, rack);
    if (!paramUsers(out, name))
        out.params.removeIf([&](const ParamDef &q) { return q.identifier == name; });
    return out;
}

}
