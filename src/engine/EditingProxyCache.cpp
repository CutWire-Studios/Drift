#include "EditingProxyCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QProcess>
#include <QStandardPaths>
#include <QThreadPool>
#include <QUuid>

namespace drift {

namespace {

QString indexPath()
{
    const QString dir = EditingProxyCache::proxyCacheDir();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(QStringLiteral("index.json"));
}

} // namespace

EditingProxyCache &EditingProxyCache::instance()
{
    static EditingProxyCache cache;
    return cache;
}

EditingProxyCache::EditingProxyCache(QObject *parent)
    : QObject(parent)
{
}

QString EditingProxyCache::proxyCacheDir()
{
#ifdef Q_OS_ANDROID
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#endif
    if (base.isEmpty())
        return {};
    const QString dir = QDir(base).filePath(QStringLiteral("editing_proxies"));
    QDir().mkpath(dir);
    return dir;
}

void EditingProxyCache::setEnabled(bool enabled)
{
    QMutexLocker lock(&m_mutex);
    m_enabled = enabled;
}

QString EditingProxyCache::lookup(const QString &sourcePath) const
{
    if (sourcePath.isEmpty())
        return {};

    QMutexLocker lock(&m_mutex);
    if (!m_enabled)
        return {};

    const auto it = m_entries.constFind(sourcePath);
    if (it == m_entries.constEnd())
        return {};

    const Entry &entry = it.value();
    const QFileInfo proxy(entry.proxyPath);
    if (!proxy.exists() || proxy.size() == 0)
        return {};

    const QFileInfo source(sourcePath);
    if (!source.exists())
        return {};

    if (source.size() != entry.sourceSize || source.lastModified().toMSecsSinceEpoch() != entry.sourceMtimeMs)
        return {};

    return entry.proxyPath;
}

bool EditingProxyCache::hasProxy(const QString &sourcePath) const
{
    return !lookup(sourcePath).isEmpty();
}

bool EditingProxyCache::isGenerating(const QString &sourcePath) const
{
    QMutexLocker lock(&m_mutex);
    return m_inFlight.contains(sourcePath);
}

void EditingProxyCache::requestProxy(const QString &sourcePath)
{
    if (sourcePath.isEmpty())
        return;

    {
        QMutexLocker lock(&m_mutex);
        if (!m_enabled || m_inFlight.contains(sourcePath))
            return;

        if (m_entries.contains(sourcePath)) {
            const Entry &entry = m_entries.value(sourcePath);
            const QFileInfo proxy(entry.proxyPath);
            const QFileInfo source(sourcePath);
            if (proxy.exists() && proxy.size() > 0 && source.exists()
                && source.size() == entry.sourceSize
                && source.lastModified().toMSecsSinceEpoch() == entry.sourceMtimeMs) {
                return;
            }
        }
        m_inFlight.insert(sourcePath);
    }

    const QString cacheDir = proxyCacheDir();
    if (cacheDir.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        m_inFlight.remove(sourcePath);
        return;
    }

    const QString outPath = QDir(cacheDir).filePath(
        QStringLiteral("proxy-%1.mp4").arg(QUuid::createUuid().toString(QUuid::Id128)));

    QThreadPool::globalInstance()->start([this, sourcePath, outPath] {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) {
            QMutexLocker lock(&m_mutex);
            m_inFlight.remove(sourcePath);
            emit proxyFailed(sourcePath, QStringLiteral("ffmpeg not found in PATH"));
            return;
        }

        // Fast 540p proxy transcode: ultrafast x264, copy audio for fast seeking
        QStringList args;
        args << QStringLiteral("-y")
             << QStringLiteral("-i") << sourcePath
             << QStringLiteral("-vf") << QStringLiteral("scale=-2:min(540\\,ih)")
             << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-preset") << QStringLiteral("ultrafast")
             << QStringLiteral("-crf") << QStringLiteral("24")
             << QStringLiteral("-c:a") << QStringLiteral("aac")
             << QStringLiteral("-b:a") << QStringLiteral("128k")
             << outPath;

        QProcess proc;
        proc.start(ffmpeg, args);
        if (!proc.waitForStarted(5000)) {
            QMutexLocker lock(&m_mutex);
            m_inFlight.remove(sourcePath);
            emit proxyFailed(sourcePath, QStringLiteral("Failed to start ffmpeg process"));
            return;
        }

        proc.waitForFinished(-1);

        const QFileInfo outInfo(outPath);
        if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0 && outInfo.exists() && outInfo.size() > 0) {
            const QFileInfo sourceInfo(sourcePath);
            Entry entry;
            entry.proxyPath = outPath;
            entry.sourceSize = sourceInfo.size();
            entry.sourceMtimeMs = sourceInfo.lastModified().toMSecsSinceEpoch();

            {
                QMutexLocker lock(&m_mutex);
                m_entries.insert(sourcePath, entry);
                m_inFlight.remove(sourcePath);
                saveLocked();
            }
            emit proxyReady(sourcePath, outPath);
        } else {
            QFile::remove(outPath);
            {
                QMutexLocker lock(&m_mutex);
                m_inFlight.remove(sourcePath);
            }
            emit proxyFailed(sourcePath, QString::fromUtf8(proc.readAllStandardError()));
        }
    });
}

void EditingProxyCache::saveLocked() const
{
    const QString path = indexPath();
    if (path.isEmpty())
        return;

    QJsonObject root;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        const Entry &e = it.value();
        QJsonObject obj;
        obj[QStringLiteral("proxyPath")] = e.proxyPath;
        obj[QStringLiteral("sourceSize")] = e.sourceSize;
        obj[QStringLiteral("sourceMtimeMs")] = e.sourceMtimeMs;
        root[it.key()] = obj;
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}

void EditingProxyCache::load()
{
    const QString path = indexPath();
    if (path.isEmpty() || !QFile::exists(path))
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    QMutexLocker lock(&m_mutex);
    m_entries.clear();
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        if (!it.value().isObject())
            continue;
        const QJsonObject obj = it.value().toObject();
        Entry e;
        e.proxyPath = obj.value(QStringLiteral("proxyPath")).toString();
        e.sourceSize = obj.value(QStringLiteral("sourceSize")).toInteger(0);
        e.sourceMtimeMs = obj.value(QStringLiteral("sourceMtimeMs")).toInteger(0);

        if (QFile::exists(e.proxyPath)) {
            m_entries.insert(it.key(), e);
        }
    }
}

void EditingProxyCache::sweep(qint64 maxBytes)
{
    load();

    QMutexLocker lock(&m_mutex);
    const QString dir = proxyCacheDir();
    if (dir.isEmpty())
        return;

    // Remove any stranded temporary files
    const QDir d(dir);
    const QFileInfoList files = d.entryInfoList(QDir::Files, QDir::Time);
    qint64 totalBytes = 0;
    for (const QFileInfo &fi : files) {
        if (fi.fileName() == QStringLiteral("index.json"))
            continue;
        totalBytes += fi.size();
    }

    if (totalBytes <= maxBytes)
        return;

    // Prune oldest files
    for (int i = files.size() - 1; i >= 0 && totalBytes > maxBytes; --i) {
        const QFileInfo &fi = files.at(i);
        if (fi.fileName() == QStringLiteral("index.json"))
            continue;

        const qint64 sz = fi.size();
        if (QFile::remove(fi.absoluteFilePath())) {
            totalBytes -= sz;
            for (auto it = m_entries.begin(); it != m_entries.end();) {
                if (it.value().proxyPath == fi.absoluteFilePath()) {
                    it = m_entries.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    saveLocked();
}

} // namespace drift
