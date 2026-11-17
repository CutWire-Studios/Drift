#include "engine/encode/proxies/ProxyLocation.h"

#include "engine/encode/proxies/ProxyNaming.h"
#include "engine/encode/proxies/ReverseProxyCache.h"

#include <QDir>

namespace drift {

QString proxyLocationToString(ProxyLocation location)
{
    switch (location) {
    case ProxyLocation::Cache:
        return QStringLiteral("cache");
    case ProxyLocation::Folder:
        return QStringLiteral("folder");
    case ProxyLocation::Beside:
        break;
    }
    return QStringLiteral("beside");
}

std::optional<ProxyLocation> proxyLocationFromString(const QString &text)
{
    if (text == QLatin1String("beside"))
        return ProxyLocation::Beside;
    if (text == QLatin1String("cache"))
        return ProxyLocation::Cache;
    if (text == QLatin1String("folder"))
        return ProxyLocation::Folder;
    return std::nullopt;
}

ProxyTarget chooseProxyTarget(ProxyLocation mode, const QString &folder, const QString &sourcePath,
                              const QString &besidePath, const QString &folderPath)
{
    ProxyTarget target;
    if (isLocalSourcePath(sourcePath)) {
        if (mode == ProxyLocation::Beside) {
            if (canWriteBeside(besidePath)) {
                target.where = ProxyTarget::Where::Beside;
                target.path = besidePath;
                return target;
            }
            target.besideRefused = true;
        } else if (mode == ProxyLocation::Folder) {
            if (!folder.isEmpty() && !folderPath.isEmpty() && QDir(folder).exists()
                && canWriteBeside(folderPath)) {
                target.where = ProxyTarget::Where::Folder;
                target.path = folderPath;
                return target;
            }
            target.folderUnavailable = true;
        }
    }
    target.path = newReversePath();
    return target;
}

} // namespace drift
