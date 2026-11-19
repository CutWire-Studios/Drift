#include "app/bin/MediaBinController.h"

#include "app/editor/AppController.h"
#include "app/editor/AppControllerDetail.h"
#include "app/bin/AssetLibrary.h"
#include "app/mcp/McpController.h"
#include "app/editor/ProjectFileController.h"
#include "app/mcp/protocol/McpJson.h"
#include "core/project/media/ImageSequence.h"
#include "engine/platform/AndroidUri.h"
#include "engine/encode/MediaEditor.h"
#include "engine/decode/probe/MediaProbe.h"

#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QRegularExpression>
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
    // An undo step without finishEdit: the revision (and the change feed) moves here.
    if (McpController *mcp = m_app->mcp())
        mcp->bumpEditRevision();
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
    // An undo step without finishEdit: the revision (and the change feed) moves here.
    if (McpController *mcp = m_app->mcp())
        mcp->bumpEditRevision();
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
    // An undo step without finishEdit: the revision (and the change feed) moves here.
    if (McpController *mcp = m_app->mcp())
        mcp->bumpEditRevision();
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
    // An undo step without finishEdit: the revision (and the change feed) moves here.
    if (McpController *mcp = m_app->mcp())
        mcp->bumpEditRevision();
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
    // An undo step without finishEdit: the revision (and the change feed) moves here.
    if (McpController *mcp = m_app->mcp())
        mcp->bumpEditRevision();
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
    // An undo step without finishEdit: the revision (and the change feed) moves here.
    if (McpController *mcp = m_app->mcp())
        mcp->bumpEditRevision();
    m_app->settleUndoRefresh();
    return moved;
}

namespace {

// mm:ss.cc, the same reading the preview window's transport shows.
QString clockLabel(double seconds)
{
    const int total = static_cast<int>(seconds);
    const int centis = static_cast<int>((seconds - total) * 100);
    return QStringLiteral("%1:%2.%3")
        .arg(total / 60, 2, 10, QLatin1Char('0'))
        .arg(total % 60, 2, 10, QLatin1Char('0'))
        .arg(centis, 2, 10, QLatin1Char('0'));
}

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
    if (path.isEmpty() || !QDir(path).exists())
        return false;

    m_importingFolder = true;
    emit importingFolderChanged();
    m_folderBatchFolders = 0;
    m_folderBatchFiles = 0;
    m_folderBatchSkipped = 0;
    m_folderBatchTruncated = false;
    beginFolderWalk(path);
    return true;
}

QList<QUrl> MediaBinController::importFoldersFrom(const QList<QUrl> &urls)
{
    QList<QUrl> files;
    QList<QUrl> folders;
    for (const QUrl &url : urls) {
        // Only a local path can be a directory: an Android content:// pick is always a document,
        // and QFileInfo would reject it anyway, so it stays on the ordinary media path.
        const QString path = url.isLocalFile() ? url.toLocalFile() : QString();
        if (!path.isEmpty() && QFileInfo(path).isDir())
            folders.append(url);
        else
            files.append(url);
    }

    if (!folders.isEmpty()) {
        const bool idle = !m_importingFolder;
        m_folderQueue.append(folders);
        if (idle) {
            m_importingFolder = true;
            emit importingFolderChanged();
            m_folderBatchFolders = 0;
            m_folderBatchFiles = 0;
            m_folderBatchSkipped = 0;
            m_folderBatchTruncated = false;
            startNextFolderImport();
        }
    }
    return files;
}

void MediaBinController::startNextFolderImport()
{
    while (!m_folderQueue.isEmpty()) {
        const QUrl url = m_folderQueue.takeFirst();
        const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (path.isEmpty() || !QDir(path).exists())
            continue;
        beginFolderWalk(path);
        return;
    }

    m_importingFolder = false;
    emit importingFolderChanged();
    emit folderImportFinished(m_folderBatchFolders, m_folderBatchFiles, m_folderBatchSkipped,
                              m_folderBatchTruncated);
}

void MediaBinController::beginFolderWalk(const QString &path)
{
    const QDir dir(path);

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

        m_folderBatchFolders += folderCount;
        m_folderBatchFiles += fileCount;
        m_folderBatchSkipped += plan.skipped;
        m_folderBatchTruncated = m_folderBatchTruncated || plan.truncated;
        startNextFolderImport();
    });

    // Seeded with the batch's running count so a drop of several folders stops at the same file
    // limit one folder would, rather than at the limit times the number of folders.
    const int taken = m_folderBatchFiles;
    watcher->setFuture(QtConcurrent::run([dir, taken]() {
        FolderImportPlan plan;
        int fileCount = taken;
        QSet<QString> visitedDirs;
        planDirectory(dir, -1, plan, fileCount, visitedDirs);
        return plan;
    }));
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
        reportReplace(assetId, false, tr("That file could not be read."), 0);
        return false;
    }

    // Two bin rows sharing a path would break import de-duplication, which resolves a path to
    // whichever row holds it first.
    const QString absolutePath = fileInfo.absoluteFilePath();
    const int existing = m_app->m_assetLibrary->indexOfPath(absolutePath);
    if (existing >= 0 && existing != assetIndex) {
        reportReplace(assetId, false, tr("That file is already in this project."), 0);
        return false;
    }

    if (!m_app->m_assetLibrary->startReplaceProbe(assetIndex, absolutePath)) {
        reportReplace(assetId, false, tr("That file could not be read."), 0);
        return false;
    }

    // Reading a file off a slow disk is the one part of this the user waits on with nothing to
    // show for it, so the row it belongs to goes busy until the probe lands.
    m_replacingAssetId = assetId;
    // The probe lands outside the agent's call (its wait runs a nested event loop), so the change
    // feed learns whose replace it was from here.
    if (m_app->mcp() && m_app->mcp()->inAgentCall())
        m_replacesByAgent.insert(assetId);
    else
        m_replacesByAgent.remove(assetId);
    // applyProbedSource overwrites the whole struct from the probe result, which knows nothing
    // about where the file came from — so the URI is held here and put back in finalizeReplace.
    m_replacingAssetSourceUri = sourceUri;
    emit replacingAssetIdChanged();
    return true;
}

void MediaBinController::reportReplace(const QString &assetId, bool ok, const QString &message,
                                      int adjustedClips)
{
    emit replaceFinished(ok, message, adjustedClips);
    emit replaceFinishedFor(assetId, ok, message);
}

bool MediaBinController::replaceSourceWith(int assetIndex, const QUrl &url,
                                           std::function<void(drift::MediaAsset &)> adjust)
{
    m_replaceAdjust = std::move(adjust);
    if (replaceSource(assetIndex, url))
        return true;
    m_replaceAdjust = nullptr;
    return false;
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
        // An undo step without finishEdit: the revision (and the change feed) moves here.
        if (McpController *mcp = m_app->mcp())
            mcp->bumpEditRevision();
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

QString MediaBinController::addSubclip(const QString &assetId, double inSeconds, double outSeconds)
{
    if (!std::isfinite(inSeconds) || !std::isfinite(outSeconds))
        return {};
    const drift::Project before = m_app->m_project;
    const QString id = appendSubclip(assetId, drift::secondsToUs(inSeconds),
                                     drift::secondsToUs(outSeconds), QString(), 0);
    if (id.isEmpty())
        return {};
    m_app->pushProjectEdit(before, tr("Sub-clip added"));
    // An undo step without finishEdit: the revision (and the change feed) moves here.
    if (McpController *mcp = m_app->mcp())
        mcp->bumpEditRevision();
    m_app->settleUndoRefresh();
    return id;
}

QString MediaBinController::appendSubclip(const QString &assetId, drift::TimeUs inUs,
                                          drift::TimeUs outUs, const QString &name, int rating)
{
    if (!m_app->m_assetLibrary)
        return {};
    const drift::MediaAsset *source = m_app->m_project.asset(assetId);
    if (!source || (source->kind != drift::MediaKind::Video && source->kind != drift::MediaKind::Audio)
        || !QFileInfo(source->path).isFile())
        return {};

    const drift::TimeUs duration = source->durationUs;
    const drift::TimeUs trimIn = qBound<drift::TimeUs>(0, inUs, duration);
    drift::TimeUs trimOut = qBound<drift::TimeUs>(trimIn, outUs, duration);
    if (trimOut <= trimIn)
        return {};
    if (trimOut >= duration)
        trimOut = -1;

    // Copy first: the project's hash can rehash when the new asset is inserted below.
    drift::MediaAsset sub = *source;
    const QString range = QStringLiteral("%1–%2").arg(clockLabel(drift::usToSeconds(trimIn)),
                                                     clockLabel(drift::usToSeconds(trimOut < 0 ? duration : trimOut)));
    sub.id.clear();
    sub.name = name.isEmpty() ? tr("%1 (%2)").arg(source->name, range) : name;
    sub.trimInUs = trimIn;
    sub.trimOutUs = trimOut;
    sub.ranges.clear();
    sub.markers.clear();
    sub.rating = qBound(-1, rating, 1);
    sub.thumbnailPath.clear();
    sub.filmstripPath.clear();

    const QString id = m_app->m_project.addAsset(sub);
    QList<QString> &order = m_app->m_project.assetOrder();
    order.move(order.size() - 1, order.indexOf(assetId) + 1);
    m_app->m_assetLibrary->syncToProject();
    m_app->m_assetLibrary->ensureMedia(m_app->m_assetLibrary->indexOfId(id));
    return id;
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
                m_editOutputPath = spec.outputPath;
                m_editOutputAssetId = assetId;
                if (assetIndex < 0
                    || !replaceSource(assetIndex, QUrl::fromLocalFile(spec.outputPath))) {
                    m_editOutputPath.clear();
                    m_editOutputAssetId.clear();
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
    const bool byAgent = !refresh && m_replacesByAgent.remove(assetId);

    std::function<void(drift::MediaAsset &)> adjust = std::exchange(m_replaceAdjust, nullptr);
    if (refresh)
        adjust = nullptr;

    const bool fromEdit = (m_editingAssetId == assetId);
    // The edit's own replace has landed. Not keyed on fromEdit: a replace of the asset while it
    // encoded ends the edit early, and its render still lands here afterwards.
    if (!refresh && assetId == m_editOutputAssetId) {
        m_editOutputPath.clear();
        m_editOutputAssetId.clear();
    }
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
        } else if (!fromEdit) {
            reportReplace(assetId, false, message, 0);
        }
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
        replacement.sourceFrame = current->sourceFrame;
        replacement.editFriendly = current->editFriendly;
        replacement.trimInUs = current->trimInUs;
        replacement.trimOutUs = current->trimOutUs;
        replacement.ranges = current->ranges;
        replacement.markers = current->markers;
        replacement.rating = current->rating;
        if (replacement.durationUs > 0) {
            if (replacement.trimOutUs > replacement.durationUs)
                replacement.trimOutUs = -1;
            if (replacement.trimInUs >= replacement.durationUs)
                replacement.trimInUs = 0;
        }
    }
    if (adjust)
        adjust(replacement);
    const QString path = current->path;
    // Captured before applyProbedSource overwrites `current` in place — rebindClipsToAsset needs
    // to know what the bin's correction *was* to rebase each clip's own by how much it changed.
    const int oldBinCorrection = drift::rotationCorrectionOf(*current);
    const bool relinkingMissing = !refresh && !fromEdit && m_app->m_assetLibrary->isMissing(assetId);
    // A refresh of unchanged media keeps the landmarks and depth maps, which were baked from it.
    const bool mediaChanged = !refresh || current->durationUs != replacement.durationUs
        || current->width != replacement.width || current->height != replacement.height
        || current->sourceSize != replacement.sourceSize;
    if (!m_app->m_assetLibrary->applyProbedSource(assetId, replacement)) {
        refuse(tr("That media is no longer in this project."));
        return;
    }
    // A replace onto the asset's own path is new content at an old key as well. The first pass
    // ran before the probe (sourceContentChanging); this one catches a decoder that was mid-read
    // then and so stayed open on the old file.
    if (replacement.path == path)
        m_app->forgetChangedMediaSource(path, /*beforeReprobe=*/false);

    const int adjusted = rebindClipsToAsset(assetId, replacement, oldBinCorrection, mediaChanged);
    // Clips not bound to this asset id, and masks, effects and transcripts, can still name the
    // missing file; a relink should leave nothing pointing at it.
    QHash<QString, QString> siblings;
    if (relinkingMissing && replacement.path != path) {
        ProjectFileController::remapPaths(m_app->m_project, {{path, replacement.path}});
        siblings = findSiblingRelinks(assetId, path, replacement.path);
    }
    const QString label = fromEdit ? tr("Media edited")
                                   : (refresh ? tr("Media refreshed") : tr("Media replaced"));
    m_app->pushProjectEdit(before, label);
    if (byAgent && m_app->mcp())
        m_app->mcp()->markNextChangeByAgent();
    m_app->finishEdit(label);
    finishEditJob(true, newName);
    if (!fromEdit && !refresh)
        reportReplace(assetId, true, newName, adjusted);
    m_siblingRelink = siblings;
    if (!siblings.isEmpty())
        emit siblingRelinkAvailable(int(siblings.size()),
                                    QDir::toNativeSeparators(QFileInfo(replacement.path).absolutePath()));
}

QHash<QString, QString> MediaBinController::findSiblingRelinks(const QString &assetId,
                                                               const QString &oldPath,
                                                               const QString &newPath) const
{
    QHash<QString, QString> remap;
    AssetLibrary *library = m_app->m_assetLibrary;
    const QString oldDir = QFileInfo(oldPath).absolutePath();
    const QDir newDir = QFileInfo(newPath).absoluteDir();
    for (const QString &id : m_app->m_project.assetOrder()) {
        if (id == assetId || !library->isMissing(id))
            continue;
        const drift::MediaAsset *asset = m_app->m_project.asset(id);
        const QFileInfo old(asset->path);
        if (old.absolutePath() != oldDir)
            continue;
        const QFileInfo candidate(newDir.filePath(old.fileName()));
        if (!candidate.isFile() || library->indexOfPath(candidate.absoluteFilePath()) >= 0)
            continue;
        // Projects from before fingerprints have no size to compare; the user confirms anyway.
        if (asset->sourceSize > 0 && candidate.size() != asset->sourceSize)
            continue;
        remap.insert(asset->path, candidate.absoluteFilePath());
    }
    return remap;
}

void MediaBinController::dropPendingReplace()
{
    // An edit (crop, trim, convert) whose render has started its replace. One still encoding
    // hasn't, and is left to finish.
    const QString editOutput = std::exchange(m_editOutputPath, QString());
    const QString editAssetId = std::exchange(m_editOutputAssetId, QString());
    if (!m_replacingAssetId.isEmpty()) {
        const QString assetId = std::exchange(m_replacingAssetId, QString());
        m_replacingAssetSourceUri.clear();
        m_replacesByAgent.remove(assetId);
        m_replaceAdjust = nullptr;
        emit replacingAssetIdChanged();
        // Ends whoever waits on it (the replace dialog, the Source Lab, which removes its
        // render). The edit's own replace ends below.
        if (editOutput.isEmpty() || assetId != editAssetId)
            reportReplace(assetId, false, tr("The project was closed before the replace landed"), 0);
    }
    if (!editOutput.isEmpty()) {
        // The edit's own render, which nothing else points at.
        QFile::remove(editOutput);
        // Conversions queued behind it were picked in the old project.
        m_conversionQueue.clear();
        // Unless a replace of the asset while it encoded ended the edit already.
        if (m_editing && m_editingAssetId == editAssetId) {
            m_editing = false;
            m_editingAssetId.clear();
            m_editKeepName.clear();
            m_editProgress = 0.0;
            m_editStatus = tr("The project was closed before the edit landed");
            emit editChanged();
            emit editFinished(false, m_editStatus);
        }
    }
}

void MediaBinController::relinkSiblings()
{
    const QHash<QString, QString> remap = std::exchange(m_siblingRelink, {});
    if (remap.isEmpty() || !m_app->m_assetLibrary)
        return;
    if (relinkPaths(remap, tr("Relink media")))
        m_app->setLastMessage(tr("Relinked %n file(s)", nullptr, int(remap.size())),
                              QStringLiteral("success"));
}

bool MediaBinController::relinkPaths(const QHash<QString, QString> &remap, const QString &label)
{
    if (remap.isEmpty() || !m_app->m_assetLibrary)
        return false;
    const drift::Project before = m_app->m_project;
    m_app->projectFile()->remapProjectPaths(remap);
    m_app->pushProjectEdit(before, label);
    if (m_app->m_editRefusedByLock) {
        // pushProjectEdit put the project back.
        m_app->m_editRefusedByLock = false;
        m_app->setLastMessage(tr("Track is locked"), QStringLiteral("warning"));
        return false;
    }
    // An undo step without finishEdit: the revision (and the change feed) moves here.
    if (McpController *mcp = m_app->mcp())
        mcp->bumpEditRevision();
    m_app->settleUndoRefresh();
    m_app->m_assetLibrary->setProject(&m_app->m_project);
    m_folderModel.setProject(&m_app->m_project);
    m_app->restoreFilmstripsAfterLoad();
    m_app->notifyTracksChanged();
    m_app->projectFile()->setDirty(true);
    return true;
}

int MediaBinController::rebindClipsToAsset(const QString &assetId, const drift::MediaAsset &asset,
                                      int oldBinCorrection, bool mediaChanged)
{
    const int newBinCorrection = drift::rotationCorrectionOf(asset);
    int adjusted = 0;
    m_app->m_project.forEachTrackList([&](QList<drift::Track> &tracks) {
        for (drift::Track &track : tracks) {
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
                drift::appdetail::refreshClipFrame(m_app->m_project, clip);
                // Landmarks are baked against the old pixels. Left in place they would keep the face
                // warps tracking a face the new footage never had, and render without erroring.
                if (mediaChanged) {
                    clip.faceTrackPath.clear();
                    clip.faceTrackSrcOffsetUs = 0;
                    clip.depthPath.clear();
                }

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
                    // addresses frames past the end of its media. The range is re-derived from the
                    // timeline span first, so a rebased srcIn keeps the span instead of the old srcOut.
                    clip.srcOut = clip.srcIn + (clip.loops() ? clip.srcOut - clip.srcIn : clip.sourceSpanUs());
                    clip.syncSrcOutFromSpeed(asset.durationUs);
                }
                ++adjusted;
            }
        }
    });
    return adjusted;
}

QJsonObject MediaBinController::validateProject(int limit) const
{
    const drift::Project &project = m_app->m_project;
    QJsonArray problems;
    QJsonObject counts;
    int total = 0;
    const auto add = [&](const QString &kind, const QString &message, QJsonObject where) {
        counts.insert(kind, counts.value(kind).toInt() + 1);
        if (++total > limit)
            return;
        where.insert(QStringLiteral("kind"), kind);
        where.insert(QStringLiteral("message"), message);
        problems.append(where);
    };

    // Files are checked once each. Paths that aren't plain files (content: URIs, resources) are
    // left to the platform; an image sequence is there when its first frame is, as the bin checks it.
    QHash<QString, bool> present;
    const auto fileMissing = [&](const QString &path) {
        if (path.isEmpty() || path.startsWith(QLatin1String("content:")) || path.startsWith(QLatin1Char(':')))
            return false;
        auto it = present.constFind(path);
        if (it == present.constEnd()) {
            const std::optional<drift::ImageSequence> sequence = drift::parseImageSequencePath(path);
            it = present.insert(path, QFileInfo::exists(sequence ? drift::imageSequenceFramePath(*sequence, sequence->start)
                                                                 : path));
        }
        return !it.value();
    };
    // How many clips use each asset, anywhere: an unused bin item that's gone is less urgent.
    QHash<QString, int> uses;
    const auto countUses = [&](const QList<drift::Track> &tracks) {
        for (const drift::Track &track : tracks)
            for (const drift::Clip &clip : track.clips)
                if (!clip.assetId.isEmpty())
                    ++uses[clip.assetId];
    };
    countUses(project.rootTracks());
    for (const QString &id : project.sequenceIds())
        countUses(project.sequenceTracks(id));

    // Media: a file that is gone.
    QStringList assetIds = project.assets().keys();
    assetIds.sort();
    for (const QString &id : std::as_const(assetIds)) {
        const drift::MediaAsset &asset = project.assets().value(id);
        if (asset.kind != drift::MediaKind::Composite && fileMissing(asset.path))
            add(QStringLiteral("missing_media"), QStringLiteral("%1 isn't at %2").arg(asset.name, asset.path),
                {{QStringLiteral("asset"), id}, {QStringLiteral("clips"), uses.value(id)}});
    }

    // A clip's own file that's gone, once per file (a split clip is many clips on one file), with how
    // many clips play it; reported after the timelines are walked. Not one an asset row has named.
    QSet<QString> assetFiles;
    for (const drift::MediaAsset &asset : project.assets())
        if (!asset.path.isEmpty())
            assetFiles.insert(QDir::cleanPath(asset.path));
    QStringList goneClipFiles;
    QHash<QString, QPair<QJsonObject, int>> goneClipFile;
    const auto clipFileGone = [&](const QString &path, const QJsonObject &where) {
        const QString cleaned = QDir::cleanPath(path);
        if (assetFiles.contains(cleaned) || !fileMissing(path))
            return;
        auto it = goneClipFile.find(cleaned);
        if (it == goneClipFile.end()) {
            goneClipFiles.append(cleaned);
            goneClipFile.insert(cleaned, {where, 1});
        } else {
            ++it->second;
        }
    };

    const drift::TimeUs frameUs = project.fps() > 0 ? 1000000 / project.fps() : 0;
    const auto checkTracks = [&](const QList<drift::Track> &tracks, const QString &sequence) {
        const auto at = [&](QJsonObject where, int t) {
            where.insert(QStringLiteral("track"), t);
            if (!sequence.isEmpty())
                where.insert(QStringLiteral("sequence"), sequence);
            return where;
        };
        QSet<QString> clipIds;
        QHash<QString, int> linkCount;
        for (const drift::Track &track : tracks)
            for (const drift::Clip &clip : track.clips) {
                clipIds.insert(clip.id);
                if (!clip.linkId.isEmpty())
                    ++linkCount[clip.linkId];
            }
        for (int t = 0; t < tracks.size(); ++t) {
            const drift::Track &track = tracks.at(t);
            if (track.isAdjustmentLane() && track.clips.isEmpty())
                add(QStringLiteral("empty_lane"), QStringLiteral("An effects lane with nothing on it"), at({}, t));
            for (const drift::Clip &clip : track.clips) {
                const QJsonObject here = at({{QStringLiteral("clip"), clip.id}}, t);
                // An effect pinned to a clip that isn't there any more.
                if (!clip.linkedClipId.isEmpty() && !clipIds.contains(clip.linkedClipId))
                    add(QStringLiteral("orphaned_effect"),
                        QStringLiteral("Pinned to clip %1, which is gone").arg(clip.linkedClipId), here);
                // One side of an A/V link.
                if (!clip.linkId.isEmpty() && linkCount.value(clip.linkId) < 2)
                    add(QStringLiteral("broken_link"),
                        QStringLiteral("Linked (%1), but nothing else is").arg(clip.linkId), here);
                const bool media = clip.type == drift::ClipType::Video || clip.type == drift::ClipType::Audio
                                   || clip.type == drift::ClipType::Image;
                // A recording, denoised audio or a 3D model keeps only its path.
                if (clip.assetId.isEmpty()) {
                    clipFileGone(clip.path, here);
                    continue;
                }
                const drift::MediaAsset *asset = project.asset(clip.assetId);
                if (!asset) {
                    add(QStringLiteral("missing_asset"),
                        QStringLiteral("Its asset %1 isn't in the project").arg(clip.assetId), here);
                    continue;
                }
                QJsonObject withAsset = here;
                withAsset.insert(QStringLiteral("asset"), clip.assetId);
                // The file the clip itself plays, when it isn't its asset's (reported above): a 3D
                // model swapped in place, or a clip relinked on its own.
                if (!clip.path.isEmpty() && QDir::cleanPath(clip.path) != QDir::cleanPath(asset->path))
                    clipFileGone(clip.path, withAsset);
                // Media clips only: a 3D model swapped in place (set_model3d_source) keeps its old
                // asset id on purpose, and resources (stickers) were moved by a load migration.
                if (media && !clip.path.isEmpty() && !asset->path.isEmpty() && !clip.path.startsWith(QLatin1Char(':'))
                    && QDir::cleanPath(clip.path) != QDir::cleanPath(asset->path))
                    add(QStringLiteral("asset_mismatch"),
                        QStringLiteral("Plays %1, but its asset %2 is %3").arg(clip.path, asset->name, asset->path),
                        withAsset);
                // Source past the end of time-based media (more than a frame, for rounding).
                if ((asset->kind == drift::MediaKind::Video || asset->kind == drift::MediaKind::Audio)
                    && asset->durationUs > 0 && clip.srcOut > asset->durationUs + frameUs) {
                    add(QStringLiteral("range_past_media"),
                        QStringLiteral("Out point %1 s is past the end of %2 (%3 s)")
                            .arg(drift::usToSeconds(clip.srcOut), 0, 'f', 3)
                            .arg(asset->name)
                            .arg(drift::usToSeconds(asset->durationUs), 0, 'f', 3),
                        withAsset);
                }
            }
            // A transition between clips that aren't both on this track.
            QSet<QString> onTrack;
            for (const drift::Clip &clip : track.clips)
                onTrack.insert(clip.id);
            QSet<QPair<QString, QString>> joined;
            for (const drift::Transition &transition : track.transitions) {
                joined.insert({transition.fromClipId, transition.toClipId});
                if (!onTrack.contains(transition.fromClipId) || !onTrack.contains(transition.toClipId))
                    add(QStringLiteral("dangling_transition"),
                        QStringLiteral("A transition from %1 to %2, but one of them isn't on this track")
                            .arg(transition.fromClipId, transition.toClipId),
                        at({{QStringLiteral("transition"), transition.id}}, t));
            }
            // Overlaps, when overlap is off; a crossfade (a transition joining them) is one on purpose.
            if (m_app->allowClipOverlap() || track.isAdjustment())
                continue;
            QList<const drift::Clip *> byStart;
            for (const drift::Clip &clip : track.clips)
                byStart.append(&clip);
            // Stable: clips starting together keep their track order, so a crossfade between them
            // reads the same way on every call.
            std::stable_sort(byStart.begin(), byStart.end(),
                      [](const drift::Clip *a, const drift::Clip *b) { return a->timelineStart < b->timelineStart; });
            // Against every earlier clip still running, so one inside a long clip isn't missed and a
            // crossfade with one of them doesn't hide an overlap with another.
            QList<const drift::Clip *> running;
            for (const drift::Clip *b : std::as_const(byStart)) {
                running.removeIf([b](const drift::Clip *a) { return a->timelineEnd() <= b->timelineStart; });
                for (const drift::Clip *a : std::as_const(running)) {
                    if (!joined.contains({a->id, b->id}) && !joined.contains({b->id, a->id})) {
                        add(QStringLiteral("overlap"), QStringLiteral("Overlaps %1").arg(a->id),
                            at({{QStringLiteral("clip"), b->id}}, t));
                        break;
                    }
                }
                running.append(b);
            }
        }
    };
    checkTracks(project.rootTracks(), {});
    QStringList sequences = project.sequenceIds();
    sequences.sort();
    for (const QString &id : std::as_const(sequences))
        checkTracks(project.sequenceTracks(id), id);
    for (const QString &path : std::as_const(goneClipFiles)) {
        QJsonObject where = goneClipFile.value(path).first;
        where.insert(QStringLiteral("clips"), goneClipFile.value(path).second);
        add(QStringLiteral("missing_media"), QStringLiteral("Plays %1, which is gone").arg(path), where);
    }

    QJsonObject out{{QStringLiteral("problems"), problems}, {QStringLiteral("n"), total}};
    // Rows without a sequence are on the main timeline; track indexes there are its own, not the
    // composite's that's open (whose rows carry its id).
    if (!project.activeSequenceId().isEmpty())
        out.insert(QStringLiteral("open_sequence"), project.activeSequenceId());
    if (total > 0)
        out.insert(QStringLiteral("counts"), counts);
    if (total > limit)
        out.insert(QStringLiteral("truncated"), true);
    return out;
}

namespace {

// The file a path stands for: an image sequence's first frame (its path is a pattern).
QString fileOf(const QString &path)
{
    if (const auto seq = drift::parseImageSequencePath(path))
        return drift::imageSequenceFramePath(*seq, seq->start);
    return path;
}

// A path that names a file which isn't there. Paths that aren't plain files (content: URIs,
// resources) are left to the platform.
bool fileGone(const QString &path)
{
    return !path.isEmpty() && !path.startsWith(QLatin1String("content:")) && !path.startsWith(QLatin1Char(':'))
           && !QFileInfo::exists(fileOf(path));
}

QHash<QString, int> clipsPerAsset(const drift::Project &project)
{
    QHash<QString, int> uses;
    const auto count = [&](const QList<drift::Track> &tracks) {
        for (const drift::Track &track : tracks)
            for (const drift::Clip &clip : track.clips)
                if (!clip.assetId.isEmpty())
                    ++uses[clip.assetId];
    };
    count(project.rootTracks());
    for (const QString &id : project.sequenceIds())
        count(project.sequenceTracks(id));
    return uses;
}

// File names compare as the platform's file system does.
constexpr Qt::CaseSensitivity kPathCase =
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    Qt::CaseInsensitive;
#else
    Qt::CaseSensitive;
#endif

} // namespace

QJsonObject MediaBinController::missingMedia() const
{
    const drift::Project &project = m_app->m_project;
    const QHash<QString, int> uses = clipsPerAsset(project);
    QJsonArray missing;
    for (const QString &id : project.assetOrder()) {
        const drift::MediaAsset *asset = project.asset(id);
        if (!asset || asset->kind == drift::MediaKind::Composite || asset->path.isEmpty())
            continue;
        // The bin's own answer, so this agrees with what the user sees; an image sequence is
        // missing when its first frame is.
        const bool gone = m_app->m_assetLibrary ? m_app->m_assetLibrary->isMissing(id) || fileGone(asset->path)
                                                : fileGone(asset->path);
        if (!gone)
            continue;
        missing.append(QJsonObject{{QStringLiteral("asset"), id},
                                   {QStringLiteral("name"), asset->name},
                                   {QStringLiteral("path"), asset->path},
                                   {QStringLiteral("clips"), uses.value(id)}});
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("missing"), missing}, {QStringLiteral("n"), missing.size()}};
}

QJsonObject MediaBinController::relinkMedia(const QJsonObject &args)
{
    const auto fail = [](const char *code, const QString &detail) {
        return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), QString::fromLatin1(code)},
                           {QStringLiteral("detail"), detail}};
    };
    // As given: a file:// URL or a path, either separator, without a trailing slash.
    const auto clean = [](QString path) {
        path = path.trimmed();
        if (path.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
            path = QUrl(path).toLocalFile();
        path.replace(QLatin1Char('\\'), QLatin1Char('/'));
        path = QDir::cleanPath(path);
        while (path.size() > 1 && path.endsWith(QLatin1Char('/')) && !path.endsWith(QLatin1String(":/")))
            path.chop(1);
        return path;
    };
    const QString from = clean(args.value(QStringLiteral("from")).toString());
    const QString to = clean(args.value(QStringLiteral("to")).toString());
    const QString searchDir = clean(args.value(QStringLiteral("search_dir")).toString());
    const bool dryRun = args.value(QStringLiteral("dry_run")).toBool();
    const bool byPrefix = !from.isEmpty() || !to.isEmpty();
    if (byPrefix == !searchDir.isEmpty())
        return fail("bad_args", QStringLiteral("Give from and to (a folder or drive that moved), or search_dir (where "
                                               "to look for missing files by name), not both"));
    if (byPrefix && (from.isEmpty() || to.isEmpty()))
        return fail("bad_args", QStringLiteral("from and to go together"));
    // `from` only has to match the paths the project stores, which may come from another platform
    // ("D:/footage" in a project made on Windows); `to` and `search_dir` are read here.
    static const QRegularExpression kForeignAbsolute(QStringLiteral("^(/|[A-Za-z]:/)"));
    if (!from.isEmpty() && !QDir::isAbsolutePath(from) && !kForeignAbsolute.match(from).hasMatch())
        return fail("bad_args", QStringLiteral("%1 isn't an absolute path").arg(from));
    for (const QString &path : {to, searchDir})
        if (!path.isEmpty() && !QDir::isAbsolutePath(path))
            return fail("bad_args", QStringLiteral("%1 isn't an absolute path").arg(path));
    if (byPrefix && from.compare(to, kPathCase) == 0)
        return fail("bad_args", QStringLiteral("from and to are the same"));
    if (!searchDir.isEmpty() && !QFileInfo(searchDir).isDir())
        return fail("bad_args", QStringLiteral("search_dir %1 isn't a folder").arg(searchDir));

    const drift::Project &project = m_app->m_project;
    // Every path the project names a file by, the way a relink rewrites them.
    QSet<QString> paths;
    {
        drift::Project scratch = project;
        ProjectFileController::remapPaths(scratch, [&paths](const QString &path) -> std::optional<QString> {
            paths.insert(path);
            return std::nullopt;
        });
    }
    // What already names each file, so a relink never points two assets at one file, and what a
    // missing asset's file weighed, to tell its file from another of the same name.
    const auto key = [](const QString &path) {
        const QString cleaned = QDir::cleanPath(path);
        return kPathCase == Qt::CaseInsensitive ? cleaned.toLower() : cleaned;
    };
    QHash<QString, const drift::MediaAsset *> assetByPath;
    for (const drift::MediaAsset &asset : project.assets())
        assetByPath.insert(key(asset.path), &asset);
    QSet<QString> claimed;
    const auto refusal = [&](const QString &path, const QString &target) -> QString {
        const drift::MediaAsset *asset = assetByPath.value(key(path));
        // Two assets on one file breaks the bin (import dedupes by path); a mask or an effect file
        // sharing an asset's file is fine.
        const drift::MediaAsset *owner = asset ? assetByPath.value(key(target)) : nullptr;
        if (owner && owner != asset)
            return QStringLiteral("another asset (%1) already uses %2").arg(owner->name, target);
        // Size before claims, so the reason doesn't depend on which asset was looked at first.
        if (asset && asset->sourceSize > 0 && !drift::parseImageSequencePath(target)
            && QFileInfo(target).size() != asset->sourceSize)
            return QStringLiteral("%1 isn't the same size as the file was").arg(target);
        if (asset && claimed.contains(key(target)))
            return QStringLiteral("another missing file was relinked to %1").arg(target);
        return {};
    };

    QHash<QString, QString> remap;
    QJsonArray stillMissing;
    int matched = 0;
    bool truncated = false;
    int unresolvedCount = 0;
    const auto unresolved = [&](QJsonObject row, const QString &reason) {
        ++unresolvedCount;
        if (const drift::MediaAsset *asset = assetByPath.value(key(row.value(QStringLiteral("path")).toString())))
            row.insert(QStringLiteral("asset"), asset->id);
        row.insert(QStringLiteral("reason"), reason);
        if (stillMissing.size() < 200)
            stillMissing.append(row);
    };
    if (byPrefix) {
        // A missing file under `from` moves to the same place under `to`, where that file is. A root
        // ("/", "D:/") already ends with its separator.
        const QString fromPrefix = from.endsWith(QLatin1Char('/')) ? from : from + QLatin1Char('/');
        const QString toPrefix = to.endsWith(QLatin1Char('/')) ? to : to + QLatin1Char('/');
        QStringList sorted(paths.cbegin(), paths.cend());
        sorted.sort();
        for (const QString &path : std::as_const(sorted)) {
            QString cleaned = QDir::cleanPath(path);
            cleaned.replace(QLatin1Char('\\'), QLatin1Char('/'));
            const bool exact = cleaned.compare(from, kPathCase) == 0;
            if (!exact && !cleaned.startsWith(fromPrefix, kPathCase))
                continue;
            ++matched;
            if (!fileGone(path))
                continue; // still there: not ours to move
            const QString moved = exact ? to : toPrefix + cleaned.mid(fromPrefix.size());
            if (!QFileInfo(fileOf(moved)).isFile()) {
                unresolved({{QStringLiteral("path"), path}, {QStringLiteral("tried"), moved}}, QStringLiteral("no file there"));
                continue;
            }
            if (const QString why = refusal(path, moved); !why.isEmpty()) {
                unresolved({{QStringLiteral("path"), path}, {QStringLiteral("tried"), moved}}, why);
                continue;
            }
            claimed.insert(key(moved));
            remap.insert(path, moved);
        }
    } else {
        // Missing assets, found by file name under search_dir.
        QHash<QString, QList<const drift::MediaAsset *>> wanted;
        for (const drift::MediaAsset &asset : project.assets())
            if (asset.kind != drift::MediaKind::Composite && fileGone(asset.path)
                && !drift::parseImageSequencePath(asset.path))
                wanted[kPathCase == Qt::CaseInsensitive ? QFileInfo(asset.path).fileName().toLower()
                                                        : QFileInfo(asset.path).fileName()]
                    .append(&asset);
        QHash<QString, QStringList> found;
        if (!wanted.isEmpty()) {
            int seen = 0;
            QDirIterator it(searchDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                if (++seen > 200000) {
                    truncated = true;
                    break;
                }
                const QString path = it.next();
                const QString name = kPathCase == Qt::CaseInsensitive ? it.fileName().toLower() : it.fileName();
                if (wanted.contains(name))
                    found[name].append(path);
            }
        }
        QStringList names = wanted.keys();
        names.sort();
        for (const QString &name : std::as_const(names)) {
            const QStringList candidates = found.value(name);
            const QList<const drift::MediaAsset *> sharing = wanted.value(name);
            // Several missing files of one name, and nothing to tell them apart: none is guessed.
            const bool ambiguous = sharing.size() > 1
                                   && std::any_of(sharing.cbegin(), sharing.cend(),
                                                  [](const drift::MediaAsset *a) { return a->sourceSize <= 0; });
            // A walk cut short may have missed another file of the name, so no match is sure.
            if (truncated) {
                for (const drift::MediaAsset *asset : sharing)
                    unresolved({{QStringLiteral("path"), asset->path}},
                               QStringLiteral("search_dir holds over 200,000 files and wasn't searched in full; "
                                              "search a narrower folder"));
                continue;
            }
            // Which of the files of this name pass for each asset: the same size when known, not
            // another asset's, not taken.
            QHash<const drift::MediaAsset *, QStringList> fits;
            QHash<QString, int> fittedBy;
            for (const drift::MediaAsset *asset : ambiguous ? QList<const drift::MediaAsset *>{} : sharing)
                for (const QString &candidate : candidates)
                    if (refusal(asset->path, candidate).isEmpty()) {
                        fits[asset].append(candidate);
                        ++fittedBy[candidate];
                    }
            for (const drift::MediaAsset *asset : sharing) {
                if (ambiguous) {
                    unresolved({{QStringLiteral("path"), asset->path}},
                               QStringLiteral("several missing files share this name; relink them with from/to or "
                                              "replace_asset"));
                    continue;
                }
                const QStringList fit = fits.value(asset);
                // A file that fits two of them (same name and size) can't be told to be either's.
                if (fit.size() == 1 && fittedBy.value(fit.first()) > 1) {
                    unresolved({{QStringLiteral("path"), asset->path}},
                               QStringLiteral("another missing file has the same name and size; relink them with "
                                              "from/to or replace_asset"));
                    continue;
                }
                if (fit.size() == 1) {
                    claimed.insert(key(fit.first()));
                    remap.insert(asset->path, fit.first());
                    continue;
                }
                QJsonObject row{{QStringLiteral("path"), asset->path}};
                if (candidates.size() > 1)
                    row.insert(QStringLiteral("candidates"), QJsonArray::fromStringList(candidates.mid(0, 10)));
                unresolved(row, candidates.isEmpty() ? QStringLiteral("no file of that name")
                                : fit.isEmpty()
                                    ? (candidates.size() == 1 ? refusal(asset->path, candidates.first())
                                                              : QStringLiteral("none of the files of that name fits"))
                                    : QStringLiteral("several files of that name fit"));
            }
        }
    }

    QJsonArray relinked;
    QStringList keys = remap.keys();
    keys.sort();
    for (const QString &path : std::as_const(keys))
        if (relinked.size() < 200)
            relinked.append(QJsonObject{{QStringLiteral("from"), path}, {QStringLiteral("to"), remap.value(path)}});
    if (!dryRun && !remap.isEmpty() && !m_app->m_assetLibrary)
        return fail("internal", QStringLiteral("The media bin isn't available. Nothing was relinked"));
    if (!dryRun && !remap.isEmpty() && !relinkPaths(remap, tr("Relink media")))
        return fail("locked", QStringLiteral("A locked track holds a clip using these files; unlock it and try again. "
                                             "Nothing was relinked"));
    QJsonObject out{{QStringLiteral("ok"), true},
                    {QStringLiteral("relinked"), relinked},
                    {QStringLiteral("n"), int(remap.size())},
                    {QStringLiteral("still_missing"), stillMissing},
                    {QStringLiteral("still_missing_n"), unresolvedCount}};
    if (byPrefix)
        out.insert(QStringLiteral("matched"), matched);
    if (dryRun)
        out.insert(QStringLiteral("dry_run"), true);
    if (truncated)
        out.insert(QStringLiteral("truncated"), true);
    return out;
}

QJsonObject MediaBinController::removeUnusedAssets(bool dryRun)
{
    using namespace drift::mcp;
    AssetLibrary *lib = m_app->assetLibrary();
    if (!lib)
        return err("unsupported", QStringLiteral("No media library"));
    const QHash<QString, int> counts = m_app->clipCountsByAsset();
    const QHash<QString, int> derived = m_app->derivedAssetCounts();
    QStringList ids;
    QJsonArray listed;
    int composites = 0;
    int sources = 0;
    for (int i = 0; i < lib->count(); ++i) {
        const QVariantMap a = lib->assetAt(i);
        const QString id = a.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || counts.value(id) > 0)
            continue;
        // A composite is a whole nested timeline, not media: removing an unplaced one would
        // delete that timeline, so it stays (remove_asset takes it explicitly).
        if (a.value(QStringLiteral("kind")).toString() == QLatin1String("composite")) {
            ++composites;
            continue;
        }
        // The original of a stabilized render: re-stabilizing reads it, and a re-import would
        // not reconnect it (it gets a new id).
        if (derived.value(id) > 0) {
            ++sources;
            continue;
        }
        ids.append(id);
        listed.append(QJsonObject{{QStringLiteral("id"), id},
                                  {QStringLiteral("name"), a.value(QStringLiteral("name")).toString()},
                                  {QStringLiteral("kind"), a.value(QStringLiteral("kind")).toString()}});
    }
    QJsonObject reply{{QStringLiteral("assets"), listed},
                      {QStringLiteral("dry_run"), dryRun},
                      {QStringLiteral("kept_composites"), composites},
                      {QStringLiteral("kept_sources"), sources}};
    if (dryRun || ids.isEmpty()) {
        reply.insert(QStringLiteral("removed"), 0);
        return ok(reply);
    }
    // One undo step for the lot; refused as a whole if a clip started using one meanwhile.
    const int removed = m_app->removeAssets(ids);
    if (removed == 0)
        return err("conflict", QStringLiteral("An asset came into use; list again"));
    reply.insert(QStringLiteral("removed"), removed);
    return ok(reply);
}


QJsonObject MediaBinController::relinkFolder(const QString &from, const QString &to, bool dryRun)
{
    return relinkMedia({{QStringLiteral("from"), from}, {QStringLiteral("to"), to}, {QStringLiteral("dry_run"), dryRun}});
}

QJsonObject MediaBinController::relinkSearchFolder(const QUrl &folder, bool dryRun)
{
    return relinkMedia({{QStringLiteral("search_dir"), folder.isLocalFile() ? folder.toLocalFile() : folder.toString()},
                        {QStringLiteral("dry_run"), dryRun}});
}

#include "moc_MediaBinController.cpp"
