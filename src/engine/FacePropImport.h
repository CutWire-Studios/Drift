#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// The prop.json a face prop ships with (Drift-Assets face-props/<id>/prop.json), and installing
// props from a .zip or a folder into a face-props root. One archive or folder may carry any number
// of props. A prop.json may also list designs under "variants": the default keeps the prop id,
// and each other design whose model is in the folder is installed as `<id>--<variant>`.

struct FacePropVariant
{
    QString id;
    QString name;
    QString model;
    QString thumbnail;
    QString description;
    QStringList tags;
    QVariantMap params;
    bool hasTags = false;
    bool hasParams = false;
};

struct FacePropManifest
{
    QString id;
    QString name;
    QString model;     // plain file name inside the prop folder
    QString thumbnail; // plain file name, or empty
    QString description;
    QString license;
    QStringList tags;
    QVariantMap params; // only faceModelParamsFromMap placement keys
    QList<FacePropVariant> variants;
};

// The default design first, then one manifest per other design. A design whose model file is
// the default's is not repeated. Callers still have to check the model file exists.
QList<FacePropManifest> facePropDesigns(const FacePropManifest &manifest);

// `fallbackId` (sanitised) stands in when the manifest has no id — the prop's folder name.
bool parseFacePropManifest(const QByteArray &json, const QString &fallbackId,
                           FacePropManifest *out, QString *error);

struct FacePropImportResult
{
    QStringList installedIds;
    QStringList errors; // one per prop that was skipped, or one for an unreadable source
};

// Each prop is staged in destRoot/.staging-* and renamed into destRoot/<id>, replacing an existing
// prop of the same id. A failing prop is reported and skipped; the rest still install.
FacePropImportResult importFacePropsFromZip(const QString &zipPath, const QString &destRoot);
FacePropImportResult importFacePropsFromDirectory(const QString &dirPath, const QString &destRoot);

bool removeUserFaceProp(const QString &id, QString *error, const QString &destRoot);
