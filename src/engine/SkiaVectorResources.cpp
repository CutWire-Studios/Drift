#include "SkiaVectorResources.h"

#include "SkiaFonts.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"

namespace drift::skia {

namespace {

class StillImageAsset final : public skresources::ImageAsset
{
public:
    explicit StillImageAsset(sk_sp<SkImage> image) : m_image(std::move(image)) {}
    bool isMultiFrame() override { return false; }
    sk_sp<SkImage> getFrame(float) override { return m_image; }

private:
    sk_sp<SkImage> m_image;
};

sk_sp<skresources::ImageAsset> imageAssetFromBytes(const QByteArray &bytes)
{
    QImage image;
    image.loadFromData(bytes);
    sk_sp<SkImage> sk = imageFromQImage(image);
    return sk ? sk_make_sp<StillImageAsset>(std::move(sk)) : nullptr;
}

// Skia's resource paths are "<base>/<name>" pairs, exactly as Lottie's asset entries split them
// ("u": directory, "p": file name). Documents exported with images embedded carry
// "data:image/png;base64,..." as the name.
class QtResourceProvider final : public skresources::ResourceProvider
{
public:
    explicit QtResourceProvider(QString baseDir) : m_baseDir(std::move(baseDir)) {}

    sk_sp<SkData> load(const char resourcePath[], const char resourceName[]) const override
    {
        const QByteArray bytes = read(resourcePath, resourceName);
        return bytes.isEmpty() ? nullptr : SkData::MakeWithCopy(bytes.constData(), bytes.size());
    }

    sk_sp<skresources::ImageAsset> loadImageAsset(const char resourcePath[], const char resourceName[],
                                                  const char[]) const override
    {
        return imageAssetFromBytes(read(resourcePath, resourceName));
    }

    sk_sp<SkTypeface> loadTypeface(const char name[], const char url[]) const override
    {
        const QByteArray bytes = read("", url);
        if (!bytes.isEmpty()) {
            if (sk_sp<SkTypeface> tf = systemFontMgr()->makeFromData(
                    SkData::MakeWithCopy(bytes.constData(), bytes.size())))
                return tf;
        }
        return systemFontMgr()->matchFamilyStyle(name, SkFontStyle());
    }

private:
    QByteArray read(const char resourcePath[], const char resourceName[]) const
    {
        const QString name = QString::fromUtf8(resourceName);
        if (name.startsWith(QLatin1String("data:"))) {
            const int comma = name.indexOf(QLatin1Char(','));
            if (comma < 0)
                return {};
            const QByteArray payload = name.mid(comma + 1).toLatin1();
            return name.left(comma).contains(QLatin1String(";base64"))
                       ? QByteArray::fromBase64(payload)
                       : QByteArray::fromPercentEncoding(payload);
        }
        if (m_baseDir.isEmpty())
            return {};
        // Never above the document's own directory: the document is untrusted input. A leading
        // slash (dotLottie writes "u":"/i/") is document-relative, not filesystem-absolute.
        QString rel = QDir::cleanPath(QString::fromUtf8(resourcePath) + QLatin1Char('/') + name);
        while (rel.startsWith(QLatin1Char('/')))
            rel.remove(0, 1);
        if (rel.startsWith(QLatin1String("..")) || QFileInfo(rel).isAbsolute())
            return {};
        QFile file(QDir(m_baseDir).filePath(rel));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }

    QString m_baseDir;
};

} // namespace

sk_sp<SkImage> imageFromQImage(const QImage &image)
{
    if (image.isNull())
        return nullptr;
    // ARGB32_Premultiplied is BGRA in memory on little-endian hosts, which Skia reads directly.
    const QImage premul = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const SkImageInfo info = SkImageInfo::Make(premul.width(), premul.height(),
                                               kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    return SkImages::RasterFromPixmapCopy(SkPixmap(info, premul.constBits(), premul.bytesPerLine()));
}

sk_sp<skresources::ImageAsset> imageAssetFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    return imageAssetFromBytes(file.readAll());
}

sk_sp<skresources::ResourceProvider> makeVectorResourceProvider(const QString &baseDir)
{
    return sk_make_sp<QtResourceProvider>(baseDir);
}

} // namespace drift::skia
