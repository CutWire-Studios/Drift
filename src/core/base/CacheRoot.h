#pragma once

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QString>

namespace drift {

// Resolved once per process so every subsystem agrees on the root for the whole session; a
// changed "storage/cacheDir" setting only applies after a restart. Must not be called before
// QCoreApplication's organization/application names are set, since QSettings keys off them.
inline QString cacheRoot()
{
    static const QString root = [] {
        const QString custom = QSettings().value(QStringLiteral("storage/cacheDir")).toString().trimmed();
        if (!custom.isEmpty() && QDir().mkpath(custom))
            return QDir::cleanPath(custom);
        return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }();
    return root;
}

} // namespace drift
