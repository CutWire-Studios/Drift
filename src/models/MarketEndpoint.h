#pragma once

#include <QString>

// Where the marketplace client fetches from. Both values are configured in CMakeLists.txt
// (DRIFT_MARKET_API_URL / DRIFT_MARKET_CLIENT_KEY) and injected as compile definitions — this
// header only reads them, so there is one place to change the service.
//
// The key is NOT a user secret and is not treated as one: it ships inside every copy of Drift
// and anyone who rebuilds from source can pull it out of the binary. Its job is to HMAC-sign
// requests and derive a stable client id so casual users cannot mint a new quota by deleting
// app data. Rotate it by deploying a new API secret and shipping a new build; old builds then
// lose marketplace access.
//
// A translation unit built outside the `drift` target gets no service rather than a stale
// hardcoded one.

#ifndef DRIFT_MARKET_API_URL
#define DRIFT_MARKET_API_URL ""
#endif
#ifndef DRIFT_MARKET_CLIENT_KEY
#define DRIFT_MARKET_CLIENT_KEY ""
#endif

namespace drift::market {

inline const QString kApiUrl = QStringLiteral(DRIFT_MARKET_API_URL);
inline const QString kClientKey = QStringLiteral(DRIFT_MARKET_CLIENT_KEY);

// False when the build was configured with an empty URL or key. The Market tab then shows a
// disabled empty state and issues no requests.
inline bool marketServiceConfigured()
{
    return !kApiUrl.isEmpty() && !kClientKey.isEmpty();
}

} // namespace drift::market
