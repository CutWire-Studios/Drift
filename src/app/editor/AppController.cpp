#include "engine/platform/AndroidUri.h"
#include "core/base/CacheRoot.h"
#include "app/editor/appcontroller/AppControllerInternal.h"
#include "engine/render2d/pdf/PdfiumRuntime.h"
#include "app/bin/AssetPreviewController.h"
#include "app/editor/sessions/CurveEditorController.h"
#include "app/editor/sessions/KeyframeController.h"
#include "app/editor/sessions/MaskingController.h"
#include "app/editor/sessions/SubtitleController.h"
#include "app/color/ColorController.h"
#include "app/jobs/audio/DenoiseController.h"
#include "app/jobs/audio/AudioAlignController.h"
#include "app/jobs/vision/DepthController.h"
#include "app/jobs/vision/FaceTrackingController.h"
#include "app/mcp/McpController.h"
#include "app/jobs/vision/RestoreController.h"
#include "app/jobs/vision/SceneDetectController.h"
#include "app/jobs/vision/SegmentationController.h"
#include "app/bin/MediaBinController.h"
#include "app/bin/SourceLabController.h"
#include "app/jobs/vision/StabilizeController.h"
#include "app/jobs/vision/TrackingController.h"
#include "app/jobs/audio/TtsController.h"
#include "app/jobs/audio/VoiceLibrary.h"
#include "app/editor/sessions/SpeedCurveController.h"
#include "app/editor/PreferencesController.h"
#include "app/editor/ProjectFileController.h"
#include "ui/preview/PreviewController.h"

#include "app/online/addons/AddonManager.h"
#include "engine/render2d/pdf/PdfClipRenderer.h"
// Engine-internal by its own header comment, and included here only for releaseCaches(): the GPU
// caches have no other owner outside src/engine to ask. A one-line forwarder on GpuCompositor
// would restore the boundary.
#include "app/jobs/JobRegistry.h"
#include "app/online/CloudProviders.h"

#include <QGuiApplication>
#ifdef Q_OS_MACOS
#include <sys/mount.h>
#endif

#include "app/bin/AssetLibrary.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QByteArray>
#include <QStandardPaths>
#include <QUrl>

#include <QSettings>
#include <QTemporaryFile>
using namespace drift::appdetail;
using drift::mcpdetail::findClipById;
using drift::mcpdetail::runBeatAnalysis;
using namespace drift::appcontroller_detail;


// Every file dialog on Android returns a content:// URI, and a SAF document has no filesystem
// path at all. Writers that need a real file — the bundle writer, the encoder, QImage::save —
// produce their output in app storage first, and commitWriteTarget streams it into the chosen
// document. On desktop the picked path is written directly and both halves fall away.
//
// `suffix` forces the staged file's extension, for writers that pick their format off one: a
// created-document URI carries no usable name, and the display name's own extension is only as
// good as whatever the provider recorded.
QString writeTargetPath(const QUrl &url, const QString &suffix = {})
{
    Q_UNUSED(suffix);
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url)) {
        const QString dir = drift::cacheRoot()
                            + QStringLiteral("/staged");
        if (!QDir().mkpath(dir))
            return {};

        QString name = AndroidUri::displayName(url);
        name.replace(QLatin1Char('/'), QLatin1Char('_'));
        if (!suffix.isEmpty() && QFileInfo(name).suffix().compare(suffix, Qt::CaseInsensitive) != 0)
            name += QLatin1Char('.') + suffix;

        // Keyed on the document so an export and a package running at once cannot end up
        // staging through the same file.
        const QByteArray key = QCryptographicHash::hash(url.toString(QUrl::FullyEncoded).toUtf8(),
                                                        QCryptographicHash::Sha1);
        return dir + QLatin1Char('/') + QString::fromLatin1(key.left(6).toHex())
               + QLatin1Char('-') + name;
    }
#endif
    return url.toLocalFile();
}

// The mirror of writeTargetPath, for readers that need a real file: the bundle reader seeks all
// over its input and hands paths to FFmpeg, neither of which a content:// URI supports. Copies the
// document into the same <Cache>/staged directory writeTargetPath uses, which app/main.cpp already
// sweeps at startup. On desktop this is url.toLocalFile() and nothing is copied.
QString readTargetPath(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url)) {
        std::unique_ptr<QFile> src = AndroidUri::openForRead(url);
        if (!src)
            return {};

        const QString dir = drift::cacheRoot()
                            + QStringLiteral("/staged");
        if (!QDir().mkpath(dir))
            return {};

        QString name = AndroidUri::displayName(url);
        name.replace(QLatin1Char('/'), QLatin1Char('_'));
        // Keyed on the document, so opening the same project twice reuses one staged file instead
        // of littering the cache. The "read-" prefix keeps it clear of writeTargetPath's name for
        // the same document: saving back to a project that is still extracting embedded media
        // would otherwise overwrite the bundle the extract is reading from.
        const QByteArray key = QCryptographicHash::hash(url.toString(QUrl::FullyEncoded).toUtf8(),
                                                        QCryptographicHash::Sha1);
        const QString staged = dir + QStringLiteral("/read-") + QString::fromLatin1(key.left(6).toHex())
                               + QLatin1Char('-') + name;

        QFile dst(staged);
        if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        constexpr qint64 kChunk = 1024 * 1024;
        for (;;) {
            const QByteArray chunk = src->read(kChunk);
            if (chunk.isEmpty())
                break;
            if (dst.write(chunk) != chunk.size()) {
                dst.remove();
                return {};
            }
        }
        if (!dst.flush()) {
            dst.remove();
            return {};
        }
        return staged;
    }
#endif
    return url.toLocalFile();
}

// Whether a failed or cancelled job may dispose of the document it was writing to. Qt's save dialog
// maps to ACTION_CREATE_DOCUMENT, which usually hands back a new empty file — but DocumentsUI also
// returns the *existing* document's URI when the user taps a file in the folder to replace it, and
// destroying that on a cancel at 5% would take a file the job never even reached. Call before the
// job starts: once commitWriteTarget has opened the destination it has already truncated it.
bool writeTargetIsDisposable(const QUrl &url)
{
    Q_UNUSED(url);
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url)) {
        const std::unique_ptr<QFile> existing = AndroidUri::openForRead(url);
        return existing && existing->size() == 0;
    }
#endif
    return false;
}
namespace {


#ifdef Q_OS_MACOS
// Path-based statfs attributes a firmlinked directory to the sealed system volume.
// The open file's descriptor is the volume that accepted the write.
qint64 bytesAvailableOnOpenFile(int fd)
{
    if (fd < 0)
        return -1;
    struct statfs fs {};
    if (::fstatfs(fd, &fs) != 0 || (fs.f_flags & MNT_RDONLY) != 0 || fs.f_bsize == 0)
        return -1;
    if (quint64(fs.f_bavail) > std::numeric_limits<quint64>::max() / quint64(fs.f_bsize))
        return std::numeric_limits<qint64>::max();
    const quint64 bytes = quint64(fs.f_bavail) * quint64(fs.f_bsize);
    const quint64 cap = quint64(std::numeric_limits<qint64>::max());
    return bytes > cap ? std::numeric_limits<qint64>::max() : qint64(bytes);
}
#endif

// Mount matching in QStorageInfo is by path prefix. On macOS, /private, /var, /tmp and /Users are
// firmlinks onto the Data volume, but those paths are not under /System/Volumes/Data, so they match
// "/" — the sealed read-only system volume — and the free-space figure is not the space a write
// there will use. A volume mounted somewhere else (a disk image under /Volumes) already has its
// own prefix and is left alone.
QStorageInfo storageForExportDirectory(const QString &dirPath)
{
    QStorageInfo storage(dirPath);
#ifdef Q_OS_MACOS
    const QString root = QDir::cleanPath(storage.rootPath());
    if (!storage.isValid() || !storage.isReadOnly() || root != QLatin1String("/"))
        return storage;

    const QString canonical = QFileInfo(dirPath).canonicalFilePath();
    const QString dataRoot = QStringLiteral("/System/Volumes/Data");
    if (canonical.isEmpty() || canonical == dataRoot
        || canonical.startsWith(dataRoot + QLatin1Char('/')))
        return storage;

    const QString onData = dataRoot + canonical;
    if (!QFileInfo(onData).isDir())
        return storage;

    QStorageInfo data(onData);
    if (data.isValid() && data.isReady() && !data.isReadOnly()
        && QDir::cleanPath(data.rootPath()) != root)
        return data;
#endif
    return storage;
}

} // namespace

// Checked before an export starts, so a location that can't take the file fails in a second with
// the reason instead of minutes into the render with a generic encoder error. Returns an empty
// string when the location looks writable. Android SAF documents are written through the resolver
// and checked there.
QString exportTargetProblem(const QUrl &url, const QString &path)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url))
        return {};
#endif
    // gvfs/KIO hand back mtp://, smb://, sftp://… for locations with no local path.
    if (!url.isLocalFile() || path.isEmpty()) {
        return QCoreApplication::translate(
                   "AppController",
                   "%1 isn’t a folder on this computer (a phone, camera or remote location). "
                   "Export to a local folder, then copy the file there.")
            .arg(url.toDisplayString(QUrl::PreferLocalFile));
    }

    const QFileInfo target(path);
    const QString dirPath = target.absolutePath();
    const QString dirName = QDir::toNativeSeparators(dirPath);

    // Phones and cameras mounted over MTP/PTP by gvfs, or a Windows shell namespace item: the
    // FUSE layer can't seek, and every container's header is patched after the fact.
    if (path.contains(QLatin1String("/gvfs/mtp:")) || path.contains(QLatin1String("/gvfs/gphoto2:"))
        || path.contains(QLatin1String("::{"))) {
        return QCoreApplication::translate(
                   "AppController",
                   "%1 is on a phone or camera connected over MTP, which can’t be written to "
                   "directly. Export to a local folder, then copy the file there.")
            .arg(dirName);
    }

    if (!QFileInfo(dirPath).isDir()) {
        return QCoreApplication::translate("AppController", "The folder %1 doesn’t exist anymore.")
            .arg(dirName);
    }

    const QStorageInfo storage = storageForExportDirectory(dirPath);
    const QByteArray fsType = storage.isValid() ? storage.fileSystemType().toLower() : QByteArray();
    const bool network = path.startsWith(QLatin1String("//")) || path.startsWith(QLatin1String("\\\\"))
                         || fsType.startsWith("cifs") || fsType.startsWith("smb")
                         || fsType.startsWith("nfs") || fsType == "fuse.sshfs"
                         || fsType == "9p" || fsType.contains("webdav");
    // Hint only. On macOS this is still true when statfs never left the sealed system volume, so
    // a directory QTemporaryDir just created would be refused. The probe is what decides.
    const bool reportedReadOnly = storage.isValid() && storage.isReady() && storage.isReadOnly();

    // A real write is the only reliable answer: ACLs, network shares, sync folders and
    // controlled-folder access all say yes to isWritable() and then refuse the file. The same
    // probe catches a volume flag that says read-only when the directory is writable.
    QTemporaryFile probe(QDir(dirPath).filePath(QStringLiteral(".drift-write-test-XXXXXX")));
    const QByteArray block(64 * 1024, '\0');
    const bool writable = probe.open() && probe.write(block) == block.size() && probe.seek(0)
                          && probe.write(block.left(16)) == 16 && probe.flush();
    if (!writable) {
        if (reportedReadOnly) {
            return QCoreApplication::translate("AppController",
                                               "%1 is on a read-only drive. Choose another folder.")
                .arg(dirName);
        }
        if (network) {
            return QCoreApplication::translate(
                       "AppController",
                       "Can’t write to the network location %1. Check that it’s connected and "
                       "that you have permission to save there.")
                .arg(dirName);
        }
        if (probe.error() == QFileDevice::PermissionsError || !probe.isOpen()) {
            return QCoreApplication::translate(
                       "AppController",
                       "You don’t have permission to save in %1. Choose another folder.")
                .arg(dirName);
        }
        return QCoreApplication::translate("AppController", "Can’t write to %1: %2")
            .arg(dirName, probe.errorString());
    }

    // Replacing a file: opening it without truncating catches read-only files and, on Windows,
    // one that another app (a media player, a sync client) still has open. Skipped when the
    // directory itself could not be written — that failure already has its own reason.
    if (target.exists()) {
        QFile existing(path);
        if (!existing.open(QIODevice::ReadWrite)) {
            return QCoreApplication::translate(
                       "AppController",
                       "Can’t replace %1. It may be read-only or open in another app.")
                .arg(QDir::toNativeSeparators(path));
        }
    }

    qint64 bytesAvailable = -1;
    if (storage.isValid() && storage.isReady())
        bytesAvailable = storage.bytesAvailable();
#ifdef Q_OS_MACOS
    // A flag that survived storageForExportDirectory still describes the sealed volume, so its
    // free-space figure is not the one the probe file was written against.
    if (reportedReadOnly)
        bytesAvailable = bytesAvailableOnOpenFile(probe.handle());
#endif

    constexpr qint64 kMinFreeBytes = 64ll * 1024 * 1024;
    if (bytesAvailable >= 0 && bytesAvailable < kMinFreeBytes) {
        return QCoreApplication::translate("AppController",
                                           "There isn’t enough free space in %1.")
            .arg(dirName);
    }
    return {};
}

bool commitWriteTarget(const QString &staged, const QUrl &url,
                       const std::function<bool(qint64, qint64)> &progress, QString *error)
{
    Q_UNUSED(staged);
    Q_UNUSED(url);
    Q_UNUSED(progress);
    Q_UNUSED(error);
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url)) {
        constexpr qint64 kChunk = 1024 * 1024;
        QFile src(staged);
        std::unique_ptr<QFile> dst = AndroidUri::openForWrite(url);
        if (!dst || !src.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("Could not write to the location you picked");
            return false;
        }

        const qint64 total = src.size();
        qint64 done = 0;
        for (QByteArray chunk = src.read(kChunk); !chunk.isEmpty(); chunk = src.read(kChunk)) {
            if (dst->write(chunk) != chunk.size()) {
                *error = dst->errorString();
                return false;
            }
            done += chunk.size();
            if (progress && !progress(done, total)) {
                *error = QStringLiteral("Cancelled");
                return false;
            }
        }
        // The staged file is the only other copy, so it must not be unlinked until the
        // destination is known good. A short write is already caught above — chunks are 1 MiB,
        // past QFile's write-buffer threshold — but the sub-buffer tail is only flushed at
        // destruction, and a cloud-backed DocumentsProvider commits the upload when the
        // ParcelFileDescriptor closes. Both of those failures used to be reported as success.
        if (src.error() != QFile::NoError) {
            *error = src.errorString();
            return false;
        }
        if (!dst->flush()) {
            *error = dst->errorString();
            return false;
        }
        dst->close();
        if (dst->error() != QFile::NoError) {
            *error = dst->errorString();
            return false;
        }
        src.close();
        QFile::remove(staged);
    }
#endif
    return true;
}

// `deleteDestination` disposes of the document the output was headed for, not just the staging
// copy: ACTION_CREATE_DOCUMENT has already created the file by the time the picker returns, so an
// export that fails or is cancelled otherwise leaves a 0-byte "video" in the user's folder. Only
// ever pass true for a document this same operation created through the save picker — on the
// Save-in-place path the URL is the user's existing project, and destroying that because a write
// failed would take the copy they still have with it.
void discardWriteTarget(const QString &staged, const QUrl &url, bool deleteDestination = false)
{
    Q_UNUSED(staged);
    Q_UNUSED(url);
    Q_UNUSED(deleteDestination);
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url)) {
        QFile::remove(staged);
        if (deleteDestination)
            AndroidUri::deleteDocument(url);
    }
#endif
}

// The name to file an export under in the media library. A content:// URI carries only an opaque
// document id, so the provider has to be asked for the name the user actually chose.
QString exportDisplayName(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url))
        return AndroidUri::displayName(url);
#endif
    return QFileInfo(url.toLocalFile()).fileName();
}

// What a saved or opened project is remembered as. A SAF document's only handle is its URI, and
// the grant that arrives with the picker result dies with the process — without upgrading it the
// recents entry would be a dead string on the next launch.
QString projectLocation(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url)) {
        // Read and write: this is the document Save-in-place writes back to, and persisting the
        // read grant alone is what made the next launch's Save fail with "could not write to the
        // location you picked". A false return still leaves the read grant taken, so the project
        // opens either way.
        AndroidUri::takePersistableReadWritePermission(url);
        return url.toString(QUrl::FullyEncoded);
    }
#endif
    return url.toLocalFile();
}

// The chosen document's name without its extension. Save As gives the copy this as its title, so
// the header names the file you are now editing rather than the one it was copied from.
QString projectNameForUrl(const QUrl &url)
{
#ifdef Q_OS_ANDROID
    if (AndroidUri::isContentUri(url))
        return QFileInfo(AndroidUri::displayName(url)).completeBaseName();
#endif
    return QFileInfo(url.toLocalFile()).completeBaseName();
}

// Whether a remembered project location still resolves. QFileInfo knows nothing about a document
// id, so a SAF location has to be probed through the provider — the grant can also have lapsed
// since it was stored, which is indistinguishable from the file being gone and is treated the same.
bool projectLocationExists(const QString &path)
{
#ifdef Q_OS_ANDROID
    if (path.startsWith(QLatin1String("content://"), Qt::CaseInsensitive))
        return AndroidUri::openForRead(QUrl(path)) != nullptr;
#endif
    return QFileInfo::exists(path);
}

#ifdef Q_OS_ANDROID
// FFmpeg cannot open a content:// URI, so replacing a clip's media means copying the document
// into app storage first. Directory layout and key match AssetLibrary's import materialization
// so the two share copies and its cleanup on asset removal still recognises this one.
QString materializeContentUrl(const QUrl &url)
{
    constexpr qint64 kChunk = 1024 * 1024;
    std::unique_ptr<QFile> src = AndroidUri::openForRead(url);
    if (!src)
        return {};

    AndroidUri::takePersistableReadPermission(url);

    const QByteArray head = src->read(kChunk);
    if (head.isEmpty() && src->error() != QFile::NoError)
        return {};

    QCryptographicHash key(QCryptographicHash::Sha1);
    key.addData(head);
    key.addData(QByteArray::number(src->size()));

    QString name = AndroidUri::displayName(url);
    name.replace(QLatin1Char('/'), QLatin1Char('_'));
    name.replace(QLatin1Char('\\'), QLatin1Char('_'));
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        name = QStringLiteral("import.bin");

    const QString destDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                            + QStringLiteral("/imports/")
                            + QString::fromLatin1(key.result().left(8).toHex());
    const QString destPath = destDir + QLatin1Char('/') + name;
    if (QFileInfo::exists(destPath))
        return destPath;
    if (!QDir().mkpath(destDir))
        return {};

    // Copy aside and rename, so a process death mid-copy cannot leave a truncated file that
    // every later import of the same media would reuse.
    QFile dst(destPath + QStringLiteral(".part"));
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};

    for (QByteArray chunk = head; !chunk.isEmpty(); chunk = src->read(kChunk)) {
        if (dst.write(chunk) != chunk.size()) {
            dst.remove();
            return {};
        }
    }
    if (src->error() != QFile::NoError || !dst.flush()) {
        dst.remove();
        return {};
    }

    dst.close();
    if (!dst.rename(destPath)) {
        dst.remove();
        return {};
    }
    return destPath;
}
#endif

AppController::~AppController()
{
    // Tile decode captures `this` and posts back to the GUI thread. Finish that
    // work, and point playback at m_project, before members start disappearing.
    // Otherwise a worker that outlives the controller — typical in the
    // EditorState tests, which open a session and then let the object fall out
    // of scope — calls into freed memory.
    if (m_multicamTimer)
        m_multicamTimer->stop();
    const bool hadMulticamSession = m_multicamActive;
    m_multicamActive = false;
    waitForMulticamRefresh();
    if (hadMulticamSession)
        m_playback.setProject(&m_project);
    // The export worker captures `this` too; cancel it and let it unwind before teardown.
    if (m_exportFuture.isRunning()) {
        m_exportCancel.storeRelaxed(1);
        m_exportFuture.waitForFinished();
    }

    // Stop the agent server before the undo stack is cut loose below. McpController is
    // a child, so without this it would only be destroyed after the members a late
    // request could still touch.
    delete m_mcpController;
    m_mcpController = nullptr;
    // ~QUndoStack clears the stack, which emits indexChanged into the lambda
    // below — but by then the members it touches (m_selection, the models) are
    // already gone. Cut the signals before any member is destroyed.
    m_undoStack.disconnect(this);
    if (m_timelineTrimCursorSide != 0) {
        QGuiApplication::restoreOverrideCursor();
        m_timelineTrimCursorSide = 0;
        m_timelineTrimCursorHeight = 0;
        m_timelineTrimCursorOwner = 0;
    }
    // Each entry holds a platform cursor; drop them while QGuiApplication is still alive.
    m_trimCursorCache.clear();
}

void AppController::updateFrameCacheSuspension()
{
    const bool suspended =
        m_playback.isPlaying() || m_assetPreview->playing() || m_speedCurve->playing();
    m_filmstripTiles.setSuspended(suspended);
    m_waveformBlocks.setSuspended(suspended);
}

void AppController::setAddonManager(AddonManager *manager)
{
    m_addonManager = manager;
    if (manager && m_tts)
        connect(manager, &AddonManager::kindChanged, m_tts, &TtsController::refreshAvailability);
    if (manager && m_color)
        connect(manager, &AddonManager::kindChanged, m_color, &ColorController::refreshForKind);
    if (manager) {
        connect(manager, &AddonManager::kindChanged, this, [this](const QString &kind) {
            if (kind != QLatin1String(drift::pdfium::kPdfiumKind))
                return;
            drift::clearPdfRenderCaches();
            // Clips attached before the addon existed have no page list; fill it in place, outside
            // the undo stack, since the file itself is unchanged.
            bool probed = false;
            for (drift::Track &track : m_project.tracks()) {
                for (drift::Clip &clip : track.clips) {
                    if (clip.type == drift::ClipType::Pdf && clip.pdf.pageCount == 0)
                        probed = drift::probePdfSource(clip.pdf) || probed;
                }
            }
            if (m_assetLibrary)
                m_assetLibrary->refreshPdfThumbnails();
            if (probed) {
                notifyTracksChanged();
                emit selectedClipDataChanged();
            }
            m_playback.notifyProjectEdited();
        });
    }
}

AppController::AppController(AssetLibrary *assetLibrary, QObject *parent)
    : QObject(parent)
    , m_assetLibrary(assetLibrary)
{
    m_jobs = new JobRegistry(this);
    m_cloud = new CloudProviders(this);
    m_project.resetToDefaultTimeline();
    m_project.setAuthor(QSettings().value(QStringLiteral("authorName")).toString());
    if (m_assetLibrary)
        m_assetLibrary->setProject(&m_project);
    // Before the preview: its constructor asks projectFile()->projectWidth().
    m_projectFile = new ProjectFileController(this, this);
    m_assetPreview = new AssetPreviewController(this, this);
    m_speedCurve = new SpeedCurveController(this, this);
    m_curves = new CurveEditorController(this, this);
    m_segmentation = new SegmentationController(this, this);
    m_sceneDetect = new SceneDetectController(this, this);
    m_depth = new DepthController(this, this);
    m_denoise = new DenoiseController(this, this);
    m_audioAlign = new AudioAlignController(this, this);
    m_color = new ColorController(this, this);
    m_keyframes = new KeyframeController(this, this);
    m_masking = new MaskingController(this, this);
    m_subtitles = new SubtitleController(this, this);
    m_restore = new RestoreController(this, this);
    m_faces = new FaceTrackingController(this, this);
    m_stabilize = new StabilizeController(this, this);
    m_tracking = new TrackingController(this, this);
    m_bin = new MediaBinController(this, this);
    m_sourceLab = new SourceLabController(this, this);
    m_tts = new TtsController(this, this);
    m_voices = new VoiceLibrary(this, m_tts, this);
    m_tts->setVoices(m_voices);
    m_preview = new PreviewController(*this, this);
    m_preferences = new PreferencesController(this);
    connect(m_preferences, &PreferencesController::restartNoticeRequested, this,
            [this](const QString &text) {
                setLastMessage(text, QStringLiteral("info"));
            });
    if (!m_preferences->uiGraphicsApiRevertedFrom().isEmpty()) {
        setLastMessage(tr("The interface graphics API you chose did not start, so Drift went back to OpenGL."),
                       QStringLiteral("warning"));
    }
    m_projectFile->setAutosaveIntervalSeconds(m_preferences->autosaveIntervalSeconds());
    connect(m_preferences, &PreferencesController::autosaveIntervalSecondsChanged, this, [this] {
        m_projectFile->setAutosaveIntervalSeconds(m_preferences->autosaveIntervalSeconds());
    });

    // selectedClipData reflects the current clip's live values, so it must
    // refresh on both selection changes and any edit to the timeline (e.g. a
    // WYSIWYG preview drag emits only tracksChanged). This keeps the Clip
    // Properties panel in sync with the preview in both directions.
    connect(this, &AppController::selectionChanged, this, &AppController::selectedClipDataChanged);
    connect(this, &AppController::selectedClipDataChanged, this,
            &AppController::selectedClipRotationAtPlayheadChanged);
    connect(this, &AppController::inspectorPlayheadChanged, this,
            &AppController::selectedClipRotationAtPlayheadChanged);
    connect(this, &AppController::selectionChanged, this, &AppController::editCapabilitiesChanged);
    connect(this, &AppController::tracksChanged, this, &AppController::editCapabilitiesChanged);
    connect(this, &AppController::tracksChanged, this, &AppController::selectedClipDataChanged);
    if (m_assetLibrary) {
        connect(m_assetLibrary, &AssetLibrary::assetMetadataChanged, this,
                &AppController::editCapabilitiesChanged);
    }

    connect(&m_filmstripTiles, &FilmstripTileCache::tileReady, this,
            &AppController::filmstripTileReady);
    if (m_assetLibrary) {
        // Direct: it has to run before the probe it announces starts reading the file.
        connect(m_assetLibrary, &AssetLibrary::sourceContentChanging, this,
                [this](const QString &path) { forgetChangedMediaSource(path, true); },
                Qt::DirectConnection);
    }
    connect(&m_waveformBlocks, &WaveformBlockCache::rangeReady, this,
            &AppController::waveformRangeReady);

    if (m_assetLibrary) {
        // No pushProjectEdit: a proxy is a cache, not project content. This only asks the
        // compositor to re-read, which now resolves to (or away from) the proxy.
        connect(m_assetLibrary, &AssetLibrary::proxiesChanged, this,
                &AppController::emitPreviewFrame);
        connect(m_assetLibrary, &AssetLibrary::proxyFailed, this,
                [this](const QString &name, const QString &error) {
                    setLastMessage(tr("Could not create a proxy for %1: %2").arg(name, error),
                                   QStringLiteral("error"));
                });
    }

    if (m_assetLibrary) {
        // Context is `this`, not the library: the library is a process-wide static that
        // outlives this object, and the lambda reaches into the preferences.
        m_assetLibrary->setProxyNamePatterns(m_preferences->proxyNamePatterns());
        connect(m_preferences, &PreferencesController::proxyNamePatternsChanged, this, [this] {
            m_assetLibrary->setProxyNamePatterns(m_preferences->proxyNamePatterns());
        });
        connect(m_assetLibrary, &AssetLibrary::proxyRuleLearned, m_preferences,
                &PreferencesController::addProxyNamePattern);
    }

    m_mcpController = new McpController(this, this);
    // A new step can take the stack past the history limit; so can a lower limit.
    connect(&m_undoStack, &QUndoStack::indexChanged, this, &AppController::scheduleUndoHistoryTrim);
    connect(m_preferences, &PreferencesController::undoHistoryModeChanged, this,
            &AppController::scheduleUndoHistoryTrim);
    connect(m_preferences, &PreferencesController::undoHistoryLimitChanged, this,
            &AppController::scheduleUndoHistoryTrim);
    connect(&m_undoStack, &QUndoStack::indexChanged, this, &AppController::undoStackChanged);
    connect(&m_undoStack, &QUndoStack::indexChanged, this, &AppController::undoHistoryChanged);
    connect(&m_undoStack, &QUndoStack::undoTextChanged, this, &AppController::undoHistoryChanged);
    connect(&m_undoStack, &QUndoStack::redoTextChanged, this, &AppController::undoHistoryChanged);
    connect(&m_undoStack, &QUndoStack::indexChanged, this, [this] {
        // Undo/redo swaps the whole project, including the asset table the
        // media bin reads through; without this an undone removal leaves the
        // model with a stale row count.
        if (m_assetLibrary)
            m_assetLibrary->syncToProject();
        m_bin->folderModel()->syncToProject();
        m_sourceLab->refresh();
        // An undo/redo can restore a project state where the folder currently being viewed no
        // longer exists (its creation was undone) — back the viewer out to root rather than
        // leaving it pointed at nothing.
        if (!m_bin->currentFolderId().isEmpty() && !m_project.binFolder(m_bin->currentFolderId()))
            m_bin->setCurrentFolderId(QString());
        restoreSelectionClipIds();
        normalizeSelection();
        reconcileSequenceTabs();
        m_projectFile->setDirty(true);
        emit bookmarksChanged();
        emit workAreaChanged();
        emit m_projectFile->projectNameChanged();
        emit m_projectFile->backgroundChanged();
        // The timeline and selection refresh is the expensive part. An edit's own push leaves it
        // to finishEdit, which sends it anyway; undo and redo send it here.
        m_undoRefreshOwed = true;
        if (!m_quietUndoPush)
            settleUndoRefresh();
    });

    m_audioOutputDeviceId =
        QSettings().value(QStringLiteral("audio/outputDeviceId")).toString();
    if (!m_audioOutputDeviceId.isEmpty()) {
        const QByteArray id = m_audioOutputDeviceId.toUtf8();
        m_playback.setAudioDeviceId(id);
        m_speedCurve->setAudioDeviceId(id);
        m_assetPreview->setAudioDeviceId(id);
    }
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &AppController::audioOutputDevicesChanged);
    // Silent playback used to be entirely invisible: the sink failed to open and nothing said so,
    // in the UI or the log.
    connect(&m_playback, &PlaybackEngine::audioError, this, [this](const QString &message) {
        if (message == m_lastAudioError)
            return;
        m_lastAudioError = message;
        setLastMessage(message, QStringLiteral("error"));
    });

    connect(&m_audioRecorder, &drift::AudioRecorder::recordingStateChanged, this,
            &AppController::audioRecordingStateChanged);
    connect(&m_audioRecorder, &drift::AudioRecorder::pausedChanged, this,
            &AppController::audioRecordingPausedChanged);
    connect(&m_audioRecorder, &drift::AudioRecorder::gainChanged, this,
            &AppController::audioRecordGainChanged);
    connect(&m_audioRecorder, &drift::AudioRecorder::audioLevelChanged, this,
            &AppController::audioRecordLevelChanged);
    connect(&m_audioRecorder, &drift::AudioRecorder::recordedSecondsChanged, this,
            &AppController::audioRecordSecondsChanged);
    connect(&m_audioRecorder, &drift::AudioRecorder::livePeaksChanged, this,
            &AppController::audioRecordLivePeaksChanged);
    connect(&m_audioRecorder, &drift::AudioRecorder::availableDevicesChanged, this,
            &AppController::availableMicrophonesChanged);
    connect(&m_audioRecorder, &drift::AudioRecorder::currentDeviceChanged, this,
            &AppController::currentMicrophoneChanged);
    connect(&m_audioRecorder, &drift::AudioRecorder::recordingError, this,
            [this](const QString &err) {
                setLastMessage(err, QStringLiteral("error"));
            });

    // Hardware decode that dies mid-playback is otherwise silent — the reader drops to
    // software on its own and the preview just gets slower, which reads as a Drift bug.
    connect(&m_playback, &PlaybackEngine::hardwareDecodeFellBack, this,
            [this](const QString &backendName) {
                setLastMessage(backendName.isEmpty()
                                   ? tr("Hardware decoding failed on this clip; using software "
                                        "decoding instead.")
                                   : tr("%1 decoding failed on this clip; using software decoding "
                                        "instead.")
                                         .arg(backendName),
                               QStringLiteral("warning"));
            });

    // Decoding on the wrong GPU is not a failure — the picture is correct, it is just paying a
    // bus crossing per frame that the user did not knowingly ask for. Say so once.
    connect(&m_playback, &PlaybackEngine::zeroCopyUnavailable, this,
            [this](const QString &note, const QString &reason) {
                qInfo("PlaybackEngine: preview zero-copy declined: %s", qPrintable(reason));
                setLastMessage(note, QStringLiteral("warning"));
            });

    // Unlike a decode fallback, nothing still works when this fires: the preview
    // panel is blank and used to blame the timeline for it. "error", not "warning".
    connect(&m_playback, &PlaybackEngine::gpuCompositorUnavailable, this,
            [this](const QString &statusId, const QString &detail) {
                QString message;
                if (statusId == QStringLiteral("version-too-low")) {
                    message = detail.isEmpty()
                        ? tr("Your graphics driver is too old for the preview, which needs "
                             "OpenGL 3.3. See Help → Debug info.")
                        : tr("Your graphics driver only provides %1; the preview needs "
                             "OpenGL 3.3. See Help → Debug info.")
                              .arg(detail);
                } else {
                    message = tr("GPU preview rendering is unavailable on this machine. "
                                 "See Help → Debug info.");
                }
                setLastMessage(message, QStringLiteral("error"));
            });

    // An empty device list on a machine that plainly has speakers means the multimedia backend
    // plugin did not load — which is silent everywhere else, because video decoding does not go
    // through it. Deferred so the toast host exists by the time this fires.
    if (QMediaDevices::audioOutputs().isEmpty()) {
        qWarning("AppController: no audio output devices; playback will be silent");
        QTimer::singleShot(0, this, [this] {
            setLastMessage(tr("No audio output devices were found, so playback will be silent."),
                           QStringLiteral("warning"));
        });
    }

    m_playback.setProject(&m_project);
    connect(&m_playback, &PlaybackEngine::playheadUsChanged, this, [this](quint64 us) {
        if (!m_playing)
            return;
        const drift::TimeUs newUs = static_cast<drift::TimeUs>(us);
        if (m_playheadUs == newUs)
            return;
        m_playheadUs = newUs;
        emit playheadSecondsChanged();
    });
    // Stopped only: during playback the selection would be rebuilt every frame. Stopping emits
    // this once more, which applies the selection for where playback ended.
    connect(this, &AppController::playheadSecondsChanged, this, [this] {
        if (!m_playing && m_preferences->autoSelectUnderPlayhead())
            selectClipUnderPlayhead();
    });
    connect(this, &AppController::playheadSecondsChanged, this, [this] {
        constexpr qint64 kInspectorPlayheadIntervalMs = 100;
        if ((m_playing || m_scrubbing) && m_inspectorPlayheadClock.isValid()
            && m_inspectorPlayheadClock.elapsed() < kInspectorPlayheadIntervalMs)
            return;
        m_inspectorPlayheadClock.start();
        emit inspectorPlayheadChanged();
    });
    // The throttle may have swallowed the last position before a stop.
    connect(this, &AppController::playingChanged, this, [this] {
        if (!m_playing)
            emit inspectorPlayheadChanged();
    });
    connect(&m_playback, &PlaybackEngine::playingChanged, this, [this] {
        // Every play and pause passes through here — UI, shortcuts and MCP alike.
        updateFrameCacheSuspension();
        const bool enginePlaying = m_playback.isPlaying();
        if (!enginePlaying && m_playing) {
            m_playing = false;
            m_playheadUs = m_playback.playheadUs();
            emit playingChanged();
            emit playheadSecondsChanged();
        }
    });
    // Modal previews play through their own players, which the engine never sees.
    connect(m_assetPreview, &AssetPreviewController::playingChanged, this,
            &AppController::updateFrameCacheSuspension);
    connect(m_speedCurve, &SpeedCurveController::playingChanged, this,
            &AppController::updateFrameCacheSuspension);
    // The extra snap targets are the beat grid, the bookmarks and the work area — none of which
    // a timeline edit can move. Driven off the signals rather than the two dozen call sites that
    // emit them, so a new one cannot forget.
    connect(this, &AppController::bookmarksChanged, this,
            &AppController::invalidateExtraSnapTargets);
    connect(this, &AppController::workAreaChanged, this,
            &AppController::invalidateExtraSnapTargets);
    connect(this, &AppController::beatAnalysisChanged, this,
            &AppController::invalidateExtraSnapTargets);

    connect(this, &AppController::tracksChanged, this, [this] {
        if (m_multicamActive && !m_multicamSnaps.isEmpty()) {
            bool intact = true;
            for (const MulticamAngleSnap &snap : m_multicamSnaps) {
                if (snap.trackIndex < 0 || snap.trackIndex >= m_project.tracks().size()) {
                    intact = false;
                    break;
                }
                if (clipIndexById(m_project.tracks().at(snap.trackIndex), snap.clipId) < 0) {
                    intact = false;
                    break;
                }
            }
            if (!intact)
                endMulticamSession();
        }
        // The pointer has not changed — the project behind it was edited in place. Saying so
        // directly skips a pointless trip through the mixer and, more to the point, coalesces
        // the composite instead of forcing one per edit. This is the only preview refresh an
        // edit gets; the preview panel no longer asks for a second, uncoalesced one.
        m_playback.notifyProjectEdited();
        emit selectedTransitionDataChanged();
    });
    // Multicam is a view of the timeline, so it follows the same signals the main window does.
    // Which angle is live and what each one is showing both depend on the playhead, so the
    // window's bindings have to be re-evaluated even when no tile has landed yet.
    connect(this, &AppController::playheadSecondsChanged, this, [this] {
        if (!m_multicamActive)
            return;
        // Only the active angle and which angles cover the playhead depend on it. Emitting
        // multicamChanged hands the window a fresh angle list and rebuilds every tile, so do
        // that when one of those actually moves rather than on every frame.
        const quint64 signature = multicamPlayheadSignature();
        if (signature != m_multicamPlayheadSignature) {
            m_multicamPlayheadSignature = signature;
            emit multicamChanged();
        }
        // While playing, the ~12 Hz timer owns tile refreshes: the playhead ticks at the
        // display cadence, and decoding every angle that often would starve the compositor.
        if (!m_playing)
            refreshMulticamTiles();
    });
    connect(this, &AppController::tracksChanged, this, [this] {
        if (!m_multicamActive)
            return;
        emit multicamChanged();
        refreshMulticamTiles();
    });
    // multicamCanSetUp counts imported cameras, and importing does not touch the timeline — so
    // without this the window's set-up offer would not appear until something else changed.
    if (m_assetLibrary) {
        connect(m_assetLibrary, &AssetLibrary::countChanged, this, [this] {
            if (m_multicamActive)
                emit multicamChanged();
        });
    }
    connect(this, &AppController::playingChanged, this, [this] {
        if (!m_multicamActive || !m_multicamTimer)
            return;
        if (m_playing) {
            m_multicamTimer->start();
        } else {
            m_multicamTimer->stop();
            // Land on the frame the playhead actually stopped at, not the last timer tick.
            refreshMulticamTiles();
        }
    });

    loadShortcuts();
    QSettings settings;
    for (const QJsonValue &value :
         QJsonDocument::fromJson(settings.value(QStringLiteral("preview/guideSets")).toByteArray()).array())
        m_guideLibrary.append(drift::guideSetFromJson(value.toObject()));
    // Settings from before guide sets held a single guide type.
    m_activeGuideSets = settings.contains(QStringLiteral("preview/activeGuideSets"))
        ? settings.value(QStringLiteral("preview/activeGuideSets")).toStringList()
        : QStringList{settings.value(QStringLiteral("preview/guideType"), QStringLiteral("thirds")).toString()};
    m_loopWorkAreaEnabled = settings.value(QStringLiteral("playback/loopWorkArea"), false).toBool();
    m_playback.setLoopWorkArea(m_loopWorkAreaEnabled);
    // Off by default: with it on, nudging a clip while the playhead sits anywhere writes a
    // keyframe, and an animation appears where the user only meant to reposition something.
    m_autoKeyEnabled = settings.value(QStringLiteral("editor/autoKeyEnabled"), false).toBool();
    loadAssetFavorites();

    m_previewAutoCommit = new QTimer(this);
    m_previewAutoCommit->setSingleShot(true);
    m_previewAutoCommit->setInterval(400);
    connect(m_previewAutoCommit, &QTimer::timeout, this, &AppController::commitPreviewDrag);

    m_projectFile->detectRecoveryFile();
    m_projectFile->sweepExtractionDirs();
}

int AppController::clipCount() const
{
    int total = 0;
    for (const drift::Track &track : m_project.tracks())
        total += track.clips.size();
    return total;
}

void AppController::notifySelectionChanged()
{
    ++m_selectionRevision;
    emit selectionChanged();
}

void AppController::notifyStabilizeStateChanged(const QString &clipId)
{
    if (m_selectedTrack < 0 || m_selectedClip < 0 || m_selectedTrack >= m_project.tracks().size())
        return;
    const drift::Track &track = m_project.tracks().at(m_selectedTrack);
    if (m_selectedClip >= track.clips.size() || track.clips.at(m_selectedClip).id != clipId)
        return;
    emit selectedClipDataChanged();
}

void AppController::notifyTracksChanged()
{
    // Trims and the other full-path preview edits land here rather than in emitPreviewEdit.
    if (m_previewDragActive)
        m_previewDragDirty = true;
    m_tracksCacheValid = false;
    // Dropped now rather than left to the next rebuild: this is a QVariant graph over every clip
    // in the project, and holding a stale one until the next read doubles the peak.
    m_tracksCache.clear();
    m_durationCacheValid = false;
    ++m_tracksRevision;
    // After the cache drop, before the signal. A row insert or removal rebuilds the Repeater's
    // delegates synchronously from inside this call, and those delegates read track-level fields
    // back through tracks() — which must already be answering from the new project.
    syncClipModels();
    // Inside a batch the invalidation above has already happened, so a reader still gets the
    // truth; only the announcement is held back, and the batch sends exactly one.
    if (m_tracksBatchDepth > 0) {
        m_tracksBatchPending = true;
        return;
    }
    emit tracksChanged();
}

void AppController::endTracksBatch()
{
    if (--m_tracksBatchDepth > 0)
        return;
    m_tracksBatchDepth = 0;
    if (!m_tracksBatchPending)
        return;
    m_tracksBatchPending = false;
    emit tracksChanged();
}

// Layout only: the track flags a row is drawn from, and the four clip fields the timeline
// panel's JS helpers need to answer "which clip is under this x", "where are the gaps", "how
// tall is the stack". Everything the clip delegate itself renders comes from clipsModel(), so
// this list stays small enough that QML deep-converting it to JS on every edit does not matter.
QVariantList AppController::tracks() const
{
    // QList is implicitly shared, so handing back the cache is a refcount bump.
    if (m_tracksCacheValid)
        return m_tracksCache;

    QVariantList result;
    result.reserve(m_project.tracks().size());

    for (int ti = 0; ti < m_project.tracks().size(); ++ti) {
        const drift::Track &track = m_project.tracks().at(ti);

        QVariantList clips;
        clips.reserve(track.clips.size());
        // Whether the track feeds the mix, for the audio mixer's strip list. Same rule as a
        // clip's waveform bar: an unprobed source gets the benefit of the doubt.
        bool hasAudio = track.type == drift::TrackType::Audio;

        for (const drift::Clip &clip : track.clips) {
            if (!hasAudio && track.type == drift::TrackType::Video && !clip.suppressEmbeddedAudio) {
                const drift::MediaAsset *asset = m_project.asset(clip.assetId);
                hasAudio = clip.type == drift::ClipType::Composite
                    || (clip.type == drift::ClipType::Video
                        && (!asset || !asset->hasAudioKnown || asset->hasAudio));
            }
            QVariantMap clipEntry{
                {QStringLiteral("id"), clip.id},
                {QStringLiteral("name"), clip.name},
                {QStringLiteral("start"), drift::usToSeconds(clip.timelineStart)},
                {QStringLiteral("duration"), drift::usToSeconds(clip.timelineDuration)},
            };
            if (clip.type == drift::ClipType::Adjustment)
                clipEntry.insert(QStringLiteral("adjustmentKind"),
                                 drift::adjustmentKindToString(clip.adjustmentKind));
            clips.append(clipEntry);
        }

        QString trackAdjustmentKind;
        if (track.isTransformLayer())
            trackAdjustmentKind = QStringLiteral("transform");
        else if (track.isCameraLayer())
            trackAdjustmentKind = QStringLiteral("camera");
        else if (track.isAdjustment() && !track.clips.isEmpty())
            trackAdjustmentKind = drift::adjustmentKindToString(track.clips.constFirst().adjustmentKind);
        QVariantList coveredBy;
        for (const int layer : drift::transformLayersCovering(m_project.tracks(), ti))
            coveredBy.append(layer);

        QVariantList transitions;
        transitions.reserve(track.transitions.size());
        for (const drift::Transition &transition : track.transitions)
            transitions.append(transitionToMap(m_project, track, transition));

        result.append(QVariantMap{
            {QStringLiteral("id"), track.id},
            {QStringLiteral("type"), drift::trackTypeToString(track.type)},
            {QStringLiteral("adjustmentScope"), drift::adjustmentScopeToString(track.adjustmentScope)},
            {QStringLiteral("parentTrackId"), track.parentTrackId},
            // The timeline draws a lane inside its parent's row instead of giving it one of its
            // own, so it needs to tell the two apart without re-deriving the rule.
            {QStringLiteral("isAdjustmentLane"), track.isAdjustmentLane()},
            {QStringLiteral("adjustmentKind"), trackAdjustmentKind},
            // Transform layers: the span a Range track covers, how deeply it nests, and for any
            // track the layers over it, outermost first.
            {QStringLiteral("isTransformLayer"), track.isTransformLayer()},
            // The camera track: no span and no pixels, so the timeline gives it a row and a label
            // but none of the coverage drawing a Range track gets.
            {QStringLiteral("isCameraLayer"), track.isCameraLayer()},
            {QStringLiteral("spanEndTrackId"), track.spanEndTrackId},
            {QStringLiteral("spanEndIndex"), drift::transformSpanEndIndex(m_project.tracks(), ti)},
            {QStringLiteral("spanDepth"), track.isTransformLayer() ? coveredBy.size() : 0},
            {QStringLiteral("transformCoveredBy"), coveredBy},
            {QStringLiteral("name"), track.name},
            {QStringLiteral("clips"), clips},
            {QStringLiteral("hasAudio"), hasAudio},
            {QStringLiteral("transitions"), transitions},
            {QStringLiteral("muted"), track.muted},
            {QStringLiteral("hidden"), track.hidden},
            {QStringLiteral("solo"), track.solo},
            {QStringLiteral("volume"), track.volume},
            {QStringLiteral("pan"), track.pan},
            {QStringLiteral("clipDisplay"), static_cast<int>(track.clipDisplay)},
            {QStringLiteral("showChannelWaveforms"), track.showChannelWaveforms},
            {QStringLiteral("heightScale"), track.heightScale},
        });
    }

    m_tracksCache = result;
    m_tracksCacheValid = true;
    return m_tracksCache;
}


