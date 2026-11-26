#pragma once

#include "core/project/folder/ProjectFolder.h"

#include <QJsonArray>
#include <QJsonObject>

namespace drift::projdir {

struct TemplateParam
{
    QString name;
    QString type; // text, media, color, number, choice
    QString label;
    QJsonValue defaultValue;
    QStringList options;
};

struct TemplateSpec
{
    QList<TemplateParam> params;
    QString dataFile;
    QString outputPattern;
};

// Everything a render reads from a folder, so the render needs no filesystem and the farm can ship it.
struct FolderContents
{
    FileMap project; // what readProjectFolder returns
    FileMap scripts;
    FileMap data;
    QByteArray templateJson;
    QString rootDir;
};

FolderContents readFolderContents(const QString &dir);
bool isTemplateFolder(const QString &dir);

// Parses template.json. False with `problems` filled when it is not a valid template.
bool readTemplate(const QByteArray &templateJson, TemplateSpec *spec, QList<Problem> *problems);

// The rows of the template's data file, or of `dataOverride` (named `dataName`, so the suffix picks the
// format) when that is not empty. No data file gives no rows.
QJsonArray templateRows(const FolderContents &folder, const TemplateSpec &spec, const QByteArray &dataOverride = {},
                        const QString &dataName = {}, QString *error = nullptr);

// The parameter values for one render: defaults, then the row, then `overrides`, each coerced to its type.
// Problems (a non-number in a number, a choice outside its options) are appended to `problems`.
QJsonObject resolveParams(const FolderContents &folder, const TemplateSpec &spec, const QJsonObject &row,
                          const QJsonObject &overrides, QList<Problem> *problems);

struct Expansion
{
    QJsonObject doc;    // a complete project document, ready for Project::fromJson
    QJsonObject params; // the values the scripts saw
    QList<Problem> problems;
    bool ok = false;
};

// folder + row -> project document: params from defaults, then the row, then `overrides`; scripts run
// with them. A pure function, so a farm worker can call it with the shipped FolderContents.
Expansion expandTemplate(const FolderContents &folder, const TemplateSpec &spec, const QJsonObject &row = {},
                         const QJsonObject &overrides = {});

// `pattern` with {row} (1-based) and every {param} replaced; values are made safe for a file name.
QString expandOutputPattern(const QString &pattern, int row, const QJsonObject &params);

} // namespace drift::projdir
