#include "app/online/marketplace/MarketServices.h"

#include "app/bin/AssetLibrary.h"
#include "app/editor/EditorState.h"
#include "app/online/addons/AddonManager.h"
#include "app/online/downloads/DownloadBridges.h"
#include "app/online/downloads/DownloadCenter.h"
#include "engine/face/props/FacePropCatalog.h"
#include "engine/packages/effects/EffectTemplateCatalog.h"
#include "ui/theme/ThemeManager.h"

#include <QUrl>

#include <algorithm>

MarketServices::MarketServices(AssetLibrary &library, EditorState &state, AddonManager &addons)
    : router(&stock, &driftAssets, &sfx, &community, DownloadCenter::instance())
{
    stock.setAssetLibrary(&library);
    const auto importIntoBin = [&library](const QStringList &paths) { return library.importLocalPaths(paths); };
    driftAssets.setImporter(importIntoBin);
    sfx.setImporter(importIntoBin);
    driftAssets.setPropImporter([&state](const QString &dir) {
        return state.importFaceProps(QUrl::fromLocalFile(dir)).value(QStringLiteral("installed")).toStringList();
    });
    driftAssets.setPropLookup([](const QString &id) {
        const QList<FacePropEntry> props = facePropsSnapshot();
        return std::any_of(props.cbegin(), props.cend(), [&](const FacePropEntry &p) { return p.id == id; });
    });
    for (const char *kind : {"effect", "transition", "audio-effect"}) {
        community.setHandler(QLatin1String(kind), [&addons](const QString &path, QString *error) {
            *error = addons.installUserPackageFile(path);
            return QString();
        });
    }
    community.setHandler(QStringLiteral("theme"), [](const QString &path, QString *error) {
        const QString id = ThemeManager::shared()->importTheme(QUrl::fromLocalFile(path), true);
        if (id.isEmpty())
            *error = ThemeManager::shared()->lastError();
        return id;
    });
    community.setHandler(QStringLiteral("look"), [&state](const QString &path, QString *error) {
        return state.color()->importLookFile(path, error);
    });
    community.setHandler(QStringLiteral("effect-template"), [](const QString &path, QString *error) {
        QString id;
        *error = installUserTemplate(path, &id);
        return id;
    });
    bridgeAddons(DownloadCenter::instance(), &addons);
}

McpMarketContext MarketServices::context()
{
    return {&manifest, &router, DownloadCenter::instance(), &stock};
}
