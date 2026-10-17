#include "MediaBinController.h"

#include "AppController.h"
#include "AppControllerDetail.h"
#include "AssetLibrary.h"
#include "ProjectFileController.h"
#include "engine/AndroidUri.h"
#include "engine/MediaEditor.h"
#include "engine/MediaProbe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSet>
#include <QtConcurrent>

#include <cmath>
#include <memory>
#include <utility>

MediaBinController::MediaBinController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    m_folderModel.setProject(&m_app->m_project);
    if (m_app->m_assetLibrary)
        connect(m_app->m_assetLibrary, &AssetLibrary::assetSourceProbed, this,
                &MediaBinController::finalizeReplace);
    // A file replaced in place while the project was closed: its saved duration and every cache
    // of it describe the old content.
    connect(m_app->projectFile(), &ProjectFileController::projectLoadFinished, this,
            [this](bool ok) {
                if (ok)
                    refreshChangedSources();
            });
    // Queued: the finished job's own bookkeeping is still unwinding when this is emitted.
    connect(this, &MediaBinController::editFinished, this, &MediaBinController::startNextConversion,
            Qt::QueuedConnection);
}

void MediaBinController::setCurrentFolderId(const QString &folderId)
{
    if (m_currentFolderId == folderId)
        return;
    m_currentFolderId = folderId;
    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->setImportFolderId(folderId);
    emit currentFolderIdChanged();
}

QString MediaBinController::createFolder(const QString &name, const QString &parentId)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return {};

    // Plain copy, not detachedCopy(): nothing here runs off the GUI thread, so there's no
    // concurrent reader to race — the same reasoning removeAsset already relies on. A full
    // detach walks every clip's keyframes/masks/effects across the whole timeline, which is
    // real, perceptible latency on a project of any size for an edit that touches none of it.
    const drift::Project before = m_app->m_project;
    const QString id = m_folderModel.createFolder(trimmed, parentId);
    m_app->pushProjectEdit(before, tr("Folder created"));
    m_app->settleUndoRefresh();
    return id;
}

bool MediaBinController::renameFolder(const QString &folderId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return false;

    // Plain copy, not detachedCopy(): nothing here runs off the GUI thread, so there's no
    // concurrent reader to race — the same reasoning removeAsset already relies on. A full
    // detach walks every clip's keyframes/masks/effects across the whole timeline, which is
    // real, perceptible latency on a project of any size for an edit that touches none of it.
    const drift::Project before = m_app->m_project;
    if (!m_folderModel.renameFolder(folderId, trimmed))
        return false;

    m_app->pushProjectEdit(before, tr("Folder renamed"));
    m_app->settleUndoRefresh();
    return true;
}

bool MediaBinController::moveFolder(const QString &folderId, const QString &newParentId)
{
    // Plain copy, not detachedCopy(): nothing here runs off the GUI thread, so there's no
    // concurrent reader to race — the same reasoning removeAsset already relies on. A full
    // detach walks every clip's keyframes/masks/effects across the whole timeline, which is
    // real, perceptible latency on a project of any size for an edit that touches none of it.
    const drift::Project before = m_app->m_project;
    if (!m_folderModel.moveFolder(folderId, newParentId))
        return false;

    m_app->pushProjectEdit(before, tr("Folder moved"));
    m_app->settleUndoRefresh();
    return true;
}

bool MediaBinController::deleteFolder(const QString &folderId)
{
    // Plain copy, not detachedCopy(): nothing here runs off the GUI thread, so there's no
    // concurrent reader to race — the same reasoning removeAsset already relies on. A full
    // detach walks every clip's keyframes/masks/effects across the whole timeline, which is
    // real, perceptible latency on a project of any size for an edit that touches none of it.
    const drift::Project before = m_app->m_project;
    const QString parentId = m_folderModel.parentIdOf(folderId);
    if (!m_folderModel.deleteFolder(folderId))
        return false;

    if (m_app->m_assetLibrary)
        m_app->m_assetLibrary->reparentAssetsInFolder(folderId, parentId);
    if (m_currentFolderId == folderId)
        setCurrentFolderId(parentId);

    m_app->pushProjectEdit(before, tr("Folder deleted"));
    m_app->settleUndoRefresh();
    return true;
}

bool MediaBinController::moveAssetToFolder(int assetIndex, const QString &folderId)
{
    if (!m_app->m_assetLibrary)
        return false;

    // Plain copy, not detachedCopy(): nothing here runs off the GUI thread, so there's no
    // concurrent reader to race — the same reasoning removeAsset already relies on. A full
    // detach walks every clip's keyframes/masks/effects across the whole timeline, which is
    // real, perceptible latency on a project of any size for an edit that touches none of it.
    const drift::Project before = m_app->m_project;
    if (!m_app->m_assetLibrary->moveAssetToFolder(assetIndex, folderId))
        return false;

    m_app->pushProjectEdit(before, tr("Media moved"));
    m_app->settleUndoRefresh();
    return true;
}

int MediaBinController::moveAssetsToFolder(const QStringList &assetIds, const QString &folderId)
{
    if (!m_app->m_assetLibrary || assetIds.isEmpty())
        return 0;

    const drift::Project before = m_app->m_project;
    int moved = 0;
    for (const QString &id : assetIds) {
        const int index = m_app->m_assetLibrary->indexOfId(id);
        if (index < 0)
            continue;
        if (m_app->m_assetLibrary->moveAssetToFolder(index, folderId))
            ++moved;
    }
    if (moved == 0)
        return 0;

    m_app->pushProjectEdit(before,
                           moved == 1 ? tr("Media moved") : tr("%n items moved", "", moved));
    m_app->settleUndoRefresh();
    return moved;
}

namespace {

// A folder picked by mistake — a home directory, an external drive — can hold tens of thousands
// of files, and importing them all means a probe and a thumbnail job each. The walk stops here
// and says so, rather than filling the bin with something nobody asked for.
constexpr int kFolderImportFileLimit = 500;

// One filesystem directory, flattened out of the walk so the GUI-thread half can replay the tree
// without touching the disk again. `parentIndex` points at an earlier entry in the same list;
// -1 is the folder the user picked.
struct FolderImportEntry
{
    QString name;
    int parentIndex = -1;
    QStringList files;
};

struct FolderImportPlan
{
    QList<FolderImportEntry> entries;
    // Files the walk looked at and passed over because nothing recognizes the extension. Only
    // counts what was actually examined, so hitting the limit below does not inflate it with
    // everything the walk never reached.
    int skipped = 0;
    bool truncated = false;
};

// Runs on a worker thread: nothing here touches the project, the models, or anything else the
// GUI thread owns. That matters most under Flatpak, where the picked directory is a
// document-portal FUSE mount and every stat is a round trip out of the sandbox.
void planDirectory(const QDir &dir, int parentIndex, FolderImportPlan &plan, int &fileCount,
                   QSet<QString> &visitedDirs)
{
    // Guards against a symlinked subdirectory that loops back to an ancestor (or to another
    // already-planned directory): canonicalFilePath() resolves the symlink, so the second
    // visit is recognized and skipped instead of recursing forever.
    const QString canonicalPath = QFileInfo(dir.absolutePath()).canonicalFilePath();
    if (canonicalPath.isEmpty() || visitedDirs.contains(canonicalPath))
        return;
    visitedDirs.insert(canonicalPath);

    FolderImportEntry entry;
    entry.name = dir.dirName();
    entry.parentIndex = parentIndex;

    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &info : files) {
        if (!AssetLibrary::isMediaPath(info.fileName())) {
            ++plan.skipped;
            continue;
        }
        if (fileCount >= kFolderImportFileLimit) {
            plan.truncated = true;
            break;
        }
        entry.files.append(info.absoluteFilePath());
        ++fileCount;
    }

    const int index = plan.entries.size();
    plan.entries.append(entry);
    // Stop the whole walk at the limit rather than only the file collection, so hitting it
    // leaves a partial tree instead of thousands of empty mirrored folders.
    if (plan.truncated)
        return;

    const QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &subdirInfo : subdirs) {
        planDirectory(QDir(subdirInfo.absoluteFilePath()), index, plan, fileCount, visitedDirs);
        if (plan.truncated)
            return;
    }
}

} // namespace

bool MediaBinController::importFolder(const QUrl &folderUrl)
{
    if (m_importingFolder)
        return false;

    const QString path = folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString();
    const QDir dir(path);
    if (path.isEmpty() || !dir.exists())
        return false;

    m_importingFolder = true;
    emit importingFolderChanged();

    // Only the walk is off-thread. Creating the bin folders and the asset rows stays on the GUI
    // thread, because both are model mutations — but with the file limit above, that half is
    // bounded work on data already in memory.
    auto *watcher = new QFutureWatcher<FolderImportPlan>(this);
    connect(watcher, &QFutureWatcher<FolderImportPlan>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        const FolderImportPlan plan = watcher->result();

        QStringList folderIds;
        folderIds.reserve(plan.entries.size());
        int folderCount = 0;
        int fileCount = 0;

        for (const FolderImportEntry &entry : plan.entries) {
            const QString parentId =
                entry.parentIndex < 0 ? m_currentFolderId : folderIds.at(entry.parentIndex);
            const QString folderId = m_folderModel.createFolder(entry.name, parentId);
            // Appended before the empty check so later entries' parentIndex stays aligned.
            folderIds.append(folderId);
            if (folderId.isEmpty())
                continue;
            ++folderCount;

            if (entry.files.isEmpty() || !m_app->m_assetLibrary)
                continue;

            // Retargeted for the duration of this one entry — importLocalPaths reads it via
            // m_importFolderId — then put back once the whole tree is done.
            m_app->m_assetLibrary->setImportFolderId(folderId);
            const QStringList ids = m_app->m_assetLibrary->importLocalPaths(entry.files);
            fileCount += ids.size();
            // A path already in the bin from an earlier import is returned as-is by
            // importLocalPaths, keeping whatever folder it already lived in — otherwise this
            // mirrored folder would look empty despite the file counting as imported into it.
            for (const QString &id : ids) {
                const int index = m_app->m_assetLibrary->indexOfId(id);
                if (index < 0)
                    continue;
                if (m_app->m_assetLibrary->assetAt(index).value(QStringLiteral("folderId")).toString()
                    != folderId)
                    m_app->m_assetLibrary->moveAssetToFolder(index, folderId);
            }
        }

        // The loop above repointed the import destination at whichever folder it last populated;
        // put it back at wherever the user is actually browsing.
        if (m_app->m_assetLibrary)
            m_app->m_assetLibrary->setImportFolderId(m_currentFolderId);

        // Not pushed through pushProjectEdit: like a plain media import, this isn't meant to be
        // undoable — "undo" is deleting the folder by hand, same as removing an imported asset.
        // But unlike plain media import, autosave/the unsaved-changes prompt should still cover
        // it, so it's marked dirty directly (the same split setProjectName/setProjectMetadata
        // already use).
        if (folderCount > 0)
            m_app->m_projectFile->setDirty(true);

        m_importingFolder = false;
        emit importingFolderChanged();
        emit folderImportFinished(folderCount, fileCount, plan.skipped, plan.truncated);
    });

    watcher->setFuture(QtConcurrent::run([dir]() {
        FolderImportPlan plan;
        int fileCount = 0;
        QSet<QString> visitedDirs;
        planDirectory(dir, -1, plan, fileCount, visitedDirs);
        return plan;
    }));
    return true;
}

bool MediaBinController::replaceSource(int assetIndex, const QUrl &url)
{
    if (!m_app->m_assetLibrary)
        return false;

    const QString assetId = m_app->m_assetLibrary->assetIdAt(assetIndex);
    if (assetId.isEmpty())
        return false;

    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString sourceUri;
#ifdef Q_OS_ANDROID
    // A content:// document has no filesystem path at all, so QFileInfo below rejects every SAF
    // pick outright. Copy it out first, exactly as import does, and probe the copy.
    if (AndroidUri::isContentUri(url)) {
        path = materializeContentUrl(url);
        if (!path.isEmpty())
            sourceUri = url.toString(QUrl::FullyEncoded);
    }
#endif
    const QFileInfo fileInfo(path);
    if (path.isEmpty() || !fileInfo.isFile()) {
        emit replaceFinished(false, tr("That file could not be read."), 0);
        return false;
    }

    // Two bin rows sharing a path would break import de-duplication, which resolves a path to
    // whichever row holds it first.
    const QString absolutePath = fileInfo.absoluteFilePath();
    const int existing = m_app->m_assetLibrary->indexOfPath(absolutePath);
    if (existing >= 0 && existing != assetIndex) {
        emit replaceFinished(false, tr("That file is already in this project."), 0);
        return false;
    }

    if (!m_app->m_assetLibrary->startReplaceProbe(assetIndex, absolutePath)) {
        emit replaceFinished(false, tr("That file could not be read."), 0);
        return false;
    }

    // Reading a file off a slow disk is the one part of this the user waits on with nothing to
    // show for it, so the row it belongs to goes busy until the probe lands.
    m_replacingAssetId = assetId;
    // applyProbedSource overwrites the whole struct from the probe result, which knows nothing
    // about where the file came from — so the URI is held here and put back in finalizeReplace.
    m_replacingAssetSourceUri = sourceUri;
    emit replacingAssetIdChanged();
    return true;
}

bool MediaBinController::refreshSource(int assetIndex)
{
    return m_app->m_assetLibrary && m_app->m_assetLibrary->startRefreshProbe(assetIndex);
}

int MediaBinController::refreshChangedSources()
{
    AssetLibrary *library = m_app->m_assetLibrary;
    if (!library)
        return 0;
    int started = 0;
    for (int i = 0; i < library->rowCount(); ++i) {
        const drift::MediaAsset *asset = m_app->m_project.asset(library->assetIdAt(i));
        if (asset && drift::sourceChangedOnDisk(*asset) && library->startRefreshProbe(i))
            ++started;
    }
    return started;
}

bool MediaBinController::exportImage(int assetIndex, const QUrl &url)
{
    if (!m_app->m_assetLibrary)
        return false;

    const QVariantMap asset = m_app->m_assetLibrary->assetAt(assetIndex);
    if (asset.value(QStringLiteral("kind")).toString()
        != drift::mediaKindToString(drift::MediaKind::Image)) {
        return false;
    }

    const QString sourcePath = asset.value(QStringLiteral("path")).toString();
    if (sourcePath.isEmpty() || !QFileInfo(sourcePath).isFile())
        return false;

    // The path the picker returned is written to exactly as given — see FileDialogs::saveFile for
    // why appending a suffix to it would write somewhere the document portal never registered.
    // On Android it is a content:// URI, whose path carries no extension: the format comes from
    // the document's display name instead.
    const bool contentUri = AndroidUri::isContentUri(url);
    const QString destPath = url.isLocalFile() ? url.toLocalFile() : QString();
    if (!contentUri && destPath.isEmpty())
        return false;

    const QString destName = contentUri ? AndroidUri::displayName(url) : destPath;
    const QString suffix = QFileInfo(destName).suffix().toLower();
    const bool jpeg = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg");

    // Same format in and out: copy the bytes rather than decode and re-encode, so a freeze frame
    // saved as PNG comes out pixel-for-pixel what the compositor produced.
    const bool copyBytes = !jpeg && suffix == QFileInfo(sourcePath).suffix().toLower();
    if (copyBytes && !contentUri) {
        QFile::remove(destPath); // The picker already confirmed the overwrite; copy() won't clobber.
        return QFile::copy(sourcePath, destPath);
    }

    QImage image;
    if (!copyBytes) {
        image.load(sourcePath);
        if (image.isNull())
            return false;
        // JPEG has no alpha, and Qt writes transparent pixels as black without this.
        if (jpeg && image.hasAlphaChannel())
            image = image.convertToFormat(QImage::Format_RGB32);
    }

    if (!contentUri)
        return jpeg ? image.save(destPath, "JPG", 95) : image.save(destPath, "PNG");

    const bool disposable = writeTargetIsDisposable(url);
    std::unique_ptr<QFile> dst = AndroidUri::openForWrite(url);
    bool ok = false;
    if (dst) {
        if (copyBytes) {
            QFile src(sourcePath);
            if (src.open(QIODevice::ReadOnly)) {
                const QByteArray bytes = src.readAll();
                ok = src.error() == QFile::NoError && dst->write(bytes) == bytes.size();
            }
        } else {
            ok = jpeg ? image.save(dst.get(), "JPG", 95) : image.save(dst.get(), "PNG");
        }
        // See commitWriteTarget: a cloud provider only commits when the descriptor closes.
        ok = ok && dst->flush();
        dst->close();
        ok = ok && dst->error() == QFile::NoError;
    }
    if (!ok && disposable)
        AndroidUri::deleteDocument(url);
    return ok;
}

bool MediaBinController::saveAs(int assetIndex, const QUrl &url)
{
    if (!m_app->m_assetLibrary)
        return false;

    const QVariantMap asset = m_app->m_assetLibrary->assetAt(assetIndex);
    const QString name = asset.value(QStringLiteral("name")).toString();
    const QString sourcePath = asset.value(QStringLiteral("path")).toString();
    if (sourcePath.isEmpty() || !QFileInfo(sourcePath).isFile())
        return false;

    const bool contentUri = AndroidUri::isContentUri(url);
    const QString destPath = url.isLocalFile() ? url.toLocalFile() : QString();
    if (!contentUri && destPath.isEmpty())
        return false;
    const bool disposable = writeTargetIsDisposable(url);

    (void)QtConcurrent::run([this, sourcePath, destPath, url, contentUri, disposable, name]() {
        bool ok = false;
        if (contentUri) {
            constexpr qint64 kChunk = 1024 * 1024;
            QFile src(sourcePath);
            std::unique_ptr<QFile> dst = AndroidUri::openForWrite(url);
            if (dst && src.open(QIODevice::ReadOnly)) {
                ok = true;
                for (QByteArray chunk = src.read(kChunk); !chunk.isEmpty(); chunk = src.read(kChunk)) {
                    if (dst->write(chunk) != chunk.size()) {
                        ok = false;
                        break;
                    }
                }
                // See commitWriteTarget: the tail is only flushed, and a cloud provider only
                // commits, when the descriptor closes.
                ok = ok && src.error() == QFile::NoError && dst->flush();
                dst->close();
                ok = ok && dst->error() == QFile::NoError;
            }
            if (!ok && disposable)
                AndroidUri::deleteDocument(url);
        } else if (QFileInfo(destPath).canonicalFilePath() == QFileInfo(sourcePath).canonicalFilePath()) {
            // Saving a file over itself: the remove below would delete the only copy.
            ok = true;
        } else {
            QFile::remove(destPath); // The picker already confirmed the overwrite; copy() won't clobber.
            ok = QFile::copy(sourcePath, destPath);
        }

        QMetaObject::invokeMethod(
            this, [this, ok, name]() { emit saveFinished(ok, name); }, Qt::QueuedConnection);
    });
    return true;
}

void MediaBinController::cancelEdit()
{
    m_conversionQueue.clear();
    if (m_editing)
        m_editCancel.storeRelaxed(1);
}

bool MediaBinController::saveEdit(int assetIndex, double inSeconds, double outSeconds,
                                  double cropX, double cropY, double cropW, double cropH)
{
    if (m_editing) {
        m_app->setLastMessage(tr("An edit is already saving"), QStringLiteral("warning"));
        return false;
    }
    if (!m_app->m_assetLibrary)
        return false;

    const QVariantMap asset = m_app->m_assetLibrary->assetAt(assetIndex);
    if (asset.isEmpty())
        return false;

    const QString kind = asset.value(QStringLiteral("kind")).toString();
    const QString path = asset.value(QStringLiteral("path")).toString();
    const QString name = asset.value(QStringLiteral("name")).toString();
    const QString assetId = m_app->m_assetLibrary->assetIdAt(assetIndex);
    if (path.isEmpty() || assetId.isEmpty() || !QFileInfo(path).isFile()) {
        m_app->setLastMessage(tr("Could not open the media file"), QStringLiteral("error"));
        return false;
    }

    // Video and images never re-encode: the crop is stored as the asset's source frame and a
    // video's range goes through the same non-destructive trim as the plain-trim path below.
    if (kind == QStringLiteral("image")) {
        const QRectF frame = drift::normalizedSourceFrame(cropX, cropY, cropW, cropH);
        drift::MediaAsset *media = m_app->m_project.asset(assetId);
        if (media->sourceFrame == frame) {
            emit editFinished(true, QString());
            return true;
        }
        const drift::Project before = m_app->m_project.detachedCopy();
        media->sourceFrame = frame;
        m_app->pushProjectEdit(before, tr("Crop image"));
        m_app->finishEdit(tr("Crop saved"));
        emit editFinished(true, QString());
        return true;
    }
    if (kind == QStringLiteral("video")) {
        if (!std::isfinite(inSeconds) || !std::isfinite(outSeconds))
            return false;
        const QRectF frame = drift::normalizedSourceFrame(cropX, cropY, cropW, cropH);
        const drift::TimeUs trimIn = drift::secondsToUs(qMax(0.0, inSeconds));
        const drift::TimeUs trimOut = outSeconds < 0.0 ? -1 : drift::secondsToUs(outSeconds);
        const drift::Project before = m_app->m_project.detachedCopy();
        bool changed = m_app->m_assetLibrary->setAssetTrim(assetIndex, trimIn, trimOut);
        drift::MediaAsset *media = m_app->m_project.asset(assetId);
        if (media->sourceFrame != frame) {
            media->sourceFrame = frame;
            changed = true;
        }
        if (!changed) {
            emit editFinished(true, QString());
            return true;
        }
        m_app->pushProjectEdit(before, tr("Crop video"));
        m_app->finishEdit(tr("Crop saved"));
        emit editFinished(true, QString());
        return true;
    }

    // A plain trim with no real crop needs no re-encode at all: it is stored on the asset the
    // same non-destructive way a placed clip's srcIn/srcOut already works, and the original file
    // is left untouched — mirroring how trimming a clip already on the timeline never re-encodes.
    constexpr double kFullCropEps = 0.001;
    const bool cropIsFull = cropX <= kFullCropEps && cropY <= kFullCropEps
                            && cropW >= 1.0 - kFullCropEps && cropH >= 1.0 - kFullCropEps;
    if (cropIsFull) {
        const drift::TimeUs trimIn = drift::secondsToUs(qMax(0.0, inSeconds));
        const drift::TimeUs trimOut = outSeconds < 0.0 ? -1 : drift::secondsToUs(outSeconds);
        const drift::Project before = m_app->m_project;
        if (!m_app->m_assetLibrary->setAssetTrim(assetIndex, trimIn, trimOut)) {
            // Nothing actually changed (e.g. the range already matches what is stored) — that is
            // still a successful, no-op save from the dialog's point of view.
            emit editFinished(true, QString());
            return true;
        }
        m_app->pushProjectEdit(before, tr("Media trimmed"));
        m_app->settleUndoRefresh();
        m_app->setLastMessage(tr("Trim saved"));
        emit editFinished(true, tr("Trim saved"));
        return true;
    }

    const QString outPath = drift::newEditedMediaPath(m_app->m_project.id(), kind);
    if (outPath.isEmpty()) {
        m_app->setLastMessage(tr("Could not create an output file"), QStringLiteral("error"));
        return false;
    }

    drift::MediaEditSpec spec;
    spec.inputPath = path;
    spec.outputPath = outPath;
    spec.kind = kind;
    spec.inSeconds = inSeconds;
    spec.outSeconds = outSeconds;
    spec.cropX = cropX;
    spec.cropY = cropY;
    spec.cropW = cropW;
    spec.cropH = cropH;
    // The crop rectangle above is drawn in the preview against effectiveRotation, so the encoder
    // has to rotate against that same correction — not the file's own tag — or the region it
    // crops will not be the one the user saw and dragged a box around.
    spec.rotationOverride = asset.value(QStringLiteral("effectiveRotation"), -1).toInt();

    startEditJob(assetId, name, spec, false);
    return true;
}

bool MediaBinController::renderCopy(int assetIndex, double inSeconds, double outSeconds,
                                    double cropX, double cropY, double cropW, double cropH)
{
    if (m_editing) {
        m_app->setLastMessage(tr("An edit is already saving"), QStringLiteral("warning"));
        return false;
    }
    if (!m_app->m_assetLibrary)
        return false;
    const drift::MediaAsset *source = m_app->m_project.asset(m_app->m_assetLibrary->assetIdAt(assetIndex));
    if (!source || source->kind != drift::MediaKind::Video || !QFileInfo(source->path).isFile()) {
        m_app->setLastMessage(tr("Could not open the media file"), QStringLiteral("error"));
        return false;
    }
    const QString outPath = drift::newEditedMediaPath(m_app->m_project.id(), QStringLiteral("video"));
    if (outPath.isEmpty()) {
        m_app->setLastMessage(tr("Could not create an output file"), QStringLiteral("error"));
        return false;
    }

    drift::MediaEditSpec spec;
    spec.inputPath = source->path;
    spec.outputPath = outPath;
    spec.kind = QStringLiteral("video");
    spec.inSeconds = inSeconds;
    spec.outSeconds = outSeconds;
    spec.cropX = cropX;
    spec.cropY = cropY;
    spec.cropW = cropW;
    spec.cropH = cropH;
    // The crop was drawn against the bin's rotation correction; see saveEdit.
    spec.rotationOverride = drift::effectiveRotation(*source);
    const QString name = tr("%1 (trimmed)").arg(source->name);
    const QString folderId = source->folderId;

    m_app->setPlaying(false);
    m_editCancel.storeRelaxed(0);
    m_editProgress = 0.0;
    m_editStatus = tr("Rendering…");
    m_editing = true;
    m_editingAssetId = source->id;
    m_editIsConversion = false;
    emit editChanged();

    (void)QtConcurrent::run([this, spec, name, folderId]() {
        QString error;
        const bool ok = drift::editMedia(spec, &error, [this](double fraction) {
            if (m_editCancel.loadRelaxed() != 0)
                return false;
            QMetaObject::invokeMethod(
                this,
                [this, fraction]() {
                    m_editProgress = fraction;
                    emit editChanged();
                },
                Qt::QueuedConnection);
            return true;
        });
        const MediaInfo info = ok ? MediaProbe::probe(spec.outputPath) : MediaInfo();

        QMetaObject::invokeMethod(
            this,
            [this, ok, error, spec, name, folderId, info]() {
                m_editing = false;
                m_editingAssetId.clear();
                m_editProgress = 0.0;
                m_editStatus = ok ? QString() : error;
                emit editChanged();
                if (ok) {
                    emit copyRendered(m_app->addRenderedVideoAsset(spec.outputPath, name, folderId,
                                                                   info, tr("Render Trimmed Copy")));
                } else {
                    QFile::remove(spec.outputPath);
                    m_app->setLastMessage(error.isEmpty() ? tr("Couldn’t save that edit") : error,
                                          QStringLiteral("error"));
                    emit editFinished(false, error);
                }
                startNextConversion();
            },
            Qt::QueuedConnection);
    });
    return true;
}

void MediaBinController::startEditJob(const QString &assetId, const QString &name,
                                      const drift::MediaEditSpec &spec, bool conversion)
{
    m_app->setPlaying(false);

    m_editCancel.storeRelaxed(0);
    m_editProgress = 0.0;
    m_editStatus = conversion ? tr("Converting…") : tr("Saving…");
    m_editing = true;
    m_editingAssetId = assetId;
    m_editKeepName = name;
    m_editIsConversion = conversion;
    emit editChanged();
    m_app->setLastMessage(conversion ? tr("Converting %1 to an edit-friendly format…").arg(name)
                              : tr("Saving media…"));

    (void)QtConcurrent::run([this, spec, assetId]() {
        QString error;
        const bool ok = drift::editMedia(spec, &error, [this](double fraction) {
            if (m_editCancel.loadRelaxed() != 0)
                return false;
            QMetaObject::invokeMethod(
                this,
                [this, fraction]() {
                    m_editProgress = fraction;
                    emit editChanged();
                },
                Qt::QueuedConnection);
            return true;
        });

        QMetaObject::invokeMethod(
            this,
            [this, ok, error, spec, assetId]() {
                if (!ok) {
                    QFile::remove(spec.outputPath);
                    m_editing = false;
                    m_editingAssetId.clear();
                    m_editKeepName.clear();
                    m_editProgress = 0.0;
                    m_editStatus = error;
                    emit editChanged();
                    m_app->setLastMessage(error.isEmpty() ? tr("Couldn’t save that edit") : error,
                                          QStringLiteral("error"));
                    emit editFinished(false, error);
                    return;
                }

                m_editStatus = tr("Updating the library…");
                emit editChanged();
                // By id: a conversion runs in the background, and the bin may have been sorted
                // or had rows removed since it started.
                const int assetIndex = m_app->m_assetLibrary
                                           ? m_app->m_assetLibrary->indexOfId(assetId)
                                           : -1;
                if (assetIndex < 0
                    || !replaceSource(assetIndex, QUrl::fromLocalFile(spec.outputPath))) {
                    QFile::remove(spec.outputPath);
                    m_editing = false;
                    m_editingAssetId.clear();
                    m_editKeepName.clear();
                    m_editProgress = 0.0;
                    m_editStatus = tr("Couldn’t update the library");
                    emit editChanged();
                    emit editFinished(false, m_editStatus);
                }
            },
            Qt::QueuedConnection);
    });
}

void MediaBinController::convertToConstantFrameRate(const QStringList &assetIds)
{
    for (const QString &id : assetIds) {
        if (!m_conversionQueue.contains(id) && id != m_editingAssetId)
            m_conversionQueue.append(id);
    }
    if (!m_editing)
        startNextConversion();
}

void MediaBinController::startNextConversion()
{
    while (!m_conversionQueue.isEmpty() && !m_editing && m_app->m_assetLibrary) {
        const QString assetId = m_conversionQueue.takeFirst();
        const int assetIndex = m_app->m_assetLibrary->indexOfId(assetId);
        const QVariantMap asset = m_app->m_assetLibrary->assetAt(assetIndex);
        const QString path = asset.value(QStringLiteral("path")).toString();
        if (asset.value(QStringLiteral("kind")).toString() != QLatin1String("video")
            || !QFileInfo(path).isFile())
            continue;
        const QString outPath = drift::newEditedMediaPath(m_app->m_project.id(), QStringLiteral("video"));
        if (outPath.isEmpty()) {
            m_app->setLastMessage(tr("Could not create an output file"), QStringLiteral("error"));
            m_conversionQueue.clear();
            return;
        }

        drift::MediaEditSpec spec;
        spec.inputPath = path;
        spec.outputPath = outPath;
        spec.kind = QStringLiteral("video");
        spec.conformFrameRate = true;
        // Baked upright the way a crop is, so the bin's rotation correction carries over.
        spec.rotationOverride = asset.value(QStringLiteral("effectiveRotation"), -1).toInt();
        startEditJob(assetId, asset.value(QStringLiteral("name")).toString(), spec, true);
    }
}

void MediaBinController::finalizeReplace(const QString &assetId, const drift::MediaAsset &filled,
                                         bool ok)
{
    // A refresh re-read the asset's own file after it changed in place (AssetLibrary
    // startRefreshProbe). It is nobody's replace, so the replace bookkeeping below is left for
    // the one it belongs to.
    const bool refresh = m_app->m_assetLibrary && m_app->m_assetLibrary->takeRefresh(assetId);

    // The probe has landed, so the row stops being busy whatever the outcome below turns out to
    // be — including the refusals, which never touch the project at all.
    if (!refresh && m_replacingAssetId == assetId) {
        m_replacingAssetId.clear();
        emit replacingAssetIdChanged();
    }
    // Taken here rather than at the point of use so the refusals below cannot leave it set for
    // whichever replace runs next.
    const QString replacementSourceUri =
        refresh ? QString() : std::exchange(m_replacingAssetSourceUri, QString());

    const bool fromEdit = (m_editingAssetId == assetId);
    const QString keptEditName = fromEdit ? m_editKeepName : QString();
    auto finishEditJob = [&](bool editOk, const QString &message) {
        if (!fromEdit)
            return;
        m_editing = false;
        m_editingAssetId.clear();
        m_editKeepName.clear();
        m_editProgress = editOk ? 1.0 : 0.0;
        m_editStatus = message;
        emit editChanged();
        emit editFinished(editOk, message);
    };

    // Every way out that leaves the project untouched. A refresh was never asked for in the
    // replace dialog, so it reports on the status line instead of through replaceFinished.
    const QString currentName = [&] {
        const drift::MediaAsset *asset = m_app->m_project.asset(assetId);
        return asset ? asset->name : QString();
    }();
    if (refresh)
        m_refreshFailed.remove(assetId);
    auto refuse = [&](const QString &message) {
        finishEditJob(false, message);
        if (refresh) {
            m_refreshFailed.insert(assetId);
            m_app->setLastMessage(tr("Could not refresh “%1”: %2").arg(currentName, message),
                                  QStringLiteral("warning"));
        } else if (!fromEdit)
            emit replaceFinished(false, message, 0);
    };

    const drift::MediaAsset *current = m_app->m_project.asset(assetId);
    if (!current) {
        // The row was removed while the probe ran.
        refuse(tr("That media is no longer in this project."));
        return;
    }

    if (!ok) {
        refuse(tr("That file could not be read."));
        return;
    }

    // A clip's type is fixed when it is created and decides which track it may sit on, so media
    // of another kind would leave e.g. a video clip on a video track with nothing to draw.
    if (filled.kind != current->kind) {
        refuse(tr("“%1” is %2, but this slot holds %3.")
                   .arg(filled.name, drift::mediaKindToString(filled.kind),
                        drift::mediaKindToString(current->kind)));
        return;
    }

    const QString newName = refresh ? current->name
                                    : (keptEditName.isEmpty() ? filled.name : keptEditName);
    const drift::Project before = m_app->m_project;
    drift::MediaAsset replacement = filled;
    replacement.name = newName;
    replacement.sourceUri = refresh ? current->sourceUri : replacementSourceUri;
    // Every edit job writes onto a constant-rate grid, so its output needs no check. The
    // edit-friendly mark comes from the conversion, and a later crop of converted media keeps it.
    if (fromEdit) {
        replacement.frameRateKnown = true;
        replacement.variableFrameRate = false;
        replacement.editFriendly = m_editIsConversion || current->editFriendly;
    }
    // Same file, new content: what the user set on the asset still stands. Only what was read
    // from the file is taken from the probe, and the bin trim is clamped to the new length.
    if (refresh) {
        replacement.rotationOverride = current->rotationOverride;
        replacement.generator = current->generator;
        replacement.editFriendly = current->editFriendly;
        replacement.trimInUs = current->trimInUs;
        replacement.trimOutUs = current->trimOutUs;
        if (replacement.durationUs > 0) {
            if (replacement.trimOutUs > replacement.durationUs)
                replacement.trimOutUs = -1;
            if (replacement.trimInUs >= replacement.durationUs)
                replacement.trimInUs = 0;
        }
    }
    const QString path = current->path;
    // Captured before applyProbedSource overwrites `current` in place — rebindClipsToAsset needs
    // to know what the bin's correction *was* to rebase each clip's own by how much it changed.
    const int oldBinCorrection = drift::rotationCorrectionOf(*current);
    if (!m_app->m_assetLibrary->applyProbedSource(assetId, replacement)) {
        refuse(tr("That media is no longer in this project."));
        return;
    }
    // A replace onto the asset's own path is new content at an old key as well. The first pass
    // ran before the probe (sourceContentChanging); this one catches a decoder that was mid-read
    // then and so stayed open on the old file.
    if (replacement.path == path)
        m_app->forgetChangedMediaSource(path, /*beforeReprobe=*/false);

    const int adjusted = rebindClipsToAsset(assetId, replacement, oldBinCorrection);
    const QString label = fromEdit ? tr("Media edited")
                                   : (refresh ? tr("Media refreshed") : tr("Media replaced"));
    m_app->pushProjectEdit(before, label);
    m_app->finishEdit(label);
    finishEditJob(true, newName);
    if (!fromEdit && !refresh)
        emit replaceFinished(true, newName, adjusted);
}

int MediaBinController::rebindClipsToAsset(const QString &assetId, const drift::MediaAsset &asset,
                                      int oldBinCorrection)
{
    const int newBinCorrection = drift::rotationCorrectionOf(asset);
    int adjusted = 0;
    for (drift::Track &track : m_app->m_project.tracks()) {
        for (drift::Clip &clip : track.clips) {
            if (clip.assetId != assetId)
                continue;

            // Rewriting the clip's own copy of the path is what actually moves the pixels. It
            // also keys the reverse-proxy and audio-block caches, so for a new path both fall out
            // of scope on their own; the same path is handled by forgetChangedMediaSource.
            clip.path = asset.path;
            clip.thumbnailPath = asset.thumbnailPath;
            clip.filmstripPath = asset.filmstripPath;
            // The clip's correction started as the bin's (applyAssetLayout) and may have been
            // turned further on the timeline since. Keep that extra part and swap the bin's old
            // share for its new one — most pointedly, a crop-save bakes the bin's correction into
            // the re-encoded pixels, so the freshly probed `asset` has none left and every clip
            // has to drop that same amount or it turns already-upright pixels a second time.
            clip.rotationCorrection =
                ((clip.rotationCorrection - oldBinCorrection + newBinCorrection) % 360 + 360) % 360;
            // Landmarks are baked against the old pixels. Left in place they would keep the face
            // warps tracking a face the new footage never had, and render without erroring.
            clip.faceTrackPath.clear();
            clip.faceTrackSrcOffsetUs = 0;
            clip.depthPath.clear();

            // Stills have no source range to fit.
            if (asset.durationUs <= 0 || clip.srcOut <= asset.durationUs)
                continue;

            // The framed window starts past the end of the new file, so there is nothing about
            // it worth preserving; rebase to the head and keep as much of the span as fits.
            if (clip.srcIn >= asset.durationUs)
                clip.srcIn = 0;

            if (clip.hasSpeedCurve()) {
                // Curved clips derive their timeline duration from the source range, so the
                // range is clamped first and the ramp re-timed against what is left.
                clip.srcOut = asset.durationUs;
                clip.syncDurationFromSpeedCurve();
            } else {
                // Pulls timelineDuration along with the shortened range, so the clip never
                // addresses frames past the end of its media.
                clip.syncSrcOutFromSpeed(asset.durationUs);
            }
            ++adjusted;
        }
    }
    return adjusted;
}
