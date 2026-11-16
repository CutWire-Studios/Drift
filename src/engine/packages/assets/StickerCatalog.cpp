#include "engine/packages/assets/StickerCatalog.h"

#include "engine/addons/PackageSearchPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>

namespace {

constexpr char kLegacyQrcPrefix[] = ":/qt/qml/Drift/resources/stickers/";

QMutex g_mutex;
QList<StickerPack> g_packs;
bool g_initialized = false;

bool loadPackage(const QString &dir, StickerPack *out)
{
    QFile file(QDir(dir).filePath(QStringLiteral("pack.json")));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    StickerPack pack;
    pack.id = root.value(QStringLiteral("id")).toString();
    pack.name = root.value(QStringLiteral("name")).toString();
    pack.license = root.value(QStringLiteral("license")).toString();
    pack.order = root.value(QStringLiteral("order")).toInt();
    pack.custom = root.value(QStringLiteral("custom")).toBool();
    pack.packageDir = dir;
    if (pack.id.isEmpty())
        return false;

    for (const QJsonValue &value : root.value(QStringLiteral("categories")).toArray()) {
        const QJsonObject object = value.toObject();
        StickerCategory category;
        category.id = object.value(QStringLiteral("id")).toString();
        category.label = object.value(QStringLiteral("label")).toString();
        category.packId = pack.id;
        category.custom = pack.custom;
        if (!category.id.isEmpty())
            pack.categories.append(category);
    }

    for (const QJsonValue &value : root.value(QStringLiteral("stickers")).toArray()) {
        const QJsonObject object = value.toObject();
        const QString file = object.value(QStringLiteral("file")).toString();
        const QString id = object.value(QStringLiteral("id")).toString();
        if (file.isEmpty() || id.isEmpty())
            continue;

        const QString path = QDir(dir).filePath(file);
        if (!QFileInfo::exists(path))
            continue;

        StickerEntry entry;
        entry.id = pack.id + QLatin1Char('/') + id;
        entry.label = object.value(QStringLiteral("label")).toString();
        entry.category = object.value(QStringLiteral("category")).toString();
        entry.path = path;
        pack.stickers.append(entry);
    }

    if (pack.stickers.isEmpty())
        return false;

    *out = pack;
    return true;
}

void rebuildLocked(const QStringList &packageRoots)
{
    g_packs.clear();
    g_initialized = true;

    const QStringList roots = packageRoots.isEmpty()
        ? PackageSearchPaths::defaultSearchPaths(QStringLiteral("DRIFT_STICKERS_DIR"),
                                                 QStringLiteral("stickers"),
                                                 QStringLiteral("stickers"))
        : packageRoots;

    for (const QString &root : roots) {
        QDir dir(root);
        if (!dir.exists())
            continue;
        const QStringList packages = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : packages) {
            StickerPack pack;
            if (!loadPackage(dir.filePath(name), &pack))
                continue;
            // First root wins, so DRIFT_STICKERS_DIR shadows an installed addon.
            const bool duplicate = std::any_of(g_packs.cbegin(), g_packs.cend(),
                                               [&](const StickerPack &p) { return p.id == pack.id; });
            if (!duplicate)
                g_packs.append(pack);
        }
    }

    std::stable_sort(g_packs.begin(), g_packs.end(),
                     [](const StickerPack &a, const StickerPack &b) { return a.order < b.order; });
}

void ensureLoadedLocked()
{
    if (!g_initialized)
        rebuildLocked({});
}

} // namespace

const QList<StickerPack> &stickerPacks()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    return g_packs;
}

QList<StickerCategory> stickerCategories()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();

    QList<StickerCategory> merged;
    for (const StickerPack &pack : std::as_const(g_packs)) {
        for (const StickerCategory &category : pack.categories) {
            const bool seen = std::any_of(merged.cbegin(), merged.cend(),
                                          [&](const StickerCategory &c) { return c.id == category.id; });
            if (!seen)
                merged.append(category);
        }
    }
    return merged;
}

QList<StickerEntry> stickers()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();

    QList<StickerEntry> merged;
    for (const StickerPack &pack : std::as_const(g_packs))
        merged.append(pack.stickers);
    return merged;
}

void reloadStickerCatalog(const QStringList &packageRoots)
{
    QMutexLocker lock(&g_mutex);
    rebuildLocked(packageRoots);
}

QString resolveLegacyStickerPath(const QString &qrcPath)
{
    if (!qrcPath.startsWith(QLatin1String(kLegacyQrcPrefix)))
        return {};

    const QString fileName = qrcPath.mid(int(sizeof(kLegacyQrcPrefix)) - 1);
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();

    for (const StickerPack &pack : std::as_const(g_packs)) {
        const QString candidate = QDir(pack.packageDir).filePath(fileName);
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}
