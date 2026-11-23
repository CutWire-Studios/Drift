#pragma once

#include <QQuickImageProvider>

// Serves one decoded source frame as image://slipframe/f?path=...&us=...; see SlipFrameUrl.h.
// QML loads these asynchronously on its pixmap-reader thread, which ClipReaderPool::readVideoFrame
// is safe to be called from (it blocks on the file's own decode thread).
class SlipFrameImageProvider : public QQuickImageProvider
{
public:
    SlipFrameImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
