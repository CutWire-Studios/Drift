#include "AssetLibrary.h"

#include "core/Project.h"

#include "engine/MediaProbe.h"
#include "engine/MediaThumbnail.h"
#include "engine/VectorInspect.h"
#include "core/DotLottie.h"

#ifdef Q_OS_ANDROID
#include "engine/AndroidUri.h"
#include <QCryptographicHash>
#include <QStandardPaths>
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageIOHandler>
#include "engine/StillImage.h"

#include <QImageReader>
#include <QJsonObject>
#include <QMetaObject>
#include <QUrl>
#include <QUuid>
#include <QFutureWatcher>
#include <QtConcurrent>

#include <algorithm>
#include <optional>

namespace {

#ifdef Q_OS_ANDROID

constexpr qint64 kImportChunkBytes = 1024 * 1024;

// Copies of SAF documents. AppDataLocation and not CacheLocation: a saved project points
// straight at these files, and Android reclaims the cache under storage pressure while
// Settings > Clear cache wipes it outright.
QString importsDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/imports");
}

QString sanitizedImportFileName(QString name)
{
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("import.bin");
    return name;
}

// FFmpeg and the rest of the media pipeline need real filesystem paths. On Android the SAF
// picker returns content:// URIs; Qt can read them via QFile, but avformat cannot. Copy into
// app storage once so the rest of the import pipeline stays path-based.
QString materializeImportUrl(const QUrl &url)
{
    if (!AndroidUri::isContentUri(url))
        return {};

    const QString uri = url.toString(QUrl::FullyEncoded);
    std::unique_ptr<QFile> src = AndroidUri::openForRead(url);
    if (!src) {
        qWarning("import: cannot open %s", qPrintable(uri));
        return {};
    }

    // The grant that came with the picker result dies with the process, which would make the URI
    // worthless if anything ever needs to re-read it after this session.
    AndroidUri::takePersistableReadPermission(url);

    // The same file picked through Photos, Files and a cloud provider arrives as three unrelated
    // URIs, so the URI cannot key the copy: the head of the stream and its length can, and the
    // head is a chunk of the copy that is about to happen anyway.
    const QByteArray head = src->read(kImportChunkBytes);
    if (head.isEmpty() && src->error() != QFile::NoError) {
        qWarning("import: read failed for %s (%s)", qPrintable(uri), qPrintable(src->errorString()));
        return {};
    }
    QCryptographicHash key(QCryptographicHash::Sha1);
    key.addData(head);
    key.addData(QByteArray::number(src->size()));

    // One directory per file so the copy can keep the document's real name — the bin, the clip
    // labels and the export default all show it, and provisionalKind reads the kind off its suffix.
    const QString destDir = importsDir() + QLatin1Char('/')
                            + QString::fromLatin1(key.result().left(8).toHex());
    const QString destPath =
        destDir + QLatin1Char('/') + sanitizedImportFileName(AndroidUri::displayName(url));
    if (QFileInfo::exists(destPath))
        return destPath;

    if (!QDir().mkpath(destDir)) {
        qWarning("import: cannot create %s", qPrintable(destDir));
        return {};
    }

    // Copy aside and rename, so a process death mid-copy cannot leave a truncated file that a
    // later import of the same media would reuse.
    const QString partPath = destPath + QStringLiteral(".part");
    QFile dst(partPath);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("import: cannot write %s (%s)", qPrintable(partPath), qPrintable(dst.errorString()));
        return {};
    }

    QByteArray chunk = head;
    while (!chunk.isEmpty()) {
        if (dst.write(chunk) != chunk.size()) {
            qWarning("import: write failed for %s (%s)", qPrintable(partPath),
                     qPrintable(dst.errorString()));
            dst.remove();
            return {};
        }
        chunk = src->read(kImportChunkBytes);
        if (chunk.isEmpty() && src->error() != QFile::NoError) {
            qWarning("import: read failed for %s (%s)", qPrintable(uri),
                     qPrintable(src->errorString()));
            dst.remove();
            return {};
        }
    }

    dst.close();
    if (!dst.rename(destPath)) {
        qWarning("import: cannot finish %s (%s)", qPrintable(destPath), qPrintable(dst.errorString()));
        dst.remove();
        return {};
    }
    return destPath;
}

#endif // Q_OS_ANDROID

// The one place that decides what counts as media. The picker's name filter, the folder-import
// walk and the provisional kind guess all read these lists, so a format can no longer be offered
// by one entry point and skipped by another.
// Containers FFmpeg demuxes, not everything it can be made to open: its own extension table is
// no help here, since half of these demuxers probe instead of matching on a suffix (mpegts and
// mpeg declare none at all) while the ones that do declare cover raw streams, subtitles and
// tracker music. Anything not here can still be dragged onto the bin, where the probe decides.
const QStringList &videoExtensions()
{
    static const QStringList extensions = {
        // MP4 / QuickTime family
        QStringLiteral("mp4"),  QStringLiteral("m4v"),  QStringLiteral("mov"),
        QStringLiteral("3gp"),  QStringLiteral("3g2"),
        // Matroska
        QStringLiteral("mkv"),  QStringLiteral("webm"),
        // AVI / ASF
        QStringLiteral("avi"),  QStringLiteral("wmv"),  QStringLiteral("asf"),
        QStringLiteral("divx"),
        // Flash
        QStringLiteral("flv"),  QStringLiteral("f4v"),
        // MPEG program and transport streams
        QStringLiteral("mpg"),  QStringLiteral("mpeg"), QStringLiteral("m2v"),
        QStringLiteral("ts"),   QStringLiteral("m2ts"), QStringLiteral("mts"),
        QStringLiteral("m2t"),  QStringLiteral("vob"),
        // Ogg, RealMedia
        QStringLiteral("ogv"),  QStringLiteral("rm"),   QStringLiteral("rmvb"),
        // Broadcast and camera
        QStringLiteral("mxf"),  QStringLiteral("dv"),   QStringLiteral("y4m"),
    };
    return extensions;
}

const QStringList &audioExtensions()
{
    static const QStringList extensions = {
        QStringLiteral("mp3"),  QStringLiteral("wav"),  QStringLiteral("aac"),
        QStringLiteral("flac"), QStringLiteral("ogg"),  QStringLiteral("m4a"),
        QStringLiteral("wma"),  QStringLiteral("aiff"), QStringLiteral("aif"),
    };
    return extensions;
}

// Canonical list lives in core so the engine and the project importers can share it.
const QStringList &imageExtensions()
{
    return drift::imageExtensions();
}

drift::MediaKind kindFrom(const MediaInfo &info, const QString &path)
{
    if (AssetLibrary::isImagePath(path))
        return drift::MediaKind::Image;

    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Video && !stream.attachedPicture)
            return drift::MediaKind::Video;
    }
    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Audio)
            return drift::MediaKind::Audio;
    }
    return drift::MediaKind::Other;
}

drift::MediaKind provisionalKind(const QString &path)
{
    if (AssetLibrary::isVectorPath(path))
        return drift::MediaKind::Vector;
    if (AssetLibrary::isImagePath(path))
        return drift::MediaKind::Image;
    if (AssetLibrary::isAudioPath(path))
        return drift::MediaKind::Audio;
    return drift::MediaKind::Video;
}

QString formatDuration(drift::TimeUs durationUs)
{
    if (durationUs <= 0)
        return {};

    const int totalSeconds = static_cast<int>(durationUs / drift::kUsPerSecond);
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;

    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QChar('0'))
            .arg(seconds, 2, 10, QChar('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

// MediaAsset carries one sampleRate/channels pair for the whole file, so a multi-stream source
// has to pick one. It describes the *first* audio stream, matching Clip::audioStreamIndex's
// default of 0 and ensureAudioPresence(), which also breaks on the first. This used to keep
// overwriting with each stream in turn and end up describing the last one, so the same asset
// reported different channel counts depending on which path had populated it.
void fillAudioPresence(drift::MediaAsset &asset, const MediaInfo &info)
{
    bool hasAudio = false;
    for (const StreamInfo &stream : info.streams) {
        if (stream.type != StreamInfo::Type::Audio)
            continue;
        hasAudio = true;
        asset.sampleRate = stream.sampleRate;
        asset.channels = stream.channels;
        if (asset.codecName.isEmpty())
            asset.codecName = stream.codecName;
        break;
    }
    asset.hasAudio = hasAudio;
    asset.hasAudioKnown = true;
}

drift::MediaAsset buildProbedAsset(const QString &absolutePath, const QString &name, const MediaInfo &info)
{
    drift::MediaAsset asset;
    asset.name = name;
    asset.path = absolutePath;
    asset.kind = kindFrom(info, absolutePath);
    asset.durationUs = info.durationUs;
    asset.durationLabel =
        asset.kind == drift::MediaKind::Image ? QString() : formatDuration(info.durationUs);

    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Video && !stream.attachedPicture) {
            asset.width = stream.width;
            asset.height = stream.height;
            asset.fps = stream.fps;
            asset.rotationDegrees = stream.rotationDegrees;
            asset.codecName = stream.codecName;
        }
    }
    fillAudioPresence(asset, info);

    const QString kindString = drift::mediaKindToString(asset.kind);
    asset.thumbnailPath = MediaThumbnail::generate(absolutePath, kindString);
    asset.filmstripPath = asset.kind == drift::MediaKind::Video
                              ? MediaThumbnail::generateFilmstrip(absolutePath, kindString)
                              : asset.thumbnailPath;
    return asset;
}

// nullopt when neither Qt nor the FFmpeg fallback can read the file. The suffix is on the import
// whitelist, so reaching that means a format Drift claims to support has no decoder here at all.
// Returning a zero-sized asset instead, which is what this used to do, left a row in the bin that
// silently rendered as nothing.
std::optional<drift::MediaAsset> buildImageAsset(const QString &absolutePath, const QString &name)
{
    const QString kindString = drift::mediaKindToString(drift::MediaKind::Image);
    const QString thumb = MediaThumbnail::generate(absolutePath, kindString);
    const QSize size = drift::stillImageSize(absolutePath);

    if (size.isEmpty()) {
        qWarning("import: cannot read image %s. Qt decodes: %s", qPrintable(absolutePath),
                 QImageReader::supportedImageFormats().join(", ").constData());
        return std::nullopt;
    }

    drift::MediaAsset asset;
    asset.name = name;
    asset.path = absolutePath;
    asset.kind = drift::MediaKind::Image;
    asset.width = size.width();
    asset.height = size.height();
    asset.thumbnailPath = thumb;
    asset.filmstripPath = thumb;
    asset.hasAudio = false;
    asset.hasAudioKnown = true;
    return asset;
}

// A Lottie document: parsed by the vector renderer rather than probed by FFmpeg, which would
// only report "unknown format" for a .json.
std::optional<drift::MediaAsset> buildVectorAsset(const QString &absolutePath, const QString &name)
{
    drift::VectorSource source;
    source.kind = drift::VectorKind::Lottie;
    source.path = absolutePath;
    QString error;
    if (!drift::vec::probeVectorSource(source, &error)) {
        qWarning("import: %s is not a Lottie document: %s", qPrintable(absolutePath), qPrintable(error));
        return std::nullopt;
    }
    const QString thumb =
        MediaThumbnail::generate(absolutePath, drift::mediaKindToString(drift::MediaKind::Vector));

    drift::MediaAsset asset;
    asset.name = source.title.isEmpty() ? name : source.title;
    asset.path = absolutePath;
    asset.kind = drift::MediaKind::Vector;
    asset.width = source.width;
    asset.height = source.height;
    asset.fps = source.fps;
    asset.durationUs = source.durationUs;
    asset.durationLabel = formatDuration(source.durationUs);
    asset.thumbnailPath = thumb;
    asset.filmstripPath = thumb;
    asset.hasAudio = false;
    asset.hasAudioKnown = true;
    return asset;
}

// Reads everything the bin needs about a file. Blocking, so it only ever runs on a worker
// thread — shared by the import path and the replace path.
std::optional<drift::MediaAsset> probeAsset(const QString &absolutePath, bool imageOnly)
{
    const QString name = QFileInfo(absolutePath).fileName();
    if (AssetLibrary::isVectorPath(absolutePath))
        return buildVectorAsset(absolutePath, name);
    if (imageOnly)
        return buildImageAsset(absolutePath, name);

    const MediaInfo info = MediaProbe::probe(absolutePath);
    if (!info.ok)
        return std::nullopt;
    return buildProbedAsset(absolutePath, name, info);
}

} // namespace

bool AssetLibrary::isVideoPath(const QString &path)
{
    return videoExtensions().contains(QFileInfo(path).suffix().toLower());
}

bool AssetLibrary::isAudioPath(const QString &path)
{
    return audioExtensions().contains(QFileInfo(path).suffix().toLower());
}

bool AssetLibrary::isImagePath(const QString &path)
{
    return imageExtensions().contains(QFileInfo(path).suffix().toLower());
}

// Lottie only: SVG stays an image (Qt rasterises it), which is what existing projects expect.
// add_svg / addVectorClip put an SVG on the timeline as a vector clip without going through
// the bin. A .lottie bundle is unpacked into plain .json on import (see importFilesReturningIds),
// so it never reaches the probe under its own name.
bool AssetLibrary::isVectorPath(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("json"), Qt::CaseInsensitive) == 0
           || drift::isDotLottiePath(path);
}

bool AssetLibrary::isMediaPath(const QString &path)
{
    return isVideoPath(path) || isAudioPath(path) || isImagePath(path) || isVectorPath(path);
}

QString AssetLibrary::mediaNameFilter() const
{
    static const QString pattern = [] {
        QStringList globs;
        for (const QStringList *group : {&videoExtensions(), &audioExtensions(), &imageExtensions()}) {
            for (const QString &extension : *group)
                globs.append(QStringLiteral("*.") + extension);
        }
        globs.append(QStringLiteral("*.json"));
        globs.append(QStringLiteral("*.lottie"));
        return globs.join(QLatin1Char(' '));
    }();
    return tr("Media files (%1)").arg(pattern);
}

bool AssetLibrary::sandboxed() const
{
#if defined(Q_OS_ANDROID)
    return false;
#else
    return qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists(QStringLiteral("/.flatpak-info"))
        || qEnvironmentVariableIsSet("SNAP");
#endif
}

AssetLibrary::AssetLibrary(QObject *parent)
    : QAbstractListModel(parent)
{
    // Re-broadcast every row-count change as countChanged so QML bindings on
    // `count` stay live without each mutation site having to remember to emit.
    connect(this, &QAbstractItemModel::rowsInserted, this, &AssetLibrary::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &AssetLibrary::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &AssetLibrary::countChanged);

    connect(this, &QAbstractItemModel::rowsInserted, this, &AssetLibrary::snapshotAssets);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &AssetLibrary::snapshotAssets);
    connect(this, &QAbstractItemModel::modelReset, this, &AssetLibrary::snapshotAssets);
}

// Every probe and thumbnail job captures `this` and posts its result back to this object, so
// none of them may outlive it. clear() drops the ones that have not started — an import of a
// few hundred files leaves a long queue, and there is no reason to run it to completion just
// to throw the answers away — and waitForDone() waits out the handful already running.
// Results that did get posted are ordinary queued events, which ~QObject discards.
AssetLibrary::~AssetLibrary()
{
    m_jobs.clear();
    m_jobs.waitForDone();
}

QList<QString> AssetLibrary::currentPaths() const
{
    if (!m_project)
        return {};

    QList<QString> paths;
    paths.reserve(m_project->assetOrder().size());
    for (const QString &id : m_project->assetOrder()) {
        const drift::MediaAsset *asset = m_project->asset(id);
        paths.append(asset ? asset->path : QString{});
    }
    return paths;
}

QList<QString> AssetLibrary::currentFolderIds() const
{
    if (!m_project)
        return {};

    QList<QString> folderIds;
    folderIds.reserve(m_project->assetOrder().size());
    for (const QString &id : m_project->assetOrder()) {
        const drift::MediaAsset *asset = m_project->asset(id);
        folderIds.append(asset ? asset->folderId : QString{});
    }
    return folderIds;
}

void AssetLibrary::snapshotAssets()
{
    m_syncedOrder = m_project ? m_project->assetOrder() : QList<QString>{};
    m_syncedPaths = currentPaths();
    m_syncedFolderIds = currentFolderIds();
}

void AssetLibrary::syncToProject()
{
    if (!m_project)
        return;

    // Undo/redo assigns the whole project behind this model's back. Resetting
    // unconditionally would rebuild every card on every unrelated timeline
    // undo, so only an actual order change is worth the churn.
    if (m_syncedOrder != m_project->assetOrder()) {
        beginResetModel();
        endResetModel();
        return;
    }

    // An undone source replace or folder move leaves the order untouched — same row, same id,
    // different file or folder — so both have to be compared too or the card keeps showing stale
    // data. Only the rows that actually changed are re-read.
    const QList<QString> paths = currentPaths();
    const QList<QString> folderIds = currentFolderIds();
    if (paths == m_syncedPaths && folderIds == m_syncedFolderIds)
        return;

    for (int i = 0; i < paths.size(); ++i) {
        const bool pathChanged = i >= m_syncedPaths.size() || m_syncedPaths.at(i) != paths.at(i);
        const bool folderChanged =
            i >= m_syncedFolderIds.size() || m_syncedFolderIds.at(i) != folderIds.at(i);
        if (!pathChanged && !folderChanged)
            continue;
        // Empty roles: every role may have moved with the file. A folder-only change only
        // touches FolderIdRole.
        emitAssetRowChanged(i, pathChanged ? QList<int>{} : QList<int>{FolderIdRole});
        if (pathChanged)
            emit assetMetadataChanged(m_project->assetIdAt(i));
    }
    m_syncedPaths = paths;
    m_syncedFolderIds = folderIds;
}

void AssetLibrary::setProject(drift::Project *project)
{
    beginResetModel();
    m_project = project;
    m_importPending.clear();
    m_thumbPending.clear();
    m_audioProbePending.clear();
    endResetModel();
}

int AssetLibrary::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_project)
        return 0;
    return m_project->assetOrder().size();
}

const drift::MediaAsset *AssetLibrary::assetAtIndex(int index) const
{
    if (!m_project || index < 0 || index >= m_project->assetOrder().size())
        return nullptr;
    return m_project->asset(m_project->assetIdAt(index));
}

drift::MediaAsset *AssetLibrary::assetAtIndex(int index)
{
    if (!m_project || index < 0 || index >= m_project->assetOrder().size())
        return nullptr;
    return m_project->asset(m_project->assetIdAt(index));
}

QVariant AssetLibrary::data(const QModelIndex &index, int role) const
{
    const drift::MediaAsset *asset = assetAtIndex(index.row());
    if (!index.isValid() || !asset)
        return {};

    switch (role) {
    case IdRole:
        return asset->id;
    case NameRole:
        return asset->name;
    case KindRole:
        return drift::mediaKindToString(asset->kind);
    case DurationRole:
        return asset->durationLabel;
    case DurationSecondsRole:
        return drift::usToSeconds(asset->durationUs);
    case PathRole:
        return asset->path;
    case ThumbnailPathRole:
        return asset->thumbnailPath;
    case FilmstripPathRole:
        return asset->filmstripPath;
    case FolderIdRole:
        return asset->folderId;
    default:
        return {};
    }
}

QHash<int, QByteArray> AssetLibrary::roleNames() const
{
    return {
        {IdRole, "id"},
        {NameRole, "name"},
        {KindRole, "kind"},
        {DurationRole, "duration"},
        {DurationSecondsRole, "durationSeconds"},
        {PathRole, "path"},
        {ThumbnailPathRole, "thumbnailPath"},
        {FilmstripPathRole, "filmstripPath"},
        {FolderIdRole, "folderId"},
    };
}

bool AssetLibrary::containsPath(const QString &path) const
{
    return indexOfPath(path) >= 0;
}

int AssetLibrary::indexOfPath(const QString &path) const
{
    if (!m_project)
        return -1;

    const QString normalized = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < m_project->assetOrder().size(); ++i) {
        const drift::MediaAsset *asset = assetAtIndex(i);
        if (asset && asset->path == normalized)
            return i;
    }
    return -1;
}

int AssetLibrary::indexOfId(const QString &id) const
{
    if (!m_project)
        return -1;
    return m_project->assetIndex(id);
}

QString AssetLibrary::assetIdAt(int index) const
{
    if (!m_project)
        return {};
    return m_project->assetIdAt(index);
}

void AssetLibrary::emitAssetRowChanged(int index, const QList<int> &roles)
{
    if (index < 0)
        return;
    const QModelIndex modelIndex = createIndex(index, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
}

void AssetLibrary::startThumbJob(const QString &assetId)
{
    if (!m_project || assetId.isEmpty() || m_thumbPending.contains(assetId))
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    if (!asset)
        return;

    const bool needThumb = asset->thumbnailPath.isEmpty() || !QFileInfo::exists(asset->thumbnailPath);
    const bool needStrip = asset->kind == drift::MediaKind::Video
                           && (asset->filmstripPath.isEmpty() || !QFileInfo::exists(asset->filmstripPath));
    if (!needThumb && !needStrip) {
        if (asset->kind != drift::MediaKind::Video && !asset->thumbnailPath.isEmpty()
            && asset->filmstripPath != asset->thumbnailPath) {
            asset->filmstripPath = asset->thumbnailPath;
            emitAssetRowChanged(indexOfId(assetId), {FilmstripPathRole});
        }
        return;
    }

    m_thumbPending.insert(assetId);
    const QString path = asset->path;
    const drift::MediaKind kind = asset->kind;

    (void)QtConcurrent::run(&m_jobs, [this, assetId, path, kind, needThumb, needStrip]() {
        const QString kindString = drift::mediaKindToString(kind);
        QString thumb;
        QString strip;
        if (needThumb)
            thumb = MediaThumbnail::generate(path, kindString);
        if (needStrip)
            strip = MediaThumbnail::generateFilmstrip(path, kindString);
        else if (!thumb.isEmpty() && kind != drift::MediaKind::Video)
            strip = thumb;

        QMetaObject::invokeMethod(
            this,
            [this, assetId, path, thumb, strip]() { applyThumbResult(assetId, path, thumb, strip); },
            Qt::QueuedConnection);
    });
}

void AssetLibrary::applyThumbResult(const QString &assetId, const QString &sourcePath,
                                    const QString &thumb, const QString &strip)
{
    m_thumbPending.remove(assetId);
    if (!m_project)
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    // The source was replaced while this job ran, so these frames are of a file the row no
    // longer points at.
    if (!asset || asset->path != sourcePath)
        return;

    bool changed = false;
    if (!thumb.isEmpty() && asset->thumbnailPath != thumb) {
        asset->thumbnailPath = thumb;
        changed = true;
    }
    if (!strip.isEmpty() && asset->filmstripPath != strip) {
        asset->filmstripPath = strip;
        changed = true;
    } else if (asset->kind != drift::MediaKind::Video && !asset->thumbnailPath.isEmpty()
               && asset->filmstripPath != asset->thumbnailPath) {
        asset->filmstripPath = asset->thumbnailPath;
        changed = true;
    }

    if (!changed)
        return;

    emitAssetRowChanged(indexOfId(assetId), {ThumbnailPathRole, FilmstripPathRole});
    emit assetMetadataChanged(assetId);
}

void AssetLibrary::refreshMediaAt(int index)
{
    drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset)
        return;
    startThumbJob(asset->id);
}

void AssetLibrary::startImportJob(const QString &assetId, const QString &absolutePath, bool imageOnly)
{
    if (assetId.isEmpty() || m_importPending.contains(assetId))
        return;

    m_importPending.insert(assetId);

    (void)QtConcurrent::run(&m_jobs, [this, assetId, absolutePath, imageOnly]() {
        const std::optional<drift::MediaAsset> probed = probeAsset(absolutePath, imageOnly);
        const drift::MediaAsset filled = probed.value_or(drift::MediaAsset{});
        const bool ok = probed.has_value();

        QMetaObject::invokeMethod(
            this,
            [this, assetId, filled, ok]() { applyImportResult(assetId, filled, ok); },
            Qt::QueuedConnection);
    });
}

bool AssetLibrary::startReplaceProbe(int index, const QString &absolutePath)
{
    const drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset || absolutePath.isEmpty())
        return false;

    const QString assetId = asset->id;
    if (m_importPending.contains(assetId))
        return false;

    m_importPending.insert(assetId);
    const bool imageOnly = isImagePath(absolutePath);

    (void)QtConcurrent::run(&m_jobs, [this, assetId, absolutePath, imageOnly]() {
        const std::optional<drift::MediaAsset> probed = probeAsset(absolutePath, imageOnly);
        const drift::MediaAsset filled = probed.value_or(drift::MediaAsset{});
        const bool ok = probed.has_value();

        QMetaObject::invokeMethod(
            this,
            [this, assetId, filled, ok]() {
                m_importPending.remove(assetId);
                emit assetSourceProbed(assetId, filled, ok);
            },
            Qt::QueuedConnection);
    });
    return true;
}

bool AssetLibrary::applyProbedSource(const QString &assetId, const drift::MediaAsset &filled)
{
    if (!m_project)
        return false;

    const int index = indexOfId(assetId);
    drift::MediaAsset *asset = index < 0 ? nullptr : m_project->asset(assetId);
    if (!asset)
        return false;

    const QString id = asset->id;
    const QString folderId = asset->folderId;
    *asset = filled;
    asset->id = id;
    asset->folderId = folderId;

    // Jobs still in flight were started against the old file. They drop themselves on landing
    // because the path they probed no longer matches; clearing the pending flags is what lets
    // the replacement start its own.
    m_thumbPending.remove(assetId);
    m_audioProbePending.remove(assetId);

    snapshotAssets();
    emitAssetRowChanged(index,
                        {NameRole, KindRole, DurationRole, DurationSecondsRole, PathRole,
                         ThumbnailPathRole, FilmstripPathRole});
    emit assetMetadataChanged(assetId);
    return true;
}

void AssetLibrary::applyImportResult(const QString &assetId, const drift::MediaAsset &filled, bool ok)
{
    m_importPending.remove(assetId);
    if (!m_project)
        return;

    const int index = indexOfId(assetId);
    if (index < 0)
        return;

    if (!ok) {
        const drift::MediaAsset *failing = m_project->asset(assetId);
        const QString name = failing ? failing->name : QString();
        beginRemoveRows({}, index, index);
        m_project->assets().remove(assetId);
        m_project->assetOrder().removeAll(assetId);
        endRemoveRows();
        emit assetImportFailed(name);
        return;
    }

    drift::MediaAsset *asset = m_project->asset(assetId);
    if (!asset)
        return;

    asset->name = filled.name;
    asset->kind = filled.kind;
    asset->durationUs = filled.durationUs;
    asset->durationLabel = filled.durationLabel;
    asset->path = filled.path;
    asset->width = filled.width;
    asset->height = filled.height;
    asset->fps = filled.fps;
    asset->rotationDegrees = filled.rotationDegrees;
    asset->sampleRate = filled.sampleRate;
    asset->channels = filled.channels;
    asset->codecName = filled.codecName;
    asset->hasAudio = filled.hasAudio;
    asset->hasAudioKnown = filled.hasAudioKnown;
    asset->thumbnailPath = filled.thumbnailPath;
    asset->filmstripPath = filled.filmstripPath;

    emitAssetRowChanged(index,
                        {NameRole, KindRole, DurationRole, DurationSecondsRole, PathRole,
                         ThumbnailPathRole, FilmstripPathRole});
    emit assetMetadataChanged(assetId);
}

QVariantMap AssetLibrary::assetAt(int index) const
{
    const drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset)
        return {};

    return {
        {QStringLiteral("id"), asset->id},
        {QStringLiteral("name"), asset->name},
        {QStringLiteral("kind"), drift::mediaKindToString(asset->kind)},
        {QStringLiteral("duration"), asset->durationLabel},
        {QStringLiteral("durationSeconds"), drift::usToSeconds(asset->durationUs)},
        {QStringLiteral("path"), asset->path},
        {QStringLiteral("width"), asset->width},
        {QStringLiteral("height"), asset->height},
        {QStringLiteral("fps"), asset->fps},
        {QStringLiteral("rotationDegrees"), asset->rotationDegrees},
        {QStringLiteral("thumbnailPath"), asset->thumbnailPath},
        {QStringLiteral("filmstripPath"), asset->filmstripPath},
        {QStringLiteral("assetIndex"), index},
        {QStringLiteral("folderId"), asset->folderId},
    };
}

QString AssetLibrary::thumbnailAt(int index) const
{
    const drift::MediaAsset *asset = assetAtIndex(index);
    return asset ? asset->thumbnailPath : QString{};
}

QString AssetLibrary::filmstripAt(int index) const
{
    const drift::MediaAsset *asset = assetAtIndex(index);
    return asset ? asset->filmstripPath : QString{};
}

void AssetLibrary::ensureMedia(int index)
{
    refreshMediaAt(index);
}

void AssetLibrary::ensureAllMedia()
{
    if (!m_project)
        return;
    for (int i = 0; i < m_project->assetOrder().size(); ++i)
        refreshMediaAt(i);
}

void AssetLibrary::ensureAudioPresence(const QString &assetId)
{
    if (!m_project || assetId.isEmpty() || m_audioProbePending.contains(assetId))
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    if (!asset || asset->hasAudioKnown)
        return;

    if (asset->channels > 0 || asset->sampleRate > 0) {
        asset->hasAudio = true;
        asset->hasAudioKnown = true;
        emit assetMetadataChanged(assetId);
        return;
    }

    m_audioProbePending.insert(assetId);
    const QString path = asset->path;

    (void)QtConcurrent::run(&m_jobs, [this, assetId, path]() {
        const MediaInfo info = MediaProbe::probe(path);
        bool hasAudio = false;
        int sampleRate = 0;
        int channels = 0;
        if (info.ok) {
            for (const StreamInfo &stream : info.streams) {
                if (stream.type == StreamInfo::Type::Audio) {
                    hasAudio = true;
                    sampleRate = stream.sampleRate;
                    channels = stream.channels;
                    break;
                }
            }
        }
        QMetaObject::invokeMethod(
            this,
            [this, assetId, path, hasAudio, sampleRate, channels]() {
                applyAudioPresence(assetId, path, hasAudio, sampleRate, channels);
            },
            Qt::QueuedConnection);
    });
}

void AssetLibrary::applyAudioPresence(const QString &assetId, const QString &sourcePath,
                                      bool hasAudio, int sampleRate, int channels)
{
    m_audioProbePending.remove(assetId);
    if (!m_project)
        return;

    drift::MediaAsset *asset = m_project->asset(assetId);
    // Answered for a file the row no longer points at; the replacement brought its own.
    if (!asset || asset->path != sourcePath)
        return;

    asset->hasAudio = hasAudio;
    asset->hasAudioKnown = true;
    if (hasAudio) {
        if (sampleRate > 0)
            asset->sampleRate = sampleRate;
        if (channels > 0)
            asset->channels = channels;
    }
    emit assetMetadataChanged(assetId);
}

void AssetLibrary::sortByName()
{
    if (!m_project || m_project->assetOrder().size() < 2)
        return;

    beginResetModel();
    QList<QString> order = m_project->assetOrder();
    std::sort(order.begin(), order.end(), [this](const QString &a, const QString &b) {
        const drift::MediaAsset *assetA = m_project->asset(a);
        const drift::MediaAsset *assetB = m_project->asset(b);
        if (!assetA || !assetB)
            return a < b;
        return assetA->name.compare(assetB->name, Qt::CaseInsensitive) < 0;
    });
    m_project->assetOrder() = order;
    endResetModel();
}

bool AssetLibrary::setAssetName(int index, const QString &name)
{
    drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset)
        return false;

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || asset->name == trimmed)
        return false;

    asset->name = trimmed;
    emitAssetRowChanged(index, {NameRole});
    snapshotAssets();
    return true;
}

bool AssetLibrary::moveAssetToFolder(int index, const QString &folderId)
{
    drift::MediaAsset *asset = assetAtIndex(index);
    if (!asset || asset->folderId == folderId)
        return false;

    asset->folderId = folderId;
    emitAssetRowChanged(index, {FolderIdRole});
    snapshotAssets();
    return true;
}

int AssetLibrary::reparentAssetsInFolder(const QString &folderId, const QString &newFolderId)
{
    if (!m_project)
        return 0;

    int moved = 0;
    for (int i = 0; i < m_project->assetOrder().size(); ++i) {
        drift::MediaAsset *asset = assetAtIndex(i);
        if (!asset || asset->folderId != folderId)
            continue;
        asset->folderId = newFolderId;
        emitAssetRowChanged(i, {FolderIdRole});
        ++moved;
    }
    if (moved > 0)
        snapshotAssets();
    return moved;
}

void AssetLibrary::sortByKind()
{
    if (!m_project || m_project->assetOrder().size() < 2)
        return;

    beginResetModel();
    QList<QString> order = m_project->assetOrder();
    std::sort(order.begin(), order.end(), [this](const QString &a, const QString &b) {
        const drift::MediaAsset *assetA = m_project->asset(a);
        const drift::MediaAsset *assetB = m_project->asset(b);
        if (!assetA || !assetB)
            return a < b;
        const int cmp = drift::mediaKindToString(assetA->kind)
                            .compare(drift::mediaKindToString(assetB->kind), Qt::CaseInsensitive);
        return cmp != 0 ? cmp < 0 : assetA->name.compare(assetB->name, Qt::CaseInsensitive) < 0;
    });
    m_project->assetOrder() = order;
    endResetModel();
}

bool AssetLibrary::removeAssetAt(int index)
{
    if (!m_project || index < 0 || index >= m_project->assetOrder().size())
        return false;

    const QString assetId = m_project->assetIdAt(index);
    beginRemoveRows({}, index, index);
    m_project->assets().remove(assetId);
    m_project->assetOrder().removeAll(assetId);
    endRemoveRows();

    // In-flight probe/thumb jobs already no-op when the id is gone; this just
    // keeps the pending sets from retaining ids nothing will ever clear.
    m_importPending.remove(assetId);
    m_thumbPending.remove(assetId);
    m_audioProbePending.remove(assetId);
    return true;
}

void AssetLibrary::clear()
{
    if (!m_project || m_project->assetOrder().isEmpty())
        return;

    beginResetModel();
    m_project->assets().clear();
    m_project->assetOrder().clear();
    m_importPending.clear();
    m_thumbPending.clear();
    m_audioProbePending.clear();
    endResetModel();
}

QJsonArray AssetLibrary::toJsonArray() const
{
    if (!m_project)
        return {};

    QJsonArray assets;
    for (const QString &id : m_project->assetOrder()) {
        const drift::MediaAsset *asset = m_project->asset(id);
        if (!asset)
            continue;
        QJsonObject object{
            {QStringLiteral("id"), asset->id},
            {QStringLiteral("name"), asset->name},
            {QStringLiteral("kind"), drift::mediaKindToString(asset->kind)},
            {QStringLiteral("durationUs"), static_cast<double>(asset->durationUs)},
            {QStringLiteral("duration"), asset->durationLabel},
            {QStringLiteral("path"), asset->path},
            {QStringLiteral("width"), asset->width},
            {QStringLiteral("height"), asset->height},
            {QStringLiteral("fps"), asset->fps},
            {QStringLiteral("rotationDegrees"), asset->rotationDegrees},
            {QStringLiteral("sampleRate"), asset->sampleRate},
            {QStringLiteral("channels"), asset->channels},
            {QStringLiteral("codecName"), asset->codecName},
            {QStringLiteral("thumbnailPath"), asset->thumbnailPath},
            {QStringLiteral("filmstripPath"), asset->filmstripPath},
        };
        if (asset->hasAudioKnown)
            object.insert(QStringLiteral("hasAudio"), asset->hasAudio);
        assets.append(object);
    }
    return assets;
}

void AssetLibrary::loadFromJsonArray(const QJsonArray &assets)
{
    if (!m_project)
        return;

    beginResetModel();
    m_project->assets().clear();
    m_project->assetOrder().clear();
    m_importPending.clear();
    m_thumbPending.clear();
    m_audioProbePending.clear();

    for (const QJsonValue &value : assets) {
        const QJsonObject object = value.toObject();
        drift::MediaAsset asset;
        asset.id = object.value(QStringLiteral("id")).toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
        asset.name = object.value(QStringLiteral("name")).toString();
        asset.kind = drift::mediaKindFromString(object.value(QStringLiteral("kind")).toString());
        asset.durationLabel = object.value(QStringLiteral("duration")).toString();
        if (object.contains(QStringLiteral("durationUs"))) {
            asset.durationUs = static_cast<drift::TimeUs>(object.value(QStringLiteral("durationUs")).toDouble());
        } else {
            asset.durationUs = drift::secondsToUs(object.value(QStringLiteral("durationSeconds")).toDouble());
        }
        asset.path = object.value(QStringLiteral("path")).toString();
        asset.width = object.value(QStringLiteral("width")).toInt();
        asset.height = object.value(QStringLiteral("height")).toInt();
        asset.fps = object.value(QStringLiteral("fps")).toDouble();
        asset.rotationDegrees = object.value(QStringLiteral("rotationDegrees")).toInt();
        asset.sampleRate = object.value(QStringLiteral("sampleRate")).toInt();
        asset.channels = object.value(QStringLiteral("channels")).toInt();
        asset.codecName = object.value(QStringLiteral("codecName")).toString();
        asset.thumbnailPath = object.value(QStringLiteral("thumbnailPath")).toString();
        asset.filmstripPath = object.value(QStringLiteral("filmstripPath")).toString();
        if (object.contains(QStringLiteral("hasAudio"))) {
            asset.hasAudioKnown = true;
            asset.hasAudio = object.value(QStringLiteral("hasAudio")).toBool();
        } else if (asset.channels > 0 || asset.sampleRate > 0) {
            asset.hasAudioKnown = true;
            asset.hasAudio = true;
        }
        m_project->addAsset(asset);
    }

    endResetModel();

    for (int i = 0; i < m_project->assetOrder().size(); ++i)
        refreshMediaAt(i);
}

// One URL through the copy stage. Returns the path the rest of the import pipeline should use,
// or empty when the document could not be read. `sourceUri` comes back set only for a SAF
// document, which is the only case that has anything to rehydrate from later.
//
// Free function rather than a member because importUrlsAsync runs it on a worker thread, where
// touching the model would be a data race.
namespace {

// What the off-thread copy stage hands back to the GUI thread.
struct Materialized
{
    QStringList paths;
    // Absolute path -> the content:// URI it was copied out of.
    QHash<QString, QString> sourceUris;
    int failed = 0;
};

// Host path that this process can actually open. A Flatpak drop hands us file:// of a path
// outside the sandbox: QUrl::toLocalFile() succeeds, then every later open fails. Returning
// empty here is what lets importFinished report `failed` instead of a silent skip.
QString readableLocalPath(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        qWarning("import: cannot read %s", qPrintable(path));
        return {};
    }
    return path;
}

QString materializeOne(const QUrl &url, QString *sourceUri)
{
    sourceUri->clear();
    if (url.isLocalFile())
        return readableLocalPath(url.toLocalFile());
    if (url.isEmpty())
        return {};

#ifdef Q_OS_ANDROID
    // SAF hands back content:// URIs, which FFmpeg cannot open directly — materialize a
    // real file first so the rest of the import pipeline stays path-based.
    if (AndroidUri::isContentUri(url)) {
        const QString materialized = materializeImportUrl(url);
        if (materialized.isEmpty()) {
            qWarning("import: skipped unreadable URL %s", qPrintable(url.toString()));
            return {};
        }
        *sourceUri = url.toString(QUrl::FullyEncoded);
        return materialized;
    }
#endif

    const QString asString = url.toString();
    if (asString.startsWith(QLatin1String("file://"), Qt::CaseInsensitive))
        return readableLocalPath(QUrl(asString).toLocalFile());
    return asString;
}
// What to show while this URL is being copied. A SAF document's URI carries only an opaque id,
// so the provider has to be asked for the name the user knows the file by.
QString importLabel(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url))
        return AndroidUri::displayName(url);
#endif
    return url.fileName();
}

} // namespace

void AssetLibrary::importUrls(const QList<QUrl> &urls)
{
    QStringList paths;
    QHash<QString, QString> sourceUris;
    paths.reserve(urls.size());
    for (const QUrl &url : urls) {
        QString sourceUri;
        const QString path = materializeOne(url, &sourceUri);
        if (path.isEmpty())
            continue;
        paths.append(path);
        if (!sourceUri.isEmpty())
            sourceUris.insert(QFileInfo(path).absoluteFilePath(), sourceUri);
    }
    importFiles(paths, sourceUris, m_importFolderId);
}

bool AssetLibrary::importUrlsAsync(const QList<QUrl> &urls)
{
    if (m_importing)
        return false;
    if (urls.isEmpty()) {
        emit importFinished(0, 0);
        return true;
    }

    m_importing = true;
    emit importingChanged();

    // Captured now, not read from m_importFolderId when the copy finishes: the user can
    // navigate to a different folder while a large/slow copy is still running, and the import
    // should land wherever they were when they started it, not wherever they ended up.
    const QString destinationFolderId = m_importFolderId;
    const int total = urls.size();
    auto *watcher = new QFutureWatcher<Materialized>(this);
    connect(watcher, &QFutureWatcher<Materialized>::finished, this,
            [this, watcher, destinationFolderId]() {
        watcher->deleteLater();
        const Materialized result = watcher->result();

        // The rows have to exist before importFinished lands: AndroidHome walks countBefore..count
        // and turns every new asset into a clip the moment it sees the signal.
        importFiles(result.paths, result.sourceUris, destinationFolderId);

        m_importing = false;
        emit importingChanged();
        emit importFinished(result.paths.size(), result.failed);
    });

    watcher->setFuture(QtConcurrent::run([this, urls, total]() {
        Materialized out;
        out.paths.reserve(total);
        for (int i = 0; i < total; ++i) {
            const QUrl &url = urls.at(i);
            // Reported before the copy, not after, so the name on screen is the file being worked
            // on rather than the one that just finished.
            const QString label = importLabel(url);
            QMetaObject::invokeMethod(
                this, [this, i, total, label]() { emit importProgress(i, total, label); },
                Qt::QueuedConnection);

            QString sourceUri;
            const QString path = materializeOne(url, &sourceUri);
            if (path.isEmpty()) {
                ++out.failed;
                continue;
            }
            out.paths.append(path);
            if (!sourceUri.isEmpty())
                out.sourceUris.insert(QFileInfo(path).absoluteFilePath(), sourceUri);
        }
        return out;
    }));
    return true;
}

QStringList AssetLibrary::importLocalPaths(const QStringList &paths)
{
    return importFilesReturningIds(paths, {}, m_importFolderId);
}

bool AssetLibrary::isImportPending(const QString &assetId) const
{
    return m_importPending.contains(assetId);
}

QString AssetLibrary::addGeneratedAsset(drift::MediaAsset asset)
{
    if (!m_project || asset.path.isEmpty())
        return {};

    if (asset.id.isEmpty())
        asset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    asset.path = QFileInfo(asset.path).absoluteFilePath();

    const int row = m_project->assetOrder().size();
    beginInsertRows({}, row, row);
    const QString id = m_project->addAsset(asset);
    endInsertRows();
    return id;
}

void AssetLibrary::importFiles(const QStringList &paths, const QHash<QString, QString> &sourceUris,
                               const QString &destinationFolderId)
{
    importFilesReturningIds(paths, sourceUris, destinationFolderId);
}

QStringList AssetLibrary::importFilesReturningIds(const QStringList &paths,
                                                  const QHash<QString, QString> &sourceUris,
                                                  const QString &destinationFolderId)
{
    QStringList ids;
    if (!m_project)
        return ids;

    // The destination was captured when the import started, but completion can land well after
    // that — long enough for the folder to have been deleted, or for the whole project to have
    // been replaced by a load. An asset filed under an id that no longer names a folder would be
    // unreachable from the bin (nothing lists "every asset regardless of folder"), so re-check
    // against the project as it stands now and fall back to root rather than orphan it.
    const QString validatedFolderId =
        (destinationFolderId.isEmpty() || m_project->binFolder(destinationFolderId))
            ? destinationFolderId
            : QString();

    // A .lottie bundle becomes one plain Lottie .json per animation it holds, unpacked once into
    // app data (content-addressed, so the same bundle always maps to the same files).
    QStringList expanded;
    for (const QString &path : paths) {
        if (!drift::isDotLottiePath(path)) {
            expanded.append(path);
            continue;
        }
        QString error;
        const QStringList animations = drift::unpackDotLottie(
            path, QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/lottie"),
            &error);
        if (animations.isEmpty())
            qWarning("import: %s: %s", qPrintable(path), qPrintable(error));
        expanded.append(animations);
    }

    for (const QString &path : std::as_const(expanded)) {
        const QFileInfo fileInfo(path);
        const QString absolutePath = fileInfo.absoluteFilePath();
        if (!fileInfo.isFile())
            continue;

        const int existingIndex = indexOfPath(absolutePath);
        if (existingIndex >= 0) {
            refreshMediaAt(existingIndex);
            ids.append(assetIdAt(existingIndex));
            continue;
        }

        drift::MediaAsset placeholder;
        placeholder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        placeholder.name = fileInfo.fileName();
        placeholder.path = absolutePath;
        placeholder.sourceUri = sourceUris.value(absolutePath);
        placeholder.kind = provisionalKind(absolutePath);
        placeholder.folderId = validatedFolderId;

        const int row = m_project->assetOrder().size();
        beginInsertRows({}, row, row);
        m_project->addAsset(placeholder);
        endInsertRows();

        startImportJob(placeholder.id, absolutePath, isImagePath(path));
        ids.append(placeholder.id);
    }
    return ids;
}
