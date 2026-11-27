#pragma once

#include "core/interchange/InterchangeReport.h"
#include "core/project/Project.h"

#include <optional>
#include <QByteArray>
#include <QString>

namespace drift::vegas {

// Returns true if filePath is a Sony/MAGIX VEGAS Pro project (.veg) or contains Wave64 RIFF magic.
bool isVegasProject(const QString &filePath);

// Returns true if data begins with Sony Wave64 RIFF header.
bool isVegasData(const QByteArray &data);

// Reads a Sony/MAGIX VEGAS Pro project (.veg) from disk and converts it to a drift::Project.
// Nested .veg timelines are imported as composite sequences, each parsed once per call.
// report, when given, receives everything that had no Drift equivalent.
std::optional<Project> readProject(const QString &filePath, QString *error = nullptr, drift::interchange::InterchangeReport *report = nullptr);

// Reads raw binary VEGAS project data and converts it to a drift::Project.
// sourceDir is used to resolve media and nested projects relative to the project file.
std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir = QString(),
                                       QString *error = nullptr, drift::interchange::InterchangeReport *report = nullptr);

} // namespace drift::vegas
