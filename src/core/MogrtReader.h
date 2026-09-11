#pragma once

#include "Project.h"

#include <optional>
#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace drift::mogrt {

struct MogrtProperty
{
    QString id;
    QString name;
    QString type; // "text", "color", "slider", "checkbox", "image"
    QString stringValue;
    QColor colorValue = Qt::white;
    double numberValue = 0.0;
    bool boolValue = false;
    QString fontFamily;
    int fontSize = 0;
};

struct MogrtTemplate
{
    QString id;
    QString title;
    QString author;
    QString description;
    int width = 1920;
    int height = 1080;
    int fps = 30;
    TimeUs durationUs = 5000000; // default 5.0 seconds (5,000,000 us)
    QString thumbnailPath;
    QString prprojFilePath; // Set if an inner Premiere project was found
    QStringList assetPaths; // Extracted image/video/audio assets
    QList<MogrtProperty> properties;
    QString extractDir;
};

// Returns true if the filePath looks like a Motion Graphics Template (.mogrt).
bool isMogrtFile(const QString &filePath);

// Returns true if the buffer starts with a ZIP header (PK\x03\x04).
bool isMogrtData(const QByteArray &data);

// Reads and unpacks a .mogrt template archive into destDir.
// If destDir is empty, a directory in the app's cache or temp space is used.
std::optional<MogrtTemplate> readTemplate(const QString &filePath,
                                         const QString &destDir = QString(),
                                         QString *error = nullptr);

// Applies the template to a project:
// 1. Adds extracted media assets to the project's AssetLibrary in a dedicated bin folder.
// 2. If an inner Premiere project exists, imports its sequence tracks.
// 3. Otherwise, creates background/overlay media clips and styled text clips for the template's properties.
bool applyTemplateToProject(const MogrtTemplate &tmpl,
                            Project &project,
                            TimeUs insertTimeUs = 0,
                            QString *error = nullptr);

} // namespace drift::mogrt
