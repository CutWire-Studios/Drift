#include "app/forge/edit/ParamOps.h"

#include "app/forge/doc/Naming.h"
#include "app/forge/doc/ParamTypes.h"
#include "app/forge/doc/Util.h"
#include "app/forge/nodes/Registry.h"

#include <QRegularExpression>

#include <algorithm>
#include <functional>

namespace drift::forge {

namespace {

using ValueFn = std::function<QJsonValue(const QJsonValue &)>;

QJsonArray mapChain(const QJsonArray &chain, const ValueFn &f);

QJsonObject mapKnobs(const QJsonObject &knobs, const ValueFn &f)
{
    QJsonObject out;
    for (auto it = knobs.begin(); it != knobs.end(); ++it)
        out.insert(it.key(), f(it.value()));
    return out;
}

QJsonArray mapChain(const QJsonArray &chain, const ValueFn &f)
{
    QJsonArray out;
    for (const QJsonValue &v : chain) {
        QJsonObject item = v.toObject();
        if (item.value("type").toString() == QLatin1String("split")) {
            if (item.contains("blend"))
                item["blend"] = f(item.value("blend"));
            if (item.contains("crossovers")) {
                QJsonArray xs;
                for (const QJsonValue &c : item.value("crossovers").toArray())
                    xs.append(f(c));
                item["crossovers"] = xs;
            }
            QJsonArray lanes;
            for (const QJsonValue &l : item.value("lanes").toArray()) {
                QJsonObject lane = l.toObject();
                lane["chain"] = mapChain(lane.value("chain").toArray(), f);
                lanes.append(lane);
            }
            item["lanes"] = lanes;
        } else {
            item["knobs"] = mapKnobs(item.value("knobs").toObject(), f);
            if (item.contains("bypass"))
                item["bypass"] = f(item.value("bypass"));
        }
        out.append(item);
    }
    return out;
}

// Every slot in a rack that can hold a value or a slider binding.
QJsonObject mapRack(const QJsonObject &rack, const ValueFn &f)
{
    QJsonObject out = rack;
    out["chain"] = mapChain(rack.value("chain").toArray(), f);
    QJsonArray mods;
    for (const QJsonValue &m : rack.value("modulators").toArray()) {
        QJsonObject mod = m.toObject();
        mod["knobs"] = mapKnobs(mod.value("knobs").toObject(), f);
        mods.append(mod);
    }
    out["modulators"] = mods;
    QJsonArray routes;
    for (const QJsonValue &r : rack.value("routes").toArray()) {
        QJsonObject route = r.toObject();
        route["depth"] = f(route.value("depth"));
        routes.append(route);
    }
    out["routes"] = routes;
    return out;
}

void mapAudio(ForgeDoc &d, const ValueFn &f)
{
    if (!d.audio)
        return;
    (*d.audio)["rack"] = mapRack(d.audio->value("rack").toObject(), f);
}

QString camel(const QString &label)
{
    static const QRegularExpression parens(QStringLiteral("\\(.*?\\)"));
    static const QRegularExpression nonAlnum(QStringLiteral("[^A-Za-z0-9 ]+"));
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    static const QRegularExpression startsAlpha(QStringLiteral("^[A-Za-z]"));
    QString t = label;
    t.replace(parens, QString());
    t.replace(nonAlnum, QStringLiteral(" "));
    const QStringList words = t.trimmed().split(ws, Qt::SkipEmptyParts);
    QString s;
    for (int i = 0; i < words.size(); ++i) {
        const QString &w = words[i];
        s += i ? w.left(1).toUpper() + w.mid(1).toLower() : w.toLower();
    }
    return startsAlpha.match(s).hasMatch() ? s : QStringLiteral("p") + s;
}

double jsNumber(const QJsonValue &v)
{
    if (v.isBool())
        return v.toBool() ? 1 : 0;
    return v.toDouble();
}

double firstNumber(const QJsonValue &v)
{
    if (v.isArray())
        return v.toArray().at(0).toDouble();
    return jsNumber(v);
}

QString inputUnit(const InputDef &input)
{
    if (!input.unit.isEmpty())
        return input.unit;
    static const QRegularExpression re(QStringLiteral("\\((px|s|%)\\)"));
    const auto m = re.match(input.label);
    return m.hasMatch() ? m.captured(1) : QString();
}

// Display hints for Drift's inspector; today's Drift ignores them, so they're always written.
QJsonObject uiFor(const InputDef &input)
{
    QJsonObject ui;
    if (input.widget == QLatin1String("angle")) {
        ui["control"] = QStringLiteral("angle");
        ui["unit"] = QStringLiteral("°");
    }
    const QString unit = inputUnit(input);
    if (!unit.isEmpty())
        ui["unit"] = unit;
    if (input.integer)
        ui["step"] = 1;
    if (input.widget == QLatin1String("seed"))
        ui["control"] = QStringLiteral("seed");
    return ui;
}

// Puts every input/option bound to `identifier` back to a fixed value.
void releaseParam(ForgeDoc &d, const QString &identifier)
{
    const ParamDef *found = d.param(identifier);
    if (!found)
        return;
    const ParamDef p = *found;
    const ForgeDoc snapshot = d;
    for (ForgeNode &n : d.nodes) {
        for (auto it = n.inputs.begin(); it != n.inputs.end(); ++it) {
            const QJsonValue v = it.value();
            if (!isParamRef(v))
                continue;
            const QJsonValue ref = v.toObject().value("param");
            if (ref.isArray()) {
                const QStringList names = paramRefNames(v);
                if (!names.contains(identifier))
                    continue;
                QJsonArray lit;
                for (const QString &name : names) {
                    const ParamDef *q = snapshot.param(name);
                    lit.append(q ? jsNumber(q->defaultValue) : 0.0);
                }
                it.value() = lit;
            } else if (ref.toString() == identifier) {
                it.value() = paramLiteral(p);
            }
        }
        const QStringList keys = n.data.keys();
        for (const QString &k : keys) {
            const QJsonValue v = n.data.value(k);
            if (isParamRef(v) && v.toObject().value("param").toString() == identifier) {
                if (isTrackParam(p))
                    n.data.remove(k);
                else
                    n.data.insert(k, p.defaultValue);
            }
        }
    }
    mapAudio(d, [&](const QJsonValue &v) -> QJsonValue {
        if (isParamRef(v) && v.toObject().value("param").toString() == identifier)
            return p.defaultValue.isBool() ? p.defaultValue : QJsonValue(jsNumber(p.defaultValue));
        return v;
    });
}

// The slider an input becomes, shaped by its type and widget.
bool inputSlider(const InputDef &input, const QJsonValue &cur, const QStringList &labels, ParamDef &p, QString *error)
{
    const QJsonObject ui = uiFor(input);
    p.min = input.min.value_or(0);
    p.max = input.max.value_or(1);
    p.ui = ui;
    const double num = firstNumber(cur);
    if (input.type == SocketType::Color) {
        if (input.widget != QLatin1String("swatch")) {
            *error = QStringLiteral("Image inputs can't be sliders; connect an image instead.");
            return false;
        }
        const QJsonArray c = cur.isArray() && cur.toArray().size() == 4 ? cur.toArray() : QJsonArray{1, 1, 1, 1};
        p.type = ParamType::Color;
        p.min = 0;
        p.max = 1;
        p.ui = {};
        p.defaultValue = rgbToHex(c);
        return true;
    }
    if (input.type == SocketType::Vec2) {
        const QJsonArray v = cur.isArray() ? cur.toArray() : QJsonArray{num, num};
        p.type = ParamType::Point;
        p.defaultValue = QJsonArray{v.at(0), v.at(1)};
        return true;
    }
    p.ui = {};
    if (input.widget == QLatin1String("toggle")) {
        p.type = ParamType::Bool;
        p.min = 0;
        p.max = 1;
        p.defaultValue = num > 0.5;
        return true;
    }
    if (input.widget == QLatin1String("choice")) {
        p.type = ParamType::Choice;
        p.min = 0;
        p.max = std::max<double>(labels.size() - 1, 1);
        p.options = labels;
        p.defaultValue = jsRound(num);
        return true;
    }
    p.ui = ui;
    if (input.widget == QLatin1String("seed")) {
        p.type = ParamType::Seed;
        p.defaultValue = jsRound(num);
    } else if (input.integer) {
        p.type = ParamType::Int;
        p.defaultValue = jsRound(num);
    } else {
        p.type = ParamType::Float;
        p.defaultValue = num;
    }
    return true;
}

bool truthy(const QJsonValue &v)
{
    if (v.isUndefined() || v.isNull())
        return false;
    if (v.isString())
        return !v.toString().isEmpty();
    if (v.isBool())
        return v.toBool();
    if (v.isDouble())
        return v.toDouble() != 0;
    return true;
}

bool jsonDefined(const QJsonValue &v)
{
    return !v.isUndefined() && !v.isNull();
}

}

QString uniqueParamName(const ForgeDoc &doc, const QString &base)
{
    QSet<QString> taken;
    for (const ParamDef &p : doc.params)
        taken.insert(p.identifier);
    QString name = camel(base);
    if (name.isEmpty())
        name = QStringLiteral("value");
    if (!paramNameProblem(name).isNull())
        name += QStringLiteral("Value");
    if (!paramNameProblem(name).isNull())
        name = QStringLiteral("value");
    QString n = name;
    for (int i = 2; taken.contains(n); ++i)
        n = name + QString::number(i);
    return n;
}

int paramUsers(const ForgeDoc &doc, const QString &name)
{
    int n = 0;
    for (const ForgeNode &node : doc.nodes) {
        for (const QJsonObject &o : {node.inputs, node.data}) {
            for (auto it = o.begin(); it != o.end(); ++it)
                if (paramRefNames(it.value()).contains(name))
                    ++n;
        }
    }
    if (doc.audio) {
        ForgeDoc copy;
        copy.audio = doc.audio;
        mapAudio(copy, [&](const QJsonValue &v) {
            if (isParamRef(v) && v.toObject().value("param").toString() == name)
                ++n;
            return v;
        });
    }
    return n;
}

OpResult expose(const ForgeDoc &doc, const QString &nodeId, const QString &inputId, const QString &labelOverride)
{
    const ForgeNode *node = doc.node(nodeId);
    const NodeDef *def = node ? nodeDef(node->type) : nullptr;
    const InputDef *input = def ? def->input(inputId) : nullptr;
    if (!node || !input || input->noExpose)
        return OpResult::fail(QStringLiteral("That setting can't be a slider."));
    const QJsonValue stored = node->inputs.value(inputId);
    const QJsonValue cur = jsonDefined(stored) ? stored : input->defaultValue;
    if (isParamRef(cur))
        return {doc, {}, paramRefNames(cur)};
    QStringList labels;
    for (const QJsonValue &l : node->data.value("labels").toArray())
        labels << l.toString();
    ParamDef param;
    QString error;
    if (!inputSlider(*input, cur, labels, param, &error))
        return OpResult::fail(error);
    static const QRegularExpression parens(QStringLiteral("\\s*\\(.*?\\)"));
    QString label = labelOverride.isNull() ? input->label : labelOverride;
    label.replace(parens, QString());
    param.identifier = uniqueParamName(doc, label);
    param.displayName = label;
    ForgeDoc d = doc;
    d.params << param;
    d.node(nodeId)->inputs.insert(inputId, makeParamRef(param.identifier));
    return {d, {}, {param.identifier}};
}

ForgeDoc unexpose(const ForgeDoc &doc, const QString &nodeId, const QString &inputId)
{
    const ForgeNode *node = doc.node(nodeId);
    const QJsonValue v = node ? node->inputs.value(inputId) : QJsonValue();
    if (!isParamRef(v))
        return doc;
    const QStringList names = paramRefNames(v);
    ForgeDoc d = doc;
    QJsonValue lit;
    if (names.size() == 2) {
        const ParamDef *a = d.param(names[0]);
        const ParamDef *b = d.param(names[1]);
        lit = QJsonArray{a ? jsNumber(a->defaultValue) : 0.0, b ? jsNumber(b->defaultValue) : 0.0};
    } else {
        const ParamDef *p = d.param(names.value(0));
        lit = p ? paramLiteral(*p) : QJsonValue(0);
    }
    d.node(nodeId)->inputs.insert(inputId, lit);
    for (const QString &name : names) {
        if (!paramUsers(d, name))
            d.params.removeIf([&](const ParamDef &p) { return p.identifier == name; });
    }
    return d;
}

OpResult exposeOption(const ForgeDoc &doc, const QString &nodeId, const QString &optionId)
{
    const ForgeNode *node = doc.node(nodeId);
    const NodeDef *def = node ? nodeDef(node->type) : nullptr;
    const OptionDef *option = def ? def->option(optionId) : nullptr;
    if (!node || !option || !option->exposable())
        return OpResult::fail(QStringLiteral("That setting can't be a slider."));
    const QJsonValue stored = node->data.value(optionId);
    const QJsonValue cur = jsonDefined(stored) ? stored : option->defaultValue;
    if (isParamRef(cur) && !cur.toObject().value("param").isArray())
        return {doc, {}, {cur.toObject().value("param").toString()}};
    ParamDef p;
    p.min = 0;
    p.max = 1;
    switch (option->kind) {
    case OptionKind::Asset:
        if (!truthy(cur))
            return OpResult::fail(QStringLiteral("Pick a picture first; it becomes the default."));
        p.type = ParamType::Image;
        p.defaultValue = cur.isString() ? cur.toString() : QString::number(cur.toDouble());
        break;
    case OptionKind::Region:
        p.type = ParamType::Region;
        p.defaultValue = cur;
        p.shape = node->data.value("shape").toString() == QLatin1String("rect") ? QStringLiteral("rect") : QStringLiteral("ellipse");
        break;
    case OptionKind::Gradient:
        p.type = ParamType::Gradient;
        p.defaultValue = cur;
        break;
    default:
        p.type = ParamType::Curve;
        p.defaultValue = cur;
        break;
    }
    p.identifier = uniqueParamName(doc, option->label);
    p.displayName = option->label;
    ForgeDoc d = doc;
    d.params << p;
    d.node(nodeId)->data.insert(optionId, makeParamRef(p.identifier));
    return {d, {}, {p.identifier}};
}

ForgeDoc unexposeOption(const ForgeDoc &doc, const QString &nodeId, const QString &optionId)
{
    const ForgeNode *node = doc.node(nodeId);
    const QJsonValue v = node ? node->data.value(optionId) : QJsonValue();
    if (!isParamRef(v) || v.toObject().value("param").isArray())
        return doc;
    const QString name = v.toObject().value("param").toString();
    ForgeDoc d = doc;
    const ParamDef *p = d.param(name);
    if (p)
        d.node(nodeId)->data.insert(optionId, p->defaultValue);
    else
        d.node(nodeId)->data.remove(optionId);
    if (!paramUsers(d, name))
        d.params.removeIf([&](const ParamDef &q) { return q.identifier == name; });
    return d;
}

ForgeDoc addParam(const ForgeDoc &doc, const ParamDef &p)
{
    ForgeDoc d = doc;
    d.params << p;
    return d;
}

OpResult updateParam(const ForgeDoc &doc, const QString &identifier, const QJsonObject &patch)
{
    const bool renamed = patch.contains("identifier") && patch.value("identifier").toString() != identifier;
    const QString to = patch.value("identifier").toString();
    if (renamed) {
        const QString problem = paramNameProblem(to);
        if (!problem.isNull())
            return OpResult::fail(problem);
        if (doc.param(to))
            return OpResult::fail(QStringLiteral("Another slider already uses that name."));
        for (const ParamDef &p : doc.params) {
            if (p.type == ParamType::Audio && audioScalarNames(p.identifier).contains(to))
                return OpResult::fail(QStringLiteral("An audio slider's level, bass and beat already use that name."));
        }
    }
    ForgeDoc d = doc;
    ParamDef *p = d.param(identifier);
    if (!p)
        return {d, {}, {}};
    QJsonObject merged = paramToJson(*p);
    for (auto it = patch.begin(); it != patch.end(); ++it)
        merged.insert(it.key(), it.value());
    *p = paramFromJson(merged);
    if (!renamed)
        return {d, {}, {}};

    const auto swap = [&](const QJsonValue &v) -> QJsonValue {
        if (!isParamRef(v))
            return v;
        const QJsonValue ref = v.toObject().value("param");
        if (ref.isArray()) {
            QJsonArray a;
            for (const QString &x : paramRefNames(v))
                a.append(x == identifier ? to : x);
            return QJsonObject{{"param", a}};
        }
        return ref.toString() == identifier ? QJsonValue(makeParamRef(to)) : v;
    };
    for (ForgeNode &n : d.nodes) {
        for (auto it = n.inputs.begin(); it != n.inputs.end(); ++it)
            it.value() = swap(it.value());
        for (auto it = n.data.begin(); it != n.data.end(); ++it)
            it.value() = swap(it.value());
    }
    mapAudio(d, swap);
    for (ParamDef &q : d.params)
        if (q.showWhen && q.showWhen->param == identifier)
            q.showWhen->param = to;
    if (d.presets) {
        for (Preset &pr : *d.presets) {
            if (pr.values.contains(identifier)) {
                pr.values.insert(to, pr.values.value(identifier));
                pr.values.remove(identifier);
            }
        }
    }
    return {d, {}, {}};
}

ForgeDoc removeParam(const ForgeDoc &doc, const QString &identifier)
{
    ForgeDoc d = doc;
    releaseParam(d, identifier);
    d.params.removeIf([&](const ParamDef &p) { return p.identifier == identifier; });
    for (ParamDef &q : d.params)
        if (q.showWhen && q.showWhen->param == identifier)
            q.showWhen.reset();
    if (d.presets)
        for (Preset &pr : *d.presets)
            pr.values.remove(identifier);
    return d;
}

ForgeDoc moveParam(const ForgeDoc &doc, const QString &identifier, int delta)
{
    ForgeDoc d = doc;
    const auto it = std::find_if(d.params.begin(), d.params.end(), [&](const ParamDef &p) { return p.identifier == identifier; });
    const int i = it == d.params.end() ? -1 : int(it - d.params.begin());
    const int j = i + delta;
    if (i < 0 || j < 0 || j >= d.params.size())
        return d;
    d.params.move(i, j);
    return d;
}

ForgeDoc savePreset(const ForgeDoc &doc, const QString &name, const QJsonObject &values)
{
    QJsonObject kept;
    for (const ParamDef &p : doc.params) {
        if (isTrackParam(p))
            continue;
        const QJsonValue v = values.value(p.identifier);
        kept.insert(p.identifier, jsonDefined(v) ? v : p.defaultValue);
    }
    ForgeDoc d = doc;
    QList<Preset> list = d.presets.value_or(QList<Preset>());
    list.removeIf([&](const Preset &p) { return p.name == name; });
    list.append({name, kept, {}});
    d.presets = list;
    return d;
}

ForgeDoc deletePreset(const ForgeDoc &doc, const QString &name)
{
    ForgeDoc d = doc;
    QList<Preset> list = d.presets.value_or(QList<Preset>());
    list.removeIf([&](const Preset &p) { return p.name == name; });
    d.presets = list;
    return d;
}

}
