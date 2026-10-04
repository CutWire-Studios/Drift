#include "AddonRegistry.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStandardPaths>

namespace drift::addon {
namespace {

constexpr int kRegistrySchema = 1;

QMutex g_mutex;
QList<InstalledAddon> g_addons;
bool g_initialized = false;

QString registryFilePath()
{
    return QDir(addonsDir()).filePath(QStringLiteral("installed.json"));
}

InstalledAddon parseEntry(const QJsonObject &object)
{
    InstalledAddon addon;
    addon.id = object.value(QStringLiteral("id")).toString();
    addon.version = object.value(QStringLiteral("version")).toString();
    addon.name = object.value(QStringLiteral("name")).toString();
    addon.installedAt =
        QDateTime::fromString(object.value(QStringLiteral("installedAt")).toString(), Qt::ISODate);
    addon.sizeBytes = quint64(object.value(QStringLiteral("sizeBytes")).toDouble());
    addon.unofficial = object.value(QStringLiteral("unofficial")).toBool();
    addon.dir = addonInstallDir(addon.id);

    const QDir installDir(addon.dir);
    const QJsonArray provides = object.value(QStringLiteral("provides")).toArray();
    for (const QJsonValue &value : provides) {
        const QJsonObject entry = value.toObject();
        InstalledProvide provide;
        provide.kind = entry.value(QStringLiteral("kind")).toString();
        provide.root = installDir.filePath(entry.value(QStringLiteral("root")).toString());
        if (!provide.kind.isEmpty())
            addon.provides.append(provide);
    }
    return addon;
}

QJsonObject serializeEntry(const InstalledAddon &addon)
{
    const QDir installDir(addonInstallDir(addon.id));
    QJsonArray provides;
    for (const InstalledProvide &provide : addon.provides) {
        provides.append(QJsonObject{
            {QStringLiteral("kind"), provide.kind},
            // Stored relative so a moved or renamed AppData directory still resolves.
            {QStringLiteral("root"), installDir.relativeFilePath(provide.root)},
        });
    }
    QJsonObject entry{
        {QStringLiteral("id"), addon.id},
        {QStringLiteral("version"), addon.version},
        {QStringLiteral("name"), addon.name},
        {QStringLiteral("installedAt"), addon.installedAt.toString(Qt::ISODate)},
        {QStringLiteral("sizeBytes"), double(addon.sizeBytes)},
        {QStringLiteral("provides"), provides},
    };
    if (addon.unofficial)
        entry.insert(QStringLiteral("unofficial"), true);
    return entry;
}

void scanCustomLocked()
{
    const QDir root(customAddonsDir());
    const QStringList folders = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : folders) {
        // A zip install in progress, or one a crash left behind.
        if (folder.endsWith(QLatin1String(".partial")))
            continue;
        const QDir dir(root.filePath(folder));
        QFile file(dir.filePath(QStringLiteral("manifest.json")));
        if (!file.open(QIODevice::ReadOnly))
            continue;

        QString error;
        const auto info = parseFolderManifest(file.readAll(), &error);
        if (!info || !checkCompatible(*info, &error)) {
            qWarning() << "AddonRegistry: skipping custom addon" << dir.path() << "—" << error;
            continue;
        }
        const bool taken = std::any_of(g_addons.cbegin(), g_addons.cend(),
                                       [&](const InstalledAddon &a) { return a.id == info->id; });
        if (taken) {
            qWarning() << "AddonRegistry: skipping custom addon" << dir.path() << "— id" << info->id
                       << "is already installed";
            continue;
        }

        InstalledAddon addon;
        addon.id = info->id;
        addon.version = info->version;
        addon.name = info->name.isEmpty() ? info->id : info->name;
        addon.installedAt = QFileInfo(file).lastModified();
        addon.sizeBytes = info->installedSize;
        addon.dir = dir.absolutePath();
        addon.unofficial = true;
        addon.custom = true;
        for (const PackageProvide &provide : info->provides)
            addon.provides.append({provide.kind, dir.filePath(provide.root)});
        g_addons.append(addon);
    }
}

void readRegistryLocked()
{
    QFile file(registryFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return; // no addons installed yet — a normal state

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("schema")).toInt() != kRegistrySchema)
        return;

    const QJsonArray addons = root.value(QStringLiteral("addons")).toArray();
    for (const QJsonValue &value : addons) {
        const InstalledAddon addon = parseEntry(value.toObject());
        // An entry whose directory was deleted behind our back is stale, not fatal.
        if (!addon.id.isEmpty() && QDir(addonInstallDir(addon.id)).exists())
            g_addons.append(addon);
    }
}

void rebuildLocked()
{
    g_addons.clear();
    g_initialized = true;
    readRegistryLocked();
    scanCustomLocked();
}

void ensureLoadedLocked()
{
    if (!g_initialized)
        rebuildLocked();
}

bool writeRegistryLocked(QString *error)
{
    if (!QDir().mkpath(addonsDir())) {
        if (error)
            *error = QStringLiteral("cannot create %1").arg(addonsDir());
        return false;
    }

    QJsonArray addons;
    for (const InstalledAddon &addon : std::as_const(g_addons)) {
        if (!addon.custom)
            addons.append(serializeEntry(addon));
    }

    const QJsonDocument doc(QJsonObject{
        {QStringLiteral("schema"), kRegistrySchema},
        {QStringLiteral("addons"), addons},
    });

    QSaveFile file(registryFilePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(doc.toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        if (error)
            *error = QStringLiteral("cannot write installed.json: %1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace

QString addonsDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("addons"));
}

QString addonInstallDir(const QString &id)
{
    return QDir(addonsDir()).filePath(id);
}

QString addonDownloadCacheDir()
{
    return QDir(addonsDir()).filePath(QStringLiteral("cache"));
}

QString customAddonsDir()
{
    return QDir(addonsDir()).filePath(QStringLiteral("custom"));
}

const QList<InstalledAddon> &installedAddons()
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    return g_addons;
}

const InstalledAddon *installedAddon(const QString &id)
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    for (const InstalledAddon &addon : std::as_const(g_addons)) {
        if (addon.id == id)
            return &addon;
    }
    return nullptr;
}

QStringList addonRootsForKind(const QString &kind)
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();

    QStringList roots;
    for (const InstalledAddon &addon : std::as_const(g_addons)) {
        for (const InstalledProvide &provide : addon.provides) {
            if (provide.kind == kind && QDir(provide.root).exists())
                roots.append(provide.root);
        }
    }
    return roots;
}

const InstalledAddon *addonForPath(const QString &path)
{
    if (path.isEmpty())
        return nullptr;

    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());

    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();
    for (const InstalledAddon &addon : std::as_const(g_addons)) {
        const QString root = QDir::cleanPath(addon.dir);
        if (clean == root || clean.startsWith(root + QLatin1Char('/')))
            return &addon;
    }
    return nullptr;
}

void reloadAddonRegistry()
{
    QMutexLocker lock(&g_mutex);
    rebuildLocked();
}

bool recordInstalledAddon(const PackageInfo &info, QString *error, bool unofficial)
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();

    InstalledAddon addon;
    addon.id = info.id;
    addon.version = info.version;
    addon.name = info.name;
    addon.installedAt = QDateTime::currentDateTimeUtc();
    addon.sizeBytes = info.installedSize;
    addon.dir = addonInstallDir(info.id);
    addon.unofficial = unofficial;
    const QDir installDir(addon.dir);
    for (const PackageProvide &provide : info.provides)
        addon.provides.append({provide.kind, installDir.filePath(provide.root)});

    // A custom folder of the same id would otherwise shadow nothing yet still list twice.
    g_addons.removeIf([&](const InstalledAddon &a) { return a.custom && a.id == info.id; });
    const auto existing = std::find_if(g_addons.begin(), g_addons.end(),
                                       [&](const InstalledAddon &a) { return a.id == info.id; });
    if (existing != g_addons.end())
        *existing = addon;
    else
        g_addons.append(addon);

    return writeRegistryLocked(error);
}

bool forgetInstalledAddon(const QString &id, QString *error)
{
    QMutexLocker lock(&g_mutex);
    ensureLoadedLocked();

    g_addons.removeIf([&](const InstalledAddon &addon) { return addon.id == id; });
    return writeRegistryLocked(error);
}

} // namespace drift::addon
