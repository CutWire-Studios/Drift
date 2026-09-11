#include "StillImage.h"

#include "ClipReader.h"
#include "MediaProbe.h"

#include <QFileInfo>
#include <QImageReader>
#include <QSet>

namespace drift {
namespace {

// SVG is the one suffix that must never reach the FFmpeg fallback. libavformat has an
// image_svg_pipe demuxer but libavcodec has no SVG decoder, so the attempt fails deep in the
// decoder with a message about the codec rather than about the missing qtsvg plugin — replacing
// the one diagnostic that tells the user what to install with one that does not.
bool isSvg(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0;
}

// One frame out of a file Qt could not read. Deliberately a stack-local reader rather than
// ClipReaderPool: the pool keeps an AVFormatContext alive per stream id for sequential playback,
// and a still is decoded once and then cached by the caller.
QImage decodeWithFfmpeg(const QString &path, int maxWidth, int maxHeight)
{
    if (isSvg(path))
        return {};

    ClipReader reader;
    if (!reader.open(path) || !reader.hasVideo())
        return {};

    // 0 means unbounded here, but readVideoFrameAt treats its bounds as a box to fit into, so
    // pass something larger than any real still rather than zero.
    constexpr int kNoBound = 1 << 16;
    QImage out;
    if (!reader.readVideoFrameAt(0, out, maxWidth > 0 ? maxWidth : kNoBound,
                                 maxHeight > 0 ? maxHeight : kNoBound)) {
        return {};
    }
    return out;
}

} // namespace

bool qtCanDecodeStill(const QString &path)
{
    static const QSet<QString> suffixes = [] {
        QSet<QString> out;
        for (const QByteArray &format : QImageReader::supportedImageFormats())
            out.insert(QString::fromLatin1(format).toLower());
        return out;
    }();
    return suffixes.contains(QFileInfo(path).suffix().toLower());
}

QImage decodeStillImage(const QString &path, int maxWidth, int maxHeight)
{
    QImageReader reader(path);
    // Phone cameras write the orientation to EXIF rather than rotating the pixels. Every other
    // caller of QImageReader in the project sets this; decodedStillImage() did not, which is why
    // a portrait JPEG used to sit upright in the bin card and sideways on the canvas.
    reader.setAutoTransform(true);
    if (maxWidth > 0 && maxHeight > 0) {
        QSize size = reader.size();
        if (size.isValid() && !size.isEmpty()) {
            // Bound only; scaling up a small still here would just cost memory, and the caller
            // scales to its layout rect anyway.
            if (size.width() > maxWidth || size.height() > maxHeight) {
                size.scale(maxWidth, maxHeight, Qt::KeepAspectRatio);
                reader.setScaledSize(size);
            }
        }
    }

    const QImage image = reader.read();
    if (!image.isNull())
        return image;

    return decodeWithFfmpeg(path, maxWidth, maxHeight);
}

QSize stillImageSize(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize size = reader.size();
    if (size.isValid() && !size.isEmpty()) {
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            size.transpose();
        return size;
    }

    if (isSvg(path))
        return {};

    // Metadata is enough here, so this never decodes a frame.
    const MediaInfo info = MediaProbe::probe(path);
    if (!info.ok)
        return {};
    for (const StreamInfo &stream : info.streams) {
        if (stream.type != StreamInfo::Type::Video || stream.attachedPicture)
            continue;
        QSize probed(stream.width, stream.height);
        if (probed.isEmpty())
            continue;
        if (stream.rotationDegrees == 90 || stream.rotationDegrees == 270)
            probed.transpose();
        return probed;
    }
    return {};
}

} // namespace drift
