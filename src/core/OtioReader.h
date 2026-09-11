#pragma once

#include "Project.h"

#include <optional>
#include <QByteArray>
#include <QString>

namespace drift::otio {

// Returns true if filePath ends in .otio or is an OpenTimelineIO file.
bool isOtioTimeline(const QString &filePath);

// Returns true if the buffer contains valid JSON with an OpenTimelineIO schema ("OTIO_SCHEMA").
bool isOtioData(const QByteArray &data);

// Reads an OpenTimelineIO (.otio) file from disk and imports it as a drift::Project.
std::optional<Project> readProject(const QString &filePath, QString *error = nullptr);

// Reads raw OTIO JSON data and converts it to a drift::Project.
// sourceDir is used to resolve relative media references.
std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir = QString(),
                                      QString *error = nullptr);

} // namespace drift::otio
