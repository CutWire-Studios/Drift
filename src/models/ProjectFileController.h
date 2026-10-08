#pragma once

#include "engine/ProjectBundle.h"

#include <QAtomicInt>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

class QTimer;
class AppController;

// Project-file lifecycle and project setup: save, load, packaging, collect-media,
// autosave and the startup queue. Reached from QML as EditorState.projectFile.
// Document ↔ JSON conversion stays on AppController; this class calls it through m_app.
class ProjectFileController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap background READ background NOTIFY backgroundChanged)
    Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY projectNameChanged)
    Q_PROPERTY(QVariantMap projectMetadata READ projectMetadata NOTIFY projectMetadataChanged)
    Q_PROPERTY(bool packaging READ packaging NOTIFY packagingChanged)
    Q_PROPERTY(double packageProgress READ packageProgress NOTIFY packageProgressChanged)
    Q_PROPERTY(bool collectingMedia READ collectingMedia NOTIFY collectingMediaChanged)
    Q_PROPERTY(double collectMediaProgress READ collectMediaProgress NOTIFY collectMediaProgressChanged)
    // True from the moment loadProject()/loadProjectJson() is called — by the header,
    // the start screen, an external open, or a startup restore — until projectLoadFinished
    // fires. Tracked here rather than by each QML call site so a load kicked off from C++
    // (consumeStartupProject, restoreLastSessionIfEnabled) is just as visible as one QML
    // started itself; nothing else may replace the document while this is true.
    Q_PROPERTY(bool projectLoadPending READ projectLoadPending NOTIFY projectLoadPendingChanged)
    Q_PROPERTY(bool hasUnsavedChanges READ hasUnsavedChanges NOTIFY dirtyChanged)
    Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY currentProjectPathChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY recoveryChanged)
    Q_PROPERTY(QVariantMap recoveryInfo READ recoveryInfo NOTIFY recoveryChanged)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)

public:
    explicit ProjectFileController(AppController *app, QObject *parent = nullptr);

    QVariantMap background() const;
    Q_INVOKABLE void setBackground(const QVariantMap &background);

    QString projectName() const;
    Q_INVOKABLE void setProjectName(const QString &name);
    QVariantMap projectMetadata() const;
    Q_INVOKABLE void setProjectMetadata(const QString &title, const QString &author,
                                        const QString &description);

    Q_INVOKABLE int projectWidth() const;
    Q_INVOKABLE int projectHeight() const;
    Q_INVOKABLE int projectFps() const;
    Q_INVOKABLE void setProjectResolution(int width, int height);
    Q_INVOKABLE void setProjectFps(int fps);
    Q_INVOKABLE void setProjectSetup(int width, int height, int fps);

    bool packaging() const { return m_packaging; }
    double packageProgress() const { return m_packageProgress; }
    bool collectingMedia() const { return m_collectingMedia; }
    double collectMediaProgress() const { return m_collectMediaProgress; }
    bool projectLoadPending() const { return m_projectLoadPending; }
    bool hasUnsavedChanges() const { return m_dirty; }
    QString currentProjectPath() const { return m_currentProjectPath; }
    bool recoveryAvailable() const { return m_recoveryAvailable; }
    QVariantMap recoveryInfo() const { return m_recoveryInfo; }
    QVariantList recentProjects() const;

    // Writes a .drift bundle keeping each asset's current storage mode, so a referencing project
    // stays instant to save and a packaged one stays self-contained.
    Q_INVOKABLE void saveProject(const QUrl &url);
    // Save As: the same write, but the copy gets its own project id and takes its title from the
    // chosen file name, and the open document only adopts that identity once the write lands. The
    // file it was opened from is never touched, so the original stays as it was on disk and the
    // session carries on in the duplicate — which is the point of the command.
    Q_INVOKABLE void saveProjectAs(const QUrl &url);
    // Same container, every source asset embedded. Runs off the GUI thread — it copies the media.
    Q_INVOKABLE void packageProject(const QUrl &url);
    Q_INVOKABLE void cancelPackage();
    // Export-only: the raw document JSON, no container and no media. Leaves the open project's
    // path, dirty flag and recents alone — the .drift stays the project of record.
    Q_INVOKABLE void saveProjectJson(const QUrl &url);
    // Inverse of saveProjectJson. Replaces the open timeline from that document; media stays as
    // referenced paths. Does not become the project of record (no recents, empty path, dirty) so
    // Save cannot overwrite the .json with a .drift bundle. loadProject routes here when the file
    // is JSON, so a dropped / CLI / MCP path works without a second entry point.
    Q_INVOKABLE void loadProjectJson(const QUrl &url);
    // Imports an Adobe Premiere Pro project (.prproj) or Final Cut Pro XML (.xml),
    // mapping sequences, video/audio tracks, clips, in/out trimming, and media assets.
    Q_INVOKABLE void loadPremiereProject(const QUrl &url);
    // Unpacks and imports a Motion Graphics Template (.mogrt), extracting assets and mapping
    // editable text, colors, and media overlays onto the timeline and media library.
    Q_INVOKABLE void importMogrt(const QUrl &url);
    // Imports a Kdenlive (.kdenlive) or Shotcut MLT (.mlt) project, mapping
    // multitrack playlists, video/audio cuts, title text clips, and bin folders.
    Q_INVOKABLE void loadKdenliveProject(const QUrl &url);
    // Imports a DaVinci Resolve project (.drp) or Final Cut Pro X XML (.fcpxml).
    Q_INVOKABLE void loadResolveProject(const QUrl &url);
    // Imports a CMX 3600 Edit Decision List (.edl).
    Q_INVOKABLE void loadEdlTimeline(const QUrl &url);
    // Imports an OpenTimelineIO (.otio) sequence.
    Q_INVOKABLE void loadOtioTimeline(const QUrl &url);
    // Copies (or moves) every file the project uses — bin media, the images Lottie/SVG documents
    // load, textures and the derived mattes, face tracks, depth maps and stabilized renders — into
    // Video/Audio/Images/Derived/Other under the chosen folder, then relinks the project to them.
    // Runs off the GUI thread. A move leaves the undo history pointing at files that are gone, so
    // it clears it; a copy is one undoable edit.
    Q_INVOKABLE void collectMediaToFolder(const QUrl &folder, bool move);
    Q_INVOKABLE void cancelCollectMedia();
    Q_INVOKABLE void loadProject(const QUrl &url);
    // silent skips the "New project" status message — used by Close Project, which
    // reuses this reset but reports its own "Project closed" message instead; setting
    // lastMessage twice would queue two toasts, since each change is its own toast.
    Q_INVOKABLE void newProject(bool silent = false);
    Q_INVOKABLE void openRecentProject(const QString &path);
    Q_INVOKABLE void clearRecentProjects();
    // Removes one path from the recents list without deleting the file on disk.
    Q_INVOKABLE void removeRecentProject(const QString &path);
    Q_INVOKABLE void restoreAutosave();
    Q_INVOKABLE void discardAutosave();
    // Clears dirty + recovery without mutating the timeline. Used when the user
    // chooses Don't Save before quitting so the next launch does not offer restore.
    Q_INVOKABLE void discardUnsavedChanges();
    // When reopenLastProject is on: restore recovery silently, else load lastSessionPath.
    // Returns true if a restore/load was started (caller should skip RecoveryDialog).
    Q_INVOKABLE bool restoreLastSessionIfEnabled();
    // The autosave timer and aboutToQuit cover desktop, but Android never emits aboutToQuit when
    // the OS reclaims a backgrounded process — and backgrounding is how a phone app normally ends.
    // The shell calls this on the way out so the floor is the last edit, not the last 15s tick.
    Q_INVOKABLE void flushRecoverySnapshot();
    // First non-flag positional argument as a local file URL (paths, file://, portal URIs).
    static QUrl startupProjectUrlFromArguments(const QStringList &args);
    // argv / QFileOpenEvent. Queued until consumeStartupProject(); after that, emits
    // AppController::externalProjectOpenRequested so QML can confirm unsaved work.
    void queueExternalProject(const QUrl &url);
    // Load a queued startup document. True if a load started (skip recovery / last session).
    Q_INVOKABLE bool consumeStartupProject();

    // C++ access for AppController. Not invokable.
    void setDirty(bool dirty);
    void setCurrentProjectPath(const QString &path);
    QHash<QString, QString> &pendingPathRemap() { return m_pendingPathRemap; }
    const QHash<QString, QString> &pendingPathRemap() const { return m_pendingPathRemap; }
    QSet<QString> &embeddedSources() { return m_embeddedSources; }
    const QSet<QString> &embeddedSources() const { return m_embeddedSources; }
    void remapProjectPaths(const QHash<QString, QString> &remap);
    void rehydrateMissingSources();
    void reportMissingCatalogEntries();
    void sweepExtractionDirs();
    void detectRecoveryFile();

signals:
    void projectNameChanged();
    void projectMetadataChanged();
    void packagingChanged();
    void packageProgressChanged();
    void packageFinished(bool ok, const QString &message);
    void collectingMediaChanged();
    void collectMediaProgressChanged();
    // Save completion when saveProject took the Android streaming path (see saveProject). Never
    // emitted on desktop or for a plain, synchronous save — setLastMessage already covers those.
    void projectSaved(bool ok);
    // Addons the freshly opened project needs but that are not installed. Each entry is
    // id / name / version / kinds, for MissingAddonsDialog.
    void missingAddons(const QVariantList &addons);
    // Terminal result of loadProject()/loadProjectJson(): exactly one per call that
    // reaches a load generation still current when it finishes. A bundle with embedded
    // media raises the "Unpacking project media…" lastMessage first and this only once
    // extraction and apply are done — QML waiting to know whether an open landed (e.g.
    // to dismiss a "pick a project" screen) needs this rather than lastMessageChanged,
    // which fires for that progress message too.
    void projectLoadFinished(bool ok, const QString &message);
    void projectLoadPendingChanged();
    void backgroundChanged();
    void dirtyChanged();
    void currentProjectPathChanged();
    void recoveryChanged();
    void recentProjectsChanged();
    // File actions from the shortcut layer — QML owns dialogs and unsaved prompts.
    void newProjectRequested();
    void openRequested();
    void saveRequested();
    void saveAsRequested();

private:
    // Bracket every loadProject()/loadProjectJson() call, sync or async, success or
    // failure, so projectLoadPending is accurate regardless of what triggered the load.
    // beginProjectLoad() returns false (and acquires nothing) when a load already owns
    // the flag — the caller must bail out without touching m_projectLoadPending itself,
    // so a second, unrelated request can never clear the first one's pending state.
    bool beginProjectLoad();
    void finishProjectLoad(bool ok, const QString &message);
    // The actual body of loadProjectJson(), run once beginProjectLoad() has succeeded.
    // loadProject() delegates here directly for a JSON file — it already owns the
    // pending flag from its own beginProjectLoad(), so the JSON path must not try to
    // acquire it again (that would just no-op) nor release it early on failure.
    void loadProjectJsonInternal(const QUrl &url);
    // Shared by saveProject and packageProject. `embedSource` forces every source asset into the
    // bundle; otherwise each keeps whatever mode it had, tracked in m_embeddedSources. GUI thread
    // only — packageProject builds the request here and hands the finished copy to its worker.
    drift::bundle::WriteRequest buildWriteRequest(bool embedSource) const;
    // Who the document becomes when a Save As write succeeds. A fresh id keeps the copy from
    // sharing the original's extraction and derived-media directory, both of which are keyed on it.
    struct ProjectIdentity {
        QString id;
        QString name;
    };
    // Body of saveProject / saveProjectAs. `adopt` is empty for a plain Save; when set, the copy is
    // written under that identity and the open project only takes it on once the bytes are down.
    void writeProjectBundle(const QUrl &url, const std::optional<ProjectIdentity> &adopt);
    void adoptProjectIdentity(const std::optional<ProjectIdentity> &adopt);
    void rememberEmbeddedSources(const QList<drift::bundle::MediaEntry> &media);
    void reportMissingAddons(const QList<drift::bundle::AddonRef> &addons);
    void addRecentProject(const QString &path);
    // Off the GUI thread unless `synchronous` (quitting), which waits out any write in flight.
    void writeRecoveryFile(bool synchronous = false);
    void deleteRecoveryFile();
    static QString recoveryFilePath();

    AppController *m_app = nullptr;

    // Source paths that were embedded when this project was last read or written, so a plain Save
    // keeps a packaged project packaged instead of quietly making it depend on the cache dir.
    QSet<QString> m_embeddedSources;
    // Handed to applyProjectJson by loadProject, applied alongside the other load-time path
    // migrations and cleared there.
    QHash<QString, QString> m_pendingPathRemap;
    bool m_packaging = false;
    double m_packageProgress = 0.0;
    QAtomicInt m_packageCancel = 0;
    bool m_collectingMedia = false;
    double m_collectMediaProgress = 0.0;
    QAtomicInt m_collectMediaCancel = 0;
    int m_loadGeneration = 0; // bumped per loadProject; stale extracts are dropped
    bool m_projectLoadPending = false;

    // Save state / autosave / crash recovery.
    QString m_currentProjectPath;
    bool m_dirty = false;
    QTimer *m_autosaveTimer = nullptr;
    // The recovery write running on a worker, and a counter that deleteRecoveryFile() bumps so a
    // write that finishes after the file was meant to be gone does not bring it back.
    QFuture<QString> m_recoveryWrite;
    quint64 m_recoveryGeneration = 0;
    bool m_recoveryAvailable = false;
    QVariantMap m_recoveryInfo;
    QUrl m_pendingStartupProject;
    QUrl m_lastExternalProject;
    bool m_uiReady = false;

    static constexpr int kAutosaveIntervalMs = 15000;
    static constexpr int kMaxRecentProjects = 10;
};
