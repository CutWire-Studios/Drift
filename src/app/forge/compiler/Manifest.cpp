#include "app/forge/compiler/Manifest.h"

#include "app/forge/audio/AudioManifest.h"
#include "app/forge/doc/ParamTypes.h"
#include "app/forge/nodes/Registry.h"

#include <QRegularExpression>
#include <QSet>

namespace drift::forge {

QJsonObject paramManifestEntry(const ForgeDoc &doc, const ParamDef &p)
{
    const ParamTypeSpec &spec = paramSpec(p);
    QJsonObject e = paramManifestFields(p);
    e["identifier"] = p.identifier;
    e["displayName"] = p.displayName;
    e["type"] = paramTypeName(p.type);
    if (spec.exportsDefault)
        e["defaultValue"] = paramDriftValue(doc, p, p.defaultValue);
    if (p.group && !p.group->isEmpty())
        e["group"] = *p.group;
    if (!p.ui.isEmpty())
        e["ui"] = p.ui;
    if (p.showWhen)
        e["showWhen"] = QJsonObject{{"param", p.showWhen->param}, {"equals", p.showWhen->equals}};
    return e;
}

QJsonArray presetsManifest(const ForgeDoc &doc)
{
    QJsonArray presets;
    if (!doc.presets)
        return presets;
    for (const Preset &pr : *doc.presets) {
        QJsonObject values;
        for (auto it = pr.values.begin(); it != pr.values.end(); ++it)
            if (const ParamDef *p = doc.param(it.key()))
                values[it.key()] = paramDriftValue(doc, *p, it.value());
        presets.append(QJsonObject{{"name", pr.name}, {"values", values}});
    }
    return presets;
}


QStringList nextFeatures(const ForgeDoc &doc, const CompileResult &compiled)
{
    QSet<QString> f;
    for (const ParamDef &p : doc.params) {
        if (paramSpec(p).next)
            f.insert(QStringLiteral("param:") + paramTypeName(p.type));
        if (p.type == ParamType::Color && p.alpha.value_or(false))
            f.insert(QStringLiteral("param:color-alpha"));
    }
    for (const ForgeNode &n : doc.nodes) {
        const NodeDef *d = nodeDef(n.type);
        if (d && d->next)
            f.insert(QStringLiteral("node:") + n.type);
    }
    static const QRegularExpression audio(QStringLiteral("\\bu_audio(Level|Bass|Beat)\\b"));
    for (const CompiledPass &pass : compiled.passes)
        if (audio.match(pass.source).hasMatch())
            f.insert(QStringLiteral("uniform:audio"));
    if (doc.kind == Kind::Animation)
        f.insert(QStringLiteral("kind:animation"));
    if (compiled.usesMask)
        f.insert(QStringLiteral("requires:mask"));
    if (compiled.usesDepth)
        f.insert(QStringLiteral("requires:depth"));
    if (compiled.usesFace)
        f.insert(QStringLiteral("requires:face"));
    QStringList out(f.begin(), f.end());
    out.sort();
    return out;
}

QString minAppVersion(const ForgeDoc &doc, const CompileResult &compiled)
{
    if (doc.kind == Kind::Audio)
        return audioMinAppVersion(doc);
    if (doc.kind == Kind::Animation)
        return QString::fromLatin1(kAnimationAppVersion);
    static const QSet<QString> next2 = {QStringLiteral("param:track"), QStringLiteral("param:trackMask"),
                                        QStringLiteral("param:audio"), QStringLiteral("requires:depth"),
                                        QStringLiteral("requires:face")};
    const QStringList features = nextFeatures(doc, compiled);
    for (const QString &f : features)
        if (next2.contains(f))
            return QString::fromLatin1(kNext2AppVersion);
    return QString::fromLatin1(features.isEmpty() ? kMinAppVersion : kNextAppVersion);
}

QJsonObject packageManifest(const ForgeDoc &doc, const CompileResult &compiled)
{
    if (doc.kind == Kind::Audio)
        return audioManifest(doc);
    QJsonArray buffers;
    for (const QJsonValue &id : compiled.buffers)
        buffers.append(QJsonObject{{"id", id}, {"scale", 1.0}});
    QJsonArray passes;
    for (int i = 0; i < compiled.passes.size(); ++i) {
        const CompiledPass &p = compiled.passes[i];
        passes.append(QJsonObject{{"passIndex", i}, {"fragmentShader", p.file}, {"inputs", p.inputs}, {"output", p.output}});
    }
    QJsonObject pipeline{{"intermediateBuffers", buffers}, {"passes", passes}};
    if (!compiled.textures.isEmpty()) {
        QJsonArray textures;
        for (const QJsonValue &t : compiled.textures)
            textures.append(QJsonObject{{"id", t.toObject().value("id")}, {"file", t.toObject().value("file")}});
        pipeline["textures"] = textures;
    }

    QJsonObject json{{"id", doc.meta.id}, {"displayName", doc.meta.displayName}, {"category", doc.meta.category}, {"order", 1000}};
    if (!doc.meta.description.isEmpty())
        json["description"] = doc.meta.description;
    if (doc.kind == Kind::Effect) {
        json["backend"] = QStringLiteral("gpu");
    } else if (doc.kind == Kind::Transition) {
        json["audioCurve"] = QStringLiteral("crossfade");
    } else {
        QJsonArray directions;
        if (doc.meta.directions && !doc.meta.directions->isEmpty())
            for (const QString &d : *doc.meta.directions)
                directions.append(d);
        else
            directions = QJsonArray{QStringLiteral("in"), QStringLiteral("out")};
        json["directions"] = directions;
        json["defaultDurationMs"] = doc.meta.defaultDurationMs.value_or(500);
    }
    QStringList requires_;
    if (compiled.usesMask)
        requires_ << QStringLiteral("mask");
    if (compiled.usesDepth)
        requires_ << QStringLiteral("depth");
    if (compiled.usesFace)
        requires_ << QStringLiteral("face");
    if (requires_.size() == 1) {
        json["requires"] = requires_[0];
    } else if (!requires_.isEmpty()) {
        QJsonArray a;
        for (const QString &r : requires_)
            a.append(r);
        json["requires"] = a;
    }
    QJsonArray parameters;
    for (const ParamDef &p : doc.params)
        parameters.append(paramManifestEntry(doc, p));
    json["parameters"] = parameters;
    json["pipeline"] = pipeline;
    if (doc.presets && !doc.presets->isEmpty()) {
        json["presets"] = presetsManifest(doc);
    }
    const QStringList features = nextFeatures(doc, compiled);
    if (!features.isEmpty()) {
        QJsonArray a;
        for (const QString &f : features)
            a.append(f);
        json["nextFeatures"] = a;
    }
    return json;
}

QString packageManifestName(const ForgeDoc &doc)
{
    return kindInfo(doc.kind).manifestFile;
}

}
