#include "app/forge/compiler/Compiler.h"

#include "app/forge/compiler/Assemble.h"
#include "app/forge/compiler/EmitCtx.h"
#include "app/forge/doc/ParamTypes.h"
#include "app/forge/glsl/Literals.h"
#include "app/forge/nodes/Registry.h"

#include <QHash>
#include <QJsonDocument>
#include <QSet>

#include <memory>

namespace drift::forge {

namespace {

struct CompileFailure
{
    QList<CompileError> errors;
};

QString safe(const QString &id)
{
    QString out;
    for (const QChar c : id)
        if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
            || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
            out += c;
    return out;
}

// How a node input reads a parameter, through pv_<name>() so a local in generated code can never shadow
// the uniform. False for types that can't drive an input.
bool paramRead(const ParamDef &def, const QString &v, QString *expr, SocketType *type)
{
    switch (def.type) {
    case ParamType::Float:
    case ParamType::Bool:
    case ParamType::Int:
    case ParamType::Choice:
    case ParamType::Seed:
        *expr = v;
        *type = SocketType::Float;
        return true;
    case ParamType::Color:
        *expr = def.alpha.value_or(false) ? v : QStringLiteral("vec4(%1, 1.0)").arg(v);
        *type = SocketType::Color;
        return true;
    case ParamType::Point:
        *expr = v;
        *type = SocketType::Vec2;
        return true;
    default:
        return false;
    }
}

QString socketTypeName(SocketType t)
{
    switch (t) {
    case SocketType::Float: return QStringLiteral("float");
    case SocketType::Vec2: return QStringLiteral("vec2");
    case SocketType::Color: return QStringLiteral("color");
    }
    return {};
}

}

class Compiler
{
public:
    Compiler(const ForgeDoc &doc, const CompileOptions &opts)
        : doc(doc)
        , opts(opts)
    {
        for (const ForgeNode &n : doc.nodes)
            nodes.insert(n.id, &n);
        for (const ForgeEdge &e : doc.edges)
            incoming.insert(e.to + QLatin1Char(':') + e.toSocket, &e);
    }

    const ForgeDoc &doc;
    const CompileOptions &opts;
    QHash<QString, const ForgeNode *> nodes;
    QHash<QString, const ForgeEdge *> incoming;
    QSet<QString> materialized;
    QHash<QString, PassBuild *> passes;
    std::vector<std::unique_ptr<PassBuild>> owned;
    QList<PassBuild *> order;
    QSet<QString> building;
    bool usesTime = false;
    bool usesMask = false;
    bool usesDepth = false;
    bool usesFace = false;

    CompileResult run()
    {
        const ForgeNode *rootNode = nullptr;
        if (opts.target) {
            rootNode = nodes.value(opts.target->first);
        } else {
            for (const ForgeNode &n : doc.nodes) {
                const NodeDef *d = nodeDef(n.type);
                if (d && d->output) {
                    rootNode = &n;
                    break;
                }
            }
        }
        if (!rootNode)
            fail({}, QStringLiteral("Add an Output node."));
        findMaterialized(rootNode->id);
        PassBuild &root = ensurePass(QStringLiteral("root"));
        if (usesFace) {
            TexRef s;
            s.type = TexRef::Source;
            sampler(root, s);
            root.addUnique(root.engines, QStringLiteral("u_faceValid"));
        }
        return assemble(doc, order, {usesTime, usesMask, usesDepth, usesFace});
    }

    const NodeDef &def(const ForgeNode &node) const { return *nodeDef(node.type); }

    [[noreturn]] void fail(const QString &node, const QString &message) const
    {
        throw CompileFailure{{{node, {}, message}}};
    }

    std::function<QJsonValue(const QString &)> optOf(const ForgeNode &node) const
    {
        const NodeDef *d = &def(node);
        return [&node, d](const QString &id) -> QJsonValue {
            if (node.data.contains(id))
                return node.data.value(id);
            const OptionDef *o = d->option(id);
            return o ? o->defaultValue : QJsonValue(QJsonValue::Undefined);
        };
    }

    QStringList heavyInputs(const ForgeNode &node) const
    {
        const NodeDef &d = def(node);
        if (d.heavyInputs)
            return d.heavyInputs(optOf(node));
        return d.heavyInputsStatic;
    }

    // Values sampled many times per pixel get their own buffer, unless reading them is already cheap.
    void findMaterialized(const QString &rootId)
    {
        QSet<QString> seen;
        const auto visit = [&](const auto &self, const QString &id) -> void {
            if (seen.contains(id))
                return;
            seen.insert(id);
            const ForgeNode &node = *nodes.value(id);
            const QStringList heavy = heavyInputs(node);
            for (const InputDef &input : def(node).inputs) {
                const ForgeEdge *e = incoming.value(id + QLatin1Char(':') + input.id);
                if (!e)
                    continue;
                self(self, e->from);
                if (heavy.contains(input.id) && !def(*nodes.value(e->from)).cheap)
                    materialized.insert(e->from + QLatin1Char(':') + e->fromSocket);
            }
        };
        visit(visit, rootId);
    }

    PassBuild &ensurePass(const QString &key)
    {
        if (PassBuild *existing = passes.value(key))
            return *existing;
        if (building.contains(key))
            fail({}, QStringLiteral("The graph loops back on itself."));
        building.insert(key);
        owned.push_back(std::make_unique<PassBuild>());
        PassBuild &p = *owned.back();
        p.key = key;
        p.writes = key == QLatin1String("root") ? QString() : key;
        p.main = passMain(p, key);
        building.remove(key);
        passes.insert(key, &p);
        order.append(&p);
        return p;
    }

    // The expression the pass writes: the graph's output, one materialized value, or one stage.
    QString passMain(PassBuild &p, const QString &key)
    {
        if (key == QLatin1String("root"))
            return rootMain(p);
        if (key.startsWith(QLatin1String("mat:"))) {
            const QStringList parts = key.mid(4).split(QLatin1Char(':'));
            return nodeOutputMain(p, parts.value(0), parts.value(1));
        }
        return stageMain(p, key);
    }

    QString rootMain(PassBuild &p)
    {
        if (opts.target)
            return nodeOutputMain(p, opts.target->first, opts.target->second);
        const ForgeNode *out = nullptr;
        for (const ForgeNode &n : doc.nodes) {
            const NodeDef *d = nodeDef(n.type);
            if (d && d->output) {
                out = &n;
                break;
            }
        }
        EmitCtx c = ctx(p, *out);
        return c.in(QStringLiteral("image"), QStringLiteral("v_texCoord"));
    }

    QString nodeOutputMain(PassBuild &p, const QString &node, const QString &output)
    {
        const SocketType t = outputType(*nodes.value(node), output);
        const QString fn = valueFn(p, node, output);
        return coerce(fn + QStringLiteral("(v_texCoord)"), t, SocketType::Color);
    }

    QString stageMain(PassBuild &p, const QString &key)
    {
        const QStringList parts = key.split(QLatin1Char(':'));
        const QString nodeId = parts.value(1);
        const ForgeNode &node = *nodes.value(nodeId);
        const int stage = parts.value(2).toInt();
        EmitCtx c = ctx(p, node);
        StageReader prev = [](const QString &) { return QStringLiteral("vec4(0.0)"); };
        if (stage > 0)
            prev = bufferReader(p, QStringLiteral("stage:%1:%2").arg(nodeId).arg(stage - 1));
        const QString body = def(node).stages.at(stage).body(c, prev);
        const QString fn = QStringLiteral("s_%1_%2").arg(safe(nodeId)).arg(stage);
        p.functions << QStringLiteral("vec4 %1(vec2 uv) {\n    %2\n}").arg(fn, body);
        return fn + QStringLiteral("(v_texCoord)");
    }

    SocketType outputType(const ForgeNode &node, const QString &output) const
    {
        const OutputDef *o = def(node).outputDef(output);
        if (!o)
            fail(node.id, QStringLiteral("Unknown output \"%1\".").arg(output));
        return o->type;
    }

    QString sampler(PassBuild &p, const TexRef &ref)
    {
        const QString key = ref.key();
        int i = -1;
        for (int k = 0; k < p.samplers.size(); ++k) {
            if (p.samplers[k].key() == key) {
                i = k;
                break;
            }
        }
        if (i < 0) {
            p.samplers.append(ref);
            i = p.samplers.size() - 1;
        }
        return samplerName(i);
    }

    StageReader bufferReader(PassBuild &p, const QString &sym)
    {
        ensurePass(sym);
        TexRef ref;
        ref.type = TexRef::Buffer;
        ref.sym = sym;
        const QString s = sampler(p, ref);
        return [s](const QString &uv) { return QStringLiteral("texture(%1, %2)").arg(s, uv); };
    }

    // Emits the function computing one node output inside pass `p` and returns its name.
    QString valueFn(PassBuild &p, const QString &nodeId, const QString &output)
    {
        const QString key = nodeId + QLatin1Char(':') + output;
        const auto have = p.fnNames.constFind(key);
        if (have != p.fnNames.constEnd())
            return *have;
        const ForgeNode &node = *nodes.value(nodeId);
        const NodeDef &d = def(node);
        const SocketType type = outputType(node, output);
        EmitCtx c = ctx(p, node);
        StageReader stage = [](const QString &) { return QStringLiteral("vec4(0.0)"); };
        if (!d.stages.isEmpty())
            stage = bufferReader(p, QStringLiteral("stage:%1:%2").arg(nodeId).arg(d.stages.size() - 1));
        const QMap<QString, QString> bodies = d.emitFn(c, stage);
        const QString name = QStringLiteral("n_%1_%2").arg(safe(nodeId), safe(output));
        p.functions << QStringLiteral("%1 %2(vec2 uv) {\n    %3\n}").arg(glslType(type), name, bodies.value(output));
        p.fnNames.insert(key, name);
        return name;
    }

    EmitCtx ctx(PassBuild &p, const ForgeNode &node) { return EmitCtx(*this, p, node, def(node)); }

    QString time(PassBuild &p, const ForgeNode &node)
    {
        if (doc.kind != Kind::Effect) {
            fail(node.id,
                 QStringLiteral("%1s can't use Time. Use Progress instead, so it looks the same every time.")
                     .arg(kindInfo(doc.kind).label));
        }
        p.usesTime = true;
        usesTime = true;
        return QStringLiteral("u_time");
    }

    QString progress(PassBuild &p)
    {
        p.usesProgress = true;
        return QStringLiteral("u_progress");
    }

    QString res(PassBuild &p)
    {
        p.usesRes = true;
        return QStringLiteral("u_resolution");
    }

    QString clock(PassBuild &p, const ForgeNode &node)
    {
        if (kindInfo(doc.kind).clockIsTime)
            return time(p, node);
        if (doc.kind != Kind::Animation)
            return progress(p);
        p.addUnique(p.engines, QStringLiteral("u_direction"));
        const QString a = progress(p);
        const QString b = progress(p);
        return QStringLiteral("mix(%1, 1.0 - %2, u_direction)").arg(a, b);
    }

    // An input as a GLSL expression of its own type: wired, a clock, a slider, or a literal.
    QString readInput(PassBuild &p, const ForgeNode &node, const QString &id, const QString &uv)
    {
        const NodeDef &d = def(node);
        const InputDef *input = d.input(id);
        if (!input)
            fail(node.id, QStringLiteral("%1 has no input %2").arg(d.type, id));
        if (const ForgeEdge *edge = incoming.value(node.id + QLatin1Char(':') + id))
            return readEdge(p, *edge, uv, input->type);
        if (input->clock)
            return clock(p, node);
        QJsonValue v = node.inputs.value(id);
        if (v.isUndefined() || v.isNull())
            v = input->defaultValue;
        if (isParamRef(v))
            return paramExpr(p, node, paramRefNames(v), input->type);
        if (opts.mode == CompileOptions::Export)
            return glslLiteral(v, input->type);
        const QString name = QStringLiteral("k_%1_%2").arg(safe(node.id), safe(id));
        bool known = false;
        for (const LiteralUniform &l : p.literals)
            known = known || l.name == name;
        if (!known)
            p.literals.append({name, node.id, id, input->type});
        return name;
    }

    QString readEdge(PassBuild &p, const ForgeEdge &edge, const QString &uv, SocketType to)
    {
        const SocketType srcType = outputType(*nodes.value(edge.from), edge.fromSocket);
        const QString vk = edge.from + QLatin1Char(':') + edge.fromSocket;
        if (materialized.contains(vk) && p.key != QLatin1String("mat:") + vk) {
            const StageReader read = bufferReader(p, QLatin1String("mat:") + vk);
            return coerce(read(uv), SocketType::Color, to);
        }
        const QString fn = valueFn(p, edge.from, edge.fromSocket);
        return coerce(fn + QLatin1Char('(') + uv + QLatin1Char(')'), srcType, to);
    }

    // The parameter a block's sampler or scalar input is bound to through `data[key]`.
    QString paramOf(PassBuild &p, const ForgeNode &node, const QString &key)
    {
        const QJsonValue v = node.data.value(key);
        const QStringList names = paramRefNames(v);
        if (names.size() != 1)
            fail(node.id, QStringLiteral("This block lost its track slider. Delete it and add it again."));
        useParam(p, node, names[0]);
        return names[0];
    }

    const ParamDef &useParam(PassBuild &p, const ForgeNode &node, const QString &name)
    {
        const ParamDef *d = doc.param(name);
        if (!d)
            fail(node.id, QStringLiteral("This node uses a slider \"%1\" that no longer exists.").arg(name));
        p.addUnique(p.params, name);
        return *d;
    }

    QString paramExpr(PassBuild &p, const ForgeNode &node, const QStringList &ref, SocketType to)
    {
        if (ref.size() == 2) {
            useParam(p, node, ref[0]);
            useParam(p, node, ref[1]);
            return coerce(QStringLiteral("vec2(pv_%1(), pv_%2())").arg(ref[0], ref[1]), SocketType::Vec2, to);
        }
        const ParamDef &d = useParam(p, node, ref.value(0));
        QString expr;
        SocketType type = SocketType::Float;
        if (!paramRead(d, QStringLiteral("pv_%1()").arg(ref[0]), &expr, &type))
            fail(node.id, QStringLiteral("\"%1\" can't drive this setting.").arg(d.displayName));
        return coerce(expr, type, to);
    }
};

EmitCtx::EmitCtx(Compiler &compiler, PassBuild &pass, const ForgeNode &node, const NodeDef &def)
    : kind(compiler.doc.kind)
    , prefix(QStringLiteral("x_") + safe(node.id))
    , m_c(compiler)
    , m_p(pass)
    , m_node(node)
    , m_def(def)
{
}

QString EmitCtx::in(const QString &id, const QString &uv)
{
    return m_c.readInput(m_p, m_node, id, uv);
}

bool EmitCtx::connected(const QString &id) const
{
    return m_c.incoming.contains(m_node.id + QLatin1Char(':') + id);
}

QJsonValue EmitCtx::opt(const QString &id) const
{
    if (m_node.data.contains(id))
        return m_node.data.value(id);
    const OptionDef *o = m_def.option(id);
    return o ? o->defaultValue : QJsonValue(QJsonValue::Undefined);
}

std::optional<QString> EmitCtx::optParam(const QString &id)
{
    const QStringList names = paramRefNames(m_node.data.value(id));
    if (names.size() != 1)
        return std::nullopt;
    m_c.useParam(m_p, m_node, names[0]);
    return names[0];
}

void EmitCtx::helper(const QString &name)
{
    m_p.addUnique(m_p.helpers, name);
}

QString EmitCtx::clock()
{
    return m_c.clock(m_p, m_node);
}

QString EmitCtx::time()
{
    return m_c.time(m_p, m_node);
}

QString EmitCtx::progress()
{
    return m_c.progress(m_p);
}

QString EmitCtx::res()
{
    return m_c.res(m_p);
}

QString EmitCtx::aspect()
{
    const QString a = res();
    const QString b = res();
    return QStringLiteral("%1.x / max(%2.y, 1.0)").arg(a, b);
}

QString EmitCtx::source(int index, const QString &uv)
{
    if (kind != Kind::Transition && index != 0)
        m_c.fail(m_node.id, QStringLiteral("Effects and animations only have one clip."));
    TexRef ref;
    ref.type = TexRef::Source;
    ref.index = index;
    return QStringLiteral("texture(%1, %2)").arg(m_c.sampler(m_p, ref), uv);
}

QString EmitCtx::asset(const QString &uv, const QString &option)
{
    const QJsonValue ref = m_node.data.value(option);
    const QStringList names = paramRefNames(ref);
    if (names.size() == 1) {
        m_c.useParam(m_p, m_node, names[0]);
        return QStringLiteral("texture(%1, %2)").arg(names[0], uv);
    }
    const QString assetId = ref.isString() ? ref.toString() : QString();
    bool found = false;
    for (const ForgeAsset &a : m_c.doc.assets)
        found = found || a.id == assetId;
    if (assetId.isEmpty() || !found)
        m_c.fail(m_node.id, QStringLiteral("Pick a picture for this node."));
    TexRef t;
    t.type = TexRef::Asset;
    t.assetId = assetId;
    return QStringLiteral("texture(%1, %2)").arg(m_c.sampler(m_p, t), uv);
}

QString EmitCtx::paramSampler(const QString &key, const QString &uv)
{
    return QStringLiteral("texture(%1, %2)").arg(m_c.paramOf(m_p, m_node, key), uv);
}

QString EmitCtx::paramName(const QString &key)
{
    return m_c.paramOf(m_p, m_node, key);
}

QString EmitCtx::mask(const QString &uv)
{
    m_c.usesMask = true;
    return QStringLiteral("driftMask(%1)").arg(uv);
}

QString EmitCtx::hasMask()
{
    m_c.usesMask = true;
    return QStringLiteral("u_hasClipMask");
}

QString EmitCtx::depth(const QString &uv)
{
    m_c.usesDepth = true;
    return QStringLiteral("driftDepth(%1)").arg(uv);
}

QString EmitCtx::depthNormal(const QString &uv, const QString &strength)
{
    m_c.usesDepth = true;
    return QStringLiteral("driftNormal(%1, %2)").arg(uv, strength);
}

QString EmitCtx::hasDepth()
{
    m_c.usesDepth = true;
    return QStringLiteral("u_hasDepth");
}

QString EmitCtx::face(const QString &name)
{
    m_c.usesFace = true;
    m_p.addUnique(m_p.engines, name);
    return name;
}

QString EmitCtx::engine(const QString &name)
{
    m_p.addUnique(m_p.engines, name);
    return name;
}

void EmitCtx::declare(const QString &text)
{
    if (m_declared.contains(text))
        return;
    m_declared.insert(text);
    m_p.functions << text;
}

CompileResult compile(const ForgeDoc &doc, const CompileOptions &opts)
{
    if (doc.kind == Kind::Audio) {
        CompileResult r;
        r.ok = true;
        return r;
    }
    ValidateOptions vo;
    vo.requireOutput = !opts.target.has_value();
    CompileResult empty;
    empty.errors = validate(doc, vo);
    if (!empty.errors.isEmpty()) {
        empty.nodeErrors = nodeErrors(empty.errors);
        return empty;
    }
    try {
        Compiler c(doc, opts);
        return c.run();
    } catch (const CompileFailure &f) {
        empty.errors = f.errors;
        empty.nodeErrors = nodeErrors(f.errors);
        return empty;
    }
}

QString structureKey(const ForgeDoc &doc)
{
    QJsonArray nodes;
    for (const ForgeNode &n : doc.nodes) {
        QJsonArray refs;
        for (auto it = n.inputs.begin(); it != n.inputs.end(); ++it)
            if (isParamRef(it.value()))
                refs.append(QJsonArray{it.key(), it.value()});
        nodes.append(QJsonArray{n.id, n.type, n.data, refs});
    }
    QJsonArray edges;
    for (const ForgeEdge &e : doc.edges)
        edges.append(QJsonArray{e.from, e.fromSocket, e.to, e.toSocket});
    QJsonArray params;
    for (const ParamDef &p : doc.params)
        params.append(QJsonArray{p.identifier, paramTypeName(p.type), p.alpha.value_or(false)});
    QJsonArray assets;
    for (const ForgeAsset &a : doc.assets)
        assets.append(a.id);
    const QJsonArray all{kindInfo(doc.kind).name, nodes, edges, params, assets};
    return QString::fromUtf8(QJsonDocument(all).toJson(QJsonDocument::Compact));
}

QJsonObject previewJson(const ForgeDoc &doc, const CompileResult &r)
{
    QJsonArray errors;
    for (const CompileError &e : r.errors) {
        QJsonObject o;
        if (!e.node.isEmpty())
            o["node"] = e.node;
        if (!e.param.isEmpty())
            o["param"] = e.param;
        o["message"] = e.message;
        errors.append(o);
    }
    QJsonObject nodeErrs;
    for (auto it = r.nodeErrors.begin(); it != r.nodeErrors.end(); ++it)
        nodeErrs[it.key()] = it.value();
    QJsonArray passes;
    for (const CompiledPass &p : r.passes) {
        QJsonArray samplers;
        for (const QString &s : p.paramSamplers)
            samplers.append(s);
        passes.append(QJsonObject{{"file", p.file}, {"inputs", p.inputs}, {"output", p.output}, {"paramSamplers", samplers}});
    }
    QJsonArray literals;
    for (const LiteralUniform &l : r.literals) {
        const ForgeNode *n = doc.node(l.node);
        QJsonValue value;
        if (n) {
            value = n->inputs.value(l.input);
            if (value.isUndefined() || value.isNull()) {
                const NodeDef *d = nodeDef(n->type);
                const InputDef *in = d ? d->input(l.input) : nullptr;
                if (in)
                    value = in->defaultValue;
            }
        }
        literals.append(QJsonObject{{"name", l.name}, {"node", l.node}, {"input", l.input},
                                    {"type", socketTypeName(l.type)}, {"value", value}});
    }
    return QJsonObject{{"ok", r.ok},
                       {"structureKey", structureKey(doc)},
                       {"nodeErrors", nodeErrs},
                       {"errors", errors},
                       {"passes", passes},
                       {"buffers", r.buffers},
                       {"textures", r.textures},
                       {"literals", literals},
                       {"usesTime", r.usesTime},
                       {"usesMask", r.usesMask},
                       {"usesDepth", r.usesDepth},
                       {"usesFace", r.usesFace}};
}

}
