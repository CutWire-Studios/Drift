#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

#include <list>

// Decoded filmstrip images for the scene-graph timeline, off the GUI thread.
//
// Keyed by file: a coarse strip (all its frames side by side, each tile picks one by source
// rect) or one on-demand tile. The QML path decoded each tile through the image provider as a
// separate Image, twice per tile and again on every scroll step; here a file is decoded once
// and kept under a byte budget for as long as something keeps asking for it.
class ClipThumbnailStore : public QObject
{
    Q_OBJECT

public:
    explicit ClipThumbnailStore(QObject *parent = nullptr);
    ~ClipThumbnailStore() override;

    static ClipThumbnailStore &instance();

    // The decoded image if it is ready. Otherwise queues it and returns a null image;
    // imageReady(path) follows. `maxHeight` bounds the decode, in device pixels.
    QImage image(const QString &path, int maxHeight);

signals:
    void imageReady(const QString &path);

private:
    void finish(const QString &path, const QImage &image);
    void touch(const QString &path);
    void evict();

    struct Entry
    {
        QImage image;
        std::list<QString>::iterator lru;
    };
    QHash<QString, Entry> m_images;
    std::list<QString> m_lru; // most recent at the front
    QSet<QString> m_pending;
    QSet<QString> m_failed;
    qint64 m_bytes = 0;
    qint64 m_budget = 0;
    QThreadPool m_pool;
};
