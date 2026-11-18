#include "app/forge/compiler/PackageFiles.h"

#include "app/forge/audio/AudioManifest.h"
#include "app/forge/audio/RackGraph.h"
#include "app/forge/compiler/Manifest.h"
#include "app/forge/doc/ParamTypes.h"
#include "app/forge/doc/Util.h"

#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace drift::forge {

QByteArray assetBytes(const ForgeDoc &doc, const QString &assetId)
{
    for (const ForgeAsset &a : doc.assets) {
        if (a.id != assetId)
            continue;
        QString data = a.data;
        const int comma = data.indexOf(QLatin1String("base64,"));
        if (comma >= 0)
            data = data.mid(comma + 7);
        return QByteArray::fromBase64(data.toLatin1());
    }
    return {};
}

QString exportId(const ForgeDoc &doc)
{
    return doc.meta.id.isEmpty() ? makeEffectId(doc.meta.displayName) : doc.meta.id;
}

QString exportProblem(const ForgeDoc &doc)
{
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9][a-z0-9_.]*$"));
    if (!valid.match(doc.meta.id).hasMatch())
        return QStringLiteral("The id must use lowercase letters, digits, underscores and dots, starting with a letter or digit.");
    if (doc.meta.displayName.trimmed().isEmpty())
        return QStringLiteral("Give it a name first.");
    return {};
}

namespace {

PackageContents audioPackageContents(const ForgeDoc &doc)
{
    PackageContents out;
    out.manifest = audioManifest(doc);
    const Rack rack = rackOf(doc);
    walkRack(rack.chain, [&](const RackItem &item, int) {
        if (item.ir.isEmpty())
            return;
        const QString path = irPath(doc, item.ir);
        const bool seen = std::any_of(out.files.cbegin(), out.files.cend(), [&](const PackageFile &f) { return f.path == path; });
        if (seen)
            return;
        const QString builtin = builtinIr(item.ir);
        if (!builtin.isNull()) {
            QFile f(QStringLiteral(":/forge/ir/%1.wav").arg(builtin));
            if (f.open(QIODevice::ReadOnly))
                out.files.append({path, f.readAll()});
        } else {
            const QByteArray bytes = assetBytes(doc, item.ir);
            if (!bytes.isEmpty())
                out.files.append({path, bytes});
        }
    });
    out.files.append({QStringLiteral("forge.json"), QJsonDocument(doc.toJson()).toJson(QJsonDocument::Indented)});
    return out;
}

}

PackageContents packageContents(const ForgeDoc &doc, const CompileResult &compiled)
{
    if (doc.kind == Kind::Audio)
        return audioPackageContents(doc);
    PackageContents out;
    out.manifest = packageManifest(doc, compiled);
    for (const CompiledPass &p : compiled.passes)
        out.files.append({p.file, p.source.toUtf8()});
    for (const QJsonValue &v : compiled.textures) {
        const QJsonObject t = v.toObject();
        out.files.append({t.value("file").toString(), assetBytes(doc, t.value("assetId").toString())});
    }
    for (const ParamDef &p : doc.params) {
        if (p.type != ParamType::Image)
            continue;
        const QString file = imageParamFile(doc, p);
        const bool seen = std::any_of(out.files.cbegin(), out.files.cend(), [&](const PackageFile &f) { return f.path == file; });
        if (!file.isEmpty() && !seen)
            out.files.append({file, assetBytes(doc, p.defaultValue.toString())});
    }
    out.files.append({QStringLiteral("forge.json"), QJsonDocument(doc.toJson()).toJson(QJsonDocument::Indented)});
    return out;
}

}
