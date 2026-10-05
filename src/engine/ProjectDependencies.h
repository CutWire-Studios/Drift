#pragma once

#include "ProjectBundle.h"

#include <QHash>
#include <QList>
#include <QString>

namespace drift {
class Project;
}

// What a project needs from outside its own document, in the shape ProjectBundle wants: every file
// it points at, and every addon that must be installed for it to render as saved.

namespace drift::bundle {

// Every external file the project references, in document order: source media first, then the
// derived artifacts (mattes, face tracks, depth maps). `embedded` is preset — derived
// artifacts are always embedded, source media follows `embedSource` — and callers may still flip
// source entries. Files a Lottie/SVG document loads from beside itself follow as extra entries
// with `resourceOf` set and their document's `embedded`; flip them together with it.
//
// Thumbnails and filmstrips are deliberately absent: they are cache renders regenerated on load.
QList<MediaEntry> collectMedia(const Project &project, bool embedSource);

// Addons that must be present for the project to render as saved. Resolved by tracing each catalog
// entry the project uses back to the install directory it came from, so an effect that ships with
// the build contributes nothing. Baked artifacts (subtitle cues, mattes, face tracks) create no
// dependency — the model that produced them is not needed again.
QList<AddonRef> collectAddons(const Project &project);

// Gathers the files behind `media` (as collectMedia returns them) under destDir, each in the
// subfolder `subfolders` names for its originalPath ("Other" when absent), and fills pathRemap
// with originalPath -> new path for the caller to relink. A Lottie/SVG document that loads files
// from beside itself gets a folder of its own so those keep their relative layout; they get no
// remap entry, as in a bundle.
//
// A name already taken in destDir is reused when its bytes hash the same, so collecting twice
// copies nothing, and otherwise the file lands under a " (2)" suffix — nothing there is ever
// overwritten. Files already inside destDir, missing files, 3D models and files an addon owns
// are left where they are.
//
// `move` renames where the filesystem allows and deletes the originals once everything has
// landed; a failure or a cancel undoes the renames and removes what this run created, so either
// the whole set is collected or the project's files are where they were. An original that cannot
// be deleted afterwards is counted in undeletedOriginals rather than failing the run: the project
// is already consistent with the new folder.
bool collectToFolder(const QList<MediaEntry> &media, const QHash<QString, QString> &subfolders,
                     const QString &destDir, bool move, const ProgressFn &progress,
                     QHash<QString, QString> *pathRemap, int *undeletedOriginals, QString *error);

} // namespace drift::bundle
