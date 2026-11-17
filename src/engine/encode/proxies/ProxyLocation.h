#pragma once

#include <QString>

#include <optional>

namespace drift {

// Where Drift saves the proxies and reversed renders it makes (Settings > Preview).
enum class ProxyLocation { Beside, Cache, Folder };

QString proxyLocationToString(ProxyLocation location); // "beside" | "cache" | "folder"
std::optional<ProxyLocation> proxyLocationFromString(const QString &text);

struct ProxyTarget
{
    enum class Where { Beside, Folder, Cache };
    Where where = Where::Cache;
    QString path;                   // empty when the cache directory itself is unavailable
    bool folderUnavailable = false; // Folder mode, the folder is missing or takes no write
    bool besideRefused = false;     // Beside mode, a local source whose folder refused the write
};

// The one rule for where a generated file goes, shared by proxy builds and reversed renders.
// besidePath / folderPath are the names the caller would use in each place (generatedProxyPath /
// folderProxyPath, or the reversed pair); folderPath may be empty when no folder is set. A write
// is tried by really creating a file (canWriteBeside). The chosen folder is never created: a
// missing one usually means an unmounted drive. Never falls back to beside the source from Folder
// mode, since the folder was chosen to keep the footage folders clean.
ProxyTarget chooseProxyTarget(ProxyLocation mode, const QString &folder, const QString &sourcePath,
                              const QString &besidePath, const QString &folderPath);

} // namespace drift
