#include "engine/packages/procedurals/ProceduralCatalog.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace {

QList<drift::ProceduralPackage> g_catalog;
QHash<QString, int> g_idIndex;
QMutex g_mutex;
bool g_initialized = false;

void rebuildLocked(const QStringList &packageRoots)
{
    g_idIndex.clear();
    g_catalog = ProceduralPackageLoader::scanDirectories(
        packageRoots.isEmpty() ? ProceduralPackageLoader::defaultSearchPaths() : packageRoots);
    for (int i = 0; i < g_catalog.size(); ++i)
        g_idIndex.insert(g_catalog.at(i).id, i);
    g_initialized = true;
}

void ensureLocked()
{
    if (!g_initialized)
        rebuildLocked({});
}

} // namespace

void reloadProceduralCatalog(const QStringList &packageRoots)
{
    QMutexLocker lock(&g_mutex);
    rebuildLocked(packageRoots);
}

const QList<drift::ProceduralPackage> proceduralCatalog()
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    return g_catalog;
}

drift::ProceduralPackage proceduralForId(const QString &id)
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    const auto it = g_idIndex.constFind(id);
    return it == g_idIndex.constEnd() ? drift::ProceduralPackage{} : g_catalog.at(it.value());
}

bool hasProcedural(const QString &id)
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    return g_idIndex.contains(id);
}

QStringList proceduralIds()
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    QStringList ids;
    for (const drift::ProceduralPackage &p : std::as_const(g_catalog))
        ids.append(p.id);
    return ids;
}
