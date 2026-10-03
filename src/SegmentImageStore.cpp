#include "SegmentImageStore.h"

#include <QMutex>

namespace {

QMutex g_mutex;
QImage g_frame;
QImage g_mask;
QImage g_restoreOriginal;
QImage g_restoreEnhanced;

} // namespace

namespace SegmentImageStore {

void setFrame(const QImage &frame)
{
    QMutexLocker lock(&g_mutex);
    g_frame = frame;
}

void setMask(const QImage &mask)
{
    QMutexLocker lock(&g_mutex);
    g_mask = mask;
}

void clear()
{
    QMutexLocker lock(&g_mutex);
    g_frame = QImage();
    g_mask = QImage();
}

QImage frame()
{
    QMutexLocker lock(&g_mutex);
    return g_frame;
}

QImage mask()
{
    QMutexLocker lock(&g_mutex);
    return g_mask;
}

void setRestoreOriginal(const QImage &image)
{
    QMutexLocker lock(&g_mutex);
    g_restoreOriginal = image;
}

void setRestoreEnhanced(const QImage &image)
{
    QMutexLocker lock(&g_mutex);
    g_restoreEnhanced = image;
}

QImage restoreOriginal()
{
    QMutexLocker lock(&g_mutex);
    return g_restoreOriginal;
}

QImage restoreEnhanced()
{
    QMutexLocker lock(&g_mutex);
    return g_restoreEnhanced;
}

} // namespace SegmentImageStore
