#include <QtTest>

#include <QJsonArray>
#include <QJsonObject>

#include "models/UpdateAsset.h"

class UpdateAssetTest : public QObject
{
    Q_OBJECT

private slots:
    void versionText();
    void windowsInstallerIgnoresPortableZip();
    void macDiskImage();
    void rejectsNonGitHubUrl();
    void releaseUrlsFollowTheVersion();
    void installerFileName();
};

void UpdateAssetTest::versionText()
{
    QCOMPARE(drift::parseVersionText("0.7.5"), QStringLiteral("0.7.5"));
    QCOMPARE(drift::parseVersionText("\"0.7.5\""), QStringLiteral("0.7.5"));
    QCOMPARE(drift::parseVersionText("v0.7.5"), QStringLiteral("0.7.5"));
    QCOMPARE(drift::parseVersionText("  V1.2.3\n"), QStringLiteral("1.2.3"));
    QVERIFY(drift::parseVersionText("latest").isEmpty());
    QVERIFY(drift::parseVersionText("0.7.5-rc1").isEmpty());
}

void UpdateAssetTest::windowsInstallerIgnoresPortableZip()
{
    const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Portable-1.2.0-x64.zip")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Portable-1.2.0-x64.zip")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("digest"),
                         QStringLiteral("sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")},
                        {QStringLiteral("size"), 42}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-1.2.0-arm64.dmg")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-1.2.0-arm64.dmg")}}};

    const drift::ReleaseAsset asset = drift::selectReleaseAsset(assets, QStringLiteral("windows"),
                                                                QStringLiteral("x86_64"));
    QCOMPARE(asset.name, QStringLiteral("Drift-Setup-1.2.0-x64.exe"));
    QCOMPARE(asset.sha256,
             QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"));
    QCOMPARE(asset.size, qint64(42));
    QVERIFY(drift::selectReleaseAsset(assets, QStringLiteral("windows"), QStringLiteral("arm64")).url.isEmpty());
}

void UpdateAssetTest::macDiskImage()
{
    const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Setup-1.2.0-x64.exe")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-1.2.0-arm64.dmg")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-1.2.0-arm64.dmg")}}};

    const drift::ReleaseAsset asset = drift::selectReleaseAsset(assets, QStringLiteral("macos"),
                                                                QStringLiteral("arm64"));
    QCOMPARE(asset.name, QStringLiteral("Drift-1.2.0-arm64.dmg"));
}

void UpdateAssetTest::rejectsNonGitHubUrl()
{
    const QJsonArray assets{
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("http://github.com/CutWire-Studios/Drift/releases/download/v1.2.0/Drift-Setup-1.2.0-x64.exe")}},
            QJsonObject{{QStringLiteral("name"), QStringLiteral("Drift-Setup-1.2.0-x64.exe")},
                        {QStringLiteral("browser_download_url"),
                         QStringLiteral("https://example.com/Drift-Setup-1.2.0-x64.exe")}}};
    QVERIFY(drift::selectReleaseAsset(assets, QStringLiteral("windows"), QStringLiteral("x86_64")).url.isEmpty());
}

void UpdateAssetTest::releaseUrlsFollowTheVersion()
{
    const QString feed = QStringLiteral(
            "https://api.github.com/repos/CutWire-Studios/Drift/releases/latest");
    QCOMPARE(drift::releaseTagApiUrl(feed, QStringLiteral("0.7.5")),
             QStringLiteral("https://api.github.com/repos/CutWire-Studios/Drift/releases/tags/v0.7.5"));
    QCOMPARE(drift::releasePageUrl(feed, QStringLiteral("0.7.5")),
             QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/tag/v0.7.5"));
    QCOMPARE(drift::releaseDownloadUrl(feed, QStringLiteral("0.7.5"),
                                       QStringLiteral("Drift-Setup-0.7.5-x64.exe")),
             QStringLiteral("https://github.com/CutWire-Studios/Drift/releases/download/v0.7.5/Drift-Setup-0.7.5-x64.exe"));
}

void UpdateAssetTest::installerFileName()
{
    QCOMPARE(drift::installerFileName(QStringLiteral("windows"), QStringLiteral("x86_64"),
                                      QStringLiteral("0.7.5")),
             QStringLiteral("Drift-Setup-0.7.5-x64.exe"));
    QCOMPARE(drift::installerFileName(QStringLiteral("macos"), QStringLiteral("arm64"),
                                      QStringLiteral("0.7.5")),
             QStringLiteral("Drift-0.7.5-arm64.dmg"));
    QVERIFY(drift::installerFileName(QStringLiteral("linux"), QStringLiteral("x86_64"),
                                     QStringLiteral("0.7.5"))
                    .isEmpty());
}

QTEST_GUILESS_MAIN(UpdateAssetTest)
#include "tst_updateasset.moc"
