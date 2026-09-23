#pragma once

#include "core/Clip.h"
#include "core/Time.h"

#include <QHash>
#include <QMutex>
#include <QString>

#include <atomic>

namespace drift {

// Registry of pre-rendered reversed copies of source ranges, so a reversed clip can be read
// forwards instead of asking the decoder for an ever-earlier timestamp (which costs a keyframe
// seek and a GOP re-decode per displayed frame).
//
// This is deliberately a pure cache and lives entirely outside drift::Project: nothing here is
// serialized, nothing enters the undo stack, and nothing is embedded in a .drift bundle. A miss
// is not an error — the caller falls back to decoding the source live, which is what the app did
// before proxies existed, only slower.
class ReverseProxyCache
{
public:
    static ReverseProxyCache &instance();

    // Path of a proxy whose covered source range contains [srcIn, srcOut], or empty when nothing
    // covers it. *coverEndUs receives the covered range's end — the pivot for the timestamp flip,
    // so proxyUs == coverEndUs - sourceUs.
    QString lookup(const QString &sourcePath, TimeUs srcIn, TimeUs srcOut, TimeUs *coverEndUs) const;
    void insert(const QString &sourcePath, TimeUs coverInUs, TimeUs coverOutUs,
                const QString &proxyPath);

    // Preview proxies: whole-file, low-res copies that keep the source's own timestamps. Only
    // live preview reads them; export always decodes the original. Keyed on the proxy's short
    // side too, so changing the proxy-size setting makes old proxies stop matching.
    //
    // lookupPreview runs for every video clip on every composited frame, so unlike lookup() it
    // does not stat the source — staleness is settled by load() and insertPreview().
    QString lookupPreview(const QString &sourcePath, int shortSide) const;
    void insertPreview(const QString &sourcePath, int shortSide, const QString &proxyPath);
    void removePreview(const QString &sourcePath);

    // Set from QSettings by PlaybackEngine; read on the compositor threads.
    static std::atomic<bool> previewProxiesEnabled;
    static std::atomic<int> previewProxyShortSide;

    // Reads the on-disk index, dropping entries whose proxy or source is gone or whose source has
    // changed since the render.
    void load();
    // Startup GC: removes stray .part files, then prunes the directory to maxBytes, oldest first.
    // Deleting a proxy that is still referenced is safe; the clip falls back to live decode.
    void sweep(qint64 maxBytes);

#ifdef Q_OS_ANDROID
    // A phone's internal storage is shared with every other app and the user has no file manager
    // route to these proxies, so the budget is a small fraction of the desktop one.
    static constexpr qint64 kDefaultMaxBytes = 1LL * 1024 * 1024 * 1024;
#else
    static constexpr qint64 kDefaultMaxBytes = 8LL * 1024 * 1024 * 1024;
#endif

private:
    ReverseProxyCache() = default;

    struct Entry
    {
        QString proxyPath;
        bool preview = false;
        int shortSide = 0;
        TimeUs coverInUs = 0;
        TimeUs coverOutUs = 0;
        // The source as it was when the render ran. There is no file watching anywhere in the app
        // and ClipReader::open matches on path identity alone, so a source replaced in place has
        // to invalidate by key rather than by path.
        qint64 sourceMtimeMs = 0;
        qint64 sourceSize = 0;
    };

    void saveLocked() const;

    mutable QMutex m_mutex;
    QHash<QString, QList<Entry>> m_entries; // absolute source path -> proxies covering parts of it
};

// Where a clip's video pixels for `timelineUs` actually come from: the reversed proxy when one
// covers the clip, otherwise the source itself. Mattes, face tracks and audio are indexed in the
// source's own time base and must keep using Clip::timelineToSourceUs directly.
struct VideoRead
{
    QString path;
    TimeUs sourceUs = 0;
};
//
// allowPreviewProxy is set only by live preview; export must leave it off so it always reads
// full-quality pixels.
VideoRead resolveVideoRead(const Clip &clip, TimeUs timelineUs, bool allowPreviewProxy = false);

// The path a reversed clip reads its video from, without resolving a time. For retaining decoder
// workers, where tearing down the proxy's worker would reopen the file every frame.
QString videoReadPath(const Clip &clip, bool allowPreviewProxy = false);

// <AppDataLocation>/reversed, created on demand. Mirrors matteCacheDir(). On Android it is
// <CacheLocation>/reversed instead — see the definition.
QString reverseCacheDir();
// A fresh, unused absolute path inside reverseCacheDir(). Never reuse a path: a ClipReader worker
// may already hold the old one open and would keep serving frames from the stale file.
QString newReversePath();

} // namespace drift
