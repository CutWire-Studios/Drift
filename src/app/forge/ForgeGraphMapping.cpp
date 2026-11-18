#include "app/forge/ForgeGraphMapping.h"

#include "app/forge/doc/Util.h"
#include "app/forge/nodes/Registry.h"

#include <QVector2D>
#include <QVector4D>

namespace drift::forge {

namespace {

QVariantMap inputItem(const ForgeDoc &doc, const ForgeNode &node, const InputDef &in, bool connected)
{
    QVariantMap m{{"id", in.id}, {"label", in.label}, {"type", socketTypeName(in.type)}, {"connected", connected}};
    if (!in.widget.isEmpty())
        m["widget"] = in.widget;
    if (in.min)
        m["min"] = *in.min;
    if (in.max)
        m["max"] = *in.max;
    if (in.step)
        m["step"] = *in.step;
    else if (in.integer)
        m["step"] = 1;
    if (in.widget == QLatin1String("angle")) {
        m["min"] = in.min.value_or(0);
        m["max"] = in.max.value_or(360);
    }
    if (in.widget == QLatin1String("choice")) {
        QVariantList options;
        const QJsonArray labels = node.data.value("labels").toArray();
        for (int i = 0; i < labels.size(); ++i)
            options.append(QVariantMap{{"value", i}, {"label", labels.at(i).toString()}});
        m["options"] = options;
    }
    const QJsonValue stored = node.inputs.value(in.id);
    if (isParamRef(stored)) {
        m["exposed"] = paramRefNames(stored).join(QStringLiteral(", "));
        if (const ParamDef *p = doc.param(paramRefNames(stored).value(0)))
            m["value"] = p->defaultValue.toVariant();
    } else {
        m["value"] = (stored.isUndefined() ? in.defaultValue : stored).toVariant();
    }
    return m;
}

double number(const QJsonValue &v)
{
    if (v.isBool())
        return v.toBool() ? 1 : 0;
    if (v.isArray())
        return v.toArray().at(0).toDouble();
    return v.toDouble();
}

}

QString accentForCategory(const QString &category)
{
    static const QMap<QString, QString> table{
        {"input", "emerald"}, {"output", "red"},    {"math", "neutral"},   {"color", "pink"},
        {"blur", "cyan"},     {"distort", "violet"}, {"stylize", "amber"}, {"generate", "lime"},
        {"mix", "blue"},      {"transition", "orange"}, {"animate", "indigo"}, {"three", "indigo"},
        {"advanced", "neutral"},
    };
    return table.value(category, QStringLiteral("neutral"));
}

QString socketTypeName(SocketType t)
{
    switch (t) {
    case SocketType::Float: return QStringLiteral("float");
    case SocketType::Vec2: return QStringLiteral("vec2");
    case SocketType::Color: return QStringLiteral("color");
    }
    return QStringLiteral("float");
}

QList<NodeItem> nodeItems(const ForgeDoc &doc, const QMap<QString, QString> &nodeErrors)
{
    QList<NodeItem> items;
    for (const ForgeNode &n : doc.nodes) {
        const NodeDef *def = nodeDef(n.type);
        NodeItem item;
        item.nodeId = n.id;
        item.type = n.type;
        item.x = n.x;
        item.y = n.y;
        item.width = 220;
        item.error = nodeErrors.value(n.id);
        if (!def) {
            item.title = n.type;
            item.accent = QStringLiteral("neutral");
            item.error = QStringLiteral("Unknown block type.");
            items.append(item);
            continue;
        }
        item.title = def->label;
        item.category = def->category;
        item.accent = accentForCategory(def->category);
        item.deletable = !def->output;
        for (const OutputDef &o : def->outputs) {
            bool connected = false;
            for (const ForgeEdge &e : doc.edges)
                connected = connected || (e.from == n.id && e.fromSocket == o.id);
            item.outputs.append(QVariantMap{{"id", o.id}, {"label", o.label}, {"type", socketTypeName(o.type)},
                                            {"connected", connected}});
        }
        for (const InputDef &in : def->inputs) {
            bool connected = false;
            for (const ForgeEdge &e : doc.edges)
                connected = connected || (e.to == n.id && e.toSocket == in.id);
            item.inputs.append(inputItem(doc, n, in, connected));
        }
        items.append(item);
    }
    return items;
}

QList<EdgeItem> edgeItems(const ForgeDoc &doc)
{
    QList<EdgeItem> items;
    for (const ForgeEdge &e : doc.edges) {
        EdgeItem item{e.id, e.from, e.fromSocket, e.to, e.toSocket, QStringLiteral("float")};
        const ForgeNode *from = doc.node(e.from);
        const NodeDef *def = from ? nodeDef(from->type) : nullptr;
        if (const OutputDef *o = def ? def->outputDef(e.fromSocket) : nullptr)
            item.type = socketTypeName(o->type);
        items.append(item);
    }
    return items;
}

QList<NodeGraphController::CatalogEntry> catalogEntries(Kind kind)
{
    QList<NodeGraphController::CatalogEntry> out;
    for (const NodeDef *def : allNodeDefs()) {
        if (def->output || !availableFor(*def, kind))
            continue;
        NodeGraphController::CatalogEntry e;
        e.type = def->type;
        e.label = def->label;
        e.category = def->category;
        e.description = def->description;
        e.accent = accentForCategory(def->category);
        for (const InputDef &i : def->inputs)
            e.inputs.append({i.id, socketTypeName(i.type)});
        for (const OutputDef &o : def->outputs)
            e.outputs.append({o.id, socketTypeName(o.type)});
        out.append(e);
    }
    return out;
}

QVariantList nodeOptionList(const ForgeDoc &doc, const QString &nodeId)
{
    QVariantList out;
    const ForgeNode *n = doc.node(nodeId);
    const NodeDef *def = n ? nodeDef(n->type) : nullptr;
    if (!def)
        return out;
    static const QMap<OptionKind, QString> kinds{
        {OptionKind::Select, "select"}, {OptionKind::Toggle, "toggle"}, {OptionKind::Number, "number"},
        {OptionKind::Code, "code"},     {OptionKind::Labels, "labels"}, {OptionKind::Curve, "curve"},
        {OptionKind::Gradient, "gradient"}, {OptionKind::Region, "region"}, {OptionKind::Asset, "asset"}};
    for (const OptionDef &o : def->options) {
        QVariantMap m{{"id", o.id}, {"label", o.label}, {"kind", kinds.value(o.kind)}, {"exposable", o.exposable()}};
        QVariantList choices;
        for (const SelectChoice &c : o.choices)
            choices.append(QVariantMap{{"value", c.value}, {"label", c.label}});
        m["choices"] = choices;
        m["min"] = o.min;
        m["max"] = o.max;
        m["step"] = o.step.value_or(0);
        const QJsonValue stored = n->data.value(o.id);
        if (isParamRef(stored))
            m["exposed"] = paramRefNames(stored).join(QStringLiteral(", "));
        else
            m["value"] = (stored.isUndefined() ? o.defaultValue : stored).toVariant();
        out.append(m);
    }
    return out;
}

QVariant literalUniform(const QJsonValue &lit, SocketType type)
{
    if (type == SocketType::Float)
        return number(lit);
    const QJsonArray a = lit.toArray();
    auto at = [&](int i) { return float(a.at(i).toDouble()); };
    if (type == SocketType::Vec2) {
        if (a.size() >= 2)
            return QVector2D(at(0), at(1));
        const float f = float(number(lit));
        return QVector2D(f, f);
    }
    if (a.size() >= 4)
        return QVector4D(at(0), at(1), at(2), at(3));
    if (a.size() == 3)
        return QVector4D(at(0), at(1), at(2), 1.0f);
    if (a.size() == 2)
        return QVector4D(at(0), at(1), 0.0f, 1.0f);
    const float f = float(number(lit));
    return QVector4D(f, f, f, 1.0f);
}

QMap<QString, QVariant> previewParams(const ForgeDoc &doc, const CompileResult &result,
                                      const QMap<QString, QJsonValue> &values)
{
    QMap<QString, QVariant> out;
    for (const ParamDef &p : doc.params) {
        const QJsonValue v = values.contains(p.identifier) ? values.value(p.identifier) : p.defaultValue;
        const QString id = p.identifier;
        switch (p.type) {
        case ParamType::Float:
        case ParamType::Seed:
            out[id] = number(v);
            break;
        case ParamType::Int:
        case ParamType::Choice:
            out[id] = jsRound(number(v));
            break;
        case ParamType::Bool:
            out[id] = number(v) != 0;
            break;
        case ParamType::Color: {
            QString s = v.toString().toLower();
            if (!p.alpha.value_or(false))
                s = s.left(7);
            else if (s.size() == 7)
                s += QStringLiteral("ff");
            out[id] = s;
            break;
        }
        case ParamType::Point: {
            const QJsonArray a = v.toArray();
            out[id + QStringLiteral(".x")] = a.at(0).toDouble();
            out[id + QStringLiteral(".y")] = a.at(1).toDouble();
            break;
        }
        case ParamType::Region: {
            const QJsonArray a = v.toArray();
            const char *axes[] = {".x", ".y", ".w", ".h"};
            for (int i = 0; i < 4; ++i)
                out[id + QLatin1String(axes[i])] = a.at(i).toDouble();
            break;
        }
        default:
            break;
        }
    }
    for (const LiteralUniform &l : result.literals) {
        const ForgeNode *n = doc.node(l.node);
        const NodeDef *def = n ? nodeDef(n->type) : nullptr;
        const InputDef *in = def ? def->input(l.input) : nullptr;
        QJsonValue v = n ? n->inputs.value(l.input) : QJsonValue();
        if (v.isUndefined() && in)
            v = in->defaultValue;
        out[l.name] = literalUniform(v, l.type);
    }
    return out;
}

DocSnapshotCommand::DocSnapshotCommand(ForgeDoc before, ForgeDoc after, const QString &text, const QString &key,
                                       std::function<void(const ForgeDoc &)> apply)
    : QUndoCommand(text)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_key(key)
    , m_apply(std::move(apply))
{
}

bool DocSnapshotCommand::mergeWith(const QUndoCommand *other)
{
    const auto *next = dynamic_cast<const DocSnapshotCommand *>(other);
    if (!next || m_key.isEmpty() || next->m_key != m_key)
        return false;
    m_after = next->m_after;
    return true;
}

}
