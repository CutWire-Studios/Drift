#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

// The .drift container: a whole project as one transferable file.
//
//   0    "DRIFTPRJ"          8   magic
//   8    containerVersion    4   uint32 LE, currently 1 — framing revision, not the format semver
//   12   manifestStored      4   uint32 LE
//   16   manifestRaw         4   uint32 LE
//   20   manifestSha256     32   over the *uncompressed* manifest
//   52   manifest            n   one zstd frame: UTF-8 JSON (see BundleInfo)
//        blobs               …   concatenated, each raw or its own zstd frame
//        blobHashes        32*N  SHA-256 of each blob's uncompressed bytes, in manifest order
//
// Blob offsets in the manifest are relative to the start of the blob region (52 + manifestStored),
// not to the file. Absolute offsets would be circular: the manifest states them, and its own
// compressed length is what shifts them.
//
// Unlike .driftpkg this is not signed — a user document is not distributed code — and it does not
// compress solidly: H.264/FLAC/JPEG bytes gain nothing from zstd, so media is stored raw at
// absolute offsets and only text (the manifest, face-track JSON) is compressed. Storing raw at
// known offsets also keeps a future read-in-place AVIO path possible.
//
// Hashes live in a trailer rather than the manifest so writing is a single pass: the layout only
// needs each file's size, which stat gives, while the digest falls out of the copy.

namespace drift::bundle {

// Bumped only by this file. Major is the compatibility gate: a reader refuses a larger major and
// warns on a larger minor.
inline constexpr int kFormatMajor = 1;
inline constexpr int kFormatMinor = 1;
inline constexpr int kFormatPatch = 0;

QString formatVersionString();

// An addon the project depends on at render time, recorded so opening elsewhere can offer to
// install it instead of silently dropping the effect.
struct AddonRef
{
    QString id;
    QString version;
    QString name;
    QStringList kinds;
};

// Source media is the only thing that can be referenced instead of embedded; derived artifacts are
// always embedded, because they live in a volatile app-data cache the user never backs up.
enum class MediaRole { Source, Matte, FaceTrack, Model3d };

QString mediaRoleToString(MediaRole role);
MediaRole mediaRoleFromString(const QString &role);

struct MediaEntry
{
    QString originalPath; // the path as the document records it; the remap key on load
    QString fileName;     // basename inside the extraction dir; unique across the bundle
    MediaRole role = MediaRole::Source;
    bool embedded = false;
    int blob = -1; // index into the blob table, -1 when referenced. Assigned by write().
};

struct BundleInfo
{
    QString formatVersion;
    QString appVersion;

    QString projectId;
    QString title;
    QString author;
    QString description;
    QDateTime createdAt;
    QDateTime modifiedAt;

    QList<AddonRef> addons;
    QList<MediaEntry> media;
    QJsonObject document; // exactly what AppController::serializeProjectJson() produced
    quint64 embeddedBytes = 0;
};

struct WriteRequest
{
    QJsonObject document;

    QString projectId;
    QString title;
    QString author;
    QString description;
    QDateTime createdAt;
    QDateTime modifiedAt;

    QList<AddonRef> addons;
    // `blob` and `fileName` are assigned by write(); callers set originalPath, role and embedded.
    QList<MediaEntry> media;
};

// Return false to abort. Aborting a write leaves any previous file at `path` untouched.
using ProgressFn = std::function<bool(qint64 done, qint64 total)>;

// Streams every embedded entry through a QSaveFile, so a cancel, a full disk or a crash cannot
// destroy the bundle being overwritten. Entries whose originalPath resolves to the same file share
// one blob.
bool write(const QString &path, const WriteRequest &request, const ProgressFn &progress,
           QString *error);

// Header + manifest only: cheap, and does not touch the blobs.
std::optional<BundleInfo> readManifest(const QString &path, QString *error);

// Unpacks every embedded blob into destDir, verifying each against the trailer as it lands, and
// fills pathRemap with originalPath -> extracted path for them. A blob already present at the
// right size is left alone, so reopening the same bundle is cheap.
bool extract(const QString &path, const QString &destDir, const ProgressFn &progress,
             QHash<QString, QString> *pathRemap, QString *error);

} // namespace drift::bundle
