#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace drift::crash {

struct BundleItem
{
    QString name;     // path inside the archive
    QString filePath; // read from disk when set, otherwise `data`
    QByteArray data;
};

// Writes a zstd-compressed tar (.tar.zst), streaming: a full dump can be hundreds of MB.
bool writeTarZst(const QString &path, const QList<BundleItem> &items, QString *error);

// Files that exist are included, the rest skipped: a crash bundle has a dump and diagnostics have
// none. `crashDir` may be empty.
QList<BundleItem> bundleItems(const QString &crashDir, const QString &logDir, const QString &systemInfo);

} // namespace drift::crash
