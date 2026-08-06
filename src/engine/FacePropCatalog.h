#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Enumerates *.glb under the face-props addon roots. Mirrors StickerCatalog: an empty catalog is
// a normal state until the user installs a pack. Named face-props (not face-models) because
// face-model already means the ONNX landmarker.

struct FacePropEntry
{
    QString id;    // basename without extension, unique within a pack dir
    QString label;
    QString path; // absolute
};

const QList<FacePropEntry> &faceProps();

void reloadFacePropCatalog(const QStringList &packageRoots = {});

QStringList facePropSearchPaths();
