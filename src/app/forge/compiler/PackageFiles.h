#pragma once

#include "app/forge/compiler/Compiler.h"

#include <QByteArray>
#include <QJsonObject>
#include <QList>

namespace drift::forge {

struct PackageFile
{
    QString path;
    QByteArray data;
};

struct PackageContents
{
    QJsonObject manifest;
    // Pass shaders, textures, image-parameter pictures and forge.json; the manifest is separate.
    QList<PackageFile> files;
};

QByteArray assetBytes(const ForgeDoc &doc, const QString &assetId);

// meta.id, or a fresh forge_<slug>_<4 chars> from the display name when it is empty.
QString exportId(const ForgeDoc &doc);

// Why `doc` can't be installed as a package (id rules, display name, kind); empty when it can.
QString exportProblem(const ForgeDoc &doc);

// The files of one Drift package folder for an Export-mode compile of `doc`. Audio documents ignore `compiled`;
// their files are the manifest, ir/<name>.wav for each impulse response and forge.json.
PackageContents packageContents(const ForgeDoc &doc, const CompileResult &compiled);

}
