#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

// App-wide QSettings preferences and persisted UI layout. Reached from QML as
// EditorState.preferences. Nothing here reads the open project.
class PreferencesController : public QObject
{
    Q_OBJECT

    // App-wide theme preference, backed by QSettings("ui/darkMode"). Until the user
    // toggles once, darkModeOverridden is false and the UI follows the OS colour
    // scheme live; after that the stored choice wins on every launch.
    Q_PROPERTY(bool darkModeOverridden READ darkModeOverridden NOTIFY darkModePreferenceChanged)
    Q_PROPERTY(bool darkModePreferred READ darkModePreferred NOTIFY darkModePreferenceChanged)
    // Editor workspace arrangement. In the landscape workspace the preview sits in the
    // three-pane top row, so its size is bounded by that row's *height* — a 9:16 canvas
    // ends up postage-stamp small. The portrait workspace gives the preview a full-height
    // column beside the whole editing stack instead. Follows the canvas orientation until
    // the user picks one explicitly, after which the stored choice wins on every launch —
    // the same override rule as the theme above, backed by QSettings("ui/workspaceLayout").
    // The effective layout is resolved in QML so it can track both signals at once.
    Q_PROPERTY(bool workspaceLayoutOverridden READ workspaceLayoutOverridden
                   NOTIFY workspaceLayoutPreferenceChanged)
    Q_PROPERTY(QString workspaceLayoutPreferred READ workspaceLayoutPreferred
                   NOTIFY workspaceLayoutPreferenceChanged)
    // On by default. Worth turning off on a very long timeline: the strip repaints every clip
    // in the project on every edit.
    Q_PROPERTY(bool timelineOverviewVisible READ timelineOverviewVisible
                   WRITE setTimelineOverviewVisible NOTIFY timelineOverviewVisibleChanged)
    Q_PROPERTY(bool audioMixerVisible READ audioMixerVisible
                   WRITE setAudioMixerVisible NOTIFY audioMixerVisibleChanged)
    // Width the user dragged the mixer to; 0 means size to fit its channel strips.
    Q_PROPERTY(qreal audioMixerWidth READ audioMixerWidth
                   WRITE setAudioMixerWidth NOTIFY audioMixerWidthChanged)
    Q_PROPERTY(qreal trackLabelsWidth READ trackLabelsWidth
                   WRITE setTrackLabelsWidth NOTIFY trackLabelsWidthChanged)
    // Timeline toolbar layout: action ids shown as buttons, then the ones in its More menu.
    // Empty means the QML defaults; ids are validated on the QML side, which owns the registry.
    Q_PROPERTY(QStringList timelineToolbarItems READ timelineToolbarItems
                   NOTIFY timelineToolbarLayoutChanged)
    Q_PROPERTY(QStringList timelineMenuItems READ timelineMenuItems
                   NOTIFY timelineToolbarLayoutChanged)
    // Name patterns (ProxyNaming strings such as "*_proxy") that find proxies made outside Drift
    // beside a source. Seeded with the defaults until the user changes them.
    Q_PROPERTY(QStringList proxyNamePatterns READ proxyNamePatterns WRITE setProxyNamePatterns
                   NOTIFY proxyNamePatternsChanged)
    // Opt-in: on launch, restore the last open project (saved .drift or unsaved recovery snapshot).
    Q_PROPERTY(bool reopenLastProject READ reopenLastProject WRITE setReopenLastProject NOTIFY reopenLastProjectChanged)
    // Preview zero-copy import: VAAPI dma-buf on Linux, D3D11 interop on Windows. Takes effect
    // after restart; hidden when this machine has no decode backend for either.
    Q_PROPERTY(bool vaapiZeroCopy READ vaapiZeroCopy WRITE setVaapiZeroCopy NOTIFY vaapiZeroCopyChanged)
    Q_PROPERTY(bool vaapiZeroCopySupported READ vaapiZeroCopySupported CONSTANT)
    // Android MediaCodec zero-copy preview. Same shape and the same caveat as the VAAPI pair
    // above: a driver can import the surface and still sample it wrongly, which shows up as a
    // corrupt preview with nothing to catch it — so it is opt-in and takes effect on restart.
    Q_PROPERTY(bool mediaCodecZeroCopy READ mediaCodecZeroCopy WRITE setMediaCodecZeroCopy NOTIFY
                   mediaCodecZeroCopyChanged)
    Q_PROPERTY(bool mediaCodecZeroCopySupported READ mediaCodecZeroCopySupported CONSTANT)
    // Hybrid-graphics Windows laptops: which GPU Drift asks to run on — "auto", "integrated" or
    // "discrete". The driver picks the GPU when it loads, so this takes effect on the next
    // launch. Hidden on single-GPU machines and off Windows.
    Q_PROPERTY(QString preferredGpu READ preferredGpu WRITE setPreferredGpu NOTIFY preferredGpuChanged)
    Q_PROPERTY(bool gpuPreferenceSupported READ gpuPreferenceSupported CONSTANT)
    Q_PROPERTY(bool gpuPreferenceInSystemSettings READ gpuPreferenceInSystemSettings CONSTANT)
    // Graphics API the interface is drawn with (GraphicsApiChoice.h), next launch. uiGraphicsApis
    // lists what this platform offers; uiGraphicsApiRevertedFrom names a choice this launch fell
    // back from after it failed to start.
    Q_PROPERTY(QString uiGraphicsApi READ uiGraphicsApi WRITE setUiGraphicsApi NOTIFY uiGraphicsApiChanged)
    Q_PROPERTY(QStringList uiGraphicsApis READ uiGraphicsApis CONSTANT)
    Q_PROPERTY(QString uiGraphicsApiRevertedFrom READ uiGraphicsApiRevertedFrom CONSTANT)
    Q_PROPERTY(bool invertTimelineScroll READ invertTimelineScroll WRITE setInvertTimelineScroll
                   NOTIFY invertTimelineScrollChanged)
    Q_PROPERTY(bool autoCrossfadeOverlaps READ autoCrossfadeOverlaps WRITE setAutoCrossfadeOverlaps
                   NOTIFY autoCrossfadeOverlapsChanged)
    // Timeline toolbar auto-select: while stopped, moving the playhead selects the clip under it.
    Q_PROPERTY(bool autoSelectUnderPlayhead READ autoSelectUnderPlayhead WRITE setAutoSelectUnderPlayhead
                   NOTIFY autoSelectUnderPlayheadChanged)
    // Network proxy, QSettings("network/proxy*"). Mode is system, none, http or socks5.
    Q_PROPERTY(QString networkProxyMode READ networkProxyMode NOTIFY networkProxyChanged)
    Q_PROPERTY(QString networkProxyHost READ networkProxyHost NOTIFY networkProxyChanged)
    Q_PROPERTY(int networkProxyPort READ networkProxyPort NOTIFY networkProxyChanged)
    Q_PROPERTY(QString networkProxyUser READ networkProxyUser NOTIFY networkProxyChanged)
    Q_PROPERTY(QString networkProxyPassword READ networkProxyPassword NOTIFY networkProxyChanged)
    // Recovery snapshot interval for unsaved work, QSettings("editor/autosaveIntervalSeconds").
    // 0 is off; the other choices are 15, 30, 60 and 300. AppController applies it live.
    Q_PROPERTY(int autosaveIntervalSeconds READ autosaveIntervalSeconds WRITE setAutosaveIntervalSeconds
                   NOTIFY autosaveIntervalSecondsChanged)
    // Length of a still image dropped on the timeline, QSettings("timeline/stillDurationSeconds").
    Q_PROPERTY(double stillDurationSeconds READ stillDurationSeconds WRITE setStillDurationSeconds
                   NOTIFY stillDurationSecondsChanged)
    // Length of a transition added to a plain cut, QSettings("timeline/transitionDurationSeconds").
    // Transitions on an overlap keep the overlap's length.
    Q_PROPERTY(double transitionDurationSeconds READ transitionDurationSeconds
                   WRITE setTransitionDurationSeconds NOTIFY transitionDurationSecondsChanged)
    // Size of the cache folder, minus the subfolders a live disk cache owns. refreshCacheSize and
    // clearCache run off the GUI thread; cacheBusy is true while either is in flight.
    Q_PROPERTY(qint64 cacheSizeBytes READ cacheSizeBytes NOTIFY cacheSizeChanged)
    Q_PROPERTY(bool cacheBusy READ cacheBusy NOTIFY cacheSizeChanged)
    Q_PROPERTY(QString cacheLocation READ cacheLocation CONSTANT)
    // App-wide interface language, QSettings("ui/language"). Empty means follow the OS locale.
    // "en" is the source catalog (no .qm). Other codes match i18n/drift_<code>.qm.
    // needsUiLanguagePrompt is true only on a brand-new install, before the first-launch chooser
    // (or a later language pick from the header / Android Settings) has written ui/languageChosen.
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(QVariantList uiLanguages READ uiLanguages NOTIFY uiLanguageChanged)
    Q_PROPERTY(bool needsUiLanguagePrompt READ needsUiLanguagePrompt NOTIFY uiLanguageChanged)
    // QSettings("ui/scale"), 1.0..2.0 in 0.25 steps. Extra scale on top of the OS display scale,
    // except where applyStoredUiScale is given the OS scale (Windows): there it is the absolute
    // size, 1.0 being 96 DPI. Applied as QT_SCALE_FACTOR before QApplication; a change needs a
    // restart.
    Q_PROPERTY(double uiScale READ uiScale WRITE setUiScale NOTIFY uiScaleChanged)
    Q_PROPERTY(double appliedUiScale READ appliedUiScale CONSTANT)
    Q_PROPERTY(bool uiScaleNeedsRestart READ uiScaleNeedsRestart NOTIFY uiScaleChanged)
    // Which shell to build at startup: "auto", "desktop" or "mobile". QSettings("ui/shell").
    // Shell.qml decides once, so a change needs a restart. appliedShell is the shell actually
    // running ("desktop" or "mobile"), after --shell= and DRIFT_SHELL have had their say.
    Q_PROPERTY(QString interfaceShell READ interfaceShell WRITE setInterfaceShell NOTIFY interfaceShellChanged)
    Q_PROPERTY(QString appliedShell READ appliedShell CONSTANT)
    Q_PROPERTY(bool interfaceShellNeedsRestart READ interfaceShellNeedsRestart NOTIFY interfaceShellChanged)
    // Power-user performance knobs, QSettings("performance/*"). The values live in
    // drift::tuning (engine/EngineTuning.h) as atomics so the playback hot paths never read
    // settings; every setter writes them through. 0 means off / unlimited / automatic per knob.
    Q_PROPERTY(int staleFrameLagMs READ staleFrameLagMs WRITE setStaleFrameLagMs NOTIFY performanceChanged)
    Q_PROPERTY(int readAheadMs READ readAheadMs WRITE setReadAheadMs NOTIFY performanceChanged)
    Q_PROPERTY(int targetPoolMax READ targetPoolMax WRITE setTargetPoolMax NOTIFY performanceChanged)
    Q_PROPERTY(int targetPoolMiB READ targetPoolMiB WRITE setTargetPoolMiB NOTIFY performanceChanged)
    Q_PROPERTY(int uploadCacheMax READ uploadCacheMax WRITE setUploadCacheMax NOTIFY performanceChanged)
    Q_PROPERTY(int uploadCacheMiB READ uploadCacheMiB WRITE setUploadCacheMiB NOTIFY performanceChanged)
    Q_PROPERTY(int readerHistoryMiB READ readerHistoryMiB WRITE setReaderHistoryMiB NOTIFY performanceChanged)
    Q_PROPERTY(int frameCacheMiB READ frameCacheMiB WRITE setFrameCacheMiB NOTIFY performanceChanged)
    Q_PROPERTY(bool continuousReadAhead READ continuousReadAhead WRITE setContinuousReadAhead NOTIFY performanceChanged)
    Q_PROPERTY(bool preferHardwareExport READ preferHardwareExport WRITE setPreferHardwareExport NOTIFY performanceChanged)
    // False inside Flatpak: the sandbox dies with its first process, taking a relaunched child
    // with it, so the UI asks for a manual restart instead.
    Q_PROPERTY(bool canRestart READ canRestart CONSTANT)

public:
    explicit PreferencesController(QObject *parent = nullptr);

    bool darkModeOverridden() const { return m_darkModeOverridden; }
    bool darkModePreferred() const { return m_darkModePreferred; }
    Q_INVOKABLE void setDarkModePreference(bool enabled);
    Q_INVOKABLE void clearDarkModePreference();

    bool workspaceLayoutOverridden() const { return m_workspaceLayoutOverridden; }
    QString workspaceLayoutPreferred() const { return m_workspaceLayoutPreferred; }
    // "portrait" / "landscape"; anything else is treated as landscape.
    Q_INVOKABLE void setWorkspaceLayoutPreference(const QString &layout);
    // Back to following the canvas orientation.
    Q_INVOKABLE void clearWorkspaceLayoutPreference();

    bool timelineOverviewVisible() const { return m_timelineOverviewVisible; }
    void setTimelineOverviewVisible(bool visible);
    bool audioMixerVisible() const { return m_audioMixerVisible; }
    void setAudioMixerVisible(bool visible);
    qreal audioMixerWidth() const { return m_audioMixerWidth; }
    void setAudioMixerWidth(qreal width);
    qreal trackLabelsWidth() const { return m_trackLabelsWidth; }
    void setTrackLabelsWidth(qreal width);
    QStringList timelineToolbarItems() const { return m_timelineToolbarItems; }
    QStringList timelineMenuItems() const { return m_timelineMenuItems; }
    QStringList proxyNamePatterns() const { return m_proxyNamePatterns; }
    // Drops anything that isn't a valid pattern, and duplicates.
    void setProxyNamePatterns(const QStringList &patterns);
    void addProxyNamePattern(const QString &pattern);
    Q_INVOKABLE void resetProxyNamePatterns();
    Q_INVOKABLE void setTimelineToolbarLayout(const QStringList &toolbarItems,
                                              const QStringList &menuItems);

    bool reopenLastProject() const { return m_reopenLastProject; }
    void setReopenLastProject(bool enabled);
    bool vaapiZeroCopy() const { return m_vaapiZeroCopy; }
    void setVaapiZeroCopy(bool enabled);
    bool vaapiZeroCopySupported() const;
    bool mediaCodecZeroCopy() const { return m_mediaCodecZeroCopy; }
    void setMediaCodecZeroCopy(bool enabled);
    bool mediaCodecZeroCopySupported() const;
    QString preferredGpu() const { return m_preferredGpu; }
    void setPreferredGpu(const QString &id);
    bool gpuPreferenceSupported() const;
    bool gpuPreferenceInSystemSettings() const;
    QString uiGraphicsApi() const { return m_uiGraphicsApi; }
    void setUiGraphicsApi(const QString &id);
    QStringList uiGraphicsApis() const;
    QString uiGraphicsApiRevertedFrom() const;
    bool invertTimelineScroll() const { return m_invertTimelineScroll; }
    void setInvertTimelineScroll(bool enabled);
    bool autoCrossfadeOverlaps() const { return m_autoCrossfadeOverlaps; }
    void setAutoCrossfadeOverlaps(bool enabled);
    bool autoSelectUnderPlayhead() const { return m_autoSelectUnderPlayhead; }
    void setAutoSelectUnderPlayhead(bool enabled);
    QString networkProxyMode() const { return m_networkProxyMode; }
    QString networkProxyHost() const { return m_networkProxyHost; }
    int networkProxyPort() const { return m_networkProxyPort; }
    QString networkProxyUser() const { return m_networkProxyUser; }
    QString networkProxyPassword() const { return m_networkProxyPassword; }
    Q_INVOKABLE void setNetworkProxy(const QString &mode, const QString &host, int port,
                                     const QString &user, const QString &password);

    int autosaveIntervalSeconds() const { return m_autosaveIntervalSeconds; }
    void setAutosaveIntervalSeconds(int seconds);
    double stillDurationSeconds() const { return m_stillDurationSeconds; }
    void setStillDurationSeconds(double seconds);
    double transitionDurationSeconds() const { return m_transitionDurationSeconds; }
    void setTransitionDurationSeconds(double seconds);
    // Performance tunables (see the properties above).
    int staleFrameLagMs() const;
    void setStaleFrameLagMs(int value);
    int readAheadMs() const;
    void setReadAheadMs(int value);
    int targetPoolMax() const;
    void setTargetPoolMax(int value);
    int targetPoolMiB() const;
    void setTargetPoolMiB(int value);
    int uploadCacheMax() const;
    void setUploadCacheMax(int value);
    int uploadCacheMiB() const;
    void setUploadCacheMiB(int value);
    int readerHistoryMiB() const;
    void setReaderHistoryMiB(int value);
    int frameCacheMiB() const;
    void setFrameCacheMiB(int value);
    bool continuousReadAhead() const;
    void setContinuousReadAhead(bool enabled);
    bool preferHardwareExport() const;
    void setPreferHardwareExport(bool enabled);
    Q_INVOKABLE void resetPerformanceDefaults();

    qint64 cacheSizeBytes() const { return m_cacheSizeBytes; }
    bool cacheBusy() const { return m_cacheBusy; }
    QString cacheLocation() const;
    Q_INVOKABLE void refreshCacheSize();
    Q_INVOKABLE void clearCache();

    QString uiLanguage() const { return m_uiLanguage; }
    QVariantList uiLanguages() const;
    bool needsUiLanguagePrompt() const { return m_needsUiLanguagePrompt; }
    void setUiLanguage(const QString &language);
    // First-launch chooser: persist the pick and never ask again. Settings uses setUiLanguage.
    Q_INVOKABLE void chooseUiLanguage(const QString &code);
    double uiScale() const { return m_uiScale; }
    double appliedUiScale() const;
    bool uiScaleNeedsRestart() const;
    void setUiScale(double scale);
    // Snaps to 1.0, 1.25, 1.5, 1.75, or 2.0. Safe before any PreferencesController exists.
    static double storedUiScale();
    // Once per install: sets ui/scale from the primary screen's short side, 1080 rows being 1.0,
    // and records ui/autoScaleDone. Same preconditions as applyStoredUiScale.
    static void autoUiScaleOnce(int shortSide, double osDisplayScale);
    // Writes QT_SCALE_FACTOR from ui/scale unless the environment already set one. With
    // osDisplayScale, ui/scale is the absolute size and the factor is divided by it.
    // Call once before QApplication; organization/application names must already be set.
    static void applyStoredUiScale(double osDisplayScale = 0.0);
    QString interfaceShell() const { return m_interfaceShell; }
    void setInterfaceShell(const QString &shell);
    QString appliedShell() const;
    bool interfaceShellNeedsRestart() const;
    // "auto", "desktop" or "mobile". Safe before any PreferencesController exists.
    static QString storedInterfaceShell();
    // Maps "auto" to this platform's default; "desktop" and "mobile" pass through.
    static QString resolveInterfaceShell(const QString &shell);
    // Recorded by app/main.cpp once the shell for this run is decided.
    static void setAppliedShell(const QString &shell);
    bool canRestart() const;
    // False once the first-run Quick Setup has been completed (and always on wasm, which skips it).
    static bool quickSetupNeeded();
    Q_INVOKABLE void completeQuickSetup();
    // Clears the done flag and relaunches into setup; the caller handles unsaved work first.
    Q_INVOKABLE void rerunQuickSetup();
    // Android relaunches the activity right away; flush recovery first. Elsewhere this only
    // arms a relaunch for aboutToQuit, so the caller still closes the window and the
    // unsaved-changes prompt gets its say.
    Q_INVOKABLE void restartApp();
    // Writes every preference except machine-specific and secret ones as JSON. Import applies the
    // same filter, then asks for a restart. Both return "" on success, else the message to show.
    Q_INVOKABLE QString exportSettings(const QUrl &url);
    Q_INVOKABLE QString importSettings(const QUrl &url);
    // Installs the .qm for ui/language (or the system locale). Call once after QApplication
    // is named, and again from setUiLanguage. Safe before any PreferencesController exists.
    static void installUiTranslators();

signals:
    void darkModePreferenceChanged();
    void workspaceLayoutPreferenceChanged();
    void timelineOverviewVisibleChanged();
    void audioMixerVisibleChanged();
    void audioMixerWidthChanged();
    void trackLabelsWidthChanged();
    void proxyNamePatternsChanged();
    void timelineToolbarLayoutChanged();
    void reopenLastProjectChanged();
    void vaapiZeroCopyChanged();
    void mediaCodecZeroCopyChanged();
    void preferredGpuChanged();
    void uiGraphicsApiChanged();
    void invertTimelineScrollChanged();
    void autoCrossfadeOverlapsChanged();
    void autoSelectUnderPlayheadChanged();
    void networkProxyChanged();
    void autosaveIntervalSecondsChanged();
    void stillDurationSecondsChanged();
    void transitionDurationSecondsChanged();
    void cacheSizeChanged();
    void uiLanguageChanged();
    void quickSetupCompleted();
    void uiScaleChanged();
    void interfaceShellChanged();
    void performanceChanged();
    // setVaapiZeroCopy, setMediaCodecZeroCopy, setPreferredGpu and setUiGraphicsApi take effect on the next
    // launch. AppController turns this into a status toast.
    void restartNoticeRequested(const QString &text);

private:
    bool m_darkModeOverridden = false;
    bool m_darkModePreferred = true;
    bool m_workspaceLayoutOverridden = false;
    QString m_workspaceLayoutPreferred = QStringLiteral("landscape");
    bool m_timelineOverviewVisible = false;
    bool m_audioMixerVisible = false;
    qreal m_audioMixerWidth = 0;
    qreal m_trackLabelsWidth = 130;
    QStringList m_timelineToolbarItems;
    QStringList m_proxyNamePatterns;
    QStringList m_timelineMenuItems;
    bool m_reopenLastProject = false;
    bool m_vaapiZeroCopy = false;
    bool m_mediaCodecZeroCopy = false;
    QString m_preferredGpu = QStringLiteral("auto");
    QString m_uiGraphicsApi;
    bool m_invertTimelineScroll = false;
    bool m_autoCrossfadeOverlaps = true;
    bool m_autoSelectUnderPlayhead = false;
    QString m_networkProxyMode = QStringLiteral("system");
    QString m_networkProxyHost;
    int m_networkProxyPort = 0;
    QString m_networkProxyUser;
    QString m_networkProxyPassword;
    QString m_uiLanguage;
    bool m_needsUiLanguagePrompt = false;
    double m_uiScale = 1.0;
    QString m_interfaceShell = QStringLiteral("auto");
    bool m_relaunchOnQuit = false;
    int m_autosaveIntervalSeconds = 15;
    double m_stillDurationSeconds = 5.0;
    double m_transitionDurationSeconds = 0.5;
    qint64 m_cacheSizeBytes = 0;
    bool m_cacheBusy = false;

    void runCacheJob(bool clear);
    void loadPerformanceSettings();
};
