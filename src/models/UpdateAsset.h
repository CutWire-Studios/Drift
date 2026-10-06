#pragma once

#include <QJsonArray>
#include <QString>

namespace drift {

// The one file from a GitHub release this build can download and install itself.
// Empty url means there is nothing for this platform — the dialog falls back to the release page.
struct ReleaseAsset
{
    QString name;
    QString url;
    QString sha256;
    qint64 size = 0;

    bool isValid() const { return !url.isEmpty(); }
};

// platform is "windows" or "macos". arch is QSysInfo::currentCpuArchitecture()
// ("x86_64", "arm64"). Only the installer formats those two packages actually ship:
// Drift-Setup-*-x64.exe and Drift-*-arm64.dmg. The portable zip is a different product.
ReleaseAsset selectReleaseAsset(const QJsonArray &assets, const QString &platform, const QString &arch);

// A TXT value from drift-version.cutwire.org. Accepts "0.7.5" and "v0.7.5"; anything else
// is empty so a stray record cannot advertise a non-version.
QString parseVersionText(const QByteArray &raw);

// feedUrl is the configured GitHub releases/latest endpoint. These rebuild the tag, page and
// file URLs for the version the DNS record named, without asking /releases/latest.
QString releaseTagApiUrl(const QString &feedUrl, const QString &version);
QString releasePageUrl(const QString &feedUrl, const QString &version);
QString releaseDownloadUrl(const QString &feedUrl, const QString &version, const QString &fileName);

// File name the packaging workflow uploads for this platform, or empty when it ships nothing
// this build can install.
QString installerFileName(const QString &platform, const QString &arch, const QString &version);

} // namespace drift
