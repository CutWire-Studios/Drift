#pragma once

#include "core/interchange/InterchangeReport.h"
#include "core/project/Project.h"

#include <optional>
#include <QByteArray>
#include <QString>

namespace drift::kdenlive {

// Returns true if the filePath looks like a Kdenlive (.kdenlive) or Shotcut MLT (.mlt) project.
bool isKdenliveProject(const QString &filePath);

// Returns true if the buffer starts with GZIP magic bytes or an MLT XML root element (<mlt).
bool isKdenliveData(const QByteArray &data);

// Reads a Kdenlive (.kdenlive) or Shotcut (.mlt) project from disk and imports it as a drift::Project.
// report, when given, receives everything that had no Drift equivalent.
std::optional<Project> readProject(const QString &filePath, QString *error = nullptr, drift::interchange::InterchangeReport *report = nullptr);

// Reads raw project XML data (either gzipped or uncompressed) and converts it to a drift::Project.
// sourceDir is used to attempt relative media resolution if original paths do not exist.
std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir = QString(),
                                      QString *error = nullptr, drift::interchange::InterchangeReport *report = nullptr);

} // namespace drift::kdenlive
