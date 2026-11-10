#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>

namespace drift {

class Project;

// Bumped when the bytes a bake would hold for the same project change, so older bakes read as stale.
constexpr int kBakeSchemaVersion = 1;

// SHA-256 of everything that decides what frames [startFrame, endFrame) of the main timeline
// look like: canvas size, fps, background, every track's render properties, and every clip and
// transition whose span overlaps the range (a partial overlap counts whole), recursing into the
// composites those clips play, plus each source file's size and mtime. Audio-only and UI-only
// fields are left out so renaming a track or changing a volume does not stale a bake.
QByteArray bakeFingerprint(const Project &project, qint64 startFrame, qint64 endFrame);

// Same, for several ranges; serialises the project once.
QList<QByteArray> bakeFingerprints(const Project &project, const QList<QPair<qint64, qint64>> &ranges);

} // namespace drift
