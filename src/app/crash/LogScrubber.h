#pragma once

#include <QString>

namespace drift::crash {

struct ScrubContext
{
    QString homeDir;
    QString userName;
    // The install directory. Paths under it keep their folders; only the file name goes.
    QString appDir;
};

// Removes what identifies the person from a log before it leaves the machine.
QString scrubLog(const QString &text, const ScrubContext &context);

// The last `maxBytes` of `text` as UTF-8, starting on a line boundary.
QByteArray tailOfLog(const QString &text, qsizetype maxBytes);

} // namespace drift::crash
