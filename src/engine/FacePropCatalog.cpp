#include "FacePropCatalog.h"

#include "FacePropImport.h"
#include "GpuPackageParse.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

QMutex g_mutex;
QList<FacePropEntry> g_props;
bool g_initialized = false;

void rebuildLocked(const QStringList &packageRoots)
{
    g_props.clear();
    g_initialized = true;

    const QStringList roots = packageRoots.isEmpty() ? facePropSearchPaths() : packageRoots;
    const QString userRoot = QDir::cleanPath(userFacePropsDir());
    QSet<QString> seenIds;

    const auto append = [&](FacePropEntry entry) {
        if (seenIds.contains(entry.id))
            return;
        seenIds.insert(entry.id);
        g_props.append(entry);
    };

    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;
        const bool userInstalled = QDir::cleanPath(dir.absolutePath()) == userRoot;

        // Flat *.glb in the root, and one level of prop or pack subdirectories.
        const auto appendGlb = [&](const QString &path) {
            const QFileInfo info(path);
            if (!info.exists() || info.suffix().compare(QLatin1String("glb"), Qt::CaseInsensitive) != 0)
                return;
            FacePropEntry entry;
            entry.id = info.completeBaseName();
            entry.label = entry.id;
            entry.path = info.absoluteFilePath();
            entry.userInstalled = userInstalled;
            append(entry);
        };

        for (const QFileInfo &file :
             dir.entryInfoList({QStringLiteral("*.glb"), QStringLiteral("*.GLB")},
                               QDir::Files, QDir::Name)) {
            appendGlb(file.absoluteFilePath());
        }
        for (const QFileInfo &subdir :
             dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QDir pack(subdir.absoluteFilePath());
            QFile manifestFile(pack.filePath(QStringLiteral("prop.json")));
            if (manifestFile.exists()) {
                FacePropManifest manifest;
                if (!manifestFile.open(QIODevice::ReadOnly)
                    || !parseFacePropManifest(manifestFile.read(64 * 1024), subdir.fileName(),
                                              &manifest, nullptr)
                    || !QFileInfo::exists(pack.filePath(manifest.model))) {
                    continue;
                }
                FacePropEntry entry;
                entry.id = manifest.id;
                entry.label = manifest.name;
                entry.path = pack.filePath(manifest.model);
                entry.dir = pack.absolutePath();
                if (!manifest.thumbnail.isEmpty() && QFileInfo::exists(pack.filePath(manifest.thumbnail)))
                    entry.thumbnailPath = pack.filePath(manifest.thumbnail);
                entry.description = manifest.description;
                entry.license = manifest.license;
                entry.tags = manifest.tags;
                entry.params = manifest.params;
                entry.userInstalled = userInstalled;
                append(entry);
                continue;
            }
            for (const QFileInfo &file :
                 pack.entryInfoList({QStringLiteral("*.glb"), QStringLiteral("*.GLB")},
                                    QDir::Files, QDir::Name)) {
                appendGlb(file.absoluteFilePath());
            }
        }
    }

    std::stable_sort(g_props.begin(), g_props.end(), [](const FacePropEntry &a, const FacePropEntry &b) {
        return a.label.compare(b.label, Qt::CaseInsensitive) < 0;
    });
}

void ensureLoadedLocked()
{
    if (!g_initialized)
        rebuildLocked({});
}

} // namespace

QStringList facePropSearchPaths()
{
    QStringList roots = GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_FACE_PROPS_DIR"),
                                                            QStringLiteral("face-props"),
                                                            QStringLiteral("face-props"));
    roots.prepend(userFacePropsDir());
    roots.removeDuplicates();
    return roots;
}

QString userFacePropsDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("face-props"));
}

const QList<FacePropEntry> &faceProps()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    return g_props;
}

QList<FacePropEntry> facePropsSnapshot()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    return g_props;
}

void reloadFacePropCatalog(const QStringList &packageRoots)
{
    QMutexLocker lock(&g_mutex);
    rebuildLocked(packageRoots);
}
