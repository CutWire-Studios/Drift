#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

// Enumerates face props under the face-props roots: prop folders carrying a prop.json (the
// Drift-Assets layout), and bare *.glb files. Mirrors StickerCatalog: an empty catalog is a
// normal state until the user installs a pack. Named face-props (not face-models) because
// face-model already means the ONNX landmarker.

struct FacePropEntry
{
    QString id;    // prop.json id, else the .glb basename; unique across the catalog
    QString label;
    QString path; // absolute .glb
    QString dir;  // the prop folder; empty for a bare .glb
    QString thumbnailPath;
    QString description;
    QString license;
    QStringList tags;
    QVariantMap params; // faceModelParamsFromMap keys the prop was fitted with
    bool userInstalled = false;
};

const QList<FacePropEntry> &faceProps();
QList<FacePropEntry> facePropsSnapshot();

void reloadFacePropCatalog(const QStringList &packageRoots = {});

QStringList facePropSearchPaths();

// Where imported props are installed. First in facePropSearchPaths(), so a re-import beats an
// addon or bundled copy of the same id.
QString userFacePropsDir();
