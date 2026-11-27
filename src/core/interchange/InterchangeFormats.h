#pragma once

#include "core/interchange/InterchangeReport.h"
#include "core/project/Project.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace drift::interchange {

struct InterchangeFormat
{
    enum class Stability { Stable, Beta, Experimental };

    QString id;
    QString displayName;
    QStringList extensions;
    bool canImport = false;
    bool canExport = false;
    Stability stability = Stability::Experimental;
    QStringList knownGaps;
};

// Import rows come first, then export rows. A format that does both (edl, otio, fcpxml via resolve)
// has one row per direction.
const QList<InterchangeFormat> &interchangeFormats();

// Returns the format id of an importable file, or an empty string if it is not recognised.
QString detectImportFormat(const QString &path);

// Parses path as formatId into a Project, filling report (when given) with what was lost.
// .mogrt is not handled here.
std::optional<Project> readImport(const QString &formatId, const QString &path, InterchangeReport *report,
                                  QString *error);

// Dry-run of an export: walks the project's current timeline and reports what formatId
// ("fcpxml", "edl", "otio" or "tsv") would not carry over. Nothing is written.
InterchangeReport analyzeExport(const Project &project, const QString &formatId);

} // namespace drift::interchange
