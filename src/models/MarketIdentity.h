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

// The HMAC key as bytes. The service hex-decodes a configured key that parses as hex and is at
// least 16 bytes, and uses the raw text otherwise; both sides must agree or every signature
// mismatches. Exposed as a pure function so tests can cover both branches — hmacKeyBytes()
// applies it to the compile-time key.
QByteArray keyBytesFromConfigured(const QString &configured);

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
