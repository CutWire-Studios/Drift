#pragma once

#include "Project.h"

#include <optional>
#include <QByteArray>
#include <QString>

namespace drift::edl {

// Returns true if filePath ends in .edl or is a CMX 3600 EDL file.
bool isEdlTimeline(const QString &filePath);

// Returns true if the buffer looks like a CMX 3600 EDL (contains TITLE:, FCM:, or EDL event rows).
bool isEdlData(const QByteArray &data);

// Reads an EDL (.edl) file from disk and imports it as a drift::Project.
std::optional<Project> readProject(const QString &filePath, QString *error = nullptr);

// Reads raw EDL text data and converts it to a drift::Project.
// sourceDir is used to resolve relative media references.
std::optional<Project> readProjectData(const QByteArray &data, const QString &sourceDir = QString(),
                                      QString *error = nullptr);

} // namespace drift::edl
