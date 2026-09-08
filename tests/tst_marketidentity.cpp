#include "models/MarketIdentity.h"

#include <QUrl>
#include <QtTest>

using namespace drift::market;

class TestMarketIdentity : public QObject
{
    Q_OBJECT

private slots:
    void sha256OfEmptyBody();
    void hmacKnownVector();
    void keyBytesFollowServerEncoding();
    void canonicalStringFormat();
    void signatureCoversClientId();
    void pathAndQueryEncoding();
    void clientIdIsStableForSameFingerprint();
    void authCallbackUrls();
};

void TestMarketIdentity::sha256OfEmptyBody()
{
    QCOMPARE(sha256Hex({}),
             QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
}

void TestMarketIdentity::hmacKnownVector()
{
    // RFC 4231 HMAC-SHA256 test case 1 (key = 20 bytes of 0x0b, data = "Hi There")
    const QByteArray key(20, '\x0b');
    const QByteArray data("Hi There");
    QCOMPARE(hmacSha256Hex(key, data),
             QStringLiteral("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
}

// The service hex-decodes a configured key that parses as hex and is at least 16 bytes, and
// keeps the raw text otherwise. Signing with the other interpretation is a signature mismatch
// on every request, which the API reports only as a flat "invalid client".
void TestMarketIdentity::keyBytesFollowServerEncoding()
{
    const QString hexKey = QStringLiteral("5409a0be00000000000000000000000000000000000000000000000000007438");
    QCOMPARE(keyBytesFromConfigured(hexKey), QByteArray::fromHex(hexKey.toLatin1()));
    QCOMPARE(keyBytesFromConfigured(hexKey).size(), 32);

    // Uppercase is still hex to the service's decoder.
    QCOMPARE(keyBytesFromConfigured(hexKey.toUpper()), QByteArray::fromHex(hexKey.toLatin1()));

    // Not hex, too short to decode, or an odd length: the raw text is the key.
    const QString text = QStringLiteral("a-passphrase-that-is-not-hex-at-all");
    QCOMPARE(keyBytesFromConfigured(text), text.toUtf8());
    QCOMPARE(keyBytesFromConfigured(QStringLiteral("abcdef")), QByteArray("abcdef"));
    const QString odd = hexKey.left(hexKey.size() - 1);
    QCOMPARE(keyBytesFromConfigured(odd), odd.toUtf8());
}

void TestMarketIdentity::canonicalStringFormat()
{
    const QString got = canonicalString(QStringLiteral("1700000000"),
                                        QStringLiteral("aa"),
                                        QStringLiteral("client"),
                                        "GET",
                                        QStringLiteral("/api/v1/catalog"),
                                        {});
    const QString expected =
        QStringLiteral("1700000000\naa\nclient\nGET\n/api/v1/catalog\n"
                       "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    QCOMPARE(got, expected);
}

void TestMarketIdentity::signatureCoversClientId()
{
    const QByteArray key("test-key");
    const QString a = signRequest(key, QStringLiteral("1"), QStringLiteral("n"),
                                  QStringLiteral("client-a"), "GET",
                                  QStringLiteral("/api/v1/catalog"), {});
    const QString b = signRequest(key, QStringLiteral("1"), QStringLiteral("n"),
                                  QStringLiteral("client-b"), "GET",
                                  QStringLiteral("/api/v1/catalog"), {});
    QVERIFY(a != b);
}

void TestMarketIdentity::pathAndQueryEncoding()
{
    QCOMPARE(pathAndQuery(QUrl(QStringLiteral("https://market.cutwire.org/api/v1/catalog"))),
             QStringLiteral("/api/v1/catalog"));
    QCOMPARE(pathAndQuery(QUrl(QStringLiteral("https://market.cutwire.org/api/v1/search?type=video&q=ocean"))),
             QStringLiteral("/api/v1/search?type=video&q=ocean"));
}

void TestMarketIdentity::clientIdIsStableForSameFingerprint()
{
    const QByteArray key("official-build-key");
    const QString a = clientIdFromFingerprint(key, QStringLiteral("linux"), QStringLiteral("abc|1000"));
    const QString b = clientIdFromFingerprint(key, QStringLiteral("linux"), QStringLiteral("abc|1000"));
    const QString c = clientIdFromFingerprint(key, QStringLiteral("linux"), QStringLiteral("abc|1001"));
    QCOMPARE(a, b);
    QVERIFY(a != c);
    QCOMPARE(a.size(), 64);
}

void TestMarketIdentity::authCallbackUrls()
{
    QVERIFY(isAuthCallbackUrl(QUrl(QStringLiteral("cutwire://market/auth/callback?code=abc"))));
    QVERIFY(isAuthCallbackUrl(QUrl(QStringLiteral("https://market.cutwire.org/app/auth/callback?code=abc"))));
    QVERIFY(isAuthCallbackUrl(QUrl(QStringLiteral("cutwire://market/auth/callback/?code=abc"))));
    QVERIFY(!isAuthCallbackUrl(QUrl(QStringLiteral("cutwire://market/other"))));
    QVERIFY(!isAuthCallbackUrl(QUrl(QStringLiteral("https://evil.example/app/auth/callback?code=abc"))));
    QVERIFY(!isAuthCallbackUrl(QUrl(QStringLiteral("file:///tmp/project.drift"))));
}

QTEST_APPLESS_MAIN(TestMarketIdentity)
#include "tst_marketidentity.moc"
