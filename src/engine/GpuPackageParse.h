#pragma once

#include "GpuEffectDefinition.h"
#include "core/EffectPreset.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

// Shared JSON grammar for GPU packages. Effects (effect.json) and transitions
// (transition.json) declare parameters and pipelines identically; only the
// surrounding metadata differs.
namespace GpuPackageParse {

QString readTextFile(const QString &path, QString *errorOut);

QString slugifyCategory(const QString &raw);

QVariant jsonToVariant(const QJsonValue &value);

// Parse "parameters": [...]. Rejects identifiers colliding with reserved uniforms when gpuBackend.
// packageDir is used to resolve FilePath defaults via resolvePackageAsset; may be empty.
bool parseParameters(const QJsonArray &params, QList<drift::EffectParamSpec> *out, bool gpuBackend,
                     QString *errorOut, const QString &packageDir = {});

void parseFixedParams(const QJsonObject &obj, QMap<QString, QVariant> *out);

// Parse "pipeline": { intermediateBuffers, textures, passes }. Sets out->valid on success.
// maxSourceIndex bounds "source_texture" indices: 0 for effects, 1 for transitions.
bool loadGpuPipeline(const QJsonObject &root, const QString &packageDir, int maxSourceIndex,
                     drift::GpuEffectDefinition *out, QString *errorOut);

// Resolve an optional relative/absolute asset path inside a package; empty when missing.
QString resolvePackageAsset(const QString &packageDir, const QString &relOrAbs);

// Search roots for a package kind, highest priority first:
//
//   1. $<envVar>                      a developer override, always wins
//   2. installed addons of addonKind  downloaded updates (empty addonKind to skip)
//   3. <appDir>/<subdir>              content bundled with the build, if any
//   4. <AppDataLocation>/<subdir>     hand-placed content
//
// Every catalog resolves duplicate ids by first-root-wins, so this ordering is what lets an
// addon update replace a bundled package of the same id without touching the install.
QStringList defaultSearchPaths(const QString &envVar, const QString &subdir,
                               const QString &addonKind = {});

} // namespace GpuPackageParse
