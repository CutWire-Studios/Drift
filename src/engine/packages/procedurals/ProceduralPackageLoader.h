#pragma once

#include "core/effects/EffectPreset.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

#include <cstddef>

// Procedural 3D packages: <slug>/procedural.json, main.lua and one or more <name>.vert + <name>.frag pairs.
namespace drift {

struct ProceduralShaderPair
{
    QString vert;
    QString frag;
};

struct ProceduralPackage
{
    QString id;
    QString displayName;
    QString description;
    QString category;
    int order = 0;
    QList<EffectParamSpec> parameters;
    QString luaSource;
    QMap<QString, ProceduralShaderPair> shaders; // keyed by file basename, i.e. the name scene:add refers to
    QString sourceHash;                          // hex SHA-256 over manifest, main.lua and every shader file
    std::size_t maxVertices = 0;                 // manifest value clamped to the platform budget
    std::size_t maxInstances = 0;
    QString packageDir;
    QString thumbnailPath;
    QString errorMessage;                        // empty when valid
    bool valid = false;
};

} // namespace drift

namespace ProceduralPackageLoader {

constexpr qint64 kMaxManifestBytes = 256 * 1024;
constexpr qint64 kMaxLuaBytes = 1024 * 1024;
constexpr qint64 kMaxShaderBytes = 256 * 1024;
constexpr int kMaxShaderPairs = 32;

// On failure the returned package has valid == false and errorMessage set (also copied to errorOut).
drift::ProceduralPackage loadPackage(const QString &packageDir, QString *errorOut = nullptr);

// Immediate subdirectories of each root that contain procedural.json; first root wins on duplicate ids.
QList<drift::ProceduralPackage> scanDirectories(const QStringList &rootDirs);

// DRIFT_PROCEDURALS_DIR, installed "procedurals" addons, <appDir>/procedurals, <AppData>/procedurals.
QStringList defaultSearchPaths();

} // namespace ProceduralPackageLoader
