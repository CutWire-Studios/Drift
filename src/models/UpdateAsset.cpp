#include "UpdateAsset.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QUrl>

namespace drift {

namespace {

bool isGitHubDownload(const QUrl &url)
{
    if (url.scheme() != QLatin1String("https") || url.host() != QLatin1String("github.com"))
        return false;
    if (!url.userName().isEmpty() || !url.password().isEmpty())
        return false;
    return url.path().contains(QLatin1String("/releases/download/"));
}

QString sha256Digest(const QJsonObject &asset)
{
    const QString digest = asset.value(QStringLiteral("digest")).toString();
    const QString prefix = QStringLiteral("sha256:");
    if (!digest.startsWith(prefix))
        return {};
    const QString hex = digest.mid(prefix.size()).trimmed().toLower();
    for (const QChar c : hex) {
        if (!c.isDigit() && (c < QLatin1Char('a') || c > QLatin1Char('f')))
            return {};
    }
    return hex.size() == 64 ? hex : QString();
}

} // namespace

ReleaseAsset selectReleaseAsset(const QJsonArray &assets, const QString &platform, const QString &arch)
{
    const bool windows = platform == QLatin1String("windows");
    const bool macos = platform == QLatin1String("macos");
    QString suffix;
    if (windows && arch == QLatin1String("x86_64"))
        suffix = QStringLiteral("-x64.exe");
    else if (macos && (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")))
        suffix = QStringLiteral("-arm64.dmg");
    else if (macos && arch == QLatin1String("x86_64"))
        suffix = QStringLiteral("-x86_64.dmg");
    else
        return {};

    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QStringLiteral("name")).toString();
        if (!name.endsWith(suffix))
            continue;
        // The portable zip is not an installer, and a dmg is never named Setup.
        if (windows && !name.startsWith(QLatin1String("Drift-Setup-")))
            continue;
        if (macos && !name.startsWith(QLatin1String("Drift-")))
            continue;

        const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
        if (!isGitHubDownload(url))
            continue;

        ReleaseAsset out;
        out.name = name;
        out.url = url.toString();
        out.sha256 = sha256Digest(asset);
        out.size = asset.value(QStringLiteral("size")).toVariant().toLongLong();
        return out;
    }
    return {};
}

QString parseVersionText(const QByteArray &raw)
{
    QString text = QString::fromUtf8(raw).trimmed();
    if (text.size() >= 2 && text.front() == QLatin1Char('"') && text.back() == QLatin1Char('"'))
        text = text.mid(1, text.size() - 2).trimmed();
    if (!text.isEmpty() && text.front().toLower() == QLatin1Char('v') && text.size() > 1
        && text.at(1).isDigit())
        text = text.mid(1);
    static const QRegularExpression version(QStringLiteral("^\\d+(?:\\.\\d+)*$"));
    return version.match(text).hasMatch() ? text : QString();
}

namespace {

QStringList feedParts(const QString &feedUrl)
{
    const QStringList parts = QUrl(feedUrl).path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // repos / owner / name / releases / latest
    if (parts.size() < 5 || parts.at(0) != QLatin1String("repos")
        || parts.at(3) != QLatin1String("releases"))
        return {};
    return parts;
}

} // namespace

QString releaseTagApiUrl(const QString &feedUrl, const QString &version)
{
    QUrl url(feedUrl);
    QString path = url.path();
    const QString latest = QStringLiteral("/releases/latest");
    if (version.isEmpty() || !path.endsWith(latest))
        return {};
    path.chop(latest.size());
    path += QStringLiteral("/releases/tags/v") + version;
    url.setPath(path);
    return url.toString();
}

QString releasePageUrl(const QString &feedUrl, const QString &version)
{
    const QStringList parts = feedParts(feedUrl);
    if (parts.isEmpty() || version.isEmpty())
        return {};
    return QStringLiteral("https://github.com/%1/%2/releases/tag/v%3")
            .arg(parts.at(1), parts.at(2), version);
}

QString releaseDownloadUrl(const QString &feedUrl, const QString &version, const QString &fileName)
{
    const QStringList parts = feedParts(feedUrl);
    if (parts.isEmpty() || version.isEmpty() || fileName.isEmpty())
        return {};
    return QStringLiteral("https://github.com/%1/%2/releases/download/v%3/%4")
            .arg(parts.at(1), parts.at(2), version, fileName);
}

QString installerFileName(const QString &platform, const QString &arch, const QString &version)
{
    if (version.isEmpty())
        return {};
    if (platform == QLatin1String("windows") && arch == QLatin1String("x86_64"))
        return QStringLiteral("Drift-Setup-%1-x64.exe").arg(version);
    if (platform == QLatin1String("macos")
        && (arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")))
        return QStringLiteral("Drift-%1-arm64.dmg").arg(version);
    if (platform == QLatin1String("macos") && arch == QLatin1String("x86_64"))
        return QStringLiteral("Drift-%1-x86_64.dmg").arg(version);
    return {};
}

} // namespace drift
