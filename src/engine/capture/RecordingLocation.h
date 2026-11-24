#pragma once

#include <QDateTime>
#include <QString>

#include <optional>

namespace drift {

// Where Drift saves the screen and camera recordings it makes (Settings > Capture).
//
// A proxy or a reversed render can go in the cache because it is derived: delete it and Drift
// rebuilds it. A recording is the opposite — it is the only copy of something that happened once,
// so the cache is deliberately not an option here, and nor is any directory Drift sweeps.
enum class RecordingLocation { Movies, Project, Folder };

QString recordingLocationToString(RecordingLocation location); // "movies" | "project" | "folder"
std::optional<RecordingLocation> recordingLocationFromString(const QString &text);

struct RecordingTarget
{
    enum class Where { Movies, Project, Folder };
    Where where = Where::Movies;
    QString path;                    // empty when nowhere would take the write
    bool folderUnavailable = false;  // Folder mode: missing, or refused the write
    bool projectUnavailable = false; // Project mode: project never saved, or its folder refused
};

// The one rule for where a recording goes. `fileName` is the leaf the caller wants, from
// recordingFileName(). `projectPath` is the open project's file, empty when it has never been
// saved — which is the common case when someone hits record on a fresh project, hence the flag
// rather than a failure.
//
// A mode that cannot be honoured falls back to the Movies folder. Falling back to the cache would
// be worse than useless: the recording would work, appear on the timeline, and then vanish the
// next time the cache is cleared. The user's chosen folder is never created, since a missing one
// usually means an unmounted drive; the project's own Recordings subfolder is, because a project
// directory that exists but lacks that subfolder is simply the first recording of the project.
RecordingTarget chooseRecordingTarget(RecordingLocation mode, const QString &folder,
                                      const QString &projectPath, const QString &fileName);

// The Movies-mode directory: an app-named subfolder of the OS movies location, so recordings are
// grouped rather than strewn among the user's own videos. Created on demand.
QString moviesRecordingDir();

// The leaf name for a recording: "<kind> <yyyy-MM-dd HH-mm-ss>.<extension>", e.g.
// "Screen 2026-11-24 14-03-09.mp4". Local time, because the only way anyone looks for a recording
// is by when they made it. Colons are out — Windows rejects them and macOS shows them as slashes.
QString recordingFileName(const QString &kind, const QDateTime &when, const QString &extension);

// `dir`/`fileName`, with " 2", " 3", … inserted before the suffix until nothing is in the way.
// Two recordings can start inside the same second — a screen capture and its webcam companion do
// exactly that — and a name collision there would have one overwrite the other.
QString uniqueRecordingPath(const QString &dir, const QString &fileName);

} // namespace drift
