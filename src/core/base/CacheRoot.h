#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace drift {

// Resolved once per process so every subsystem agrees on the root for the whole session; a
// changed "storage/cacheDir" setting only applies after a restart. Must not be called before
// QCoreApplication's organization/application names are set, since QSettings keys off them.
// The cache lives in an app-named subfolder of the picked folder, so "Clear cache" can never
// wipe unrelated files when the user picks something like ~/Videos.
inline QString customCacheRoot(const QString &picked)
{
    return QDir::cleanPath(picked + QLatin1Char('/') + QCoreApplication::applicationName());
}

inline QString cacheRoot()
{
    static const QString root = [] {
        const QString custom = QSettings().value(QStringLiteral("storage/cacheDir")).toString().trimmed();
        if (!custom.isEmpty() && QDir().mkpath(customCacheRoot(custom)))
            return customCacheRoot(custom);
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }();
    return root;
}

} // namespace drift
