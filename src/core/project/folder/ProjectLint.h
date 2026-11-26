#pragma once

#include "core/project/folder/ProjectFolder.h"

#include <QStringList>

#include <functional>
#include <optional>

namespace drift::projdir {

// What the lint needs from the effect catalog, which lives in the engine. Without it the effect
// checks are skipped.
struct LintContext
{
    // Parameter keys of the effect `catalogId`, or nullopt when there is no such effect.
    std::function<std::optional<QStringList>(const QString &catalogId)> effectParams;
    // Check that media files exist (relative paths resolve against the folder).
    bool checkMedia = true;
};

// Everything the JSON Schema cannot say: unknown asset, track and clip ids, overlaps on a track,
// keyframes outside their clip, unknown effects and parameters, sidecars that belong to no clip.
// Also runs the structural checks (types, enums, unknown keys) so the result is complete without a
// schema-aware editor. Problems carry file:line:column.
QList<Problem> lintProject(const FileMap &files, const QString &rootDir, const LintContext &context = {});

// "file:line:col: severity: message", one per line.
QString formatProblems(const QList<Problem> &problems);

// 1-based position of the first `"key": value-text` in `bytes`, for pointing a semantic problem at the
// line that holds it; 1:1 when there is no match.
void locateKey(const QByteArray &bytes, const QString &key, const QString &value, int *line, int *column);

} // namespace drift::projdir
