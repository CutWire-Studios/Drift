#include "app/forge/audio/RackGraphMapping.h"

#include "app/forge/audio/RackValidate.h"
#include "engine/audio/dsp/graph/AudioGraph.h"
#include "engine/audio/dsp/graph/PedalCatalog.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace drift::forge::rackgraph {

using audiofx::KnobSpec;
using audiofx::PedalSpec;
using Scale = KnobSpec::Scale;

namespace {

constexpr qreal kColumn = 280;
constexpr qreal kLaneGap = 40;
constexpr qreal kRowHeight = 26;
constexpr qreal kChrome = 52;
constexpr qreal kNodeWidth = 220;
const QString kMergeSuffix = QStringLiteral("~merge");
const QString kLayoutKey = QStringLiteral("nativeLayout");
const QStringList kBuiltinIrs{QStringLiteral("room"), QStringLiteral("plate"), QStringLiteral("hall"),
                              QStringLiteral("spring")};

QString laneId(int i)
{
    return QStringLiteral("lane%1").arg(i + 1);
}

QString accentFor(const QString &category)
{
    static const QMap<QString, QString> table{
        {"filter", "cyan"},   {"texture", "amber"},  {"space", "blue"},      {"utility", "neutral"},
        {"voice", "pink"},    {"transmission", "orange"}, {"modulator", "violet"}, {"split", "lime"},
    };
    return table.value(category, QStringLiteral("neutral"));
}

QString capitalized(const QString &s)
{
    return s.isEmpty() ? s : s.at(0).toUpper() + s.mid(1);
}

bool routed(const Rack &rack, const QString &to, const QString &knob)
{
    return std::any_of(rack.routes.cbegin(), rack.routes.cend(),
                       [&](const ModRoute &r) { return r.to == to && r.knob == knob; });
}

QVariantMap audioPort(const QString &id, const QString &label, bool connected = true)
{
    return {{"id", id}, {"label", label}, {"type", "audio"}, {"connected", connected}};
}

QVariantMap valueFields(const ForgeDoc &doc, const KnobValue &v, bool asBool)
{
    QVariantMap m;
    if (v.bound()) {
        m["exposed"] = v.param;
        const ParamDef *p = doc.param(v.param);
        m["value"] = p ? p->defaultValue.toVariant() : QVariant(v.number);
    } else {
        m["value"] = asBool ? QVariant(v.truthy()) : QVariant(v.number);
    }
    return m;
}

QVariantMap knobInput(const ForgeDoc &doc, const QString &id, const KnobSpec &k, const KnobValue &v, bool connected,
                      bool routable)
{
    QVariantMap m{{"id", id}, {"label", QString::fromLatin1(k.label)}, {"connected", connected}};
    switch (k.scale) {
    case Scale::Toggle:
        m["widget"] = "toggle";
        m["type"] = "bool";
        m["hidePort"] = true;
        break;
    case Scale::Choice: {
        m["widget"] = "choice";
        m["type"] = "float";
        m["hidePort"] = true;
        QVariantList options;
        for (size_t i = 0; i < k.options.size(); ++i)
            options.append(QVariantMap{{"value", int(i)}, {"label", QString::fromLatin1(k.options[i])}});
        m["options"] = options;
        break;
    }
    default:
        m["widget"] = "slider";
        m["type"] = routable ? "mod" : "float";
        m["hidePort"] = !routable;
        m["min"] = k.min;
        m["max"] = k.max;
        break;
    }
    const QVariantMap fields = valueFields(doc, v, k.scale == Scale::Toggle);
    for (auto it = fields.begin(); it != fields.end(); ++it)
        m[it.key()] = it.value();
    return m;
}

QVariantMap plainInput(const QString &id, const QString &label, const QString &widget, const QVariant &value,
                       const QVariantList &options = {})
{
    QVariantMap m{{"id", id}, {"label", label}, {"type", widget == "toggle" ? "bool" : "float"},
                  {"connected", false}, {"hidePort", true}, {"widget", widget}, {"value", value}};
    if (!options.isEmpty())
        m["options"] = options;
    return m;
}

KnobValue knobOr(const QMap<QString, KnobValue> &knobs, const KnobSpec &k)
{
    const auto it = knobs.constFind(QString::fromLatin1(k.id));
    if (it != knobs.constEnd())
        return *it;
    return k.scale == Scale::Toggle ? KnobValue::flag(k.defaultValue > 0.5f) : KnobValue::num(k.defaultValue);
}

QVariantList irOptions(const ForgeDoc &doc)
{
    QVariantList out;
    for (const QString &name : kBuiltinIrs)
        out.append(QVariantMap{{"value", QStringLiteral("builtin:") + name}, {"label", capitalized(name)}});
    for (const ForgeAsset &a : doc.assets)
        out.append(QVariantMap{{"value", a.id}, {"label", a.name.isEmpty() ? a.id : a.name}});
    return out;
}

QString pedalLabel(const RackItem &item)
{
    const PedalSpec *spec = audiofx::pedalSpec(item.type.toStdString());
    return spec ? QString::fromLatin1(spec->label) : item.type;
}

NodeItem pedalNode(const ForgeDoc &doc, const Rack &rack, const RackItem &item)
{
    NodeItem n;
    n.nodeId = item.id;
    n.type = item.type;
    n.width = kNodeWidth;
    const PedalSpec *spec = audiofx::pedalSpec(item.type.toStdString());
    n.title = pedalLabel(item);
    n.category = spec ? QString::fromLatin1(spec->category) : QString();
    n.accent = accentFor(n.category);
    n.inputs.append(audioPort(QStringLiteral("in"), QStringLiteral("In")));
    if (spec) {
        for (const KnobSpec &k : spec->knobs) {
            const QString id = QString::fromLatin1(k.id);
            n.inputs.append(knobInput(doc, id, k, knobOr(item.knobs, k), routed(rack, item.id, id), true));
        }
    }
    if (item.type == QLatin1String("convolution"))
        n.inputs.append(plainInput(QStringLiteral("ir"), QStringLiteral("Impulse"), QStringLiteral("choice"),
                                   item.ir, irOptions(doc)));
    n.outputs.append(audioPort(QStringLiteral("out"), QStringLiteral("Out")));
    return n;
}

NodeItem splitNode(const ForgeDoc &doc, const RackItem &item)
{
    NodeItem n;
    n.nodeId = item.id;
    n.type = QStringLiteral("split");
    n.title = QStringLiteral("Split");
    n.category = QStringLiteral("split");
    n.accent = accentFor(n.category);
    n.width = kNodeWidth;
    n.inputs.append(audioPort(QStringLiteral("in"), QStringLiteral("In")));
    n.inputs.append(plainInput(QStringLiteral("mode"), QStringLiteral("Mode"), QStringLiteral("choice"), item.mode,
                               {QVariantMap{{"value", "parallel"}, {"label", "Parallel"}},
                                QVariantMap{{"value", "bands"}, {"label", "Bands"}}}));
    if (item.mode == QLatin1String("parallel") && item.lanes.size() == 2) {
        n.inputs.append(plainInput(QStringLiteral("crossfade"), QStringLiteral("Blend lanes"),
                                   QStringLiteral("toggle"), item.crossfade));
        if (item.crossfade) {
            QVariantMap blend{{"id", "blend"}, {"label", "Blend"}, {"type", "float"}, {"connected", false},
                              {"hidePort", true}, {"widget", "slider"}, {"min", 0}, {"max", 1}};
            const QVariantMap fields = valueFields(doc, item.blend.value_or(KnobValue::num(0.5)), false);
            for (auto it = fields.begin(); it != fields.end(); ++it)
                blend[it.key()] = it.value();
            n.inputs.append(blend);
        }
    }
    if (item.mode == QLatin1String("bands") && item.crossovers) {
        for (int i = 0; i < item.crossovers->size(); ++i) {
            QVariantMap c{{"id", QStringLiteral("crossover%1").arg(i)},
                          {"label", QStringLiteral("Crossover %1").arg(i + 1)},
                          {"type", "float"}, {"connected", false}, {"hidePort", true},
                          {"widget", "slider"}, {"min", 20}, {"max", 20000}};
            const QVariantMap fields = valueFields(doc, item.crossovers->at(i), false);
            for (auto it = fields.begin(); it != fields.end(); ++it)
                c[it.key()] = it.value();
            n.inputs.append(c);
        }
    }
    for (size_t i = 0; i < item.lanes.size(); ++i)
        n.outputs.append(audioPort(laneId(int(i)), QStringLiteral("Lane %1").arg(i + 1)));
    return n;
}

NodeItem mergeNode(const RackItem &split)
{
    NodeItem n;
    n.nodeId = mergeId(split.id);
    n.type = QStringLiteral("merge");
    n.title = QStringLiteral("Merge");
    n.category = QStringLiteral("split");
    n.accent = accentFor(n.category);
    n.width = kNodeWidth;
    n.deletable = false;
    for (size_t i = 0; i < split.lanes.size(); ++i)
        n.inputs.append(audioPort(laneId(int(i)), QStringLiteral("Lane %1").arg(i + 1)));
    n.outputs.append(audioPort(QStringLiteral("out"), QStringLiteral("Out")));
    return n;
}

NodeItem modulatorNode(const ForgeDoc &doc, const Rack &rack, const Modulator &m)
{
    NodeItem n;
    n.nodeId = m.id;
    n.type = m.type;
    n.category = QStringLiteral("modulator");
    n.accent = accentFor(n.category);
    n.width = kNodeWidth;
    const PedalSpec *spec = audiofx::modulatorSpec(m.type.toStdString());
    n.title = spec ? QString::fromLatin1(spec->label) : m.type;
    if (spec)
        for (const KnobSpec &k : spec->knobs)
            n.inputs.append(knobInput(doc, QString::fromLatin1(k.id), k, knobOr(m.knobs, k), false, false));
    if (m.type == QLatin1String("envelope")) {
        QVariantList options{QVariantMap{{"value", "input"}, {"label", "Input"}}};
        walkRack(rack.chain, [&](const RackItem &item, int) {
            if (!item.isSplit())
                options.append(QVariantMap{{"value", item.id}, {"label", pedalLabel(item)}});
        });
        n.inputs.append(plainInput(QStringLiteral("source"), QStringLiteral("Listens to"), QStringLiteral("choice"),
                                   m.source.isEmpty() ? QStringLiteral("input") : m.source, options));
    }
    n.outputs.append(QVariantMap{{"id", "out"}, {"label", "Modulation"}, {"type", "mod"},
                                 {"connected", std::any_of(rack.routes.cbegin(), rack.routes.cend(),
                                                           [&](const ModRoute &r) { return r.from == m.id; })}});
    return n;
}

NodeItem inputNode()
{
    NodeItem n;
    n.nodeId = kInputId;
    n.type = QStringLiteral("input");
    n.title = QStringLiteral("Input");
    n.category = QStringLiteral("io");
    n.accent = QStringLiteral("emerald");
    n.width = 160;
    n.deletable = false;
    n.outputs.append(audioPort(QStringLiteral("out"), QStringLiteral("Audio in")));
    return n;
}

NodeItem outputNode()
{
    NodeItem n;
    n.nodeId = kOutputId;
    n.type = QStringLiteral("output");
    n.title = QStringLiteral("Output");
    n.category = QStringLiteral("io");
    n.accent = QStringLiteral("red");
    n.width = 160;
    n.deletable = false;
    n.inputs.append(audioPort(QStringLiteral("in"), QStringLiteral("Audio out")));
    return n;
}

// Every node in drawing order, positions unset.
QList<NodeItem> buildNodes(const ForgeDoc &doc)
{
    Rack rack = rackOf(doc);
    QList<NodeItem> out;
    out.append(inputNode());
    walkRack(static_cast<const std::vector<RackItem> &>(rack.chain), [&](const RackItem &item, int) {
        if (item.isSplit()) {
            out.append(splitNode(doc, item));
            out.append(mergeNode(item));
        } else {
            out.append(pedalNode(doc, rack, item));
        }
    });
    for (const Modulator &m : std::as_const(rack.modulators))
        out.append(modulatorNode(doc, rack, m));
    out.append(outputNode());
    return out;
}

qreal heightOf(const NodeItem &n)
{
    return kChrome + qreal(n.inputs.size() + n.outputs.size()) * kRowHeight;
}

struct Placer
{
    QMap<QString, qreal> heights;
    QMap<QString, QPointF> out;

    QSizeF chain(const std::vector<RackItem> &items, qreal x, qreal y)
    {
        qreal cx = 0;
        qreal tallest = 0;
        for (const RackItem &item : items) {
            out[item.id] = {x + cx, y};
            tallest = std::max(tallest, heights.value(item.id));
            cx += kColumn;
            if (!item.isSplit())
                continue;
            qreal ly = 0;
            qreal widest = 0;
            for (const SplitLane &lane : item.lanes) {
                const QSizeF s = chain(lane.chain, x + cx, y + ly);
                widest = std::max(widest, s.width());
                ly += std::max(s.height(), kRowHeight * 2) + kLaneGap;
            }
            const QString merge = mergeId(item.id);
            out[merge] = {x + cx + std::max(widest, qreal(0)), y};
            tallest = std::max({tallest, ly - kLaneGap, heights.value(merge)});
            cx += std::max(widest, qreal(0)) + kColumn;
        }
        return {cx, tallest};
    }
};

QString fromPortKey(const QString &node, const QString &port)
{
    return node + QLatin1Char('.') + port;
}

void chainEdges(const std::vector<RackItem> &chain, const QString &fromNode, const QString &fromPort,
                const QString &toNode, const QString &toPort, QList<EdgeItem> &out)
{
    const auto wire = [&](const QString &a, const QString &ap, const QString &b, const QString &bp) {
        out.append({QStringLiteral("e:%1>%2").arg(fromPortKey(a, ap), fromPortKey(b, bp)), a, ap, b, bp,
                    QStringLiteral("audio")});
    };
    QString prevNode = fromNode;
    QString prevPort = fromPort;
    for (const RackItem &item : chain) {
        wire(prevNode, prevPort, item.id, QStringLiteral("in"));
        if (!item.isSplit()) {
            prevNode = item.id;
            prevPort = QStringLiteral("out");
            continue;
        }
        for (size_t i = 0; i < item.lanes.size(); ++i)
            chainEdges(item.lanes[i].chain, item.id, laneId(int(i)), mergeId(item.id), laneId(int(i)), out);
        prevNode = mergeId(item.id);
        prevPort = QStringLiteral("out");
    }
    wire(prevNode, prevPort, toNode, toPort);
}

bool sameRack(const ForgeDoc &a, const ForgeDoc &b)
{
    return rackOf(a).toJson() == rackOf(b).toJson();
}

struct Source
{
    Slot slot;
    // The item whose output this is; empty for the input node and a split's lane outputs.
    QString item;
};

std::optional<Source> sourceOf(Rack &rack, const QString &node, const QString &port)
{
    if (node == QLatin1String(kInputId))
        return port == QLatin1String("out") ? std::optional<Source>(Source{{QString(), 0}, {}}) : std::nullopt;
    const QString mergeOf = splitOfMerge(node);
    if (!mergeOf.isEmpty()) {
        if (port != QLatin1String("out"))
            return std::nullopt;
        const Located at = findItem(rack, mergeOf);
        if (!at.item || !at.item->isSplit())
            return std::nullopt;
        return Source{{at.lane ? at.lane->id : QString(), at.index + 1}, mergeOf};
    }
    const Located at = findItem(rack, node);
    if (!at.item)
        return std::nullopt;
    if (at.item->isSplit()) {
        for (size_t i = 0; i < at.item->lanes.size(); ++i)
            if (port == laneId(int(i)))
                return Source{{at.item->lanes[i].id, 0}, {}};
        return std::nullopt;
    }
    if (port != QLatin1String("out"))
        return std::nullopt;
    return Source{{at.lane ? at.lane->id : QString(), at.index + 1}, node};
}

struct Sink
{
    // Set for a pedal or split input: that item goes into the source's slot.
    QString item;
    // Otherwise the chain the wire ends: the item it comes from becomes its last.
    Slot end;
};

std::optional<Sink> sinkOf(Rack &rack, const QString &node, const QString &port)
{
    if (node == QLatin1String(kOutputId))
        return port == QLatin1String("in") ? std::optional<Sink>(Sink{{}, {QString(), -1}}) : std::nullopt;
    const QString mergeOf = splitOfMerge(node);
    if (!mergeOf.isEmpty()) {
        RackItem *split = splitAt(rack, mergeOf);
        if (!split)
            return std::nullopt;
        for (size_t i = 0; i < split->lanes.size(); ++i)
            if (port == laneId(int(i)))
                return Sink{{}, {split->lanes[i].id, -1}};
        return std::nullopt;
    }
    const Located at = findItem(rack, node);
    if (!at.item || port != QLatin1String("in"))
        return std::nullopt;
    return Sink{node, {}};
}

OpResult connectAudio(const ForgeDoc &doc, const QString &fromNode, const QString &fromPort, const QString &toNode,
                      const QString &toPort)
{
    Rack rack = rackOf(doc);
    const std::optional<Source> from = sourceOf(rack, fromNode, fromPort);
    const std::optional<Sink> to = sinkOf(rack, toNode, toPort);
    if (!from || !to)
        return OpResult::fail(QStringLiteral("Those two ports can't be wired together."));
    OpResult moved;
    if (!to->item.isEmpty()) {
        if (from->item == to->item)
            return OpResult::fail(QStringLiteral("A pedal can't feed itself."));
        moved = moveItem(doc, to->item, from->slot);
    } else {
        if (from->item.isEmpty())
            return OpResult::fail(QStringLiteral("Wire a pedal's output into the end of a chain."));
        moved = moveItem(doc, from->item, to->end);
    }
    if (!moved.ok())
        return moved;
    if (sameRack(doc, moved.doc))
        return OpResult::fail(QStringLiteral("Already wired that way."));
    return moved;
}

OpResult connectMod(const ForgeDoc &doc, const QString &fromNode, const QString &toNode, const QString &toPort)
{
    Rack rack = rackOf(doc);
    const bool isModulator = std::any_of(rack.modulators.cbegin(), rack.modulators.cend(),
                                         [&](const Modulator &m) { return m.id == fromNode; });
    if (!isModulator)
        return OpResult::fail(QStringLiteral("Only modulators feed knobs."));
    for (const ModRoute &r : std::as_const(rack.routes))
        if (r.from == fromNode && r.to == toNode && r.knob == toPort)
            return OpResult::fail(QStringLiteral("Already wired that way."));
    return addRoute(doc, fromNode, toNode, toPort);
}

QMap<QString, QPointF> merged(const QMap<QString, QPointF> &base, const QMap<QString, QPointF> &over)
{
    QMap<QString, QPointF> out = base;
    for (auto it = over.begin(); it != over.end(); ++it)
        out[it.key()] = it.value();
    return out;
}

ForgeDoc dropLayout(const ForgeDoc &doc, const QStringList &ids)
{
    QMap<QString, QPointF> layout = storedLayout(doc);
    for (const QString &id : ids)
        layout.remove(id);
    return withLayout(doc, layout);
}

void placeNew(ForgeDoc &doc, const QString &id, const QPointF &at)
{
    QMap<QString, QPointF> layout = layoutOf(doc);
    layout[id] = at;
    Rack rack = rackOf(doc);
    if (splitAt(rack, id))
        layout[mergeId(id)] = at + QPointF(kColumn, 0);
    doc = withLayout(doc, layout);
}

}

QString mergeId(const QString &splitId)
{
    return splitId + kMergeSuffix;
}

QString splitOfMerge(const QString &nodeId)
{
    return nodeId.endsWith(kMergeSuffix) ? nodeId.chopped(kMergeSuffix.size()) : QString();
}

QMap<QString, QPointF> storedLayout(const ForgeDoc &doc)
{
    QMap<QString, QPointF> out;
    const QJsonObject o = doc.extra.value(kLayoutKey).toObject();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonArray a = it.value().toArray();
        if (a.size() >= 2)
            out.insert(it.key(), QPointF(a.at(0).toDouble(), a.at(1).toDouble()));
    }
    return out;
}

ForgeDoc withLayout(const ForgeDoc &doc, const QMap<QString, QPointF> &layout)
{
    ForgeDoc d = doc;
    QJsonObject o;
    for (auto it = layout.begin(); it != layout.end(); ++it)
        o[it.key()] = QJsonArray{std::round(it.value().x()), std::round(it.value().y())};
    d.extra[kLayoutKey] = o;
    return d;
}

QMap<QString, QPointF> autoLayout(const ForgeDoc &doc)
{
    const QList<NodeItem> nodes = buildNodes(doc);
    Placer p;
    for (const NodeItem &n : nodes)
        p.heights[n.nodeId] = heightOf(n);
    const Rack rack = rackOf(doc);
    constexpr qreal x0 = 40;
    constexpr qreal y0 = 80;
    const QSizeF size = p.chain(rack.chain, x0 + 200, y0);
    p.out[kInputId] = {x0, y0};
    p.out[kOutputId] = {x0 + 200 + size.width(), y0};
    qreal x = x0;
    const qreal y = y0 + std::max(size.height(), qreal(100)) + 80;
    for (const Modulator &m : rack.modulators) {
        p.out[m.id] = {x, y};
        x += kColumn;
    }
    return p.out;
}

QMap<QString, QPointF> layoutOf(const ForgeDoc &doc)
{
    return merged(autoLayout(doc), storedLayout(doc));
}

QMap<QString, QString> nodeErrors(const ForgeDoc &doc)
{
    QMap<QString, QString> out;
    QStringList remaining = validateRack(doc);
    if (remaining.isEmpty())
        return out;
    Rack rack = rackOf(doc);
    const auto claim = [&](const QString &id, Rack single) {
        ForgeDoc probe = withRack(doc, single);
        const QStringList own = validateRack(probe);
        if (own.isEmpty())
            return;
        out[id] = own.join(QLatin1Char('\n'));
        for (const QString &m : own)
            remaining.removeAll(m);
    };
    walkRack(rack.chain, [&](const Located &at) {
        Rack single;
        RackItem item = *at.item;
        if (item.isSplit())
            for (SplitLane &l : item.lanes)
                l.chain.clear();
        single.chain.push_back(item);
        claim(item.id, single);
    });
    for (const Modulator &m : std::as_const(rack.modulators)) {
        Rack single;
        single.modulators.append(m);
        Modulator &copy = single.modulators.first();
        if (copy.type == QLatin1String("envelope"))
            copy.source = QStringLiteral("input");
        claim(m.id, single);
    }
    if (!remaining.isEmpty())
        out[kOutputId] = remaining.join(QLatin1Char('\n'));
    return out;
}

QList<NodeItem> nodeItems(const ForgeDoc &doc)
{
    QList<NodeItem> nodes = buildNodes(doc);
    const QMap<QString, QPointF> layout = layoutOf(doc);
    const QMap<QString, QString> errors = nodeErrors(doc);
    for (NodeItem &n : nodes) {
        const QPointF at = layout.value(n.nodeId);
        n.x = at.x();
        n.y = at.y();
        n.error = errors.value(n.nodeId);
    }
    return nodes;
}

QList<EdgeItem> edgeItems(const ForgeDoc &doc)
{
    const Rack rack = rackOf(doc);
    QList<EdgeItem> out;
    chainEdges(rack.chain, kInputId, QStringLiteral("out"), kOutputId, QStringLiteral("in"), out);
    for (const ModRoute &r : rack.routes)
        out.append({r.id, r.from, QStringLiteral("out"), r.to, r.knob, QStringLiteral("mod")});
    return out;
}

QList<NodeGraphController::CatalogEntry> catalogEntries()
{
    QList<NodeGraphController::CatalogEntry> out;
    const auto pedalEntry = [](const PedalSpec &spec, bool modulator) {
        NodeGraphController::CatalogEntry e;
        e.type = QString::fromLatin1(spec.type);
        e.label = QString::fromLatin1(spec.label);
        e.category = QString::fromLatin1(spec.category);
        e.accent = accentFor(e.category);
        if (modulator) {
            e.outputs.append({QStringLiteral("out"), QStringLiteral("mod")});
            return e;
        }
        e.inputs.append({QStringLiteral("in"), QStringLiteral("audio")});
        for (const KnobSpec &k : spec.knobs)
            if (k.continuous())
                e.inputs.append({QString::fromLatin1(k.id), QStringLiteral("mod")});
        e.outputs.append({QStringLiteral("out"), QStringLiteral("audio")});
        return e;
    };
    for (const PedalSpec &spec : audiofx::pedalSpecs())
        out.append(pedalEntry(spec, false));
    for (const std::pair<QString, QString> &s : {std::pair{QStringLiteral("split:parallel"), QStringLiteral("Split (parallel)")},
                                                 std::pair{QStringLiteral("split:bands"), QStringLiteral("Split (bands)")}}) {
        NodeGraphController::CatalogEntry e;
        e.type = s.first;
        e.label = s.second;
        e.category = QStringLiteral("split");
        e.accent = accentFor(e.category);
        e.description = s.first.endsWith(QLatin1String("bands"))
                            ? QStringLiteral("Cuts the sound into frequency bands, one lane each.")
                            : QStringLiteral("Runs the sound through two to four lanes at once and mixes them.");
        e.inputs.append({QStringLiteral("in"), QStringLiteral("audio")});
        e.outputs.append({QStringLiteral("out"), QStringLiteral("audio")});
        out.append(e);
    }
    for (const PedalSpec &spec : audiofx::modulatorSpecs())
        out.append(pedalEntry(spec, true));
    return out;
}

QString portType(const ForgeDoc &doc, const QString &node, const QString &port, bool isOutput)
{
    for (const NodeItem &n : buildNodes(doc)) {
        if (n.nodeId != node)
            continue;
        for (const QVariant &v : isOutput ? n.outputs : n.inputs)
            if (v.toMap().value("id").toString() == port)
                return v.toMap().value("type").toString();
    }
    return {};
}

OpResult connect(const ForgeDoc &doc, const QString &fromNode, const QString &fromPort, const QString &toNode,
                 const QString &toPort)
{
    if (fromNode == toNode)
        return OpResult::fail(QStringLiteral("A pedal can't feed itself."));
    const QString outType = portType(doc, fromNode, fromPort, true);
    const QString inType = portType(doc, toNode, toPort, false);
    if (outType.isEmpty() || inType.isEmpty() || outType != inType || outType == QLatin1String("float"))
        return OpResult::fail(QStringLiteral("Those two ports can't be wired together."));
    if (outType == QLatin1String("mod"))
        return connectMod(doc, fromNode, toNode, toPort);
    return connectAudio(doc, fromNode, fromPort, toNode, toPort);
}

bool canConnect(const ForgeDoc &doc, const QString &fromNode, const QString &fromPort, const QString &toNode,
                const QString &toPort)
{
    return connect(doc, fromNode, fromPort, toNode, toPort).ok();
}

ForgeDoc disconnectInput(const ForgeDoc &doc, const QString &node, const QString &port)
{
    ForgeDoc out = doc;
    const Rack rack = rackOf(doc);
    for (const ModRoute &r : rack.routes)
        if (r.to == node && r.knob == port)
            out = removeRoute(out, r.id);
    return out;
}

ForgeDoc deleteNodes(const ForgeDoc &doc, const QStringList &ids)
{
    ForgeDoc out = doc;
    QStringList gone;
    const Rack rack = rackOf(doc);
    for (const QString &id : ids) {
        const bool isModulator = std::any_of(rack.modulators.cbegin(), rack.modulators.cend(),
                                             [&](const Modulator &m) { return m.id == id; });
        Rack probe = rackOf(out);
        if (isModulator) {
            out = removeModulator(out, id);
            gone << id;
        } else if (findItem(probe, id).item) {
            if (splitAt(probe, id))
                gone << mergeId(id);
            out = removeItem(out, id);
            gone << id;
        }
    }
    return gone.isEmpty() ? doc : dropLayout(out, gone);
}

OpResult addNode(const ForgeDoc &doc, const QString &type, const QPointF &at)
{
    OpResult r;
    if (type.startsWith(QLatin1String("split:")))
        r = addSplit(doc, type.mid(6), 2);
    else if (audiofx::modulatorSpec(type.toStdString()))
        r = addModulator(doc, type);
    else
        r = addPedal(doc, type);
    if (!r.ok())
        return r;
    placeNew(r.doc, r.created.value(0), at);
    return r;
}

OpResult addNodeConnected(const ForgeDoc &doc, const QString &type, const QPointF &at, const QString &fromNode,
                          const QString &fromPort, bool fromIsOutput)
{
    OpResult added = addNode(doc, type, at);
    if (!added.ok())
        return added;
    const QString created = added.created.value(0);
    const QString dragged = portType(doc, fromNode, fromPort, fromIsOutput);
    const bool split = type.startsWith(QLatin1String("split:"));
    const QString madeNode = created;

    for (const NodeGraphController::CatalogEntry &e : catalogEntries()) {
        if (e.type != type)
            continue;
        const auto &sockets = fromIsOutput ? e.inputs : e.outputs;
        for (const NodeGraphController::CatalogSocket &s : sockets) {
            if (s.type != dragged)
                continue;
            const QString socketNode = split && !fromIsOutput ? mergeId(madeNode) : madeNode;
            const OpResult wired = fromIsOutput ? connect(added.doc, fromNode, fromPort, madeNode, s.id)
                                                : connect(added.doc, socketNode, s.id, fromNode, fromPort);
            if (wired.ok()) {
                added.doc = wired.doc;
                return added;
            }
        }
        break;
    }
    return added;
}

ForgeDoc moveNodes(const ForgeDoc &doc, const QStringList &ids, qreal dx, qreal dy)
{
    QMap<QString, QPointF> layout = layoutOf(doc);
    for (const QString &id : ids)
        if (layout.contains(id))
            layout[id] += QPointF(dx, dy);
    return withLayout(doc, layout);
}

std::optional<ValuePath> valuePathOf(const ForgeDoc &doc, const QString &node, const QString &input)
{
    Rack rack = rackOf(doc);
    if (const RackItem *item = pedalAt(rack, node)) {
        if (input == QLatin1String("bypass"))
            return ValuePath::bypassOf(node);
        const PedalSpec *spec = audiofx::pedalSpec(item->type.toStdString());
        if (spec && audiofx::findKnob(*spec, input.toStdString()))
            return ValuePath::knobOf(node, input);
        return std::nullopt;
    }
    if (splitAt(rack, node)) {
        if (input == QLatin1String("blend"))
            return ValuePath::blendOf(node);
        if (input.startsWith(QLatin1String("crossover")))
            return ValuePath::crossoverOf(node, input.mid(9).toInt());
        return std::nullopt;
    }
    for (const Modulator &m : std::as_const(rack.modulators)) {
        if (m.id != node)
            continue;
        const PedalSpec *spec = audiofx::modulatorSpec(m.type.toStdString());
        if (spec && audiofx::findKnob(*spec, input.toStdString()))
            return ValuePath::modKnobOf(node, input);
    }
    return std::nullopt;
}

ForgeDoc setInputValue(const ForgeDoc &doc, const QString &node, const QString &input, const QVariant &value)
{
    if (input == QLatin1String("ir"))
        return setIr(doc, node, value.toString());
    if (input == QLatin1String("source"))
        return setModulatorSource(doc, node, value.toString());
    if (input == QLatin1String("mode"))
        return setSplitMode(doc, node, value.toString());
    if (input == QLatin1String("crossfade")) {
        const OpResult r = setCrossfade(doc, node, value.toBool());
        return r.ok() ? r.doc : doc;
    }
    const std::optional<ValuePath> path = valuePathOf(doc, node, input);
    if (!path)
        return doc;
    return setValue(doc, *path, value.typeId() == QMetaType::Bool ? (value.toBool() ? 1.0 : 0.0) : value.toDouble());
}

}
