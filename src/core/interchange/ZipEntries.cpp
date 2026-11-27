#include "core/interchange/ZipEntries.h"

#include "core/base/ZipArchive.h"

#include <QFile>
#include <QObject>

namespace drift::interchange {

QStringList zipEntryNames(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QStringList names;
    for (const zip::Entry &entry : zip::readEntries(file, nullptr))
        names.append(entry.path);
    return names;
}

QList<ZipEntryData> readZipEntries(const QString &path, const std::function<bool(const QString &name)> &wanted,
                                   QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("Cannot open file: %1").arg(path);
        return {};
    }

    const QList<zip::Entry> entries = zip::readEntries(file, error);
    if (entries.isEmpty()) {
        if (error && error->isEmpty())
            *error = QObject::tr("ZIP archive is empty");
        return {};
    }

    QList<ZipEntryData> out;
    for (const zip::Entry &entry : entries) {
        if (entry.isDir || !wanted(entry.path))
            continue;

        QByteArray data;
        if (zip::extractEntry(file, entry, data))
            out.append({entry.path, data});
    }
    return out;
}

} // namespace drift::interchange
