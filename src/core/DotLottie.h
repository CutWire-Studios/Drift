#pragma once

#include <QString>
#include <QStringList>

// .lottie (dotLottie) bundles: a zip holding manifest.json, the animations (animations/<id>.json
// or, in the v2 layout, a/<id>.json) and their images (images/ or i/). Drift does not render the
// bundle itself — it unpacks it once into a persistent folder and imports the plain Lottie JSON
// inside, so everything downstream (probe, renderer, MCP) sees an ordinary .json.

namespace drift {

bool isDotLottiePath(const QString &path);

// Unpacks into destRoot/<content hash>/: every animation as <archive name>[-<id>].json at the
// root, images under the directory name they had in the archive so the JSON's own "u" prefix
// still resolves. Idempotent — the same bundle lands in the same folder. Returns the extracted
// animation paths in manifest order; empty with `error` when the file is not a usable bundle.
QStringList unpackDotLottie(const QString &archivePath, const QString &destRoot, QString *error = nullptr);

} // namespace drift
