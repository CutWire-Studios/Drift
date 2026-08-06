#include "EffectCatalog.h"

#include "EffectPackageLoader.h"

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
    static const QHash<QString, QString> kCategoryLabels = {
        {QStringLiteral("color"), QStringLiteral("Color")},
        {QStringLiteral("glitch"), QStringLiteral("Glitch & Distortion")},
        {QStringLiteral("retro"), QStringLiteral("Retro / Analog")},
        {QStringLiteral("dreamy"), QStringLiteral("Dreamy & Stylish")},
        {QStringLiteral("impact"), QStringLiteral("Impact")},
        {QStringLiteral("blurs"), QStringLiteral("Blurs & Distortions")},
        {QStringLiteral("blurs_distortions"), QStringLiteral("Blurs & Distortions")},
        {QStringLiteral("funny"), QStringLiteral("Funny Face")},
        {QStringLiteral("beauty"), QStringLiteral("Beauty & Makeup")},
        {QStringLiteral("props"), QStringLiteral("Face Props")},
        {QStringLiteral("artistic"), QStringLiteral("Artistic")},
    };

    for (const EffectPresetEntry &pkg : g_mergedCatalog) {
        if (!knownCats.contains(pkg.meta.category)) {
            knownCats.insert(pkg.meta.category);
            const QString label = kCategoryLabels.value(
                pkg.meta.category,
                pkg.meta.category.isEmpty()
                    ? QStringLiteral("Other")
                    : pkg.meta.category.at(0).toUpper() + pkg.meta.category.mid(1));
            g_extraCategories.append({pkg.meta.category, label});
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
        {QStringLiteral("color"), QStringLiteral("Color")},
        {QStringLiteral("glitch"), QStringLiteral("Glitch & Distortion")},
        {QStringLiteral("retro"), QStringLiteral("Retro / Analog")},
        {QStringLiteral("dreamy"), QStringLiteral("Dreamy & Stylish")},
        {QStringLiteral("impact"), QStringLiteral("Impact")},
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
    // File params have the same hole — without this a fresh effect defaults to the double 0.0 and
    // the model silently never loads.
    for (const drift::EffectParamSpec &spec : def.meta.parameters) {
        if (spec.isColor() && params.value(spec.key).typeId() != QMetaType::QString)
            params.insert(spec.key, spec.defaultColorHex);
        if (spec.isFilePath() && params.value(spec.key).typeId() != QMetaType::QString)
            params.insert(spec.key, spec.defaultString);
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
        if (entry->meta.compositorOnly || entry->isGpu || entry->isModel3d)
            return {};

        const QMap<QString, QVariant> params = resolvedEffectParameters(effect, *entry);
        if (!entry->graphTemplate.isEmpty())
            return substituteTemplate(entry->graphTemplate, params);

        const QString filterName = entry->filterName.isEmpty() ? effect.name : entry->filterName;
        return singleFilterGraph(filterName, params);
    }

    return effect.filterGraphString();
}
