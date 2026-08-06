#include "TransitionPackageLoader.h"

#include "GpuPackageParse.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace {

void setError(QString *errorOut, TransitionPresetEntry *entry, const QString &message)
{
    entry->gpu.errorMessage = message;
    if (errorOut)
        *errorOut = message;
}

} // namespace

TransitionPresetEntry TransitionPackageLoader::loadPackage(const QString &packageDir, QString *errorOut)
{
    TransitionPresetEntry entry;
    entry.gpu.packageDir = packageDir;
    if (errorOut)
        errorOut->clear();

    const QString jsonPath = QDir(packageDir).filePath(QStringLiteral("transition.json"));
    QString readError;
    const QString jsonText = GpuPackageParse::readTextFile(jsonPath, &readError);
    if (jsonText.isEmpty()) {
        setError(errorOut, &entry,
                 readError.isEmpty() ? QStringLiteral("empty transition.json") : readError);
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
        setError(errorOut, &entry, QStringLiteral("transition.json missing id"));
        return entry;
    }
    entry.meta.displayName = root.value(QStringLiteral("displayName")).toString();
    if (entry.meta.displayName.isEmpty())
        entry.meta.displayName = entry.meta.id;
    const QString categoryRaw = root.value(QStringLiteral("category")).toString();
    entry.meta.category =
        GpuPackageParse::slugifyCategory(categoryRaw.isEmpty() ? QStringLiteral("basic") : categoryRaw);
    entry.catalogOrder = root.value(QStringLiteral("order")).toInt(0);

    entry.audioCurve = root.value(QStringLiteral("audioCurve")).toString();
    if (entry.audioCurve.isEmpty())
        entry.audioCurve = QStringLiteral("crossfade");
    if (entry.audioCurve != QLatin1String("crossfade") && entry.audioCurve != QLatin1String("dip")
        && entry.audioCurve != QLatin1String("hold")) {
        setError(errorOut, &entry,
                 QStringLiteral("unknown audioCurve '%1'").arg(entry.audioCurve));
        return entry;
    }

    QString stripRel = root.value(QStringLiteral("previewStrip")).toString();
    if (stripRel.isEmpty())
        stripRel = QStringLiteral("preview_strip.png");
    entry.previewStripPath = GpuPackageParse::resolvePackageAsset(packageDir, stripRel);
    if (!entry.previewStripPath.isEmpty()) {
        entry.previewFrames = root.value(QStringLiteral("previewFrames")).toInt(0);
        if (entry.previewFrames <= 0) {
            // Square cells: infer the count from the strip's aspect ratio without decoding it.
            const QSize size = QImageReader(entry.previewStripPath).size();
            entry.previewFrames =
                (size.isValid() && size.height() > 0) ? qMax(1, size.width() / size.height()) : 1;
        }
    }

    QString error;
    if (!GpuPackageParse::parseParameters(root.value(QStringLiteral("parameters")).toArray(),
                                          &entry.meta.parameters, /*gpuBackend=*/true, &error,
                                          packageDir)) {
        setError(errorOut, &entry, error);
        return entry;
    }

    if (root.contains(QStringLiteral("fixedParams")))
        GpuPackageParse::parseFixedParams(root.value(QStringLiteral("fixedParams")).toObject(),
                                          &entry.fixedParams);

    // Transitions get two source frames: index 0 = outgoing (from), index 1 = incoming (to).
    if (!GpuPackageParse::loadGpuPipeline(root, packageDir, /*maxSourceIndex=*/1, &entry.gpu, &error))
        setError(errorOut, &entry, error);

    return entry;
}

QList<TransitionPresetEntry> TransitionPackageLoader::scanDirectories(const QStringList &rootDirs)
{
    QList<TransitionPresetEntry> loaded;
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
            if (!QFileInfo::exists(QDir(packageDir).filePath(QStringLiteral("transition.json"))))
                continue;

            QString error;
            TransitionPresetEntry entry = loadPackage(packageDir, &error);
            if (!error.isEmpty() || entry.meta.id.isEmpty()) {
                qWarning("TransitionPackageLoader: skip %s: %s", qPrintable(packageDir),
                         qPrintable(error.isEmpty() ? QStringLiteral("invalid package") : error));
                continue;
            }
            if (seenIds.contains(entry.meta.id)) {
                // Installed addons supersede the bundled <appDir> copy — not an error.
                qDebug("TransitionPackageLoader: duplicate id '%s' in %s (ignored)",
                       qPrintable(entry.meta.id), qPrintable(packageDir));
                continue;
            }
            seenIds.insert(entry.meta.id);
            loaded.append(entry);
        }
    }

    std::sort(loaded.begin(), loaded.end(),
              [](const TransitionPresetEntry &a, const TransitionPresetEntry &b) {
                  if (a.catalogOrder != b.catalogOrder)
                      return a.catalogOrder < b.catalogOrder;
                  return a.meta.id < b.meta.id;
              });
    return loaded;
}

QStringList TransitionPackageLoader::defaultSearchPaths()
{
    return GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_TRANSITIONS_DIR"),
                                               QStringLiteral("transitions"),
                                               QStringLiteral("transitions"));
}
