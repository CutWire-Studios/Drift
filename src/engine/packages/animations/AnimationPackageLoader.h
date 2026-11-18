#pragma once

#include "engine/packages/animations/AnimationCatalog.h"

#include <QList>
#include <QString>
#include <QStringList>

// Load file-based GPU animation packages (animations/<name>/animation.json).
namespace AnimationPackageLoader {

// Parse a single package directory. On failure, entry.gpu.errorMessage is set.
AnimationPresetEntry loadPackage(const QString &packageDir, QString *errorOut = nullptr);

// Scan each root for immediate subdirectories that contain animation.json.
QList<AnimationPresetEntry> scanDirectories(const QStringList &rootDirs);

// Default search roots: DRIFT_ANIMATIONS_DIR, installed addons, <appDir>/animations,
// <AppDataLocation>/animations.
QStringList defaultSearchPaths();

} // namespace AnimationPackageLoader
