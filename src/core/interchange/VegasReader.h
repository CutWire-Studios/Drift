#pragma once

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
std::optional<Project> readProject(const QString &filePath, QString *error = nullptr);

// Reads raw binary VEGAS project data and converts it to a drift::Project.
std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir = QString(),
                                      QString *error = nullptr);

} // namespace drift::vegas
