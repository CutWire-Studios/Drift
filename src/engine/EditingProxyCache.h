#pragma once

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>

namespace drift {

// Manages lightweight editing proxies (540p fast intra/H.264) for video clips.
//
// Decoding 4K or 1080p60 in real-time saturates older CPUs and integrated GPUs.
// EditingProxyCache provides an on-demand, background-generated low-resolution copy
// for timeline scrubbing and playback.
//
// Exports always bypass the proxy and read from the original source file at 100% quality.
class EditingProxyCache : public QObject
{
    Q_OBJECT

public:
    static EditingProxyCache &instance();

    // Cache directory: <CacheLocation>/editing_proxies
    static QString proxyCacheDir();

    // Returns the proxy path if one exists, is valid, and matches the source's mtime/size.
    // Returns empty QString if no valid proxy is available.
    QString lookup(const QString &sourcePath) const;

    // Returns true if a proxy is already available or actively being rendered.
    bool hasProxy(const QString &sourcePath) const;
    bool isGenerating(const QString &sourcePath) const;

    // Asynchronously generates a proxy for sourcePath in the background.
    // If a proxy exists or is already in flight, does nothing.
    void requestProxy(const QString &sourcePath);

    // Global toggle for editing proxies (e.g. controlled by Low-Spec Mode)
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    // Load persisted index from disk and prune old cache files.
    void load();
    void sweep(qint64 maxBytes = kDefaultMaxBytes);

#ifdef Q_OS_ANDROID
    static constexpr qint64 kDefaultMaxBytes = 1LL * 1024 * 1024 * 1024;
#else
    static constexpr qint64 kDefaultMaxBytes = 6LL * 1024 * 1024 * 1024;
#endif

signals:
    void proxyReady(const QString &sourcePath, const QString &proxyPath);
    void proxyFailed(const QString &sourcePath, const QString &error);

private:
    explicit EditingProxyCache(QObject *parent = nullptr);
    ~EditingProxyCache() override = default;

    struct Entry
    {
        QString proxyPath;
        qint64 sourceMtimeMs = 0;
        qint64 sourceSize = 0;
    };

    void saveLocked() const;

    mutable QMutex m_mutex;
    QHash<QString, Entry> m_entries;
    QSet<QString> m_inFlight;
    bool m_enabled = true;
};

} // namespace drift
