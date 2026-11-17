#include "engine/packages/animations/AnimationCatalog.h"

#include "engine/packages/animations/AnimationPackageLoader.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>

namespace {

QList<AnimationPresetEntry> g_catalog;
QHash<QString, int> g_idIndex;
QMutex g_mutex;
bool g_initialized = false;

void rebuildLocked(const QStringList &packageRoots)
{
    g_catalog.clear();
    g_idIndex.clear();

    const QStringList roots =
        packageRoots.isEmpty() ? AnimationPackageLoader::defaultSearchPaths() : packageRoots;
    g_catalog = AnimationPackageLoader::scanDirectories(roots);

    for (int i = 0; i < g_catalog.size(); ++i)
        g_idIndex.insert(g_catalog.at(i).meta.id, i);

    g_initialized = true;
}

void ensureLocked()
{
    if (!g_initialized)
        rebuildLocked({});
}

} // namespace

void reloadAnimationCatalog(const QStringList &packageRoots)
{
    QMutexLocker lock(&g_mutex);
    rebuildLocked(packageRoots);
}

const QList<AnimationPresetEntry> &animationCatalog()
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    return g_catalog;
}

const AnimationPresetEntry *animationDefForId(const QString &id)
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    const auto it = g_idIndex.constFind(id);
    if (it == g_idIndex.constEnd())
        return nullptr;
    return &g_catalog.at(it.value());
}

QMap<QString, QVariant> resolvedAnimationParameters(const QMap<QString, QVariant> &instance,
                                                    const AnimationPresetEntry &def)
{
    QMap<QString, QVariant> params = def.fixedParams;
    for (const drift::EffectParamSpec &spec : def.meta.parameters)
        spec.insertDefault(params);
    for (auto it = instance.constBegin(); it != instance.constEnd(); ++it)
        params.insert(it.key(), it.value());
    return params;
}
