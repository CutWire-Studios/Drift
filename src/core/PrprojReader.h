#pragma once

#include "Project.h"

#include <optional>
#include <QByteArray>
#include <QString>

namespace drift::prproj {

// Returns true if the file looks like a Premiere Pro project (.prproj) or Final Cut Pro XML (.xml).
bool isPremiereProject(const QString &filePath);

// Returns true if the buffer starts with GZIP magic bytes or Premiere / FCP XML root elements.
bool isPremiereData(const QByteArray &data);

// Reads a Premiere Pro project (.prproj or .xml) from disk and imports it as a drift::Project.
std::optional<Project> readProject(const QString &filePath, QString *error = nullptr);

// Reads raw project data (either gzipped or uncompressed XML) and converts it to a drift::Project.
// sourceDir is used to attempt relative media resolution if original absolute paths do not exist.
std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir = QString(),
                                      QString *error = nullptr);

} // namespace drift::prproj
