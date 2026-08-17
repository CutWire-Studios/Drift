#include "TransitionCatalog.h"

#include "TransitionPackageLoader.h"

#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace {

QList<TransitionPresetEntry> g_catalog;
QHash<QString, int> g_idIndex;
QList<QPair<QString, QString>> g_categories;
QMutex g_mutex;
bool g_initialized = false;

void rebuildLocked(const QStringList &packageRoots)
{
    g_catalog.clear();
    g_idIndex.clear();
    g_categories.clear();

    const QStringList roots =
        packageRoots.isEmpty() ? TransitionPackageLoader::defaultSearchPaths() : packageRoots;
    g_catalog = TransitionPackageLoader::scanDirectories(roots);

    for (int i = 0; i < g_catalog.size(); ++i)
        g_idIndex.insert(g_catalog.at(i).meta.id, i);

    static const QHash<QString, const char *> kCategoryLabels = {
        {QStringLiteral("basic"), QT_TRANSLATE_NOOP("TransitionCatalog", "Basic")},
        {QStringLiteral("geometric"), QT_TRANSLATE_NOOP("TransitionCatalog", "Grid & Geometric")},
        {QStringLiteral("liquid"), QT_TRANSLATE_NOOP("TransitionCatalog", "Particle & Liquid")},
        {QStringLiteral("glitch"), QT_TRANSLATE_NOOP("TransitionCatalog", "Glitch & Digital")},
        {QStringLiteral("cinematic"), QT_TRANSLATE_NOOP("TransitionCatalog", "Stylized & Cinematic")},
    };

    // Categories in first-seen (i.e. catalog order) order, so `order` in the JSON controls the UI.
    QSet<QString> seen;
    for (const TransitionPresetEntry &pkg : g_catalog) {
        if (seen.contains(pkg.meta.category))
            continue;
        seen.insert(pkg.meta.category);
        const auto it = kCategoryLabels.constFind(pkg.meta.category);
        const QString label = it != kCategoryLabels.cend()
            ? QCoreApplication::translate("TransitionCatalog", it.value())
            : (pkg.meta.category.isEmpty()
                   ? QCoreApplication::translate("TransitionCatalog", "Other")
                   : pkg.meta.category.at(0).toUpper() + pkg.meta.category.mid(1));
        g_categories.append({pkg.meta.category, label});
    }

    g_initialized = true;
}

void ensureLocked()
{
    if (!g_initialized)
        rebuildLocked({});
}

} // namespace

void reloadTransitionCatalog(const QStringList &packageRoots)
{
    QMutexLocker lock(&g_mutex);
    rebuildLocked(packageRoots);
}

const QList<TransitionPresetEntry> &transitionCatalog()
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    return g_catalog;
}

const TransitionPresetEntry *transitionDefForId(const QString &id)
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    const auto it = g_idIndex.constFind(id);
    if (it == g_idIndex.constEnd())
        return nullptr;
    return &g_catalog.at(it.value());
}

QStringList transitionPresetIds()
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    QStringList ids;
    ids.reserve(g_catalog.size());
    for (const TransitionPresetEntry &def : g_catalog)
        ids.append(def.meta.id);
    return ids;
}

QList<QPair<QString, QString>> transitionCategories()
{
    QMutexLocker lock(&g_mutex);
    ensureLocked();
    return g_categories;
}

QMap<QString, QVariant> resolvedTransitionParameters(const drift::Transition &transition,
                                                     const TransitionPresetEntry &def)
{
    QMap<QString, QVariant> params = def.fixedParams;
    for (const drift::EffectParamSpec &spec : def.meta.parameters)
        params.insert(spec.key, spec.defaultVariant());
    for (auto it = transition.parameters.constBegin(); it != transition.parameters.constEnd(); ++it)
        params.insert(it.key(), it.value());
    return params;
}
