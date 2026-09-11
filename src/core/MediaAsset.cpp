#include "MediaAsset.h"

#include <QFileInfo>

namespace drift {

const QStringList &imageExtensions()
{
    static const QStringList extensions = {
        QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("gif"),  QStringLiteral("webp"), QStringLiteral("bmp"),
        QStringLiteral("tiff"), QStringLiteral("tif"),  QStringLiteral("svg"),
        // Decoded by FFmpeg, not Qt — see engine/StillImage.h. These are what an Android or
        // iPhone camera actually writes, so leaving them out meant the phone's own photos
        // could not be imported.
        QStringLiteral("heic"), QStringLiteral("heif"), QStringLiteral("avif"),
    };
    return extensions;
}

bool isImageSuffix(const QString &path)
{
    return imageExtensions().contains(QFileInfo(path).suffix().toLower());
}

QString mediaKindToString(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::Audio:
        return QStringLiteral("audio");
    case MediaKind::Image:
        return QStringLiteral("image");
    case MediaKind::Other:
        break;
    }
    return QStringLiteral("other");
}

MediaKind mediaKindFromString(const QString &kind)
{
    if (kind == QStringLiteral("video"))
        return MediaKind::Video;
    if (kind == QStringLiteral("audio"))
        return MediaKind::Audio;
    if (kind == QStringLiteral("image"))
        return MediaKind::Image;
    return MediaKind::Other;
}

} // namespace drift
