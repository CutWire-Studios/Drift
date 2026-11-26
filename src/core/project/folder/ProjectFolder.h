#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// A project as a folder of small, deterministic JSON files (`<name>.driftproj/`). Everything here is a
// pure function on JSON and bytes: no Project, no filesystem except the two helpers at the bottom.
namespace drift::projdir {

constexpr const char *kFolderSuffix = ".driftproj";

struct Problem
{
    QString file;
    int line = 0;
    int column = 0;
    QString message;
    QString severity = QStringLiteral("error"); // "error", "warning" or "info"
};

// Relative path (forward slashes) -> file bytes.
using FileMap = QMap<QString, QByteArray>;

// 2-space indent, `$schema`/id/type/kind/name first and the rest alphabetical, one trailing newline.
// Equal values always give equal bytes, which is what keeps git diffs minimal.
QByteArray canonicalJson(const QJsonValue &value);

// 1-based line and column of a byte offset.
void lineColumnAt(const QByteArray &bytes, int offset, int *line, int *column);

// Parses one file. On failure fills `problem` with file:line:col and returns false.
bool parseJsonFile(const QString &file, const QByteArray &bytes, QJsonValue *out, Problem *problem);

QString slugify(const QString &text, int maxLength = 40);
// A file-name-safe form of an id; ids made by Drift pass through unchanged.
QString safeId(const QString &id);

// `doc` is Project::toJson(true) plus an optional `guides` object. `rootDir` is the folder the files
// will live in: absolute media paths inside it are written relative to it.
FileMap splitProject(const QJsonObject &doc, const QString &rootDir = {});

struct JoinResult
{
    QJsonObject doc;
    QList<Problem> problems;
    bool ok = true;
};
// Inverse of splitProject. Unknown files are ignored. A file that does not parse fails the whole join
// (ok = false) so the caller keeps the state it has.
JoinResult joinProject(const FileMap &files, const QString &rootDir = {});

// Files joinProject reads: project.json, bin.json, markers.json, guides.json and everything under
// assets/ timeline/ sequences/ sidecars/ transcripts/. Empty map when `dir` is not a project folder.
FileMap readProjectFolder(const QString &dir);
bool isProjectFolder(const QString &dir);

struct WriteReport
{
    QStringList written;   // relative paths created or changed
    QStringList deleted;   // managed files that dropped out of the set
    QStringList conflicts; // files someone else changed since `known`; ours went to conflictDir
    QString error;
    QStringList changed() const { return written + deleted; }
};

// Applies `files` to `dir`: writes only files whose bytes differ, atomically, and deletes managed files
// that are no longer in the set. With `known` (what the folder held when we last looked) a file whose
// bytes on disk no longer match is not touched: the external version wins and ours is saved under
// `conflictDir`. Files on disk that `known` does not list are left alone.
WriteReport writeProjectFolder(const QString &dir, const FileMap &files, const FileMap *known = nullptr,
                               const QString &conflictDir = {});

} // namespace drift::projdir
