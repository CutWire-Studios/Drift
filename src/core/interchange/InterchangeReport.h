#pragma once

#include "core/base/Time.h"

#include <QList>
#include <QString>

namespace drift {

class Project;

namespace interchange {

struct InterchangeIssue
{
    enum class Severity { Info, Degraded, Dropped, Error };

    Severity severity = Severity::Info;
    QString category;
    QString message;
    QString itemName;
    TimeUs at = -1;
    int count = 1;
};

struct InterchangeReport
{
    using Severity = InterchangeIssue::Severity;

    QString formatId;
    int tracks = 0;
    int clips = 0;
    int sequences = 0;
    int assets = 0;
    int missingAssets = 0;
    QList<InterchangeIssue> issues;

    void add(Severity severity, const QString &category, const QString &message, const QString &itemName = {},
             TimeUs at = -1);

    int droppedCount() const;
};

void fillSummary(const Project &project, InterchangeReport &report);

} // namespace interchange
} // namespace drift
