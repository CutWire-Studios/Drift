#pragma once

#include <QByteArray>
#include <QString>

namespace drift {

class Project;

namespace interchange {

QByteArray exportToFcpxml(const Project &project, QString *error = nullptr);
QByteArray exportToEdl(const Project &project, QString *error = nullptr);
QByteArray exportToOtio(const Project &project, QString *error = nullptr);
QByteArray exportToVegasMarkersTsv(const Project &project, QString *error = nullptr);

// Exports the project to `.fcpxml`, `.edl`, `.otio`, or `.txt` (VEGAS Marker/Region TSV).
// When `format` is empty, the format is inferred from `filePath`'s suffix.
bool exportProjectInterchange(const Project &project, const QString &filePath,
                              const QString &format = QString(), QString *error = nullptr);

} // namespace interchange
} // namespace drift
