#include "EffectCatalog.h"

#include "EffectPackageLoader.h"

#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>

namespace {

QString substituteTemplate(QString templ, const QMap<QString, QVariant> &params)
{
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const QString placeholder = QStringLiteral("{{%1}}").arg(it.key());
        templ.replace(placeholder, it.value().toString());
    }
    return templ;
}

QString singleFilterGraph(const QString &filterName, const QMap<QString, QVariant> &params)
{
    if (filterName.isEmpty())
        return {};

    QStringList parts;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const QString value = it.value().toString();
        if (!value.isEmpty())
            parts.append(QStringLiteral("%1=%2").arg(it.key(), value));
    }

    if (parts.isEmpty())
        return filterName;

    return QStringLiteral("%1=%2").arg(filterName, parts.join(QLatin1Char(':')));
}

QString translatedCategoryLabel(const QString &slug)
{
    if (slug == QLatin1String("color"))
        return QCoreApplication::translate("EffectCatalog", "Color");
    if (slug == QLatin1String("glitch"))
        return QCoreApplication::translate("EffectCatalog", "Glitch & Distortion");
    if (slug == QLatin1String("retro"))
        return QCoreApplication::translate("EffectCatalog", "Retro / Analog");
    if (slug == QLatin1String("dreamy"))
        return QCoreApplication::translate("EffectCatalog", "Dreamy & Stylish");
    if (slug == QLatin1String("impact"))
        return QCoreApplication::translate("EffectCatalog", "Impact");
    if (slug == QLatin1String("blurs") || slug == QLatin1String("blurs_distortions"))
        return QCoreApplication::translate("EffectCatalog", "Blurs & Distortions");
    if (slug == QLatin1String("funny"))
        return QCoreApplication::translate("EffectCatalog", "Funny Face");
    if (slug == QLatin1String("beauty"))
        return QCoreApplication::translate("EffectCatalog", "Beauty & Makeup");
    if (slug == QLatin1String("artistic"))
        return QCoreApplication::translate("EffectCatalog", "Artistic");
    if (slug.isEmpty())
        return QCoreApplication::translate("EffectCatalog", "Other");
    return slug.at(0).toUpper() + slug.mid(1);
}

QList<EffectPresetEntry> g_mergedCatalog;
QHash<QString, int> g_idIndex;
QList<QPair<QString, QString>> g_extraCategories;
QMutex g_catalogMutex;
bool g_catalogInitialized = false;

void rebuildCatalogLocked(const QStringList &packageRoots)
{
    g_mergedCatalog.clear();
    g_idIndex.clear();
    g_extraCategories.clear();

    const QStringList roots =
        packageRoots.isEmpty() ? EffectPackageLoader::defaultSearchPaths() : packageRoots;
    g_mergedCatalog = EffectPackageLoader::scanDirectories(roots);

    for (int i = 0; i < g_mergedCatalog.size(); ++i)
        g_idIndex.insert(g_mergedCatalog.at(i).meta.id, i);

    QSet<QString> knownCats = {
        QStringLiteral("color"),
        QStringLiteral("glitch"),
        QStringLiteral("retro"),
        QStringLiteral("dreamy"),
        QStringLiteral("impact"),
    };

    for (const EffectPresetEntry &pkg : g_mergedCatalog) {
        if (!knownCats.contains(pkg.meta.category)) {
            knownCats.insert(pkg.meta.category);
            g_extraCategories.append({pkg.meta.category, translatedCategoryLabel(pkg.meta.category)});
        }
    }
    g_catalogInitialized = true;
}

void ensureCatalogLocked()
{
    if (g_catalogInitialized)
        return;
    rebuildCatalogLocked({});
}

} // namespace

void reloadEffectCatalog(const QStringList &packageRoots)
{
    QMutexLocker lock(&g_catalogMutex);
    rebuildCatalogLocked(packageRoots);
}

const QList<EffectPresetEntry> &effectCatalog()
{
    QMutexLocker lock(&g_catalogMutex);
    ensureCatalogLocked();
    return g_mergedCatalog;
}

const EffectPresetEntry *effectDefForId(const QString &id)
{
    QString resolved = id;
    if (resolved == QLatin1String("stylize.rgb_split"))
        resolved = QStringLiteral("rgb_split");

    QMutexLocker lock(&g_catalogMutex);
    ensureCatalogLocked();
    const auto it = g_idIndex.constFind(resolved);
    if (it == g_idIndex.constEnd())
        return nullptr;
    return &g_mergedCatalog.at(it.value());
}

QStringList effectPresetIds()
{
    QMutexLocker lock(&g_catalogMutex);
    ensureCatalogLocked();
    QStringList ids;
    ids.reserve(g_mergedCatalog.size());
    for (const EffectPresetEntry &def : g_mergedCatalog)
        ids.append(def.meta.id);
    return ids;
}

QList<QPair<QString, QString>> effectCategories()
{
    QList<QPair<QString, QString>> cats = {
        {QStringLiteral("color"), QCoreApplication::translate("EffectCatalog", "Color")},
        {QStringLiteral("glitch"), QCoreApplication::translate("EffectCatalog", "Glitch & Distortion")},
        {QStringLiteral("retro"), QCoreApplication::translate("EffectCatalog", "Retro / Analog")},
        {QStringLiteral("dreamy"), QCoreApplication::translate("EffectCatalog", "Dreamy & Stylish")},
        {QStringLiteral("impact"), QCoreApplication::translate("EffectCatalog", "Impact")},
    };
    QMutexLocker lock(&g_catalogMutex);
    ensureCatalogLocked();
    cats.append(g_extraCategories);
    return cats;
}

QString effectCategoryLabel(const QString &categoryId)
{
    for (const auto &category : effectCategories()) {
        if (category.first == categoryId)
            return category.second;
    }
    return {};
}

QMap<QString, QVariant> resolvedEffectParameters(const drift::Effect &effect, const EffectPresetEntry &def)
{
    QMap<QString, QVariant> params = def.fixedParams;
    for (const drift::EffectParamSpec &spec : def.meta.parameters)
        params.insert(spec.key, spec.defaultVariant());
    for (auto it = effect.parameters.constBegin(); it != effect.parameters.end(); ++it)
        params.insert(it.key(), it.value());

    // A colour key can carry a stale double: isKnownKeyframeProp accepts any well-formed fx.N.key
    // without consulting the catalog, so a hand-edited project or a package that changed a
    // parameter from float to colour would otherwise reach the shader as a number and bind black.
    for (const drift::EffectParamSpec &spec : def.meta.parameters) {
        if (spec.isColor() && params.value(spec.key).typeId() != QMetaType::QString)
            params.insert(spec.key, spec.defaultColorHex);
    }

    // Derived placeholders used by graph templates.
    if (params.contains(QStringLiteral("offset"))) {
        const double offset = params.value(QStringLiteral("offset")).toDouble();
        params.insert(QStringLiteral("offset_neg"), -offset);
    }
    if (params.contains(QStringLiteral("chroma"))) {
        const double chroma = params.value(QStringLiteral("chroma")).toDouble();
        params.insert(QStringLiteral("chroma_neg"), -chroma);
    }

    return params;
}

QString buildFilterGraphForEffect(const drift::Effect &effect, const EffectPresetEntry *def)
{
    const EffectPresetEntry *entry = def;
    if (!entry && !effect.catalogId.isEmpty())
        entry = effectDefForId(effect.catalogId);

    if (entry) {
        if (entry->meta.compositorOnly || entry->isGpu)
            return {};

        const QMap<QString, QVariant> params = resolvedEffectParameters(effect, *entry);
        if (!entry->graphTemplate.isEmpty())
            return substituteTemplate(entry->graphTemplate, params);

        const QString filterName = entry->filterName.isEmpty() ? effect.name : entry->filterName;
        return singleFilterGraph(filterName, params);
    }

    return effect.filterGraphString();
}
