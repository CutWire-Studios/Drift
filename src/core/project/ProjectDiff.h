#pragma once

#include "core/project/Project.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace drift {

// What changed between two project states, clip by clip: the change feed (`changes`) and
// `diff` both report it. Clips, tracks and assets are matched by id.
struct ClipDiff
{
    QString id;
    QString change;    // "added", "removed" or "changed"
    int track = -1;    // in the newer state ("removed": in the older one)
    int fromTrack = -1; // "changed" across tracks: where it was
    QStringList fields; // "changed": what changed (start, duration, in, out, crop, effects, ...)
    QString name;
    QString splitFrom; // "added": the clip this one was cut from, when it looks like a split
};

struct TrackDiff
{
    QString id;
    QString change; // "added", "removed" or "changed"
    int index = -1;
    QStringList fields; // "changed": name, locked, muted, hidden, solo, index, ...
};

struct AssetDiff
{
    QString id;
    QString change; // "added", "removed" or "changed"
    QString name;
};

struct ProjectDiff
{
    QList<ClipDiff> clips;
    QList<TrackDiff> tracks;
    QList<AssetDiff> assets;
    QStringList settings; // project-level fields that changed (fps, width, ...)
    bool isEmpty() const { return clips.isEmpty() && tracks.isEmpty() && assets.isEmpty() && settings.isEmpty(); }
};

ProjectDiff diffProjects(const Project &from, const Project &to);
QJsonObject projectDiffToJson(const ProjectDiff &diff);

} // namespace drift
