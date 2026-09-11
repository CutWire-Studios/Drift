#include "MarketIdentity.h"

#include "MarketEndpoint.h"

#include <QCryptographicHash>
#include <QFile>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QUrl>

#ifdef Q_OS_WIN
#include <QSettings>
#endif

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#ifdef Q_OS_MACOS
#include <sys/sysctl.h>
#endif

#ifdef Q_OS_ANDROID
#include <QJniEnvironment>
#include <QJniObject>
#include <QtCore/qcoreapplication_platform.h>
#endif

#ifndef DRIFT_VERSION
#define DRIFT_VERSION "0"
#endif

namespace drift::market {
namespace {

QByteArray utf8(const QString &s)
{
    return s.toUtf8();
}

QString hexLower(const QByteArray &bytes)
{
    return QString::fromLatin1(bytes.toHex());
}

} // namespace

QByteArray hmacKeyBytes()
{
    return kClientKey.toUtf8();
}

QString sha256Hex(const QByteArray &data)
{
    return hexLower(QCryptographicHash::hash(data, QCryptographicHash::Sha256));
}

QString hmacSha256Hex(const QByteArray &key, const QByteArray &message)
{
    QMessageAuthenticationCode hmac(QCryptographicHash::Sha256, key);
    hmac.addData(message);
    return hexLower(hmac.result());
}

QString canonicalString(const QString &timestamp, const QString &nonce, const QString &clientId,
                        const QByteArray &method, const QString &pathAndQuery,
                        const QByteArray &body)
{
    return timestamp + QLatin1Char('\n') + nonce + QLatin1Char('\n') + clientId + QLatin1Char('\n')
        + QString::fromLatin1(method) + QLatin1Char('\n') + pathAndQuery + QLatin1Char('\n')
        + sha256Hex(body);
}

QString signRequest(const QByteArray &key, const QString &timestamp, const QString &nonce,
                    const QString &clientId, const QByteArray &method, const QString &pathAndQuery,
                    const QByteArray &body)
{
    return hmacSha256Hex(key, utf8(canonicalString(timestamp, nonce, clientId, method,
                                                   pathAndQuery, body)));
}

QString pathAndQuery(const QUrl &url)
{
    QString path = url.path(QUrl::FullyEncoded);
    if (path.isEmpty())
        path = QStringLiteral("/");
    const QString query = url.query(QUrl::FullyEncoded);
    if (query.isEmpty())
        return path;
    return path + QLatin1Char('?') + query;
}

QString clientIdFromFingerprint(const QByteArray &key, const QString &platform,
                                const QString &fingerprint)
{
    const QString material = QStringLiteral("cutwire-market-id-v1|") + platform + QLatin1Char('|')
        + fingerprint;
    return hmacSha256Hex(key, utf8(material));
}

QString platformId()
{
#if defined(Q_OS_ANDROID)
    return QStringLiteral("android");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString machineFingerprint()
{
#if defined(Q_OS_ANDROID)
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return QStringLiteral("android-unknown");
    QJniObject resolver =
        context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    QJniObject androidId = QJniObject::callStaticObjectMethod(
        "android/provider/Settings$Secure", "getString",
        "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;",
        resolver.object(),
        QJniObject::fromString(QStringLiteral("android_id")).object<jstring>());
    QJniEnvironment().checkAndClearExceptions();
    const QString id = androidId.isValid() ? androidId.toString() : QString();
    return id.isEmpty() ? QStringLiteral("android-unknown") : id;
#elif defined(Q_OS_WIN)
    QSettings cryptography(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography"),
                           QSettings::NativeFormat);
    const QString guid = cryptography.value(QStringLiteral("MachineGuid")).toString().trimmed();
    const QString user = QString::fromLocal8Bit(qgetenv("USERNAME")).trimmed();
    return guid + QLatin1Char('|') + user;
#elif defined(Q_OS_MACOS)
    char buf[128] = {};
    size_t len = sizeof(buf);
    const int rc = sysctlbyname("kern.uuid", buf, &len, nullptr, 0);
    QString uuid;
    if (rc == 0 && len > 0) {
        int n = int(len);
        if (n > 0 && buf[n - 1] == '\0')
            --n;
        uuid = QString::fromLatin1(buf, n).trimmed();
    }
    const QString user = QString::fromLocal8Bit(qgetenv("USER")).trimmed();
    return uuid + QLatin1Char('|') + user;
#else
    QString machineId;
    QFile file(QStringLiteral("/etc/machine-id"));
    if (file.open(QIODevice::ReadOnly))
        machineId = QString::fromLatin1(file.readAll()).trimmed();
    if (machineId.isEmpty()) {
        QFile dbus(QStringLiteral("/var/lib/dbus/machine-id"));
        if (dbus.open(QIODevice::ReadOnly))
            machineId = QString::fromLatin1(dbus.readAll()).trimmed();
    }
    const QString uid = QString::number(getuid());
    return machineId + QLatin1Char('|') + uid;
#endif
}

QString clientId()
{
    const QByteArray key = hmacKeyBytes();
    if (key.isEmpty())
        return {};
    return clientIdFromFingerprint(key, platformId(), machineFingerprint());
}

QString makeNonce()
{
    QByteArray bytes(16, 0);
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < bytes.size(); i += 4) {
        const quint32 word = rng->generate();
        bytes[i] = char(word);
        bytes[i + 1] = char(word >> 8);
        bytes[i + 2] = char(word >> 16);
        bytes[i + 3] = char(word >> 24);
    }
    return hexLower(bytes);
}

QString appHeader()
{
    return QStringLiteral("Drift/%1 (%2)").arg(QLatin1String(DRIFT_VERSION), platformId());
}

bool isAuthCallbackUrl(const QUrl &url)
{
    if (!url.isValid())
        return false;
    const QString scheme = url.scheme().toLower();
    const QString host = url.host().toLower();
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')) && path.size() > 1)
        path.chop(1);

    if (scheme == QLatin1String("cutwire"))
        return host == QLatin1String("market") && path == QLatin1String("/auth/callback");

    if (scheme == QLatin1String("https") && host == QLatin1String("market.cutwire.org"))
        return path == QLatin1String("/app/auth/callback");

    return false;
}

} // namespace drift::market
