#pragma once

#include "core/MediaAsset.h"

#include <QAbstractListModel>
#include <QJsonArray>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>

namespace drift {
class Project;
}

// Media bin model backed by the project's asset table.
class AssetLibrary : public QAbstractListModel
{
    Q_OBJECT
    // Read-only row count, so QML can tell an empty bin from a populated one
    // (drives the empty state) and can detect imports that produced no asset.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // True while importUrlsAsync's copy stage is running. The Android home screen disables its
    // CTAs and raises a progress overlay on it; on desktop it is never true for long enough to
    // see, because nothing has to be copied.
    Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
    // Flatpak (and Snap) hide host paths that the file picker would have granted through the
    // portal. A dropped file:// URL then fails to open — not because the format is unsupported.
    Q_PROPERTY(bool sandboxed READ sandboxed CONSTANT)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        KindRole,
        DurationRole,
        DurationSecondsRole,
        PathRole,
        ThumbnailPathRole,
        FilmstripPathRole,
        FolderIdRole,
        RotationRole,
    };
    Q_ENUM(Role)

    explicit AssetLibrary(QObject *parent = nullptr);
    ~AssetLibrary() override;

    void setProject(drift::Project *project);
    drift::Project *project() const { return m_project; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }

    Q_INVOKABLE void importUrls(const QList<QUrl> &urls);
    // Same import, with the SAF copy moved off the GUI thread. On Android a picked document has to
    // be streamed out of the content provider before FFmpeg can open it, and doing that inline
    // froze the app for minutes on a handful of 4K clips. Returns false when an import is already
    // running; progress arrives as importProgress and the rows exist by the time importFinished
    // is emitted.
    Q_INVOKABLE bool importUrlsAsync(const QList<QUrl> &urls);
    bool importing() const { return m_importing; }
    bool sandboxed() const;
    // The one place that decides what counts as media, so the file picker, the folder-import
    // walk and the kind guess can never disagree about a format. Extension-only: the real kind
    // comes from the probe once the file is open.
    static bool isVideoPath(const QString &path);
    static bool isAudioPath(const QString &path);
    static bool isImagePath(const QString &path);
    static bool isMediaPath(const QString &path);
    // The same set spelled as a QFileDialog name filter, e.g. "Media files (*.mp4 *.mov ...)".
    Q_INVOKABLE QString mediaNameFilter() const;
    // Import local paths and return the asset ids involved (new or already-present).
    QStringList importLocalPaths(const QStringList &paths);
    // Q_INVOKABLE because QML has to know when a freshly imported row is still a placeholder:
    // importFinished fires before the off-thread probe fills width/height/fps/duration, so
    // anything sizing a canvas or a clip from a new asset must wait on this.
    Q_INVOKABLE bool isImportPending(const QString &assetId) const;
    // Registers media the app rendered itself (freeze frames and the like). The asset is already
    // complete, so this skips the probe and thumbnail jobs the import path runs. Returns its id.
    QString addGeneratedAsset(drift::MediaAsset asset);
    Q_INVOKABLE QVariantMap assetAt(int index) const;
    Q_INVOKABLE QString assetIdAt(int index) const;
    Q_INVOKABLE int indexOfId(const QString &id) const;
    Q_INVOKABLE QString thumbnailAt(int index) const;
    Q_INVOKABLE QString filmstripAt(int index) const;
    Q_INVOKABLE void ensureMedia(int index);
    Q_INVOKABLE void ensureAllMedia();
    Q_INVOKABLE void sortByName();
    Q_INVOKABLE void sortByKind();
    // Display name in the media bin. Does not rename the file on disk.
    Q_INVOKABLE bool setAssetName(int index, const QString &name);
    // Bin-preview rotation correction, snapped to the nearest 90°; -1 resets to the file's own
    // probed rotation. Forces the cached thumbnail/filmstrip to regenerate against it.
    Q_INVOKABLE bool setAssetRotation(int index, int degrees);
    // Non-destructive bin-preview trim (microseconds); trimOutUs < 0 resets to the full duration.
    // Never touches the source file — applied to a clip's srcIn/srcOut when placed on the
    // timeline (see AppController::applyAssetLayout).
    Q_INVOKABLE bool setAssetTrim(int index, qint64 trimInUs, qint64 trimOutUs);
    int indexOfPath(const QString &path) const;
    // Drops the row from the project's asset table. Callers own the undo
    // snapshot and the in-use check; this only touches the bin.
    bool removeAssetAt(int index);
    // Reassigns which bin folder the row lives in (empty = root). Callers own the undo snapshot.
    bool moveAssetToFolder(int index, const QString &folderId);
    // Moves every asset currently in `folderId` to `newFolderId` in one pass — used when a folder
    // is deleted, to carry its direct-child assets up to the parent. Callers own the undo snapshot.
    int reparentAssetsInFolder(const QString &folderId, const QString &newFolderId);
    // Folder a new import lands in; empty = bin root. Set by AppController as the user navigates.
    void setImportFolderId(const QString &folderId) { m_importFolderId = folderId; }
    // Probes `absolutePath` off-thread and reports it back through assetSourceProbed without
    // touching the project, so the caller can apply the swap, the clip fixups and the undo
    // snapshot as one transaction. Returns false when nothing was started.
    bool startReplaceProbe(int index, const QString &absolutePath);
    // Writes a probed source over the asset at `assetId`, keeping the id. Keeping it is the
    // whole point: clips address their media through it, so they stay bound across the swap.
    // Callers own the undo snapshot and the clip fixups.
    bool applyProbedSource(const QString &assetId, const drift::MediaAsset &filled);
    // Re-reads the project after undo/redo has swapped it wholesale.
    void syncToProject();

    QJsonArray toJsonArray() const;
    void loadFromJsonArray(const QJsonArray &assets);
    void clear();

    // Fills hasAudio from MediaProbe off-thread when hasAudioKnown is false.
    void ensureAudioPresence(const QString &assetId);

signals:
    void countChanged();
    void importingChanged();
    void importProgress(int done, int total, const QString &name);
    void importFinished(int materialized, int failed);
    // Fired when probe/thumb/audio metadata lands so unlink affordances can refresh.
    void assetMetadataChanged(const QString &assetId);
    // Narrower than assetMetadataChanged: only for a change to a *card-level* field the bin grid
    // snapshots (name/kind/duration/path — see MediaAssetsTab.qml's combinedItems), so its
    // listener knows a real rebuild is warranted. A thumbnail-only change (rotate, a thumbnail
    // regenerating) does not emit this — those refresh their own delegate's image in place.
    void assetCardChanged(const QString &assetId);
    // Fired only by an actual user-initiated bin edit (setAssetRotation, setAssetTrim) — never by
    // a passive background update (a thumbnail/filmstrip landing, an audio-presence probe). The
    // project's dirty flag listens to this one, not assetMetadataChanged: opening a project with
    // missing cached thumbnails, or saving while one happens to regenerate, must not by itself
    // produce an unsaved-changes prompt.
    void assetUserEdited(const QString &assetId);
    // Result of startReplaceProbe. Nothing has been applied yet; the caller decides whether the
    // probed media is an acceptable stand-in and calls applyProbedSource if so.
    void assetSourceProbed(const QString &assetId, const drift::MediaAsset &filled, bool ok);
    // A file that passed the suffix whitelist and then could not be read at all, so its bin row
    // was withdrawn. Without this the row just disappears and the user is told nothing.
    void assetImportFailed(const QString &name);

private:
    // `sourceUris` maps an absolute path to the content:// URI it was materialized from, so the
    // asset can be rehydrated after its copy is gone. Empty on desktop. `destinationFolderId` is
    // captured by the caller at import *start*, not read from m_importFolderId here — the async
    // path's copy stage can outlive the user navigating to a different folder, and a member read
    // at completion would land new assets wherever they'd navigated to instead.
    void importFiles(const QStringList &paths, const QHash<QString, QString> &sourceUris,
                     const QString &destinationFolderId);
    QStringList importFilesReturningIds(const QStringList &paths,
                                        const QHash<QString, QString> &sourceUris,
                                        const QString &destinationFolderId);
    bool containsPath(const QString &path) const;
    void refreshMediaAt(int index);
    void startImportJob(const QString &assetId, const QString &absolutePath, bool imageOnly);
    void startThumbJob(const QString &assetId);
    void applyImportResult(const QString &assetId, const drift::MediaAsset &filled, bool ok);
    // `sourcePath` is the file the job actually read. It is compared against the asset's current
    // path on landing so a result for media that has since been replaced is dropped.
    void applyThumbResult(const QString &assetId, const QString &sourcePath, const QString &thumb,
                          const QString &strip);
    void applyAudioPresence(const QString &assetId, const QString &sourcePath, bool hasAudio,
                            int sampleRate, int channels);
    void emitAssetRowChanged(int index, const QList<int> &roles);
    void snapshotAssets();
    QList<QString> currentPaths() const;
    QList<QString> currentFolderIds() const;
    const drift::MediaAsset *assetAtIndex(int index) const;
    drift::MediaAsset *assetAtIndex(int index);

    drift::Project *m_project = nullptr;
    bool m_importing = false;
    // Asset order and per-row source paths as of the last change this model itself made, so
    // syncToProject() can tell an undone asset edit from every other undo. A replaced source
    // leaves the order alone and only moves a path, which is why both are tracked.
    QList<QString> m_syncedOrder;
    QList<QString> m_syncedPaths;
    QList<QString> m_syncedFolderIds;
    QString m_importFolderId;
    QSet<QString> m_importPending;
    QSet<QString> m_thumbPending;
    // Asset ids whose settings (rotation/trim) changed again while a thumbnail job for them was
    // already in flight — the in-flight job's result is stale the moment it lands, so its landing
    // immediately kicks a fresh job rather than silently keeping the outdated image.
    QSet<QString> m_thumbStale;
    QSet<QString> m_audioProbePending;
    // Probe and thumbnail jobs run here rather than on the global pool, because the destructor
    // has to be able to wait for them: each captures `this` and posts its result back with
    // QMetaObject::invokeMethod(this, ...). Nothing joined them before, so a job outliving the
    // object called into freed memory — the tests are where that bites, since AssetLibrary is a
    // stack local per test function and the address is handed straight to the next one.
    QThreadPool m_jobs;
};
