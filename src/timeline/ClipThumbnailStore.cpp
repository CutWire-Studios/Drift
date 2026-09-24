#include "ClipThumbnailStore.h"

#include <QImageReader>
#include <QPointer>
#include <QThread>

namespace {

#ifdef Q_OS_ANDROID
constexpr qint64 kBudgetBytes = 16ll * 1024 * 1024;
#else
constexpr qint64 kBudgetBytes = 48ll * 1024 * 1024;
#endif

QImage decode(const QString &path, int maxHeight)
{
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.isValid() && maxHeight > 0 && size.height() > maxHeight) {
        reader.setScaledSize(QSize(qMax(1, qRound(double(size.width()) * maxHeight / size.height())),
                                   maxHeight));
    }
    QImage image = reader.read();
    if (image.isNull())
        return {};
    // Premultiplied, like the image provider: Android draws an RGB32 texture as transparent.
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

} // namespace

ClipThumbnailStore::ClipThumbnailStore(QObject *parent)
    : QObject(parent)
    , m_budget(kBudgetBytes)
{
    // Thumbnails are the least urgent work in the process; a scroll that outruns them just
    // shows the placeholder a little longer.
    m_pool.setMaxThreadCount(qMax(1, QThread::idealThreadCount() / 4));
    m_pool.setThreadPriority(QThread::LowPriority);
}

ClipThumbnailStore::~ClipThumbnailStore()
{
    m_pool.clear();
    m_pool.waitForDone();
}

ClipThumbnailStore &ClipThumbnailStore::instance()
{
    static ClipThumbnailStore store;
    return store;
}

QImage ClipThumbnailStore::image(const QString &path, int maxHeight)
{
    if (path.isEmpty() || m_failed.contains(path))
        return {};
    const auto it = m_images.constFind(path);
    if (it != m_images.constEnd()) {
        touch(path);
        return it->image;
    }
    if (m_pending.contains(path))
        return {};

    m_pending.insert(path);
    QPointer<ClipThumbnailStore> self(this);
    m_pool.start([self, path, maxHeight] {
        const QImage image = decode(path, maxHeight);
        if (!self)
            return;
        QMetaObject::invokeMethod(
            self.data(), [self, path, image] {
                if (self)
                    self->finish(path, image);
            },
            Qt::QueuedConnection);
    });
    return {};
}

void ClipThumbnailStore::finish(const QString &path, const QImage &image)
{
    m_pending.remove(path);
    if (image.isNull()) {
        m_failed.insert(path);
        return;
    }
    m_lru.push_front(path);
    m_images.insert(path, Entry{image, m_lru.begin()});
    m_bytes += image.sizeInBytes();
    evict();
    emit imageReady(path);
}

void ClipThumbnailStore::touch(const QString &path)
{
    auto it = m_images.find(path);
    if (it == m_images.end())
        return;
    m_lru.splice(m_lru.begin(), m_lru, it->lru);
}

void ClipThumbnailStore::evict()
{
    // Never the entry just added: a single image over budget still has to be shown once.
    while (m_bytes > m_budget && m_lru.size() > 1) {
        const QString oldest = m_lru.back();
        m_lru.pop_back();
        const auto it = m_images.find(oldest);
        if (it != m_images.end()) {
            m_bytes -= it->image.sizeInBytes();
            m_images.erase(it);
        }
    }
}
