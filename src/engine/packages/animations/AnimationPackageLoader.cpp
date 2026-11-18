#include "engine/packages/animations/AnimationPackageLoader.h"

#include "engine/addons/PackageSearchPaths.h"
#include "engine/packages/GpuPackageParse.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace {

void setError(QString *errorOut, AnimationPresetEntry *entry, const QString &message)
{
    entry->gpu.errorMessage = message;
    if (errorOut)
        *errorOut = message;
}

} // namespace

AnimationPresetEntry AnimationPackageLoader::loadPackage(const QString &packageDir, QString *errorOut)
{
    AnimationPresetEntry entry;
    entry.gpu.packageDir = packageDir;
    if (errorOut)
        errorOut->clear();

    const QString jsonPath = QDir(packageDir).filePath(QStringLiteral("animation.json"));
    QString readError;
    const QString jsonText = GpuPackageParse::readTextFile(jsonPath, &readError);
    if (jsonText.isEmpty()) {
        setError(errorOut, &entry,
                 readError.isEmpty() ? QStringLiteral("empty animation.json") : readError);
        return entry;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(errorOut, &entry, QStringLiteral("invalid JSON: %1").arg(parseError.errorString()));
        return entry;
    }

    const QJsonObject root = doc.object();

    entry.meta.id = root.value(QStringLiteral("id")).toString();
    if (entry.meta.id.isEmpty()) {
        setError(errorOut, &entry, QStringLiteral("animation.json missing id"));
        return entry;
    }
    entry.meta.displayName = root.value(QStringLiteral("displayName")).toString();
    if (entry.meta.displayName.isEmpty())
        entry.meta.displayName = entry.meta.id;
    const QString categoryRaw = root.value(QStringLiteral("category")).toString();
    entry.meta.category =
        GpuPackageParse::slugifyCategory(categoryRaw.isEmpty() ? QStringLiteral("basic") : categoryRaw);
    entry.catalogOrder = root.value(QStringLiteral("order")).toInt(0);
    entry.defaultDurationMs = qMax(1, root.value(QStringLiteral("defaultDurationMs")).toInt(500));

    if (root.contains(QStringLiteral("directions"))) {
        for (const QJsonValue &v : root.value(QStringLiteral("directions")).toArray()) {
            const QString dir = v.toString();
            if (dir != QLatin1String("in") && dir != QLatin1String("out")) {
                setError(errorOut, &entry, QStringLiteral("unknown direction '%1'").arg(dir));
                return entry;
            }
            if (!entry.directions.contains(dir))
                entry.directions.append(dir);
        }
        if (entry.directions.isEmpty()) {
            setError(errorOut, &entry, QStringLiteral("directions is empty"));
            return entry;
        }
    } else {
        entry.directions = {QStringLiteral("in"), QStringLiteral("out")};
    }

    const QString stripRel = root.value(QStringLiteral("previewStrip")).toString();
    if (!stripRel.isEmpty()) {
        entry.previewStripPath = GpuPackageParse::resolvePackageAsset(packageDir, stripRel);
    } else {
        entry.previewStripPath =
            GpuPackageParse::resolvePackageAsset(packageDir, QStringLiteral("preview_strip.webp"));
        if (entry.previewStripPath.isEmpty()) {
            entry.previewStripPath = GpuPackageParse::resolvePackageAsset(
                packageDir, QStringLiteral("preview_strip.png"));
        }
    }

    QString error;
    if (!GpuPackageParse::checkNextFeatures(root, &error)) {
        setError(errorOut, &entry, error);
        return entry;
    }
    if (!GpuPackageParse::parseParameters(root.value(QStringLiteral("parameters")).toArray(),
                                          &entry.meta.parameters, /*gpuBackend=*/true, &error,
                                          packageDir)) {
        setError(errorOut, &entry, error);
        return entry;
    }

    if (root.contains(QStringLiteral("fixedParams")))
        GpuPackageParse::parseFixedParams(root.value(QStringLiteral("fixedParams")).toObject(),
                                          &entry.fixedParams);

    // One source: the clip's isolated layer.
    if (!GpuPackageParse::loadGpuPipeline(root, packageDir, /*maxSourceIndex=*/0, &entry.gpu, &error)) {
        setError(errorOut, &entry, error);
        return entry;
    }
    GpuPackageParse::collectRuntimeParams(entry.meta.parameters, &entry.gpu);

    return entry;
}

QList<AnimationPresetEntry> AnimationPackageLoader::scanDirectories(const QStringList &rootDirs)
{
    QList<AnimationPresetEntry> loaded;
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
            if (!QFileInfo::exists(QDir(packageDir).filePath(QStringLiteral("animation.json"))))
                continue;

            QString error;
            AnimationPresetEntry entry = loadPackage(packageDir, &error);
            if (!error.isEmpty() || entry.meta.id.isEmpty()) {
                qWarning("AnimationPackageLoader: skip %s: %s", qPrintable(packageDir),
                         qPrintable(error.isEmpty() ? QStringLiteral("invalid package") : error));
                continue;
            }
            if (seenIds.contains(entry.meta.id))
                continue;
            seenIds.insert(entry.meta.id);
            loaded.append(entry);
        }
    }

    std::sort(loaded.begin(), loaded.end(),
              [](const AnimationPresetEntry &a, const AnimationPresetEntry &b) {
                  if (a.catalogOrder != b.catalogOrder)
                      return a.catalogOrder < b.catalogOrder;
                  return a.meta.id < b.meta.id;
              });
    return loaded;
}

QStringList AnimationPackageLoader::defaultSearchPaths()
{
    return PackageSearchPaths::defaultSearchPaths(QStringLiteral("DRIFT_ANIMATIONS_DIR"),
                                                  QStringLiteral("animations"),
                                                  QStringLiteral("animations"));
}
