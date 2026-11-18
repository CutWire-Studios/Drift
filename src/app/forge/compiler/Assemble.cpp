#include "app/forge/compiler/Assemble.h"

#include "app/forge/compiler/Helpers.h"
#include "app/forge/doc/ParamTypes.h"
#include "app/forge/glsl/Literals.h"

#include <QHash>

namespace drift::forge {

namespace {

struct Buffers
{
    QHash<QString, QString> physOf;
    int count = 0;
};

// A buffer can be reused once every pass reading it has run, and never as the target of a pass that also reads it.
Buffers allocateBuffers(const QList<PassBuild *> &order)
{
    QHash<QString, int> lastUse;
    for (int i = 0; i < order.size(); ++i)
        for (const TexRef &s : order[i]->samplers)
            if (s.type == TexRef::Buffer)
                lastUse[s.sym] = qMax(lastUse.value(s.sym, -1), i);
    Buffers out;
    QStringList holder;
    for (int i = 0; i < order.size(); ++i) {
        const PassBuild &p = *order[i];
        if (p.writes.isEmpty())
            continue;
        QSet<QString> reads;
        for (const TexRef &s : p.samplers)
            if (s.type == TexRef::Buffer && out.physOf.contains(s.sym))
                reads.insert(out.physOf.value(s.sym));
        int slot = -1;
        for (int b = 0; b < holder.size(); ++b) {
            if (lastUse.value(holder[b], -1) < i && !reads.contains(QStringLiteral("buf%1").arg(b))) {
                slot = b;
                break;
            }
        }
        if (slot < 0) {
            holder.append(p.writes);
            slot = holder.size() - 1;
        } else {
            holder[slot] = p.writes;
        }
        out.physOf.insert(p.writes, QStringLiteral("buf%1").arg(slot));
    }
    out.count = holder.size();
    return out;
}

QString passSource(const ForgeDoc &doc, PassBuild &p, bool faceGuard)
{
    QStringList lines = {QStringLiteral("#version 330 core"), QStringLiteral("in vec2 v_texCoord;"),
                         QStringLiteral("out vec4 fragColor;"), QString()};
    for (int i = 0; i < p.samplers.size(); ++i)
        lines << QStringLiteral("uniform sampler2D %1;").arg(samplerName(i));
    if (p.usesRes)
        lines << QStringLiteral("uniform vec2 u_resolution;");
    if (p.usesTime)
        lines << QStringLiteral("uniform float u_time;");
    if (p.usesProgress)
        lines << QStringLiteral("uniform float u_progress;");
    for (const QString &e : p.engines)
        lines << QStringLiteral("uniform float %1;").arg(e);
    QList<const ParamDef *> used;
    for (const ParamDef &d : doc.params)
        if (p.params.contains(d.identifier))
            used << &d;
    for (const ParamDef *d : used)
        lines << QStringLiteral("uniform %1 %2;").arg(paramGlslType(*d), d->identifier);
    for (const ParamDef *d : used) {
        const QString t = paramGlslType(*d);
        if (t != QLatin1String("sampler2D"))
            lines << QStringLiteral("%1 pv_%2() { return %2; }").arg(t, d->identifier);
    }
    for (const LiteralUniform &l : p.literals)
        lines << QStringLiteral("uniform %1 %2;").arg(glslType(l.type), l.name);
    lines << QString();
    bool lumaUsed = p.main.contains(QLatin1String("luma("));
    for (const QString &f : p.functions)
        lumaUsed = lumaUsed || f.contains(QLatin1String("luma("));
    if (lumaUsed)
        p.addUnique(p.helpers, QStringLiteral("luma"));
    for (const QString &h : resolveHelpers(p.helpers))
        lines << h << QString();
    for (const QString &f : p.functions)
        lines << f << QString();
    if (faceGuard) {
        int idx = 0;
        for (int i = 0; i < p.samplers.size(); ++i) {
            if (p.samplers[i].type == TexRef::Source && p.samplers[i].index == 0) {
                idx = i;
                break;
            }
        }
        lines << QStringLiteral("void main() {") << QStringLiteral("    if (u_faceValid < 0.5) {")
              << QStringLiteral("        fragColor = texture(%1, v_texCoord);").arg(samplerName(idx))
              << QStringLiteral("        return;") << QStringLiteral("    }")
              << QStringLiteral("    fragColor = %1;").arg(p.main) << QStringLiteral("}") << QString();
    } else {
        lines << QStringLiteral("void main() {") << QStringLiteral("    fragColor = %1;").arg(p.main)
              << QStringLiteral("}") << QString();
    }
    return lines.join(QLatin1Char('\n'));
}

}

CompileResult assemble(const ForgeDoc &doc, const QList<PassBuild *> &order, const AssembleFlags &flags)
{
    const Buffers buffers = allocateBuffers(order);

    QList<QPair<QString, QString>> textures;
    CompileResult r;
    const auto texId = [&](const QString &assetId) {
        for (const auto &t : textures)
            if (t.first == assetId)
                return QStringLiteral("tex%1").arg(textures.indexOf(t));
        const int n = textures.size();
        QString mime;
        for (const ForgeAsset &a : doc.assets)
            if (a.id == assetId)
                mime = a.mime;
        const QString id = QStringLiteral("tex%1").arg(n);
        textures.append({assetId, id});
        r.textures.append(QJsonObject{{"id", id}, {"assetId", assetId}, {"file", QStringLiteral("tex%1.%2").arg(n).arg(mimeExt(mime))}});
        return id;
    };

    const bool multi = order.size() > 1;
    for (int i = 0; i < order.size(); ++i) {
        PassBuild &p = *order[i];
        if (p.samplers.isEmpty()) {
            TexRef s;
            s.type = TexRef::Source;
            p.samplers.append(s);
        }
        for (const LiteralUniform &l : p.literals) {
            bool replaced = false;
            for (LiteralUniform &have : r.literals) {
                if (have.name == l.name) {
                    have = l;
                    replaced = true;
                }
            }
            if (!replaced)
                r.literals.append(l);
        }
        QJsonArray inputs;
        for (const TexRef &s : p.samplers) {
            switch (s.type) {
            case TexRef::Source: {
                QJsonObject o{{"type", "source_texture"}};
                if (doc.kind != Kind::Effect)
                    o["index"] = s.index;
                inputs.append(o);
                break;
            }
            case TexRef::Buffer:
                inputs.append(QJsonObject{{"type", "buffer"}, {"id", buffers.physOf.value(s.sym)}});
                break;
            case TexRef::Asset:
                inputs.append(QJsonObject{{"type", "texture"}, {"id", texId(s.assetId)}});
                break;
            }
        }
        CompiledPass cp;
        cp.file = !multi || p.key == QLatin1String("root") ? QStringLiteral("main.frag") : QStringLiteral("pass%1.frag").arg(i + 1);
        cp.source = passSource(doc, p, flags.usesFace && p.key == QLatin1String("root"));
        cp.inputs = inputs;
        cp.output = p.writes.isEmpty() ? QJsonObject{{"type", "canvas"}}
                                       : QJsonObject{{"type", "buffer"}, {"id", buffers.physOf.value(p.writes)}};
        for (const ParamDef &d : doc.params)
            if (p.params.contains(d.identifier) && isSamplerParam(d))
                cp.paramSamplers << d.identifier;
        r.passes.append(cp);
    }

    r.ok = true;
    for (int i = 0; i < buffers.count; ++i)
        r.buffers.append(QStringLiteral("buf%1").arg(i));
    r.usesTime = flags.usesTime;
    r.usesMask = flags.usesMask;
    r.usesDepth = flags.usesDepth;
    r.usesFace = flags.usesFace;
    return r;
}

}
