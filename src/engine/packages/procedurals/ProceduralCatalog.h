#pragma once

#include "engine/packages/procedurals/ProceduralPackageLoader.h"

#include <QList>
#include <QString>
#include <QStringList>

// Snapshots are returned by value so a reload on another thread cannot invalidate them.
const QList<drift::ProceduralPackage> proceduralCatalog();
// Empty (valid == false, id empty) when the id is unknown.
drift::ProceduralPackage proceduralForId(const QString &id);
bool hasProcedural(const QString &id);
QStringList proceduralIds();

// Rescan from the given roots (or ProceduralPackageLoader::defaultSearchPaths() when empty).
void reloadProceduralCatalog(const QStringList &packageRoots = {});
