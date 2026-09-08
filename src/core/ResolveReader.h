#pragma once

#include "Project.h"

#include <optional>
#include <QByteArray>
#include <QString>

namespace drift::resolve {

// Returns true if filePath is a DaVinci Resolve Project (.drp) or Final Cut Pro X XML (.fcpxml).
bool isResolveProject(const QString &filePath);

// Returns true if data is a DRP zip archive or starts with <fcpxml.
bool isResolveData(const QByteArray &data);

// Reads a DaVinci Resolve Project (.drp) archive or FCPXML (.fcpxml) from disk and imports it as a drift::Project.
std::optional<Project> readProject(const QString &filePath, QString *error = nullptr);

// Reads raw XML data (FCPXML or Resolve timeline XML) and converts it to a drift::Project.
std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir = QString(),
                                      QString *error = nullptr);

// Reads an extracted or archived DaVinci Resolve .drp package.
std::optional<Project> readDrpArchive(const QString &drpFilePath, QString *error = nullptr);

// Reads FCPXML (<fcpxml>) data.
std::optional<Project> readFcpxmlData(const QByteArray &data, const QString &sourceDir = QString(),
                                      QString *error = nullptr);

} // namespace drift::resolve
