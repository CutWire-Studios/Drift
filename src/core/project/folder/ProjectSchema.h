#pragma once

#include "core/project/folder/ProjectFolder.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// One description of every file in a project folder. It drives the JSON Schemas written to
// .drift/schema/, the unknown-key and type checks of the lint, the hover text of `drift --lsp` and the
// sidecar split of the folder reader (ProjectFormat.h). A test checks it against what Project::toJson
// actually writes, so a key added to the format without a line here fails the build.
namespace drift::projdir {

struct FieldDesc
{
    QString key;
    // string number integer boolean object array any, plus the shared shapes: keyframes, effects.
    QString type;
    QString doc;
    QStringList enumValues;
    // Element type of an array: string, number, object, any.
    QString items;
    bool required = false;
};

struct KindDesc
{
    QString kind;  // "project", "asset", "track", "clip", ...
    QString title;
    QString doc;
    QList<FieldDesc> fields;
    // Unknown keys are lint warnings and schema errors for kinds that are closed.
    bool closed = true;

    const FieldDesc *field(const QString &key) const;
};

const QList<KindDesc> &kindDescriptions();
const KindDesc *kindDescription(const QString &kind);

// Which kind a file path (relative to the folder) holds; empty for files the format doesn't own.
// Sidecars answer "sidecar.<group>".
QString kindForPath(const QString &relPath);

// Draft 2020-12 schema for one kind. Clips live inside the track (and sequence-track) schemas.
QJsonObject schemaFor(const QString &kind);
// Relative path of the schema file inside a folder.
QString schemaPath(const QString &kind);
// The `$schema` value for a file at `relPath`: a relative URL from the file to its schema.
QString schemaRefFor(const QString &relPath);
// Every schema as it is written to .drift/schema/.
FileMap schemaFiles();

} // namespace drift::projdir
