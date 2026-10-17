#pragma once

#include "core/MediaAsset.h"
#include "models/BinFolderListModel.h"

#include <QAtomicInt>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

class AppController;

namespace drift {
struct MediaEditSpec;
}

// Media-bin folders and file operations, reached from QML as EditorState.bin.
class MediaBinController : public QObject
{
    friend class AppController;

    Q_OBJECT

    Q_PROPERTY(BinFolderListModel *folderModel READ folderModel CONSTANT)
    // Which bin folder is currently being viewed; empty = bin root. Transient navigation state —
    // not persisted, not undoable, same treatment as mediaGridMode's touch-only sibling.
    Q_PROPERTY(QString currentFolderId READ currentFolderId WRITE setCurrentFolderId
                   NOTIFY currentFolderIdChanged)
    // True while importFolder's off-thread directory walk is running, so the bin can raise its
    // progress overlay over a slow tree (a big hierarchy, or a Flatpak document-portal mount).
    Q_PROPERTY(bool importingFolder READ importingFolder NOTIFY importingFolderChanged)
    // Id of the asset whose replacement is being probed, empty when idle. Only that one bin row
    // goes busy: the rest of the panel stays usable, and the wait belongs to the row the user
    // right-clicked.
    Q_PROPERTY(QString replacingAssetId READ replacingAssetId NOTIFY replacingAssetIdChanged)
    // True while a bin-row crop/trim is encoding. Progress belongs to MediaPreviewWindow.
    Q_PROPERTY(bool editing READ editing NOTIFY editChanged)
    Q_PROPERTY(double editProgress READ editProgress NOTIFY editChanged)
    Q_PROPERTY(QString editStatus READ editStatus NOTIFY editChanged)
    // The running (or, once it finishes, the last) asset edit job is a frame-rate conversion
    // rather than a crop, so the finish can be worded for it. Also carries its name while it runs,
    // since a conversion has no preview window to show progress in.
    Q_PROPERTY(bool editIsConversion READ editIsConversion NOTIFY editChanged)
    Q_PROPERTY(QString editName READ editName NOTIFY editChanged)

public:
    explicit MediaBinController(AppController *app, QObject *parent = nullptr);

    BinFolderListModel *folderModel() { return &m_folderModel; }
    QString currentFolderId() const { return m_currentFolderId; }
    void setCurrentFolderId(const QString &folderId);
    bool importingFolder() const { return m_importingFolder; }
    QString replacingAssetId() const { return m_replacingAssetId; }
    bool editing() const { return m_editing; }
    double editProgress() const { return m_editProgress; }
    QString editStatus() const { return m_editStatus; }
    bool editIsConversion() const { return m_editIsConversion; }
    QString editName() const { return m_editKeepName; }

    // Bin folder CRUD. parentId empty = bin root; nesting is arbitrary depth.
    Q_INVOKABLE QString createFolder(const QString &name, const QString &parentId);
    Q_INVOKABLE bool renameFolder(const QString &folderId, const QString &name);
    // Reparents the folder itself, keeping its own assets and subfolders — they stay pointed
    // at it, so they move along without being touched individually. Refuses moving a folder
    // into itself or into one of its own descendants.
    Q_INVOKABLE bool moveFolder(const QString &folderId, const QString &newParentId);
    // Moves the folder's direct children (assets and subfolders) up to its own parent, then
    // removes it. Never blocks and never recurses into deleting contents.
    Q_INVOKABLE bool deleteFolder(const QString &folderId);
    Q_INVOKABLE bool moveAssetToFolder(int assetIndex, const QString &folderId);
    // Multi-select bulk move, one undo step for the whole batch. Returns how many were moved.
    Q_INVOKABLE int moveAssetsToFolder(const QStringList &assetIds, const QString &folderId);
    // Imports a whole directory: mirrors its subfolder tree into the bin (one new bin folder per
    // filesystem folder, including empty ones) and imports every media file into the bin folder
    // matching its containing directory. `folderUrl` must be a local (file://) directory. The
    // directory walk runs off-thread, so this returns as soon as it starts — false means the URL
    // didn't resolve to a readable directory or an import was already running, and the outcome
    // arrives as folderImportFinished. Stops after a fixed number of files so a folder picked by
    // mistake can't queue thousands of probes. Not undoable — like a plain media import, "undo"
    // is deleting the folder by hand — but still marks the project dirty, so autosave and the
    // unsaved-changes prompt cover the hierarchy it creates.
    Q_INVOKABLE bool importFolder(const QUrl &folderUrl);
    // Points an existing bin row at a different file, keeping every clip that uses it where it
    // is — its position, trim, effects and transitions all survive. Asynchronous: true only means
    // the probe started, and the outcome arrives as replaceFinished.
    Q_INVOKABLE bool replaceSource(int assetIndex, const QUrl &url);
    // Re-reads a bin row's own file after it changed in place: new duration and media, with the
    // name, trim and rotation the user gave the row kept, and every cache of the old content
    // dropped. Undoable. Asynchronous: true only means the probe started — the row stays
    // import-pending until it lands.
    Q_INVOKABLE bool refreshSource(int assetIndex);
    // Refreshes every row whose file no longer matches the size and modification time recorded
    // when it was probed. Run when a project opens. Rows with no record (older projects) are left
    // alone: re-probing all of them would cost every open. Returns how many probes started.
    int refreshChangedSources();
    // Whether the last refresh of `assetId` was refused (unreadable, or now another kind of
    // media), clearing the record. The fingerprint cannot say: a refused refresh leaves it as it
    // was, and an asset from an older project never had one.
    bool takeRefreshFailure(const QString &assetId) { return m_refreshFailed.remove(assetId); }
    // Writes an image asset (a freeze frame, typically) out to `url`. The format follows the
    // destination's extension, so the picker's name filter never has to be reported back.
    Q_INVOKABLE bool exportImage(int assetIndex, const QUrl &url);
    // Copies the asset's file to `url` byte for byte. Asynchronous: true only means the copy
    // started, and the outcome arrives as saveFinished.
    Q_INVOKABLE bool saveAs(int assetIndex, const QUrl &url);
    // Rewrites the bin row: trim [inSeconds, outSeconds] and crop in display-normalized 0..1.
    // outSeconds < 0 means through the end. The original name is kept. Asynchronous; the
    // outcome arrives as editFinished, and the row is then rebound via replaceSource.
    Q_INVOKABLE bool saveEdit(int assetIndex, double inSeconds, double outSeconds,
                              double cropX, double cropY, double cropW, double cropH);
    Q_INVOKABLE void cancelEdit();
    // Renders a bin video's trim and crop into a new bin item, "<name> (trimmed)", leaving the
    // original alone. Reports progress through editChanged like a save; the new item's id
    // arrives as copyRendered, a failure as editFinished(false, ...).
    Q_INVOKABLE bool renderCopy(int assetIndex, double inSeconds, double outSeconds,
                                double cropX, double cropY, double cropW, double cropH);
    // "Convert to edit-friendly format": re-encodes each video asset onto a constant, standard
    // frame rate and swaps it in, one at a time, the same undoable way a crop does, marking the
    // asset edit-friendly. The original file is left where it was.
    Q_INVOKABLE void convertToConstantFrameRate(const QStringList &assetIds);

signals:
    void currentFolderIdChanged();
    void importingFolderChanged();
    // Outcome of importFolder: how many bin folders and assets it actually created, how many
    // files it passed over as unrecognized, and whether the walk stopped at the file limit with
    // more still on disk.
    void folderImportFinished(int folders, int files, int skipped, bool truncated);
    // Outcome of replaceSource. `message` is a ready-to-show reason on failure and the new
    // media's name on success. `adjustedClips` counts clips whose source range no longer fitted
    // the replacement and was pulled back to it.
    void replaceFinished(bool ok, const QString &message, int adjustedClips);
    void replacingAssetIdChanged();
    void saveFinished(bool ok, const QString &name);
    void editChanged();
    void editFinished(bool ok, const QString &message);
    void copyRendered(const QString &assetId);

private:
    // Applies a finished replace probe as one undoable transaction, or reports why it cannot be.
    void finalizeReplace(const QString &assetId, const drift::MediaAsset &filled, bool ok);
    // Moves every clip bound to `assetId` onto the replacement media. Returns how many had a
    // source range that no longer fitted and had to be pulled back to it.
    int rebindClipsToAsset(const QString &assetId, const drift::MediaAsset &asset, int oldBinCorrection);
    // Runs spec off-thread and swaps its output in for the asset at assetIndex, reporting through
    // editChanged/editFinished. The caller has checked no edit is running.
    void startEditJob(const QString &assetId, const QString &name,
                      const drift::MediaEditSpec &spec, bool conversion);
    void startNextConversion();

    AppController *m_app = nullptr;
    BinFolderListModel m_folderModel;
    QString m_currentFolderId;
    bool m_importingFolder = false;
    QString m_replacingAssetId;
    // The content:// URI the in-flight replace picked, held across the probe so it can be put back
    // on the asset once applyProbedSource has overwritten the struct.
    QString m_replacingAssetSourceUri;
    QSet<QString> m_refreshFailed;
    bool m_editing = false;
    double m_editProgress = 0.0;
    QString m_editStatus;
    QString m_editKeepName;
    QString m_editingAssetId;
    QAtomicInt m_editCancel = 0;
    bool m_editIsConversion = false;
    // Asset ids waiting for convertToConstantFrameRate; the edit job runs one at a time.
    QStringList m_conversionQueue;
};
