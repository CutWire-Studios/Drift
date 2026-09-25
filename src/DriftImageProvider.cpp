#include "DriftImageProvider.h"

#include <QFileInfo>
#include <QImageReader>
#include <QUrl>
#include <QUrlQuery>

DriftImageProvider::DriftImageProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage DriftImageProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    // Optional "?frame=<i>&count=<n>" selects one frame of a filmstrip so each tile can
    // request a distinct sub-image (a shared source URL would collapse to a single cached
    // frame). The path itself is percent-encoded, so a literal '?' only ever starts the query.
    int frame = -1;
    int frameCount = 0;
    QString encodedPath = id;
    const int queryStart = id.indexOf(QLatin1Char('?'));
    if (queryStart >= 0) {
        encodedPath = id.left(queryStart);
        const QUrlQuery query(id.mid(queryStart + 1));
        frame = query.queryItemValue(QStringLiteral("frame")).toInt();
        frameCount = query.queryItemValue(QStringLiteral("count")).toInt();
    }

    const QString path = QUrl::fromPercentEncoding(encodedPath.toUtf8());
    if (path.isEmpty()) {
        if (size)
            *size = QSize();
        return {};
    }

    const bool isResource = path.startsWith(QStringLiteral("qrc:")) || path.startsWith(QStringLiteral(":/"));
    if (!isResource && !QFileInfo::exists(path)) {
        if (size)
            *size = QSize();
        return {};
    }

    // Decode through QImageReader so a smaller requested size is decoded small (JPEG scales in
    // the decoder), rather than decoding the full file and scaling the result.
    QImageReader reader(path);
    QSize sourceSize = reader.size();

    // Crop out the requested filmstrip frame before any rescale, so the returned image is a
    // single frame at its native resolution.
    if (frameCount > 0 && frame >= 0 && sourceSize.width() >= frameCount) {
        const int frameW = sourceSize.width() / frameCount;
        const int x = qMin(frame, frameCount - 1) * frameW;
        reader.setClipRect(QRect(x, 0, frameW, sourceSize.height()));
        sourceSize = QSize(frameW, sourceSize.height());
    }

    // Same result as scaling with KeepAspectRatioByExpanding, but only ever downscales. A request
    // with one side 0 fits the other side and keeps the aspect ratio.
    if (sourceSize.isValid() && requestedSize.isValid()
        && (requestedSize.width() > 0 || requestedSize.height() > 0)) {
        QSize target = requestedSize;
        if (target.width() <= 0)
            target.setWidth(qMax(1, qRound(double(sourceSize.width()) * target.height() / sourceSize.height())));
        else if (target.height() <= 0)
            target.setHeight(qMax(1, qRound(double(sourceSize.height()) * target.width() / sourceSize.width())));
        const QSize scaled = sourceSize.scaled(target, Qt::KeepAspectRatioByExpanding);
        if (scaled.width() < sourceSize.width() && scaled.height() < sourceSize.height())
            reader.setScaledSize(scaled);
    }

    QImage image = reader.read();
    if (image.isNull()) {
        if (size)
            *size = QSize();
        return {};
    }

    // A format that can't report its size up front was read whole; crop and scale it here.
    if (!sourceSize.isValid()) {
        if (frameCount > 0 && frame >= 0 && image.width() >= frameCount) {
            const int frameW = image.width() / frameCount;
            image = image.copy(QRect(qMin(frame, frameCount - 1) * frameW, 0, frameW, image.height()));
        }
        if (requestedSize.width() > 0 && requestedSize.height() > 0)
            image = image.scaled(requestedSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    }

    // Everything leaves here premultiplied ARGB, whatever it was read as. Two reasons, and the
    // second one only shows up on a device: soft-alpha package thumbs (e.g. audio-effect AuraBlur
    // PNGs) blend as nearly invisible mud against the card background without it, and Android
    // draws a Format_RGB32 provider image — which is every JPEG in the thumbnail cache — as fully
    // transparent, so bin cards and filmstrips came up blank while the Image itself reported
    // Ready. convertToFormat is a no-op when the format already matches.
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    if (size)
        *size = image.size();
    return image;
}
