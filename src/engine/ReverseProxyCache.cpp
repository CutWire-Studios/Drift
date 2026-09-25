#include "ReverseProxyCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace drift {

namespace {

QString indexPath()
{
    const QString dir = reverseCacheDir();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("index.json"));
}

// The bake lives under AppDataLocation, which on this app is "CutWire Drift" —
// spaces. avformat_open_input is fine with spaces; some decode backends are not.
// Keep a no-space copy in /tmp named by clip id so preview always has a clean path.
QString bakePathForDecode(const Clip &clip)
{
    if (clip.stabilizePath.isEmpty() || !QFile::exists(clip.stabilizePath))
        return {};
    const QString &path = clip.stabilizePath;
    if (!path.contains(QLatin1Char(' ')) && !path.contains(QLatin1Char('\'')))
        return path;

    const QString tmp =
        QDir::temp().filePath(QStringLiteral("drift-stab-out-%1.mp4").arg(clip.id));
    const QFileInfo src(path);
    const QFileInfo dst(tmp);
    if (!dst.exists() || dst.size() != src.size() || dst.lastModified() < src.lastModified()) {
        QFile::remove(tmp);
        if (!QFile::copy(path, tmp) || !QFile::exists(tmp))
            return path;
    }
    return tmp;
}

QString previewProxyPath(const Clip &clip)
{
    if (clip.type != ClipType::Video || clip.path.isEmpty()
        || !ReverseProxyCache::previewProxiesEnabled.load(std::memory_order_relaxed))
        return {};
    return ReverseProxyCache::instance().lookupPreview(
        clip.path, ReverseProxyCache::previewProxyShortSide.load(std::memory_order_relaxed));
}

} // namespace

std::atomic<bool> ReverseProxyCache::previewProxiesEnabled{true};
#ifdef Q_OS_ANDROID
std::atomic<int> ReverseProxyCache::previewProxyShortSide{540};
#else
std::atomic<int> ReverseProxyCache::previewProxyShortSide{720};
#endif

ReverseProxyCache &ReverseProxyCache::instance()
{
    static ReverseProxyCache cache;
    return cache;
}

QString ReverseProxyCache::lookup(const QString &sourcePath, TimeUs srcIn, TimeUs srcOut,
                                  TimeUs *coverEndUs) const
{
    if (sourcePath.isEmpty())
        return {};

    const QFileInfo info(sourcePath);
    const QString key = info.absoluteFilePath();

    QMutexLocker lock(&m_mutex);
    const auto it = m_entries.constFind(key);
    if (it == m_entries.constEnd())
        return {};

    const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();
    const qint64 size = info.size();
    for (const Entry &entry : *it) {
        if (entry.preview || entry.sourceMtimeMs != mtimeMs || entry.sourceSize != size)
            continue;
        // Containment, not equality: trimming a reversed clip inward, splitting it, or pasting a
        // copy all stay inside the range that was rendered and keep hitting the proxy for free.
        if (entry.coverInUs > srcIn || entry.coverOutUs < srcOut)
            continue;
        if (coverEndUs)
            *coverEndUs = entry.coverOutUs;
        return entry.proxyPath;
    }
    return {};
}

void ReverseProxyCache::insert(const QString &sourcePath, TimeUs coverInUs, TimeUs coverOutUs,
                               const QString &proxyPath)
{
    if (sourcePath.isEmpty() || proxyPath.isEmpty() || coverOutUs <= coverInUs)
        return;

    const QFileInfo info(sourcePath);
    Entry entry;
    entry.proxyPath = proxyPath;
    entry.coverInUs = coverInUs;
    entry.coverOutUs = coverOutUs;
    entry.sourceMtimeMs = info.lastModified().toMSecsSinceEpoch();
    entry.sourceSize = info.size();

    QMutexLocker lock(&m_mutex);
    QList<Entry> &list = m_entries[info.absoluteFilePath()];
    // A new render supersedes any older one it fully covers — keeping both would leave a large
    // file on disk that lookup can never prefer.
    for (int i = list.size() - 1; i >= 0; --i) {
        const Entry &old = list.at(i);
        if (old.sourceMtimeMs != entry.sourceMtimeMs || old.sourceSize != entry.sourceSize
            || (!old.preview && entry.coverInUs <= old.coverInUs
                && entry.coverOutUs >= old.coverOutUs)) {
            QFile::remove(old.proxyPath);
            list.removeAt(i);
        }
    }
    list.prepend(entry);
    saveLocked();
}

QString ReverseProxyCache::lookupPreview(const QString &sourcePath, int shortSide) const
{
    if (sourcePath.isEmpty())
        return {};

    QMutexLocker lock(&m_mutex);
    const auto it = m_entries.constFind(QFileInfo(sourcePath).absoluteFilePath());
    if (it == m_entries.constEnd())
        return {};
    for (const Entry &entry : *it) {
        if (entry.preview && entry.shortSide == shortSide)
            return entry.proxyPath;
    }
    return {};
}

void ReverseProxyCache::insertPreview(const QString &sourcePath, int shortSide,
                                      const QString &proxyPath)
{
    if (sourcePath.isEmpty() || proxyPath.isEmpty())
        return;

    const QFileInfo info(sourcePath);
    Entry entry;
    entry.proxyPath = proxyPath;
    entry.preview = true;
    entry.shortSide = shortSide;
    entry.sourceMtimeMs = info.lastModified().toMSecsSinceEpoch();
    entry.sourceSize = info.size();

    QMutexLocker lock(&m_mutex);
    QList<Entry> &list = m_entries[info.absoluteFilePath()];
    // One preview proxy per source: a proxy at another size is dead weight once the setting
    // moved, and a stale-source entry of either kind can never be read again.
    for (int i = list.size() - 1; i >= 0; --i) {
        const Entry &old = list.at(i);
        if (old.preview || old.sourceMtimeMs != entry.sourceMtimeMs
            || old.sourceSize != entry.sourceSize) {
            QFile::remove(old.proxyPath);
            list.removeAt(i);
        }
    }
    list.prepend(entry);
    saveLocked();
}

void ReverseProxyCache::removePreview(const QString &sourcePath)
{
    if (sourcePath.isEmpty())
        return;

    QMutexLocker lock(&m_mutex);
    const auto it = m_entries.find(QFileInfo(sourcePath).absoluteFilePath());
    if (it == m_entries.end())
        return;
    QList<Entry> &list = it.value();
    for (int i = list.size() - 1; i >= 0; --i) {
        if (list.at(i).preview) {
            QFile::remove(list.at(i).proxyPath);
            list.removeAt(i);
        }
    }
    if (list.isEmpty())
        m_entries.erase(it);
    saveLocked();
}

void ReverseProxyCache::saveLocked() const
{
    const QString path = indexPath();
    if (path.isEmpty())
        return;

    QJsonArray array;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        for (const Entry &entry : it.value()) {
            QJsonObject object;
            object[QStringLiteral("source")] = it.key();
            object[QStringLiteral("proxy")] = entry.proxyPath;
            object[QStringLiteral("coverInUs")] = double(entry.coverInUs);
            object[QStringLiteral("coverOutUs")] = double(entry.coverOutUs);
            object[QStringLiteral("sourceMtimeMs")] = double(entry.sourceMtimeMs);
            object[QStringLiteral("sourceSize")] = double(entry.sourceSize);
            if (entry.preview) {
                object[QStringLiteral("kind")] = QStringLiteral("preview");
                object[QStringLiteral("shortSide")] = entry.shortSide;
            }
            array.append(object);
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void ReverseProxyCache::load()
{
    const QString path = indexPath();
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray array = QJsonDocument::fromJson(file.readAll()).array();
    file.close();

    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        const QString source = object[QStringLiteral("source")].toString();
        Entry entry;
        entry.proxyPath = object[QStringLiteral("proxy")].toString();
        entry.coverInUs = TimeUs(object[QStringLiteral("coverInUs")].toDouble());
        entry.coverOutUs = TimeUs(object[QStringLiteral("coverOutUs")].toDouble());
        entry.sourceMtimeMs = qint64(object[QStringLiteral("sourceMtimeMs")].toDouble());
        entry.sourceSize = qint64(object[QStringLiteral("sourceSize")].toDouble());
        entry.preview = object[QStringLiteral("kind")].toString() == QStringLiteral("preview");
        entry.shortSide = object[QStringLiteral("shortSide")].toInt();
        if (source.isEmpty() || entry.proxyPath.isEmpty())
            continue;
        if (!QFile::exists(entry.proxyPath))
            continue;

        // The source may have been edited or replaced while the app was closed. Drop the proxy
        // now rather than letting sweep() carry dead bytes until the budget forces them out.
        const QFileInfo info(source);
        if (!info.exists() || info.lastModified().toMSecsSinceEpoch() != entry.sourceMtimeMs
            || info.size() != entry.sourceSize) {
            QFile::remove(entry.proxyPath);
            continue;
        }
        m_entries[source].append(entry);
    }
    saveLocked();
}

void ReverseProxyCache::sweep(qint64 maxBytes)
{
    const QString dirPath = reverseCacheDir();
    if (dirPath.isEmpty())
        return;

    QDir dir(dirPath);
    for (const QFileInfo &partial :
         dir.entryInfoList({QStringLiteral("*.part")}, QDir::Files))
        QFile::remove(partial.absoluteFilePath());

    // Oldest first, so the prune below drops least-recently-written proxies.
    const QFileInfoList proxies =
        dir.entryInfoList({QStringLiteral("*.mp4")}, QDir::Files, QDir::Time | QDir::Reversed);
    qint64 total = 0;
    for (const QFileInfo &proxy : proxies)
        total += proxy.size();
    if (total <= maxBytes)
        return;

    QSet<QString> removed;
    for (const QFileInfo &proxy : proxies) {
        if (total <= maxBytes)
            break;
        const qint64 size = proxy.size();
        if (QFile::remove(proxy.absoluteFilePath())) {
            removed.insert(proxy.absoluteFilePath());
            total -= size;
        }
    }
    if (removed.isEmpty())
        return;

    QMutexLocker lock(&m_mutex);
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        QList<Entry> &list = it.value();
        for (int i = list.size() - 1; i >= 0; --i) {
            if (removed.contains(QFileInfo(list.at(i).proxyPath).absoluteFilePath()))
                list.removeAt(i);
        }
        if (list.isEmpty())
            it = m_entries.erase(it);
        else
            ++it;
    }
    saveLocked();
}

VideoRead resolveVideoRead(const Clip &clip, TimeUs timelineUs, bool allowPreviewProxy)
{
    const TimeUs sourceUs = clip.timelineToSourceUs(timelineUs);
    VideoRead read{clip.path, sourceUs};
    bool reversed = false;
    if (clip.reverse && clip.type == ClipType::Video && !clip.path.isEmpty()) {
        TimeUs coverEndUs = 0;
        const QString proxy =
            ReverseProxyCache::instance().lookup(clip.path, clip.srcIn, clip.srcOut, &coverEndUs);
        // The proxy holds [coverIn, coverOut] flipped end-for-end, so a source time maps to its
        // mirror. timelineToSourceUs already walked down from srcOut, and this undoes that walk —
        // the composite reads the proxy strictly forwards.
        if (!proxy.isEmpty()) {
            read = {proxy, coverEndUs - sourceUs};
            reversed = true;
        }
    }
    // A preview proxy shares the source's timestamps, so only the path changes.
    if (!reversed && allowPreviewProxy) {
        if (const QString proxy = previewProxyPath(clip); !proxy.isEmpty())
            read.path = proxy;
    }

    // A baked stabilize file is the original source, already transformed. Prefer it over the
    // live path (and over a reverse proxy of the unstabilized file).
    if (const QString baked = bakePathForDecode(clip); !baked.isEmpty())
        read.path = baked;
    return read;
}

QString videoReadPath(const Clip &clip, bool allowPreviewProxy)
{
    if (const QString baked = bakePathForDecode(clip); !baked.isEmpty())
        return baked;

    if (clip.reverse && clip.type == ClipType::Video && !clip.path.isEmpty()) {
        const QString proxy =
            ReverseProxyCache::instance().lookup(clip.path, clip.srcIn, clip.srcOut, nullptr);
        if (!proxy.isEmpty())
            return proxy;
    }
    if (allowPreviewProxy) {
        if (const QString proxy = previewProxyPath(clip); !proxy.isEmpty())
            return proxy;
    }
    return clip.path;
}

QString reverseCacheDir()
{
#ifdef Q_OS_ANDROID
    // CacheLocation, not AppDataLocation: on Android AppDataLocation is the app's files dir, which
    // Settings reports as app data and which the platform's storage reclaim never touches — so the
    // user had no way to get these bytes back short of clearing the whole app. Proxies are exactly
    // what CacheLocation is for: a miss is not an error, lookup() just falls back to live decode.
    //
    // Memoized so the one-time legacy cleanup below runs once per process rather than per call.
    static const QString dir = [] {
        const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (base.isEmpty())
            return QString();
        const QString path = QDir(base).filePath(QStringLiteral("reversed"));
        QDir().mkpath(path);
        // Proxies written by an older build sit in the files dir along with the index that names
        // them by absolute path. Moving the files would leave every one of those paths dangling —
        // orphans that only the byte budget would ever reclaim — so drop the tree instead. The
        // cost is re-rendering an already-reversed clip; nothing here is project content.
        const QString legacy = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        if (!legacy.isEmpty())
            QDir(QDir(legacy).filePath(QStringLiteral("reversed"))).removeRecursively();
        return path;
    }();
    return dir;
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("reversed"));
    QDir().mkpath(dir);
    return dir;
#endif
}

QString newReversePath()
{
    const QString dir = reverseCacheDir();
    if (dir.isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return QDir(dir).filePath(id + QStringLiteral(".mp4"));
}

} // namespace drift
