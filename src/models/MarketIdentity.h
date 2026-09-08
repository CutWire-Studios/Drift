#pragma once

#include <QByteArray>
#include <QString>

class QUrl;

// HMAC identity and request signing for market.cutwire.org. Pure functions so tests can
// feed known vectors without touching the network or a real machine fingerprint.
//
// Canonical string (UTF-8, "\n" separators, no trailing newline):
//   {timestamp}\n{nonce}\n{client_id}\n{METHOD}\n{pathAndQuery}\n{bodySha256Hex}
// See docs/marketplace/README.md.

namespace drift::market {

// The HMAC key as bytes: the configured string's own bytes, never decoded. A 64-character hex
// key is 64 bytes here, not the 32 it encodes. The service applies the same rule; when the two
// disagreed every signature mismatched, and the API can only report that as invalid_client.
QByteArray hmacKeyBytes();

QString sha256Hex(const QByteArray &data);
QString hmacSha256Hex(const QByteArray &key, const QByteArray &message);

QString canonicalString(const QString &timestamp, const QString &nonce, const QString &clientId,
                        const QByteArray &method, const QString &pathAndQuery,
                        const QByteArray &body);

QString signRequest(const QByteArray &key, const QString &timestamp, const QString &nonce,
                    const QString &clientId, const QByteArray &method, const QString &pathAndQuery,
                    const QByteArray &body);

// pathAndQuery is url.path() plus "?" + encoded query when present. No scheme or host.
QString pathAndQuery(const QUrl &url);

QString clientIdFromFingerprint(const QByteArray &key, const QString &platform,
                                const QString &fingerprint);

QString platformId();
QString machineFingerprint();

// HMAC(key, "cutwire-market-id-v1|{platform}|{fingerprint}") using the compile-time key.
// Empty when the service is not configured.
QString clientId();

QString makeNonce();
QString appHeader();

bool isAuthCallbackUrl(const QUrl &url);

} // namespace drift::market
