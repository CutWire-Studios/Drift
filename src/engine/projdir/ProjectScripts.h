#pragma once

#include "core/project/folder/ProjectFolder.h"

#include <QJsonObject>

namespace drift::projdir {

struct ScriptInput
{
    FileMap scripts;    // "scripts/x.lua" -> source, run in name order
    FileMap data;       // "data/x.csv" -> bytes, read with data "x.csv"
    QJsonObject params; // read with param "name"
    QString rootDir;    // relative media paths a script writes resolve against this
};

struct ScriptOutcome
{
    QJsonObject doc;
    QList<Problem> problems; // file = the script, line = the line in it
    QStringList log;         // what the scripts printed
};

// Runs every script against `doc` (a joined project document) and returns the document with their
// output merged in. A script's earlier output (items whose generatedBy names it) is replaced by what
// it makes now; if it fails, its earlier output is kept and the failure is a problem. Items of
// scripts that no longer exist are left alone, unowned.
ScriptOutcome runScripts(const QJsonObject &doc, const ScriptInput &input);

// Turns generated items into plain data: drops generatedBy from everything `script` made (every script
// when empty).
QJsonObject detachGenerated(QJsonObject doc, const QString &script = {});

// scripts/*.lua and data/** of a folder.
FileMap readScripts(const QString &dir);
FileMap readData(const QString &dir);

// The text of .drift/lua/drift.lua (LuaLS definitions) and .luarc.json.
QByteArray luaDefinitions();
QByteArray luarcJson();

} // namespace drift::projdir
