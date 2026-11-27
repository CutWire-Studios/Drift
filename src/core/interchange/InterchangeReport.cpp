#include "core/interchange/InterchangeReport.h"

#include "core/project/Project.h"

#include <QFileInfo>

namespace drift::interchange {

void InterchangeReport::add(Severity severity, const QString &category, const QString &message,
                            const QString &itemName, TimeUs at)
{
    for (InterchangeIssue &issue : issues) {
        if (issue.severity == severity && issue.category == category && issue.message == message
            && issue.itemName == itemName) {
            ++issue.count;
            return;
        }
    }
    InterchangeIssue issue;
    issue.severity = severity;
    issue.category = category;
    issue.message = message;
    issue.itemName = itemName;
    issue.at = at;
    issues.append(issue);
}

int InterchangeReport::droppedCount() const
{
    int n = 0;
    for (const InterchangeIssue &issue : issues)
        if (issue.severity == Severity::Dropped)
            n += issue.count;
    return n;
}

void fillSummary(const Project &project, InterchangeReport &report)
{
    const auto countTracks = [&](const QList<Track> &tracks) {
        report.tracks += static_cast<int>(tracks.size());
        for (const Track &t : tracks)
            report.clips += static_cast<int>(t.clips.size());
    };
    report.tracks = 0;
    report.clips = 0;
    countTracks(project.tracks());
    const QStringList seqIds = project.sequenceIds();
    for (const QString &id : seqIds)
        countTracks(project.sequenceTracks(id));
    report.sequences = 1 + static_cast<int>(seqIds.size());

    report.assets = static_cast<int>(project.assets().size());
    report.missingAssets = 0;
    for (const MediaAsset &asset : project.assets())
        if (!asset.path.isEmpty() && !QFileInfo::exists(asset.path))
            ++report.missingAssets;
}

} // namespace drift::interchange
