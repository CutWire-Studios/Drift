#pragma once

#include "core/project/folder/ProjectLint.h"
#include "engine/projdir/ProjectTemplate.h"

#include <QJsonArray>

namespace drift::projdir {

// Effect lookups through the engine's catalog.
LintContext engineLintContext();

// lintProject over the folder on disk, plus what needs more than the project files: template.json,
// scripts (run, with their errors as problems) and generatedBy tags that name no script.
QList<Problem> lintFolder(const QString &dir, const LintContext &context);
QList<Problem> lintContents(const FolderContents &contents, const LintContext &context);

QJsonArray problemsToJson(const QList<Problem> &problems);
bool hasErrors(const QList<Problem> &problems);

} // namespace drift::projdir
