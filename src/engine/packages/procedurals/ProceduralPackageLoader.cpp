#include "engine/packages/procedurals/ProceduralPackageLoader.h"

#include "engine/addons/PackageSearchPaths.h"
#include "engine/packages/GpuPackageParse.h"
#include "engine/procedural/ProceduralEvaluator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

using drift::ProceduralPackage;

namespace {

ProceduralPackage fail(ProceduralPackage pkg, QString *errorOut, const QString &message)
{
    pkg.valid = false;
    pkg.errorMessage = message;
    if (errorOut)
        *errorOut = message;
    return pkg;
}

bool readCapped(const QString &path, qint64 cap, QByteArray *out, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot open %1").arg(QFileInfo(path).fileName());
        return false;
    }
    if (file.size() > cap) {
        *error = QStringLiteral("%1 exceeds the %2 KiB limit")
                     .arg(QFileInfo(path).fileName())
                     .arg(cap / 1024);
        return false;
    }
    *out = file.readAll();
    return true;
}

std::size_t clampLimit(const QJsonValue &v, std::size_t platform)
{
    if (!v.isDouble() || v.toDouble() < 1.0)
        return platform;
    return std::min(static_cast<std::size_t>(v.toDouble()), platform);
}

} // namespace

ProceduralPackage ProceduralPackageLoader::loadPackage(const QString &packageDir, QString *errorOut)
{
    ProceduralPackage pkg;
    pkg.packageDir = packageDir;
    if (errorOut)
        errorOut->clear();

    const QDir dir(packageDir);
    QString error;
    QByteArray manifestBytes;
    if (!readCapped(dir.filePath(QStringLiteral("procedural.json")), kMaxManifestBytes, &manifestBytes, &error))
        return fail(pkg, errorOut, error);

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(pkg, errorOut, QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
    const QJsonObject root = doc.object();

    pkg.id = root.value(QStringLiteral("id")).toString();
    if (pkg.id.isEmpty())
        return fail(pkg, errorOut, QStringLiteral("procedural.json missing id"));
    pkg.displayName = root.value(QStringLiteral("displayName")).toString(pkg.id);
    if (pkg.displayName.isEmpty())
        pkg.displayName = pkg.id;
    pkg.description = root.value(QStringLiteral("description")).toString();
    pkg.category = GpuPackageParse::slugifyCategory(
        root.value(QStringLiteral("category")).toString(QStringLiteral("procedural")));
    pkg.order = root.value(QStringLiteral("order")).toInt(0);

    if (!GpuPackageParse::checkNextFeatures(root, &error))
        return fail(pkg, errorOut, error);
    // Params become shader uniforms, so they get the same reserved-name check as GPU effects.
    if (!GpuPackageParse::parseParameters(root.value(QStringLiteral("parameters")).toArray(),
                                          &pkg.parameters, /*gpuBackend=*/true, &error, packageDir))
        return fail(pkg, errorOut, error);

    const drift::ProceduralBudgets budgets = drift::ProceduralBudgets::forPlatform();
    pkg.maxVertices = clampLimit(root.value(QStringLiteral("maxVertices")), budgets.maxVertices);
    pkg.maxInstances = clampLimit(root.value(QStringLiteral("maxInstances")), budgets.maxInstances);

    QByteArray luaBytes;
    if (!QFileInfo::exists(dir.filePath(QStringLiteral("main.lua"))))
        return fail(pkg, errorOut, QStringLiteral("missing main.lua"));
    if (!readCapped(dir.filePath(QStringLiteral("main.lua")), kMaxLuaBytes, &luaBytes, &error))
        return fail(pkg, errorOut, error);
    pkg.luaSource = QString::fromUtf8(luaBytes);

    QSet<QString> names;
    const QFileInfoList shaderFiles = dir.entryInfoList(
        {QStringLiteral("*.vert"), QStringLiteral("*.frag")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : shaderFiles)
        names.insert(info.completeBaseName());
    if (names.isEmpty())
        return fail(pkg, errorOut, QStringLiteral("no shader pairs (*.vert + *.frag)"));
    if (names.size() > kMaxShaderPairs)
        return fail(pkg, errorOut, QStringLiteral("more than %1 shader pairs").arg(kMaxShaderPairs));

    QCryptographicHash hash(QCryptographicHash::Sha256);
    const auto feed = [&hash](const QString &label, const QByteArray &bytes) {
        hash.addData(label.toUtf8());
        hash.addData(QByteArray(1, '\0'));
        hash.addData(QByteArray::number(bytes.size()));
        hash.addData(QByteArray(1, '\0'));
        hash.addData(bytes);
    };
    feed(QStringLiteral("procedural.json"), manifestBytes);
    feed(QStringLiteral("main.lua"), luaBytes);

    QStringList sorted = names.values();
    std::sort(sorted.begin(), sorted.end());
    for (const QString &name : std::as_const(sorted)) {
        const QString vertName = name + QStringLiteral(".vert");
        const QString fragName = name + QStringLiteral(".frag");
        if (!QFileInfo::exists(dir.filePath(vertName)))
            return fail(pkg, errorOut, QStringLiteral("%1 has no matching %2").arg(fragName, vertName));
        if (!QFileInfo::exists(dir.filePath(fragName)))
            return fail(pkg, errorOut, QStringLiteral("%1 has no matching %2").arg(vertName, fragName));
        QByteArray vert;
        QByteArray frag;
        if (!readCapped(dir.filePath(vertName), kMaxShaderBytes, &vert, &error)
            || !readCapped(dir.filePath(fragName), kMaxShaderBytes, &frag, &error))
            return fail(pkg, errorOut, error);
        feed(vertName, vert);
        feed(fragName, frag);
        pkg.shaders.insert(name, {QString::fromUtf8(vert), QString::fromUtf8(frag)});
    }
    pkg.sourceHash = QString::fromLatin1(hash.result().toHex());

    const QString thumb = root.value(QStringLiteral("thumbnail")).toString();
    pkg.thumbnailPath = GpuPackageParse::resolvePackageAsset(
        packageDir, thumb.isEmpty() ? QStringLiteral("thumbnail.webp") : thumb);

    pkg.valid = true;
    return pkg;
}

QList<ProceduralPackage> ProceduralPackageLoader::scanDirectories(const QStringList &rootDirs)
{
    QList<ProceduralPackage> loaded;
    QSet<QString> seenIds;

    for (const QString &root : rootDirs) {
        if (root.isEmpty())
            continue;
        const QDir dir(root);
        if (!dir.exists())
            continue;
        const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : subdirs) {
            const QString packageDir = info.absoluteFilePath();
            if (!QFileInfo::exists(QDir(packageDir).filePath(QStringLiteral("procedural.json"))))
                continue;
            QString error;
            ProceduralPackage pkg = loadPackage(packageDir, &error);
            if (!pkg.valid) {
                qWarning("ProceduralPackageLoader: skip %s: %s", qPrintable(packageDir), qPrintable(error));
                continue;
            }
            if (seenIds.contains(pkg.id))
                continue;
            seenIds.insert(pkg.id);
            loaded.append(pkg);
        }
    }

    std::sort(loaded.begin(), loaded.end(), [](const ProceduralPackage &a, const ProceduralPackage &b) {
        if (a.order != b.order)
            return a.order < b.order;
        return a.id < b.id;
    });
    return loaded;
}

QStringList ProceduralPackageLoader::defaultSearchPaths()
{
    return PackageSearchPaths::defaultSearchPaths(QStringLiteral("DRIFT_PROCEDURALS_DIR"),
                                                  QStringLiteral("procedurals"),
                                                  QStringLiteral("procedurals"));
}
