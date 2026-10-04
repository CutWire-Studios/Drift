#pragma once

#include "AddonPackage.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

// What is installed, and where. Addons unpack under <AppDataLocation>/addons/<id>/ and are
// recorded in addons/installed.json; the catalogs then pick them up through
// GpuPackageParse::defaultSearchPaths(), which appends addonRootsForKind() to its usual roots.
//
// This lives in driftengine deliberately: reading the registry must work in tools/ and tests/
// with no network stack. Downloading is the app layer's job (src/models/AddonManager).

namespace drift::addon {

struct InstalledProvide
{
    QString kind;
    QString root; // absolute, already joined onto the install directory
};

struct InstalledAddon
{
    QString id;
    QString version;
    QString name;
    QDateTime installedAt;
    quint64 sizeBytes{};
    QList<InstalledProvide> provides;
    QString dir; // absolute install directory
    // Installed from a file whose signature is not the Drift team's.
    bool unofficial = false;
    // Found in customAddonsDir() rather than recorded in installed.json. Always unofficial.
    bool custom = false;
};

QString addonsDir();                              // <AppDataLocation>/addons
QString addonInstallDir(const QString &id);       // <addonsDir>/<id>
QString addonDownloadCacheDir();                  // <addonsDir>/cache
// <addonsDir>/custom: one folder per addon, each with a manifest.json (see parseFolderManifest).
// Scanned on every registry rebuild, so dropping a folder in and rebuilding is the whole install.
QString customAddonsDir();

const QList<InstalledAddon> &installedAddons();
const InstalledAddon *installedAddon(const QString &id);

// Absolute content roots contributed by installed addons for a kind, in install order.
QStringList addonRootsForKind(const QString &kind);

// The installed addon whose install directory contains `path`, or nullptr. Traces a catalog entry
// (an effect's packageDir, a font's packageDir, a sticker's file) back to what provided it.
const InstalledAddon *addonForPath(const QString &path);

void reloadAddonRegistry();

// Record or drop an entry and rewrite installed.json atomically. Both refresh the in-memory
// registry on success; callers still need to reload the affected catalog.
bool recordInstalledAddon(const PackageInfo &info, QString *error, bool unofficial = false);
bool forgetInstalledAddon(const QString &id, QString *error);

} // namespace drift::addon
