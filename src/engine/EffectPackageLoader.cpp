#include "EffectPackageLoader.h"

#include "GpuEffectDefinition.h"
#include "GpuPackageParse.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace {

void setError(QString *errorOut, EffectPresetEntry *entry, const QString &message)
{
    entry->gpu.errorMessage = message;
    if (errorOut)
        *errorOut = message;
}

} // namespace

EffectPresetEntry EffectPackageLoader::loadPackage(const QString &packageDir, QString *errorOut)
{
    EffectPresetEntry entry;
    entry.gpu.packageDir = packageDir;
    if (errorOut)
        errorOut->clear();

    const QString jsonPath = QDir(packageDir).filePath(QStringLiteral("effect.json"));
    QString readError;
    const QString jsonText = GpuPackageParse::readTextFile(jsonPath, &readError);
    if (jsonText.isEmpty()) {
        setError(errorOut, &entry, readError.isEmpty() ? QStringLiteral("empty effect.json") : readError);
        return entry;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(errorOut, &entry, QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
        return entry;
    }

    const QJsonObject root = doc.object();
    const QString backend = root.value(QStringLiteral("backend")).toString();
    if (backend != QLatin1String("gpu") && backend != QLatin1String("libav")
        && backend != QLatin1String("compositor") && backend != QLatin1String("model3d")) {
        setError(errorOut, &entry,
                 backend.isEmpty() ? QStringLiteral("effect.json missing backend")
                                   : QStringLiteral("unsupported backend '%1'").arg(backend));
        return entry;
    }

    entry.meta.id = root.value(QStringLiteral("id")).toString();
    entry.meta.displayName = root.value(QStringLiteral("displayName")).toString();
    if (entry.meta.displayName.isEmpty())
        entry.meta.displayName = entry.meta.id;
    const QString categoryRaw = root.value(QStringLiteral("category")).toString();
    entry.meta.category =
        GpuPackageParse::slugifyCategory(categoryRaw.isEmpty() ? QStringLiteral("dreamy") : categoryRaw);
    entry.catalogOrder = root.value(QStringLiteral("order")).toInt(0);

    const QString requirement = root.value(QStringLiteral("requires")).toString();
    if (requirement == QLatin1String("face")) {
        entry.needsFace = true;
    } else if (!requirement.isEmpty()) {
        setError(errorOut, &entry, QStringLiteral("unsupported requires '%1'").arg(requirement));
        return entry;
    }

    if (entry.meta.id.isEmpty()) {
        setError(errorOut, &entry, QStringLiteral("effect.json missing id"));
        return entry;
    }

    // Optional thumbnail: explicit relative/absolute path, else thumbnail.png in the package.
    QString thumbRel = root.value(QStringLiteral("thumbnail")).toString();
    if (thumbRel.isEmpty())
        thumbRel = QStringLiteral("thumbnail.png");
    entry.thumbnailPath = GpuPackageParse::resolvePackageAsset(packageDir, thumbRel);

    QString error;
    // model3d is not a GPU fragment pipeline, but its parameters still use the same schema
    // (and may include file defaults resolved against the package dir).
    const bool reservedUniforms = backend == QLatin1String("gpu");
    if (!GpuPackageParse::parseParameters(root.value(QStringLiteral("parameters")).toArray(),
                                          &entry.meta.parameters, reservedUniforms, &error,
                                          packageDir)) {
        setError(errorOut, &entry, error);
        return entry;
    }

    if (root.contains(QStringLiteral("fixedParams")))
        GpuPackageParse::parseFixedParams(root.value(QStringLiteral("fixedParams")).toObject(),
                                          &entry.fixedParams);

    if (backend == QLatin1String("model3d")) {
        entry.isModel3d = true;
        entry.meta.compositorOnly = true;
        entry.gpu.packageDir = packageDir;
        return entry;
    }

    if (backend == QLatin1String("gpu")) {
        entry.isGpu = true;
        entry.filterName = QStringLiteral("gpu");
        // Effects have a single source frame.
        if (!GpuPackageParse::loadGpuPipeline(root, packageDir, /*maxSourceIndex=*/0, &entry.gpu,
                                              &error)) {
            setError(errorOut, &entry, error);
        }
        return entry;
    }

    if (backend == QLatin1String("compositor")) {
        entry.meta.compositorOnly = true;
        entry.filterName.clear();
        return entry;
    }

    // libav
    entry.filterName = root.value(QStringLiteral("filterName")).toString();
    entry.graphTemplate = root.value(QStringLiteral("graphTemplate")).toString();
    if (entry.filterName.isEmpty() && entry.graphTemplate.isEmpty()) {
        setError(errorOut, &entry, QStringLiteral("libav effect needs filterName or graphTemplate"));
        return entry;
    }
    return entry;
}

QList<EffectPresetEntry> EffectPackageLoader::scanDirectories(const QStringList &rootDirs)
{
    QList<EffectPresetEntry> loaded;
    QSet<QString> seenIds;

    for (const QString &root : rootDirs) {
        if (root.isEmpty())
            continue;
        QDir dir(root);
        if (!dir.exists())
            continue;

        const QFileInfoList subdirs =
            dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : subdirs) {
            const QString packageDir = info.absoluteFilePath();
            if (!QFileInfo::exists(QDir(packageDir).filePath(QStringLiteral("effect.json"))))
                continue;

            QString error;
            EffectPresetEntry entry = loadPackage(packageDir, &error);
            if (!error.isEmpty() || entry.meta.id.isEmpty()) {
                qWarning("EffectPackageLoader: skip %s: %s", qPrintable(packageDir),
                         qPrintable(error.isEmpty() ? QStringLiteral("invalid package") : error));
                continue;
            }
            if (seenIds.contains(entry.meta.id)) {
                // Installed addons supersede the bundled <appDir> copy — not an error.
                qDebug("EffectPackageLoader: duplicate id '%s' in %s (ignored)",
                       qPrintable(entry.meta.id), qPrintable(packageDir));
                continue;
            }
            seenIds.insert(entry.meta.id);
            loaded.append(entry);
        }
    }

    std::sort(loaded.begin(), loaded.end(), [](const EffectPresetEntry &a, const EffectPresetEntry &b) {
        if (a.catalogOrder != b.catalogOrder)
            return a.catalogOrder < b.catalogOrder;
        return a.meta.id < b.meta.id;
    });
    return loaded;
}

QStringList EffectPackageLoader::defaultSearchPaths()
{
    return GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_EFFECTS_DIR"),
                                               QStringLiteral("effects"),
                                               QStringLiteral("effects"));
}
