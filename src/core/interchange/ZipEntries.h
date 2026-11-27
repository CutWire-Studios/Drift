#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace drift::interchange {

struct ZipEntryData
{
    QString name;
    QByteArray data;
};

// Entry names in central-directory order, directories included. Empty if the file cannot be read.
QStringList zipEntryNames(const QString &path);

// Extracts the entries whose names satisfy wanted, in archive order, into memory. Nothing is written to disk.
// error is set when the file cannot be opened, is not a ZIP archive, or has no entries.
QList<ZipEntryData> readZipEntries(const QString &path, const std::function<bool(const QString &name)> &wanted,
                                   QString *error = nullptr);

} // namespace drift::interchange
