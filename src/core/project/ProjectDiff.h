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
    QString sequence;  // empty: the main timeline; else the composite's sequence id
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
    QString sequence;
    QString change; // "added", "removed" or "changed"
    int index = -1;
    QStringList fields; // "changed": name, locked, muted, hidden, solo, exclude_from_export, audio, index, transitions, view
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
    // Project-level state that changed: fps, resolution, sample_rate, background, bookmarks,
    // work_area, project_info, sequences, bin_folders. (Which composites are open as tabs is view state.)
    QStringList settings;
    bool isEmpty() const { return clips.isEmpty() && tracks.isEmpty() && assets.isEmpty() && settings.isEmpty(); }
};

// Every timeline is compared (the main one and each composite's), whichever is open.
ProjectDiff diffProjects(const Project &from, const Project &to);
QJsonObject projectDiffToJson(const ProjectDiff &diff);

} // namespace drift
