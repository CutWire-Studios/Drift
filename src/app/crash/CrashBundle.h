#pragma once

#include <QByteArray>
#include <QDateTime>
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

// Files that exist are included, the rest skipped: a crash bundle has a dump (Windows) or a stack
// (Linux, macOS) and diagnostics have neither. `crashDir` may be empty; `appleReport` is the path
// from newestAppleReport, or empty.
QList<BundleItem> bundleItems(const QString &crashDir, const QString &logDir, const QString &systemInfo,
                              const QString &appleReport = {});

// The newest drift*.ips or drift*.crash in macOS's DiagnosticReports folder modified at or after
// `since`, or an empty string. It holds paths, so it goes in the saved bundle only, never the upload.
QString newestAppleReport(const QString &reportsDir, const QDateTime &since);

} // namespace drift::crash
