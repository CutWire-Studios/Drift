#include "engine/project/ProjectBundle.h"

#include "core/project/media/ImageSequence.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

#include <zstd.h>

#include <algorithm>

namespace drift::bundle {
namespace {

constexpr char kMagic[8] = {'D', 'R', 'I', 'F', 'T', 'P', 'R', 'J'};
constexpr quint32 kContainerVersion = 1;
constexpr int kHeaderSize = 52; // magic + containerVersion + 2 lengths + manifest digest
constexpr int kDigestSize = 32;
constexpr qint64 kChunkSize = 1 << 20;
// A zstd blob is decompressed in one shot, so its raw size is an allocation an untrusted file
// controls. Anything above this is stored raw instead, and rejected on read.
constexpr quint64 kMaxCompressedBlobSize = 256ull * 1024 * 1024;

void writeU32(char *p, quint32 v)
{
    auto *b = reinterpret_cast<quint8 *>(p);
    for (int i = 0; i < 4; ++i)
        b[i] = quint8((v >> (8 * i)) & 0xff);
}

quint32 readU32(const char *p)
{
    const auto *b = reinterpret_cast<const quint8 *>(p);
    return quint32(b[0]) | (quint32(b[1]) << 8) | (quint32(b[2]) << 16) | (quint32(b[3]) << 24);
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// The extraction directory is joined with these, so a bundle must not be able to name anything
// outside it.
bool safeFileName(const QString &name)
{
    if (name.isEmpty() || name.size() > 255)
        return false;
    if (name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    return !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char('\\'))
           && !name.contains(QLatin1Char(':'));
}

bool safeRelativePath(const QString &path)
{
    if (path.size() > 4096)
        return false;
    const QStringList segments = path.split(QLatin1Char('/'));
    return std::all_of(segments.begin(), segments.end(), safeFileName);
}

// An image sequence has no file at its recorded path: its first frame stands in for it, and its
// "folder name" comes from the pattern rather than the "#start=..&fps=.." suffix.
QString documentFile(const QString &path)
{
    const auto sequence = parseImageSequencePath(path);
    return sequence ? sequence->pattern : path;
}

// The folder a document's files land in. A sequence's pattern must stay the only '%d' in the path.
QString documentFolderName(const QString &path)
{
    const auto sequence = parseImageSequencePath(path);
    return sequence ? imageSequenceFolderName(*sequence) : QFileInfo(path).completeBaseName();
}

QString backingFile(const QString &path)
{
    const auto sequence = parseImageSequencePath(path);
    return sequence ? imageSequenceFramePath(*sequence, sequence->start) : path;
}

bool isSequenceEntry(const MediaEntry &entry)
{
    return entry.resourceOf.isEmpty() && isImageSequencePath(entry.originalPath);
}

QString uniqueName(const QFileInfo &info, const QSet<QString> &used)
{
    QString name = info.fileName();
    if (name.isEmpty())
        name = QStringLiteral("media");
    // Two clips can carry the same basename from different directories.
    if (used.contains(name)) {
        const QString base = info.completeBaseName();
        const QString suffix = info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
        int n = 2;
        do {
            name = QStringLiteral("%1-%2%3").arg(base).arg(n++).arg(suffix);
        } while (used.contains(name));
    }
    return name;
}

// Already-compressed media gains nothing from zstd and costs a full CPU pass over gigabytes, so
// only text and uncompressed formats are worth it.
bool shouldCompress(MediaRole role, const QString &path, quint64 size)
{
    if (size > kMaxCompressedBlobSize)
        return false;
    if (role == MediaRole::FaceTrack)
        return true;
    static const QSet<QString> kCompressible = {
        QStringLiteral("json"), QStringLiteral("txt"),  QStringLiteral("srt"),
        QStringLiteral("vtt"),  QStringLiteral("xml"),  QStringLiteral("svg"),
        QStringLiteral("wav"),  QStringLiteral("aiff"), QStringLiteral("bmp"),
    };
    return kCompressible.contains(QFileInfo(path).suffix().toLower());
}

struct BlobRecord
{
    QString sourcePath; // write side only
    QString fileName;   // write side only
    QByteArray packed;  // write side only: compressed entries, held so they compress once
    QByteArray digest;  // write side only: set alongside `packed`, over the uncompressed bytes
    quint64 offset = 0; // relative to the blob region
    quint64 storedSize = 0;
    quint64 size = 0;
    bool compressed = false;
};

QJsonObject addonToJson(const AddonRef &addon)
{
    QJsonArray kinds;
    for (const QString &kind : addon.kinds)
        kinds.append(kind);
    return QJsonObject{
        {QStringLiteral("id"), addon.id},
        {QStringLiteral("version"), addon.version},
        {QStringLiteral("name"), addon.name},
        {QStringLiteral("kinds"), kinds},
    };
}

AddonRef addonFromJson(const QJsonObject &object)
{
    AddonRef addon;
    addon.id = object.value(QStringLiteral("id")).toString();
    addon.version = object.value(QStringLiteral("version")).toString();
    addon.name = object.value(QStringLiteral("name")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("kinds")).toArray())
        addon.kinds.append(value.toString());
    return addon;
}

// Long-distance matching lets the encoder see across a whole history of near-identical project
// states. The window stops at 2^27, the decoder's default ceiling, so no reader needs to opt in.
QByteArray compressBytes(const QByteArray &raw, int level, bool longRange = false)
{
    ZSTD_CCtx *ctx = ZSTD_createCCtx();
    if (!ctx)
        return {};
    ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, level);
    if (longRange) {
        int windowLog = 10;
        while (windowLog < 27 && (qint64(1) << windowLog) < raw.size())
            ++windowLog;
        ZSTD_CCtx_setParameter(ctx, ZSTD_c_windowLog, windowLog);
        ZSTD_CCtx_setParameter(ctx, ZSTD_c_enableLongDistanceMatching, 1);
    }
    const size_t bound = ZSTD_compressBound(size_t(raw.size()));
    QByteArray out(qsizetype(bound), Qt::Uninitialized);
    const size_t written =
        ZSTD_compress2(ctx, out.data(), bound, raw.constData(), size_t(raw.size()));
    ZSTD_freeCCtx(ctx);
    if (ZSTD_isError(written))
        return {};
    out.resize(qsizetype(written));
    return out;
}

bool decompressBytes(const QByteArray &stored, quint64 rawSize, QByteArray *out, QString *error)
{
    QByteArray raw(qsizetype(rawSize), Qt::Uninitialized);
    const size_t written = ZSTD_decompress(raw.data(), size_t(rawSize), stored.constData(),
                                           size_t(stored.size()));
    if (ZSTD_isError(written) || written != size_t(rawSize))
        return fail(error, QCoreApplication::translate("ProjectBundle", "compressed block is corrupt"));
    *out = raw;
    return true;
}

// Reads the header and manifest, verifies the manifest digest, and parses both the info and the
// blob table. `blobRegion` is where blob offsets are measured from.
bool readAll(const QString &path, QFile *file, BundleInfo *info, QList<BlobRecord> *blobs,
             qint64 *blobRegion, QString *error)
{
    file->setFileName(path);
    if (!file->open(QIODevice::ReadOnly))
        return fail(error, QCoreApplication::translate("ProjectBundle", "cannot open %1").arg(QFileInfo(path).fileName()));

    const QByteArray header = file->read(kHeaderSize);
    if (header.size() != kHeaderSize)
        return fail(error, QCoreApplication::translate("ProjectBundle", "file is too short to be a Drift project"));
    if (memcmp(header.constData(), kMagic, sizeof(kMagic)) != 0)
        return fail(error, QCoreApplication::translate("ProjectBundle", "not a Drift project (bad magic)"));
    if (readU32(header.constData() + 8) != kContainerVersion)
        return fail(error, QCoreApplication::translate("ProjectBundle", "unsupported container revision"));

    const quint32 storedSize = readU32(header.constData() + 12);
    const quint32 rawSize = readU32(header.constData() + 16);
    if (storedSize == 0 || storedSize > kMaxStoredSize || rawSize > kMaxDecompressedSize)
        return fail(error, QCoreApplication::translate("ProjectBundle", "project manifest has an implausible size"));
    const QByteArray expectedDigest = header.mid(20, kDigestSize);

    const QByteArray stored = file->read(storedSize);
    if (stored.size() != qsizetype(storedSize))
        return fail(error, QCoreApplication::translate("ProjectBundle", "project file is truncated"));
    *blobRegion = kHeaderSize + qint64(storedSize);

    QByteArray manifestJson;
    if (!decompressBytes(stored, rawSize, &manifestJson, error))
        return false;
    if (QCryptographicHash::hash(manifestJson, QCryptographicHash::Sha256) != expectedDigest)
        return fail(error, QCoreApplication::translate("ProjectBundle", "project manifest is corrupt"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(manifestJson, &parseError);
    if (!document.isObject())
        return fail(error, QCoreApplication::translate("ProjectBundle", "project manifest is not valid JSON: %1")
                               .arg(parseError.errorString()));
    const QJsonObject root = document.object();

    const QJsonObject format = root.value(QStringLiteral("format")).toObject();
    info->formatVersion = format.value(QStringLiteral("version")).toString();
    info->appVersion = format.value(QStringLiteral("appVersion")).toString();
    const int major = info->formatVersion.section(QLatin1Char('.'), 0, 0).toInt();
    if (major <= 0)
        return fail(error, QCoreApplication::translate("ProjectBundle", "project manifest has no format version"));
    if (major > kFormatMajor) {
        return fail(error, QCoreApplication::translate(
                               "ProjectBundle",
                               "this project was saved by a newer version of Drift "
                               "(format %1) — update to open it")
                               .arg(info->formatVersion));
    }

    const QJsonObject project = root.value(QStringLiteral("project")).toObject();
    info->projectId = project.value(QStringLiteral("id")).toString();
    info->title = project.value(QStringLiteral("title")).toString();
    info->author = project.value(QStringLiteral("author")).toString();
    info->description = project.value(QStringLiteral("description")).toString();
    info->createdAt =
        QDateTime::fromString(project.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    info->modifiedAt =
        QDateTime::fromString(project.value(QStringLiteral("modifiedAt")).toString(), Qt::ISODate);

    for (const QJsonValue &value : root.value(QStringLiteral("addons")).toArray())
        info->addons.append(addonFromJson(value.toObject()));

    // Entries are contiguous and ascending: extraction walks the file forward.
    quint64 expectedOffset = 0;
    for (const QJsonValue &value : root.value(QStringLiteral("blobs")).toArray()) {
        const QJsonObject object = value.toObject();
        BlobRecord blob;
        blob.offset = quint64(object.value(QStringLiteral("offset")).toDouble());
        blob.storedSize = quint64(object.value(QStringLiteral("storedSize")).toDouble());
        blob.size = quint64(object.value(QStringLiteral("size")).toDouble());
        blob.compressed =
            object.value(QStringLiteral("codec")).toString() == QLatin1String("zstd");
        if (blob.offset != expectedOffset)
            return fail(error, QCoreApplication::translate("ProjectBundle", "project blob table is not contiguous"));
        if (blob.compressed && blob.size > kMaxCompressedBlobSize)
            return fail(error, QCoreApplication::translate("ProjectBundle", "project blob table is implausible"));
        expectedOffset += blob.storedSize;
        blobs->append(blob);
    }

    for (const QJsonValue &value : root.value(QStringLiteral("media")).toArray()) {
        const QJsonObject object = value.toObject();
        MediaEntry entry;
        entry.originalPath = object.value(QStringLiteral("originalPath")).toString();
        entry.fileName = object.value(QStringLiteral("fileName")).toString();
        entry.role = mediaRoleFromString(object.value(QStringLiteral("role")).toString());
        entry.embedded =
            object.value(QStringLiteral("storage")).toString() == QLatin1String("embedded");
        entry.resourceOf = object.value(QStringLiteral("resourceOf")).toString();
        entry.blob = object.value(QStringLiteral("blob")).toInt(-1);
        if (entry.embedded) {
            if (entry.blob < 0 || entry.blob >= blobs->size())
                return fail(error, QCoreApplication::translate("ProjectBundle", "project media entry names no blob"));
            if (!safeRelativePath(entry.fileName))
                return fail(error, QCoreApplication::translate("ProjectBundle", "unsafe file name in project: %1")
                                       .arg(entry.fileName));
            info->embeddedBytes += blobs->at(entry.blob).size;
        }
        info->media.append(entry);
    }

    info->document = root.value(QStringLiteral("document")).toObject();
    if (info->document.isEmpty())
        return fail(error, QCoreApplication::translate("ProjectBundle", "project file contains no timeline"));

    // The trailer must account for exactly one digest per blob.
    const qint64 expectedSize =
        *blobRegion + qint64(expectedOffset) + qint64(blobs->size()) * kDigestSize;
    if (file->size() != expectedSize)
        return fail(error, QCoreApplication::translate("ProjectBundle", "project file is truncated or has trailing data"));

    return true;
}

} // namespace

QString formatVersionString()
{
    return QStringLiteral("%1.%2.%3").arg(kFormatMajor).arg(kFormatMinor).arg(kFormatPatch);
}

QString mediaRoleToString(MediaRole role)
{
    switch (role) {
    case MediaRole::Matte:
        return QStringLiteral("matte");
    case MediaRole::FaceTrack:
        return QStringLiteral("facetrack");
    case MediaRole::Model3d:
        return QStringLiteral("model3d");
    case MediaRole::Depth:
        return QStringLiteral("depth");
    case MediaRole::Stabilized:
        return QStringLiteral("stabilized");
    case MediaRole::Source:
        break;
    }
    return QStringLiteral("source");
}

MediaRole mediaRoleFromString(const QString &role)
{
    if (role == QLatin1String("matte"))
        return MediaRole::Matte;
    if (role == QLatin1String("facetrack"))
        return MediaRole::FaceTrack;
    if (role == QLatin1String("model3d"))
        return MediaRole::Model3d;
    if (role == QLatin1String("depth"))
        return MediaRole::Depth;
    if (role == QLatin1String("stabilized"))
        return MediaRole::Stabilized;
    return MediaRole::Source;
}

QByteArray compressText(const QByteArray &raw, int level)
{
    return compressBytes(raw, level, true);
}

bool isCompressedText(const QByteArray &data)
{
    return data.size() >= 4 && quint8(data[0]) == 0x28 && quint8(data[1]) == 0xB5
        && quint8(data[2]) == 0x2F && quint8(data[3]) == 0xFD;
}

bool decompressText(const QByteArray &stored, QByteArray *out, QString *error)
{
    if (quint64(stored.size()) > kMaxStoredSize)
        return fail(error, QCoreApplication::translate("ProjectBundle", "compressed block is too large"));
    const unsigned long long rawSize = ZSTD_getFrameContentSize(stored.constData(), size_t(stored.size()));
    if (rawSize == ZSTD_CONTENTSIZE_UNKNOWN || rawSize == ZSTD_CONTENTSIZE_ERROR
        || rawSize > kMaxDecompressedSize)
        return fail(error, QCoreApplication::translate("ProjectBundle", "compressed block is corrupt"));
    return decompressBytes(stored, rawSize, out, error);
}

bool write(const QString &path, const WriteRequest &request, const ProgressFn &progress,
           QString *error)
{
    QList<MediaEntry> media = request.media;
    QList<BlobRecord> blobs;
    QHash<QString, int> blobByPath; // same file named twice shares one blob
    QSet<QString> usedNames;
    quint64 offset = 0;
    quint64 totalRaw = 0;

    // Media that is already gone cannot be embedded. Degrade to a reference rather than refusing
    // the save: the bytes are lost either way, and blocking the save loses the edit too.
    QSet<QString> embeddedDocuments;
    for (MediaEntry &entry : media) {
        if (entry.embedded && !(QFileInfo(backingFile(entry.originalPath)).isFile()
                                && QFileInfo(backingFile(entry.originalPath)).isReadable()))
            entry.embedded = false;
        if (entry.embedded && entry.resourceOf.isEmpty())
            embeddedDocuments.insert(entry.originalPath);
    }
    // A resource travels only with its document: a referenced document still finds it beside it.
    QHash<QString, QString> documentFolder;
    for (MediaEntry &entry : media) {
        if (entry.resourceOf.isEmpty())
            continue;
        entry.embedded = entry.embedded && embeddedDocuments.contains(entry.resourceOf);
        if (entry.embedded)
            documentFolder.insert(entry.resourceOf, QString());
    }
    // Each document with resources gets a folder of its own, so two documents that both name
    // "images/img_0.png" cannot collide. Reserved before any flat name is picked.
    for (auto it = documentFolder.begin(); it != documentFolder.end(); ++it) {
        const QString folder = uniqueName(QFileInfo(documentFolderName(it.key())), usedNames);
        usedNames.insert(folder);
        it.value() = folder;
    }

    for (MediaEntry &entry : media) {
        if (!entry.embedded)
            continue;

        const QFileInfo info(backingFile(entry.originalPath));
        QString layoutName; // fixed by the document's layout rather than picked freely
        if (!entry.resourceOf.isEmpty()) {
            layoutName = documentFolder.value(entry.resourceOf) + QLatin1Char('/')
                         + QDir(QFileInfo(documentFile(entry.resourceOf)).absolutePath())
                               .relativeFilePath(info.absoluteFilePath());
        } else if (documentFolder.contains(entry.originalPath)) {
            layoutName = documentFolder.value(entry.originalPath) + QLatin1Char('/')
                         + QFileInfo(documentFile(entry.originalPath)).fileName();
        }
        if (!layoutName.isEmpty() && !safeRelativePath(layoutName)) {
            entry.embedded = false;
            continue;
        }

        const QString key = info.canonicalFilePath();
        const auto existing = blobByPath.constFind(key);
        if (existing != blobByPath.constEnd()) {
            entry.blob = existing.value();
            entry.fileName = layoutName.isEmpty() ? blobs.at(entry.blob).fileName : layoutName;
            continue;
        }

        BlobRecord blob;
        blob.sourcePath = info.absoluteFilePath();
        blob.size = quint64(info.size());
        blob.compressed = shouldCompress(entry.role, info.filePath(), blob.size);
        blob.offset = offset;
        blob.storedSize = blob.size; // raw entries store what they are

        if (blob.compressed) {
            QFile source(blob.sourcePath);
            if (!source.open(QIODevice::ReadOnly)) {
                entry.embedded = false;
                continue;
            }
            const QByteArray raw = source.readAll();
            blob.packed = compressBytes(raw, 10);
            if (blob.packed.isEmpty() && !raw.isEmpty())
                return fail(error, QCoreApplication::translate("ProjectBundle", "cannot compress %1").arg(info.fileName()));
            blob.digest = QCryptographicHash::hash(raw, QCryptographicHash::Sha256);
            blob.size = quint64(raw.size()); // authoritative: stat can lag a just-written file
            blob.storedSize = quint64(blob.packed.size());
        }

        QString name = layoutName;
        if (name.isEmpty()) {
            name = uniqueName(info, usedNames);
            usedNames.insert(name);
        }
        blob.fileName = name;

        entry.blob = blobs.size();
        entry.fileName = name;
        blobByPath.insert(key, entry.blob);
        blobs.append(blob);
        offset += blob.storedSize;
        totalRaw += blob.size;
    }

    QJsonArray blobArray;
    for (const BlobRecord &blob : blobs) {
        blobArray.append(QJsonObject{
            {QStringLiteral("offset"), double(blob.offset)},
            {QStringLiteral("storedSize"), double(blob.storedSize)},
            {QStringLiteral("size"), double(blob.size)},
            {QStringLiteral("codec"),
             blob.compressed ? QStringLiteral("zstd") : QStringLiteral("raw")},
        });
    }

    QJsonArray mediaArray;
    for (const MediaEntry &entry : media) {
        QJsonObject object{
            {QStringLiteral("originalPath"), entry.originalPath},
            {QStringLiteral("role"), mediaRoleToString(entry.role)},
            {QStringLiteral("storage"),
             entry.embedded ? QStringLiteral("embedded") : QStringLiteral("reference")},
        };
        if (!entry.resourceOf.isEmpty())
            object.insert(QStringLiteral("resourceOf"), entry.resourceOf);
        if (entry.embedded) {
            object.insert(QStringLiteral("fileName"), entry.fileName);
            object.insert(QStringLiteral("blob"), entry.blob);
        }
        mediaArray.append(object);
    }

    QJsonArray addonArray;
    for (const AddonRef &addon : request.addons)
        addonArray.append(addonToJson(addon));

    const QJsonObject manifest{
        {QStringLiteral("format"),
         QJsonObject{
             {QStringLiteral("version"), formatVersionString()},
             {QStringLiteral("app"), QStringLiteral("Drift")},
             {QStringLiteral("appVersion"), QStringLiteral(DRIFT_VERSION)},
         }},
        {QStringLiteral("project"),
         QJsonObject{
             {QStringLiteral("id"), request.projectId},
             {QStringLiteral("title"), request.title},
             {QStringLiteral("author"), request.author},
             {QStringLiteral("description"), request.description},
             {QStringLiteral("createdAt"), request.createdAt.toString(Qt::ISODate)},
             {QStringLiteral("modifiedAt"), request.modifiedAt.toString(Qt::ISODate)},
         }},
        {QStringLiteral("addons"), addonArray},
        {QStringLiteral("blobs"), blobArray},
        {QStringLiteral("media"), mediaArray},
        {QStringLiteral("document"), request.document},
    };

    const QByteArray manifestJson = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    if (quint64(manifestJson.size()) > kMaxDecompressedSize)
        return fail(error, QCoreApplication::translate("ProjectBundle", "project is too large to save"));
    const QByteArray manifestStored = compressText(manifestJson, 10);
    if (manifestStored.isEmpty())
        return fail(error, QCoreApplication::translate("ProjectBundle", "cannot compress the project manifest"));
    if (quint64(manifestStored.size()) > kMaxStoredSize)
        return fail(error, QCoreApplication::translate("ProjectBundle", "project is too large to save"));

    QSaveFile out(path);
    // Over the documents portal the sibling temp file QSaveFile normally writes may be refused, and
    // the saved project must land at the path the portal handed us — never beside it. Writing the
    // target directly is the correct outcome there; elsewhere the atomic temp-and-rename still runs.
    out.setDirectWriteFallback(true);
    if (!out.open(QIODevice::WriteOnly))
        return fail(error, QCoreApplication::translate("ProjectBundle", "cannot write %1").arg(QFileInfo(path).fileName()));

    QByteArray header(kHeaderSize, Qt::Uninitialized);
    memcpy(header.data(), kMagic, sizeof(kMagic));
    writeU32(header.data() + 8, kContainerVersion);
    writeU32(header.data() + 12, quint32(manifestStored.size()));
    writeU32(header.data() + 16, quint32(manifestJson.size()));
    memcpy(header.data() + 20, QCryptographicHash::hash(manifestJson, QCryptographicHash::Sha256)
                                   .constData(),
           kDigestSize);
    if (out.write(header) != kHeaderSize || out.write(manifestStored) != manifestStored.size())
        return fail(error, QCoreApplication::translate("ProjectBundle", "cannot write the project manifest"));

    QByteArray hashes;
    hashes.reserve(blobs.size() * kDigestSize);
    qint64 done = 0;

    for (const BlobRecord &blob : blobs) {
        if (blob.compressed) {
            // Compressed and hashed during planning, so nothing is read or compressed twice.
            if (out.write(blob.packed) != blob.packed.size())
                return fail(error, QCoreApplication::translate("ProjectBundle", "cannot write project data"));
            hashes.append(blob.digest);
            done += qint64(blob.size);
            if (progress && !progress(done, qint64(totalRaw)))
                return fail(error, QCoreApplication::translate("ProjectBundle", "Cancelled"));
            continue;
        }

        QFile source(blob.sourcePath);
        if (!source.open(QIODevice::ReadOnly))
            return fail(error, QCoreApplication::translate("ProjectBundle", "cannot read %1").arg(blob.sourcePath));

        QCryptographicHash digest(QCryptographicHash::Sha256);
        quint64 remaining = blob.size;
        while (remaining > 0) {
            const QByteArray chunk = source.read(qMin<qint64>(kChunkSize, qint64(remaining)));
            if (chunk.isEmpty())
                return fail(error, QCoreApplication::translate("ProjectBundle", "%1 changed while saving").arg(blob.sourcePath));
            digest.addData(chunk);
            if (out.write(chunk) != chunk.size())
                return fail(error, QCoreApplication::translate("ProjectBundle", "cannot write project data"));
            remaining -= quint64(chunk.size());
            done += chunk.size();
            if (progress && !progress(done, qint64(totalRaw)))
                return fail(error, QCoreApplication::translate("ProjectBundle", "Cancelled"));
        }
        hashes.append(digest.result());
    }

    if (out.write(hashes) != hashes.size())
        return fail(error, QCoreApplication::translate("ProjectBundle", "cannot write project data"));
    if (!out.commit())
        return fail(error, QCoreApplication::translate("ProjectBundle", "cannot finish writing %1").arg(QFileInfo(path).fileName()));
    return true;
}

std::optional<BundleInfo> readManifest(const QString &path, QString *error)
{
    QFile file;
    BundleInfo info;
    QList<BlobRecord> blobs;
    qint64 blobRegion = 0;
    if (!readAll(path, &file, &info, &blobs, &blobRegion, error))
        return std::nullopt;
    return info;
}

bool extract(const QString &path, const QString &destDir, const ProgressFn &progress,
             QHash<QString, QString> *pathRemap, QString *error)
{
    QFile file;
    BundleInfo info;
    QList<BlobRecord> blobs;
    qint64 blobRegion = 0;
    if (!readAll(path, &file, &info, &blobs, &blobRegion, error))
        return false;

    if (!QDir().mkpath(destDir))
        return fail(error, QCoreApplication::translate("ProjectBundle", "cannot create %1").arg(destDir));

    const qint64 hashesAt = blobRegion
                            + (blobs.isEmpty() ? 0
                                               : qint64(blobs.constLast().offset
                                                        + blobs.constLast().storedSize));
    if (!file.seek(hashesAt))
        return fail(error, QCoreApplication::translate("ProjectBundle", "project file is truncated"));
    const QByteArray hashes = file.read(qint64(blobs.size()) * kDigestSize);
    if (hashes.size() != qint64(blobs.size()) * kDigestSize)
        return fail(error, QCoreApplication::translate("ProjectBundle", "project file is truncated"));

    // One blob can land under several names: a file that is both standalone media and a
    // document's resource has to exist in both places.
    qint64 total = 0;
    QSet<QString> targets;
    for (const MediaEntry &entry : info.media) {
        if (entry.embedded && !isSequenceEntry(entry) && !targets.contains(entry.fileName)) {
            targets.insert(entry.fileName);
            total += qint64(blobs.at(entry.blob).size);
        }
    }

    qint64 done = 0;
    QSet<QString> extracted;
    for (const MediaEntry &entry : info.media) {
        if (!entry.embedded)
            continue;

        const BlobRecord &blob = blobs.at(entry.blob);
        const QString target = QDir(destDir).filePath(entry.fileName);
        if (isSequenceEntry(entry)) {
            // Only the path is remapped; the frames land through their own resource entries.
            if (pathRemap) {
                ImageSequence sequence = *parseImageSequencePath(entry.originalPath);
                sequence.pattern = target;
                pathRemap->insert(entry.originalPath, imageSequencePath(sequence));
            }
            continue;
        }
        if (pathRemap && entry.resourceOf.isEmpty())
            pathRemap->insert(entry.originalPath, target);
        if (extracted.contains(entry.fileName))
            continue;
        extracted.insert(entry.fileName);

        // Reopening the same bundle should not re-extract gigabytes it already unpacked.
        const QFileInfo existing(target);
        if (existing.isFile() && existing.size() == qint64(blob.size)) {
            done += qint64(blob.size);
            if (progress && !progress(done, total))
                return fail(error, QCoreApplication::translate("ProjectBundle", "Cancelled"));
            continue;
        }

        if (!file.seek(blobRegion + qint64(blob.offset)))
            return fail(error, QCoreApplication::translate("ProjectBundle", "project file is truncated"));
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return fail(error, QCoreApplication::translate("ProjectBundle", "cannot create %1").arg(QFileInfo(target).absolutePath()));

        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly))
            return fail(error, QCoreApplication::translate("ProjectBundle", "cannot write %1").arg(entry.fileName));

        QCryptographicHash digest(QCryptographicHash::Sha256);
        if (blob.compressed) {
            const QByteArray stored = file.read(qint64(blob.storedSize));
            if (stored.size() != qint64(blob.storedSize))
                return fail(error, QCoreApplication::translate("ProjectBundle", "project file is truncated"));
            QByteArray raw;
            if (!decompressBytes(stored, blob.size, &raw, error))
                return false;
            digest.addData(raw);
            if (out.write(raw) != raw.size())
                return fail(error, QCoreApplication::translate("ProjectBundle", "cannot write %1").arg(entry.fileName));
            done += raw.size();
            if (progress && !progress(done, total))
                return fail(error, QCoreApplication::translate("ProjectBundle", "Cancelled"));
        } else {
            quint64 remaining = blob.size;
            while (remaining > 0) {
                const QByteArray chunk = file.read(qMin<qint64>(kChunkSize, qint64(remaining)));
                if (chunk.isEmpty())
                    return fail(error, QCoreApplication::translate("ProjectBundle", "project file is truncated"));
                digest.addData(chunk);
                if (out.write(chunk) != chunk.size())
                    return fail(error, QCoreApplication::translate("ProjectBundle", "cannot write %1").arg(entry.fileName));
                remaining -= quint64(chunk.size());
                done += chunk.size();
                if (progress && !progress(done, total))
                    return fail(error, QCoreApplication::translate("ProjectBundle", "Cancelled"));
            }
        }

        if (digest.result() != hashes.mid(entry.blob * kDigestSize, kDigestSize))
            return fail(error, QCoreApplication::translate("ProjectBundle", "%1 is corrupt in this project").arg(entry.fileName));
        if (!out.commit())
            return fail(error, QCoreApplication::translate("ProjectBundle", "cannot finish writing %1").arg(entry.fileName));
    }

    return true;
}

} // namespace drift::bundle
