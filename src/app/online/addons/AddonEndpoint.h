#pragma once

#include <QString>

// Where the Addon Manager fetches from. Both values are configured in CMakeLists.txt
// (DRIFT_ADDON_INDEX_URL / DRIFT_ADDON_CLIENT_TOKEN) and injected as compile definitions — this
// header only reads them, so there is one place to change the service.
//
// The token is NOT a secret and is not treated as one: it ships inside every copy of Drift and
// anyone can pull it out of the binary. Its job is to stop the bucket being crawled or hotlinked,
// which would cost us operations for someone else's benefit. Rotate it by deploying a new Worker
// secret and shipping a new build; old builds then fall back to whatever they have installed.

// A translation unit built outside the `drift` target gets no service rather than a stale
// hardcoded one.
#ifndef DRIFT_ADDON_INDEX_URL
#define DRIFT_ADDON_INDEX_URL ""
#endif
#ifndef DRIFT_ADDON_CATALOG_URL
#define DRIFT_ADDON_CATALOG_URL ""
#endif
#ifndef DRIFT_ADDON_CLIENT_TOKEN
#define DRIFT_ADDON_CLIENT_TOKEN ""
#endif

namespace drift::addon {

inline const QString kIndexUrl = QStringLiteral(DRIFT_ADDON_INDEX_URL);
// The store catalog lives next to the index (…/v1/index -> …/v1/catalog) unless configured.
inline QString catalogUrl()
{
    const QString configured = QStringLiteral(DRIFT_ADDON_CATALOG_URL);
    if (!configured.isEmpty() || !kIndexUrl.endsWith(QLatin1String("/v1/index")))
        return configured;
    return kIndexUrl.chopped(int(qstrlen("/index"))) + QStringLiteral("/catalog");
}

// Catalog image paths are relative to this.
inline QString mediaBaseUrl()
{
    const QString catalog = catalogUrl();
    if (!catalog.endsWith(QLatin1String("/catalog")))
        return {};
    return catalog.chopped(int(qstrlen("catalog"))) + QStringLiteral("media/");
}

inline const QString kClientToken = QStringLiteral(DRIFT_ADDON_CLIENT_TOKEN);

// False when the build was configured with an empty index URL. The manager then lists nothing and
// installs nothing; anything already installed, side-loaded, or pointed at by DRIFT_*_DIR still
// works, since those paths never involve the service.
inline bool addonServiceConfigured()
{
    return !kIndexUrl.isEmpty();
}

} // namespace drift::addon
