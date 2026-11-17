#include "app/forge/compiler/Validate.h"

#include "app/forge/doc/Naming.h"
#include "app/forge/nodes/Registry.h"

#include <QRegularExpression>
#include <QSet>

#include <functional>

namespace drift::forge {

namespace {

using Errors = QList<CompileError>;

Errors checkNodeTypes(const ForgeDoc &doc, const ValidateOptions &)
{
    Errors errors;
    for (const ForgeNode &n : doc.nodes) {
        const NodeDef *def = nodeDef(n.type);
        if (!def)
            errors.append({n.id, {}, QStringLiteral("Unknown node \"%1\".").arg(n.type)});
        else if (!availableFor(*def, doc.kind))
            errors.append({n.id, {}, QStringLiteral("%1 can't be used in %2.").arg(def->label, kindInfo(doc.kind).withArticle)});
    }
    return errors;
}

Errors checkOutputs(const ForgeDoc &doc, const ValidateOptions &opts)
{
    QList<const ForgeNode *> outputs;
    for (const ForgeNode &n : doc.nodes) {
        const NodeDef *d = nodeDef(n.type);
        if (d && d->output)
            outputs << &n;
    }
    if (opts.requireOutput && outputs.isEmpty())
        return {{{}, {}, QStringLiteral("Add an Output node.")}};
    Errors errors;
    for (int i = 1; i < outputs.size(); ++i)
        errors.append({outputs[i]->id, {}, QStringLiteral("Only one Output node is allowed.")});
    return errors;
}

Errors checkEdges(const ForgeDoc &doc, const ValidateOptions &)
{
    Errors errors;
    QSet<QString> ids;
    for (const ForgeNode &n : doc.nodes)
        ids.insert(n.id);
    QSet<QString> seenInputs;
    for (const ForgeEdge &e : doc.edges) {
        if (!ids.contains(e.from) || !ids.contains(e.to)) {
            errors.append({{}, {}, QStringLiteral("A connection points at a deleted node.")});
            continue;
        }
        const QString key = e.to + QLatin1Char(':') + e.toSocket;
        if (seenInputs.contains(key))
            errors.append({e.to, {}, QStringLiteral("An input has more than one connection.")});
        seenInputs.insert(key);
    }
    return errors;
}

Errors checkLoops(const ForgeDoc &doc, const ValidateOptions &)
{
    QMap<QString, QStringList> out;
    for (const ForgeEdge &e : doc.edges)
        out[e.from] << e.to;
    QMap<QString, int> state;
    std::function<bool(const QString &)> loop = [&](const QString &id) {
        if (state.value(id) == 1)
            return true;
        if (state.value(id) == 2)
            return false;
        state[id] = 1;
        for (const QString &next : out.value(id))
            if (loop(next))
                return true;
        state[id] = 2;
        return false;
    };
    for (const ForgeNode &n : doc.nodes) {
        if (loop(n.id))
            return {{n.id, {}, QStringLiteral("The graph loops back on itself. Remove the connection that closes the loop.")}};
    }
    return {};
}

QString paramValueProblem(const ParamDef &p)
{
    if (p.type == ParamType::Color) {
        static const QRegularExpression plain(QStringLiteral("^#[0-9a-fA-F]{6}$"));
        static const QRegularExpression withAlpha(QStringLiteral("^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$"));
        const QString v = p.defaultValue.toString();
        if (!(p.alpha.value_or(false) ? withAlpha : plain).match(v).hasMatch())
            return QStringLiteral("Slider \"%1\" needs a #rrggbb colour.").arg(p.displayName);
    }
    if (p.type == ParamType::Choice && !(p.options && p.options->size() >= 2))
        return QStringLiteral("Dropdown \"%1\" needs at least two choices.").arg(p.displayName);
    if ((p.type == ParamType::Float || p.type == ParamType::Int) && !(p.min < p.max))
        return QStringLiteral("Slider \"%1\": the lowest value must be below the highest.").arg(p.displayName);
    return {};
}

Errors checkParams(const ForgeDoc &doc, const ValidateOptions &)
{
    Errors errors;
    QSet<QString> names;
    QMap<QString, QString> audioNames;
    for (const ParamDef &p : doc.params)
        if (p.type == ParamType::Audio)
            for (const QString &n : audioScalarNames(p.identifier))
                audioNames.insert(n, p.identifier);
    for (const ParamDef &p : doc.params) {
        const QString problem = paramNameProblem(p.identifier);
        if (!problem.isNull())
            errors.append({{}, p.identifier, QStringLiteral("Slider \"%1\": %2").arg(p.displayName, problem)});
        if (names.contains(p.identifier))
            errors.append({{}, p.identifier, QStringLiteral("Two sliders are named \"%1\".").arg(p.identifier)});
        const auto owner = audioNames.constFind(p.identifier);
        if (owner != audioNames.constEnd() && owner.value() != p.identifier)
            errors.append({{}, p.identifier,
                           QStringLiteral("Slider \"%1\" is taken by the audio slider \"%2\".").arg(p.displayName, owner.value())});
        names.insert(p.identifier);
        const QString valueProblem = paramValueProblem(p);
        if (!valueProblem.isEmpty())
            errors.append({{}, p.identifier, valueProblem});
    }
    return errors;
}

Errors checkShowWhen(const ForgeDoc &doc, const ValidateOptions &)
{
    QSet<QString> switches;
    for (const ParamDef &q : doc.params)
        if (q.type == ParamType::Bool)
            switches.insert(q.identifier);
    Errors errors;
    for (const ParamDef &p : doc.params)
        if (p.showWhen && !switches.contains(p.showWhen->param))
            errors.append({{}, p.identifier,
                           QStringLiteral("Slider \"%1\" is set to show with a switch that no longer exists.").arg(p.displayName)});
    return errors;
}

QStringList boundParams(const ForgeNode &n)
{
    QStringList out;
    for (auto it = n.data.begin(); it != n.data.end(); ++it)
        if (isParamRef(it.value()) && !it.value().toObject().value("param").isArray())
            out << paramRefNames(it.value());
    for (auto it = n.inputs.begin(); it != n.inputs.end(); ++it)
        out << paramRefNames(it.value());
    return out;
}

Errors checkBindings(const ForgeDoc &doc, const ValidateOptions &)
{
    QSet<QString> names;
    for (const ParamDef &p : doc.params)
        names.insert(p.identifier);
    Errors errors;
    for (const ForgeNode &n : doc.nodes)
        for (const QString &name : boundParams(n))
            if (!names.contains(name))
                errors.append({n.id, {}, QStringLiteral("Uses a slider \"%1\" that no longer exists.").arg(name)});
    return errors;
}

}

QList<CompileError> validate(const ForgeDoc &doc, const ValidateOptions &opts)
{
    Errors all;
    for (auto check : {checkNodeTypes, checkOutputs, checkEdges, checkLoops, checkParams, checkShowWhen, checkBindings})
        all << check(doc, opts);
    return all;
}

QMap<QString, QString> nodeErrors(const QList<CompileError> &errors)
{
    QMap<QString, QString> out;
    for (const CompileError &e : errors)
        if (!e.node.isEmpty())
            out.insert(e.node, e.message);
    return out;
}

}
