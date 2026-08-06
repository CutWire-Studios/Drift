#include "FacePropCatalog.h"

#include "GpuPackageParse.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

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
    QSet<QString> seenIds;

    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;

        // Flat *.glb in the root, and one level of pack subdirectories.
        const auto appendGlb = [&](const QString &path) {
            const QFileInfo info(path);
            if (!info.exists() || info.suffix().compare(QLatin1String("glb"), Qt::CaseInsensitive) != 0)
                return;
            FacePropEntry entry;
            entry.id = info.completeBaseName();
            entry.label = entry.id;
            entry.path = info.absoluteFilePath();
            if (seenIds.contains(entry.id))
                return;
            seenIds.insert(entry.id);
            g_props.append(entry);
        };

        for (const QFileInfo &file :
             dir.entryInfoList({QStringLiteral("*.glb"), QStringLiteral("*.GLB")},
                               QDir::Files, QDir::Name)) {
            appendGlb(file.absoluteFilePath());
        }
        for (const QFileInfo &subdir :
             dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            QDir pack(subdir.absoluteFilePath());
            for (const QFileInfo &file :
                 pack.entryInfoList({QStringLiteral("*.glb"), QStringLiteral("*.GLB")},
                                    QDir::Files, QDir::Name)) {
                appendGlb(file.absoluteFilePath());
            }
        }
    }

    std::stable_sort(g_props.begin(), g_props.end(),
                     [](const FacePropEntry &a, const FacePropEntry &b) { return a.id < b.id; });
}

void ensureLoadedLocked()
{
    if (!g_initialized)
        rebuildLocked({});
}

} // namespace

QStringList facePropSearchPaths()
{
    return GpuPackageParse::defaultSearchPaths(QStringLiteral("DRIFT_FACE_PROPS_DIR"),
                                               QStringLiteral("face-props"),
                                               QStringLiteral("face-props"));
}

const QList<FacePropEntry> &faceProps()
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
