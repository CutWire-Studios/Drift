#pragma once

#include "core/Project.h"
#include "core/Time.h"
#include "engine/AudioOnsets.h"
#include "engine/FilmstripTileCache.h"
#include "engine/MediaWaveform.h"
#include "engine/WaveformBlockCache.h"
#include "engine/ProjectBundle.h"
#include "engine/Sam2Segmenter.h"
#include "ClipListModel.h"
#include "TimelineModel.h"
#include "models/AssetLibrary.h"

#include <QAtomicInt>
#include <QHash>
#include <QJsonObject>
#include <QMediaDevices>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QUndoStack>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>

struct EffectTemplateEntry;

class QTimer;

namespace drift::mcp {
class McpServer;
}

#include "playback/ClipPreviewPlayer.h"
#include "playback/PlaybackEngine.h"

// QML-facing controller over the core project model and undo stack.
class AppController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(AssetLibrary *assetLibrary READ assetLibrary CONSTANT)
    Q_PROPERTY(TimelineModel *timelineModel READ timelineModel CONSTANT)
    Q_PROPERTY(ClipListModel *clipListModel READ clipListModel CONSTANT)
    Q_PROPERTY(PlaybackEngine *playback READ playback CONSTANT)
    // Output devices to choose between, each {id, label}; the first entry has an empty id and
    // means "whatever the system default is at the time", which is also the default choice.
    Q_PROPERTY(QVariantList audioOutputDevices READ audioOutputDevices NOTIFY audioOutputDevicesChanged)
    Q_PROPERTY(QString audioOutputDeviceId READ audioOutputDeviceId WRITE setAudioOutputDeviceId
                   NOTIFY audioOutputDeviceIdChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
    Q_PROPERTY(double playheadSeconds READ playheadSeconds WRITE setPlayheadSeconds NOTIFY playheadSecondsChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY tracksChanged)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY playingChanged)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapEnabledChanged)
    Q_PROPERTY(bool rippleEnabled READ rippleEnabled WRITE setRippleEnabled NOTIFY rippleEnabledChanged)
    Q_PROPERTY(bool allowClipOverlap READ allowClipOverlap WRITE setAllowClipOverlap NOTIFY allowClipOverlapChanged)
  // Per-project UI prefs (serialized with the .drift file, not global QSettings).
    Q_PROPERTY(bool mediaGridMode READ mediaGridMode WRITE setMediaGridMode NOTIFY mediaGridModeChanged)
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
    Q_PROPERTY(bool projectPortrait READ projectPortrait NOTIFY tracksChanged)
    Q_PROPERTY(bool workspaceLayoutOverridden READ workspaceLayoutOverridden
                   NOTIFY workspaceLayoutPreferenceChanged)
    Q_PROPERTY(QString workspaceLayoutPreferred READ workspaceLayoutPreferred
                   NOTIFY workspaceLayoutPreferenceChanged)
    Q_PROPERTY(bool autoKeyEnabled READ autoKeyEnabled WRITE setAutoKeyEnabled NOTIFY autoKeyEnabledChanged)
    // Opt-in: on launch, restore the last open project (saved .drift or unsaved recovery snapshot).
    Q_PROPERTY(bool reopenLastProject READ reopenLastProject WRITE setReopenLastProject NOTIFY reopenLastProjectChanged)
    // Session-only localhost MCP for agents. Never persisted. Off at every launch.
    Q_PROPERTY(bool mcpEnabled READ mcpEnabled WRITE setMcpEnabled NOTIFY mcpRunningChanged)
    Q_PROPERTY(bool mcpRunning READ mcpRunning NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpUrl READ mcpUrl NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpToken READ mcpToken NOTIFY mcpRunningChanged)
    Q_PROPERTY(int mcpPort READ mcpPort NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpError READ mcpError NOTIFY mcpErrorChanged)
    Q_PROPERTY(QString mcpCursorSnippet READ mcpCursorSnippet NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpClaudeCommand READ mcpClaudeCommand NOTIFY mcpRunningChanged)
    Q_PROPERTY(QString mcpStdioSnippet READ mcpStdioSnippet NOTIFY mcpRunningChanged)
    // App-wide interface language, QSettings("ui/language"). Empty means follow the OS locale.
    // "en" is the source catalog (no .qm). Other codes match i18n/drift_<code>.qm.
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY uiLanguageChanged)
    Q_PROPERTY(QVariantList uiLanguages READ uiLanguages NOTIFY uiLanguageChanged)
    // The keyframe strip draws every animated property of the selected clip. This is the subset
    // the user has folded away: a view filter only — hiding a curve never changes what renders.
    Q_PROPERTY(QStringList keyframeGraphHiddenProperties READ keyframeGraphHiddenProperties
                   NOTIFY keyframeGraphVisibilityChanged)
    // Detected beats / onsets for the keyframe strip. Transient analysis state — never
    // saved with the project, cleared whenever the underlying audio could have moved.
    Q_PROPERTY(QVariantMap beatAnalysis READ beatAnalysis NOTIFY beatAnalysisChanged)
    Q_PROPERTY(bool beatAnalysisRunning READ beatAnalysisRunning NOTIFY beatAnalysisChanged)
    // The grid and the transients are independently shown and snapped to. Both come out of
    // one analysis pass — the tempo is derived from the same onset envelope — so these
    // gate display and snapping, not the DSP.
    Q_PROPERTY(bool beatGridVisible READ beatGridVisible WRITE setBeatGridVisible
                   NOTIFY beatAnalysisChanged)
    Q_PROPERTY(bool onsetsVisible READ onsetsVisible WRITE setOnsetsVisible
                   NOTIFY beatAnalysisChanged)
    Q_PROPERTY(bool subtitleEditing READ subtitleEditing WRITE setSubtitleEditing NOTIFY subtitleEditingChanged)
    Q_PROPERTY(int selectedSubtitleCue READ selectedSubtitleCue WRITE setSelectedSubtitleCue
                   NOTIFY selectedSubtitleCueChanged)
    Q_PROPERTY(bool undoAvailable READ undoAvailable NOTIFY undoStackChanged)
    Q_PROPERTY(bool redoAvailable READ redoAvailable NOTIFY undoStackChanged)
    Q_PROPERTY(bool exportInProgress READ exportInProgress NOTIFY exportInProgressChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportProgressChanged)
    Q_PROPERTY(bool subtitleGenerating READ subtitleGenerating NOTIFY subtitleGeneratingChanged)
    // Id of the asset whose replacement is being probed, empty when idle. Only that one bin row
    // goes busy: the rest of the panel stays usable, and the wait belongs to the row the user
    // right-clicked.
    Q_PROPERTY(QString replacingAssetId READ replacingAssetId NOTIFY replacingAssetIdChanged)
    Q_PROPERTY(double subtitleGenProgress READ subtitleGenProgress NOTIFY subtitleGenProgressChanged)
    Q_PROPERTY(QString subtitleGenStatus READ subtitleGenStatus NOTIFY subtitleGenStatusChanged)
    Q_PROPERTY(bool segmenting READ segmenting NOTIFY segmentingChanged)
    Q_PROPERTY(double segmentProgress READ segmentProgress NOTIFY segmentProgressChanged)
    Q_PROPERTY(QString segmentStatus READ segmentStatus NOTIFY segmentStatusChanged)
    Q_PROPERTY(bool reverseRendering READ reverseRendering NOTIFY reverseRenderingChanged)
    Q_PROPERTY(double reverseRenderProgress READ reverseRenderProgress NOTIFY reverseRenderProgressChanged)
    Q_PROPERTY(QString reverseRenderStatus READ reverseRenderStatus NOTIFY reverseRenderStatusChanged)
    Q_PROPERTY(bool denoising READ denoising NOTIFY denoisingChanged)
    Q_PROPERTY(double denoiseProgress READ denoiseProgress NOTIFY denoiseProgressChanged)
    Q_PROPERTY(QString denoiseStatus READ denoiseStatus NOTIFY denoiseStatusChanged)
    Q_PROPERTY(bool segmentSessionActive READ segmentSessionActive NOTIFY segmentSessionChanged)
    Q_PROPERTY(bool segmentationForTemplate READ segmentationForTemplate NOTIFY segmentSessionChanged)
    Q_PROPERTY(bool segmentEncoding READ segmentEncoding NOTIFY segmentSessionChanged)
    Q_PROPERTY(int segmentRevision READ segmentRevision NOTIFY segmentSessionChanged)
    Q_PROPERTY(QVariantList segmentPoints READ segmentPoints NOTIFY segmentSessionChanged)
    Q_PROPERTY(QSize segmentFrameSize READ segmentFrameSize NOTIFY segmentSessionChanged)
    Q_PROPERTY(bool speedCurveSessionActive READ speedCurveSessionActive NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(QVariantList speedCurvePoints READ speedCurvePoints NOTIFY speedCurveChanged)
    Q_PROPERTY(int speedCurveRevision READ speedCurveRevision NOTIFY speedCurveFrameChanged)
    Q_PROPERTY(QSize speedCurveFrameSize READ speedCurveFrameSize NOTIFY speedCurveFrameChanged)
    Q_PROPERTY(double speedCurveSourceStart READ speedCurveSourceStart NOTIFY speedCurveSessionChanged)
    // Whole media length, not the clip's trimmed span — the filmstrip's frames are sampled across
    // the source file, so placing them needs both.
    Q_PROPERTY(double speedCurveMediaDuration READ speedCurveMediaDuration NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(double speedCurveSourceDuration READ speedCurveSourceDuration NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(double speedCurveRetimedDuration READ speedCurveRetimedDuration NOTIFY speedCurveChanged)
    Q_PROPERTY(double speedCurvePosition READ speedCurvePosition NOTIFY speedCurvePositionChanged)
    // Where the playhead sits along the *source*, 0..1 — the graph's own axis.
    Q_PROPERTY(double speedCurveSourcePosition READ speedCurveSourcePosition NOTIFY speedCurvePositionChanged)
    Q_PROPERTY(bool speedCurvePlaying READ speedCurvePlaying NOTIFY speedCurvePlayingChanged)
    Q_PROPERTY(QString speedCurveClipName READ speedCurveClipName NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(QString speedCurveClipPath READ speedCurveClipPath NOTIFY speedCurveSessionChanged)
    Q_PROPERTY(QString speedCurveFilmstripPath READ speedCurveFilmstripPath NOTIFY speedCurveSessionChanged)
    // Custom fade-shape session for FadeCurveWindow. Candidate is auditioned on the live clip
    // until applyFadeCurve commits it (or endFadeCurveSession restores the prior shape).
    Q_PROPERTY(bool fadeCurveSessionActive READ fadeCurveSessionActive NOTIFY fadeCurveSessionChanged)
    Q_PROPERTY(QVariantList fadeCurvePoints READ fadeCurvePoints NOTIFY fadeCurveChanged)
    Q_PROPERTY(QString fadeCurveClipName READ fadeCurveClipName NOTIFY fadeCurveSessionChanged)
    Q_PROPERTY(bool faceDetecting READ faceDetecting NOTIFY faceDetectingChanged)
    Q_PROPERTY(double faceDetectProgress READ faceDetectProgress NOTIFY faceDetectProgressChanged)
    Q_PROPERTY(QString faceDetectStatus READ faceDetectStatus NOTIFY faceDetectStatusChanged)
    Q_PROPERTY(int selectedTrack READ selectedTrack NOTIFY selectionChanged)
    Q_PROPERTY(int selectedClip READ selectedClip NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedClipData READ selectedClipData NOTIFY selectedClipDataChanged)
    Q_PROPERTY(QVariantList selectedClipEffects READ selectedClipEffects NOTIFY selectedClipDataChanged)
    Q_PROPERTY(QVariantList selectedClipAudioEffects READ selectedClipAudioEffects NOTIFY selectedClipDataChanged)
    Q_PROPERTY(QVariantMap selectedTransitionData READ selectedTransitionData NOTIFY selectedTransitionDataChanged)
    Q_PROPERTY(int selectedTransitionTrack READ selectedTransitionTrack NOTIFY selectedTransitionDataChanged)
    Q_PROPERTY(int selectedTransitionLeftClip READ selectedTransitionLeftClip NOTIFY selectedTransitionDataChanged)
    Q_PROPERTY(bool guidesEnabled READ guidesEnabled WRITE setGuidesEnabled NOTIFY guidesChanged)
    Q_PROPERTY(QString guideType READ guideType WRITE setGuideType NOTIFY guidesChanged)
    Q_PROPERTY(QVariantMap background READ background NOTIFY backgroundChanged)
    Q_PROPERTY(bool canvasCropMode READ canvasCropMode WRITE setCanvasCropMode NOTIFY canvasCropModeChanged)
    // Direct manipulation of the selected clip's mask stack in the preview. Mutually exclusive
    // with canvasCropMode — both want the same grips and the same pointer.
    Q_PROPERTY(bool maskEditMode READ maskEditMode WRITE setMaskEditMode NOTIFY maskEditModeChanged)
    // Which stack entry the overlay draws grips for. Shared with the inspector so clicking an
    // outline in the preview opens the matching row and vice versa.
    // The cutout the lane and the properties panel are both pointing at. A mask belongs to a
    // track rather than a clip, so it takes a track index of its own — selecting a clip does not
    // select a mask and vice versa. -1 for either means nothing is selected.
    Q_PROPERTY(int selectedMaskTrack READ selectedMaskTrack NOTIFY selectedMaskChanged)
    Q_PROPERTY(int selectedMaskIndex READ selectedMaskIndex NOTIFY selectedMaskChanged)
    // Clip id whose mask coverage the preview shows instead of the composite, or empty. Held as
    // an id rather than indices so it survives a track or clip reorder.
    Q_PROPERTY(QString maskViewClipId READ maskViewClipId WRITE setMaskViewClipId NOTIFY
                       maskViewClipIdChanged)
    Q_PROPERTY(bool inlineTextEditing READ inlineTextEditing NOTIFY inlineTextEditingChanged)
    Q_PROPERTY(QVariantList actions READ actions NOTIFY shortcutsChanged)
    Q_PROPERTY(QVariantList bookmarks READ bookmarks NOTIFY bookmarksChanged)
    Q_PROPERTY(bool workAreaActive READ workAreaActive NOTIFY workAreaChanged)
    Q_PROPERTY(double workAreaInSeconds READ workAreaInSeconds NOTIFY workAreaChanged)
    Q_PROPERTY(double workAreaOutSeconds READ workAreaOutSeconds NOTIFY workAreaChanged)
    Q_PROPERTY(bool loopWorkAreaEnabled READ loopWorkAreaEnabled WRITE setLoopWorkAreaEnabled
                   NOTIFY loopWorkAreaEnabledChanged)
    Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY projectNameChanged)
    Q_PROPERTY(QVariantMap projectMetadata READ projectMetadata NOTIFY projectMetadataChanged)
    Q_PROPERTY(bool packaging READ packaging NOTIFY packagingChanged)
    Q_PROPERTY(double packageProgress READ packageProgress NOTIFY packageProgressChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)
    // Severity of lastMessage: "info" | "success" | "warning" | "error". Exists so
    // the QML toast host does not have to guess from the message wording — it used
    // to regex the prose, and none of the real failure strings matched, so a
    // corrupt-project open rendered as a neutral info toast.
    Q_PROPERTY(QString lastMessageSeverity READ lastMessageSeverity NOTIFY lastMessageChanged)
    Q_PROPERTY(int draggingAssetIndex READ draggingAssetIndex WRITE setDraggingAssetIndex NOTIFY draggingAssetIndexChanged)
    Q_PROPERTY(bool hasUnsavedChanges READ hasUnsavedChanges NOTIFY dirtyChanged)
    Q_PROPERTY(QString currentProjectPath READ currentProjectPath NOTIFY currentProjectPathChanged)
    Q_PROPERTY(bool recoveryAvailable READ recoveryAvailable NOTIFY recoveryChanged)
    Q_PROPERTY(QVariantMap recoveryInfo READ recoveryInfo NOTIFY recoveryChanged)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY recentProjectsChanged)
    Q_PROPERTY(bool separateAudioAvailable READ canSeparateAudioSelection NOTIFY editCapabilitiesChanged)
    Q_PROPERTY(bool unlinkAvailable READ canUnlinkSelection NOTIFY editCapabilitiesChanged)
    Q_PROPERTY(bool mergeAvailable READ canMergeSelection NOTIFY editCapabilitiesChanged)
    // False until the user picks a launch layout (or decides later via first-clip setup / load).
    Q_PROPERTY(bool projectLayoutChosen READ projectLayoutChosen NOTIFY projectLayoutChosenChanged)

public:
    explicit AppController(AssetLibrary *assetLibrary, QObject *parent = nullptr);
    ~AppController() override;

    AssetLibrary *assetLibrary() const { return m_assetLibrary; }
    TimelineModel *timelineModel() { return &m_timelineModel; }
    ClipListModel *clipListModel() { return &m_clipListModel; }
    PlaybackEngine *playback() { return &m_playback; }
    QVariantList audioOutputDevices() const;
    QString audioOutputDeviceId() const { return m_audioOutputDeviceId; }
    void setAudioOutputDeviceId(const QString &id);
    drift::Project *project() { return &m_project; }
    const drift::Project *project() const { return &m_project; }

    QVariantList tracks() const;
    double playheadSeconds() const;
    double durationSeconds() const;
    bool playing() const { return m_playing; }
    bool snapEnabled() const { return m_snapEnabled; }
    bool rippleEnabled() const { return m_rippleEnabled; }
    bool allowClipOverlap() const { return m_allowClipOverlap; }
    bool darkModeOverridden() const { return m_darkModeOverridden; }
    bool darkModePreferred() const { return m_darkModePreferred; }
    bool projectPortrait() const { return m_project.height() > m_project.width(); }
    bool workspaceLayoutOverridden() const { return m_workspaceLayoutOverridden; }
    QString workspaceLayoutPreferred() const { return m_workspaceLayoutPreferred; }
    // "portrait" / "landscape"; anything else is treated as landscape.
    Q_INVOKABLE void setWorkspaceLayoutPreference(const QString &layout);
    // Back to following the canvas orientation.
    Q_INVOKABLE void clearWorkspaceLayoutPreference();
    bool mediaGridMode() const { return m_mediaGridMode; }
    bool autoKeyEnabled() const { return m_autoKeyEnabled; }
    bool reopenLastProject() const { return m_reopenLastProject; }
    QString uiLanguage() const { return m_uiLanguage; }
    QVariantList uiLanguages() const;
    // Installs the .qm for ui/language (or the system locale). Call once after QApplication
    // is named, and again from setUiLanguage. Safe before any AppController exists.
    static void installUiTranslators();
    QStringList keyframeGraphHiddenProperties() const { return m_keyframeGraphHiddenProperties; }
    bool subtitleEditing() const { return m_subtitleEditing; }
    int selectedSubtitleCue() const { return m_selectedSubtitleCue; }
    bool undoAvailable() const { return m_undoStack.canUndo(); }
    bool redoAvailable() const { return m_undoStack.canRedo(); }
    bool exportInProgress() const { return m_exportInProgress; }
    double exportProgress() const;
    bool subtitleGenerating() const { return m_subtitleGenerating; }
    QString replacingAssetId() const { return m_replacingAssetId; }
    double subtitleGenProgress() const { return m_subtitleGenProgress; }
    QString subtitleGenStatus() const { return m_subtitleGenStatus; }
    bool segmenting() const { return m_segmenting; }
    double segmentProgress() const { return m_segmentProgress; }
    QString segmentStatus() const { return m_segmentStatus; }
    bool reverseRendering() const { return m_reverseRendering; }
    double reverseRenderProgress() const { return m_reverseProgress; }
    QString reverseRenderStatus() const { return m_reverseStatus; }
    bool denoising() const { return m_denoising; }
    double denoiseProgress() const { return m_denoiseProgress; }
    QString denoiseStatus() const { return m_denoiseStatus; }
    bool segmentSessionActive() const { return m_segSessionActive; }
    bool segmentationForTemplate() const { return m_segForTemplate; }
    bool segmentEncoding() const { return m_segEncoding; }
    int segmentRevision() const { return m_segRevision; }
    QVariantList segmentPoints() const { return m_segPoints; }
    QSize segmentFrameSize() const { return m_segFrame.size(); }
    bool faceDetecting() const { return m_faceDetecting; }
    double faceDetectProgress() const { return m_faceDetectProgress; }
    QString faceDetectStatus() const { return m_faceDetectStatus; }
    int selectedTrack() const { return m_selectedTrack; }
    int selectedClip() const { return m_selectedClip; }
    QVariantList selection() const;
    QVariantMap selectedClipData() const;
    QVariantList selectedClipEffects() const;
    QVariantList selectedClipAudioEffects() const;
    QVariantMap selectedTransitionData() const;
    int selectedTransitionTrack() const { return m_selectedTransitionTrack; }
    int selectedTransitionLeftClip() const { return m_selectedTransitionLeftClip; }
    bool guidesEnabled() const { return m_guidesEnabled; }
    QString guideType() const { return m_guideType; }
    QVariantMap background() const;
    QVariantList actions() const;
    QVariantList bookmarks() const;
    bool workAreaActive() const { return m_project.hasWorkArea(); }
    double workAreaInSeconds() const;
    double workAreaOutSeconds() const;
    bool loopWorkAreaEnabled() const { return m_loopWorkAreaEnabled; }
    void setLoopWorkAreaEnabled(bool enabled);
    QString projectName() const;
    QString lastMessage() const { return m_lastMessage; }
    QString lastMessageSeverity() const { return m_lastMessageSeverity; }
    int draggingAssetIndex() const { return m_draggingAssetIndex; }
    void setDraggingAssetIndex(int index);
    bool hasUnsavedChanges() const { return m_dirty; }
    QString currentProjectPath() const { return m_currentProjectPath; }
    bool recoveryAvailable() const { return m_recoveryAvailable; }
    QVariantMap recoveryInfo() const { return m_recoveryInfo; }
    QVariantList recentProjects() const;

    void setPlayheadSeconds(double seconds);
    void setPlaying(bool playing);
    void setSnapEnabled(bool enabled);
    void setRippleEnabled(bool enabled);
    void setAllowClipOverlap(bool enabled);
    Q_INVOKABLE void setDarkModePreference(bool enabled);
    Q_INVOKABLE void clearDarkModePreference();
    void setMediaGridMode(bool enabled);
    void setAutoKeyEnabled(bool enabled);
    void setReopenLastProject(bool enabled);
    Q_INVOKABLE void setMcpEnabled(bool enabled);
    bool mcpEnabled() const { return mcpRunning(); }
    bool mcpRunning() const;
    QString mcpUrl() const;
    QString mcpToken() const;
    int mcpPort() const;
    QString mcpError() const;
    QString mcpCursorSnippet() const;
    QString mcpClaudeCommand() const;
    QString mcpStdioSnippet() const;
    Q_INVOKABLE void copyMcpCursorSnippet();
    Q_INVOKABLE void copyMcpClaudeCommand();
    Q_INVOKABLE void copyMcpStdioSnippet();
    Q_INVOKABLE void copyMcpAgentGuide();
    QString mcpAgentGuide() const;

    // MCP helpers (GUI thread). Used by src/mcp, not QML.
    QPair<int, int> mcpLocateClip(const QString &id) const;
    QString mcpClipId(int trackIndex, int clipIndex) const;
    QVariantMap mcpCompactClip(int trackIndex, int clipIndex, bool includeCanvas = true) const;
    QJsonObject mcpInspect(bool includeClips, int sinceRevision = -1, bool detail = false) const;
    int mcpRevision() const { return m_mcpEditRevision; }
    bool mcpSetClipCanvas(int trackIndex, int clipIndex, const QVariantMap &patch);
    QJsonObject mcpCaptureFrame(double atSeconds, bool full);
    bool mcpSetWorkArea(double inSeconds, double outSeconds);

    // Audio for agents. All of these block: the QML-facing waveform getters return empty on the
    // first call and repaint on a signal, which works for a binding and not at all for a caller
    // that gets one reply. These decode/mix inline instead, on the mcpCaptureFrame pattern.
    QJsonObject mcpWaveformForClip(int trackIndex, int clipIndex, int buckets) const;
    QJsonObject mcpWaveformForAsset(const QString &assetId, double startSeconds,
                                    double durSeconds, int buckets) const;
    QJsonObject mcpWaveformForTimeline(double startSeconds, double durSeconds, int buckets) const;
    QJsonObject mcpDetectBeats(double startSeconds, double durSeconds, bool force);
    QJsonObject mcpBeatPayload() const;
    QJsonObject mcpAudioSummary() const;
    // Grid times from the current analysis. `unit` is beat, bar or onset; `minStrength` filters
    // onsets only. Empty when nothing has been analysed yet.
    QList<double> mcpBeatTimes(const QString &unit, double minStrength) const;
    int mcpBookmarkBeats(double startSeconds, double durSeconds, const QString &unit,
                         double minStrength, const QString &labelPrefix);
    QJsonObject mcpSetBeatLayers(bool grid, bool onsets);
    QJsonObject mcpSetClipVolume(int trackIndex, int clipIndex, double value, bool atGiven,
                                 double atSeconds);
    void mcpRememberExportSettings(const QVariantMap &settings);
    void mcpBeginBatch();
    void mcpEndBatch(const QString &text, bool pushUndo);
    void setUiLanguage(const QString &code);
    // Strip chip click — folds `prop`'s curve away, or brings it back. Purely a view filter: the
    // chip stays put either way, and the animation keeps playing while it is hidden.
    Q_INVOKABLE void toggleKeyframeGraphPropertyVisible(const QString &prop);
    // Editing a property's value/diamond/interpolation un-hides its curve, so the thing just
    // edited is the thing on screen.
    Q_INVOKABLE void showKeyframeGraphProperty(const QString &prop);
    void setSubtitleEditing(bool editing);
    void setSelectedSubtitleCue(int index);
    void setProjectName(const QString &name);
    void setGuidesEnabled(bool enabled);
    void setGuideType(const QString &type);

    Q_INVOKABLE void addClipFromAsset(int assetIndex);
    Q_INVOKABLE void addClipFromAssetAt(int assetIndex, int trackIndex, double atSeconds);
    Q_INVOKABLE void addClipFromAssetOnNewTrack(int assetIndex, double atSeconds);
    // Same, but the new track goes at insertIndex rather than always on top, so
    // a timeline drop can create a lane below the tracks as well as above them.
    Q_INVOKABLE void addClipFromAssetOnNewTrackAt(int assetIndex, int insertIndex, double atSeconds);
    Q_INVOKABLE int clipCountForAsset(int assetIndex) const;
    Q_INVOKABLE bool removeAsset(int assetIndex);
    // Bin label only — does not rename the file on disk or rewrite clip names.
    Q_INVOKABLE bool renameAsset(int assetIndex, const QString &name);
    // Points an existing bin row at a different file, keeping every clip that uses it where it
    // is — its position, trim, effects and transitions all survive. Asynchronous: true only means
    // the probe started, and the outcome arrives as assetReplaceFinished.
    Q_INVOKABLE bool replaceAssetSource(int assetIndex, const QUrl &url);
    // Writes an image asset (a freeze frame, typically) out to `url`. The format follows the
    // destination's extension, so the picker's name filter never has to be reported back.
    Q_INVOKABLE bool exportAssetImage(int assetIndex, const QUrl &url);
    Q_INVOKABLE bool trackAcceptsAsset(int trackIndex, int assetIndex) const;
    Q_INVOKABLE QString trackTypeForAsset(int assetIndex) const;
    // presetId applies a built-in style pack on create; empty keeps the default text style.
    Q_INVOKABLE void addTextClip(const QString &text, double atSeconds,
                                 const QString &presetId = QString());
    Q_INVOKABLE void addSubtitleClip(double atSeconds);
    // Import a SubRip (.srt) file as a new subtitle clip at the playhead (or atSeconds).
    Q_INVOKABLE bool importSubtitleFile(const QUrl &url, double atSeconds = -1.0);
    // Replace cues on an existing subtitle clip from a .srt file.
    Q_INVOKABLE bool importSubtitleFileIntoClip(int trackIndex, int clipIndex, const QUrl &url);
    // Export a subtitle clip's cues to a .srt file (clip-local timestamps).
    Q_INVOKABLE bool exportSubtitleFile(int trackIndex, int clipIndex, const QUrl &url);
    // maxWordsPerCue caps words per caption; 0 keeps the recommended (character-width) packing.
    Q_INVOKABLE void generateSubtitlesForClip(int trackIndex, int clipIndex,
                                              const QString &language = QString(),
                                              int maxWordsPerCue = 0);
    Q_INVOKABLE void cancelSubtitleGeneration();
    Q_INVOKABLE QVariantList whisperLanguages();
    // points: [{x, y, include}] with x/y normalized to the source frame.
    // outputMode: "clips" (foreground + background on two new tracks) or "mask" (in place).
    Q_INVOKABLE void segmentClip(int trackIndex, int clipIndex, const QVariantList &points,
                                 const QString &outputMode);
    Q_INVOKABLE void cancelSegmentation();
    Q_INVOKABLE bool segmentationAvailable();
    Q_INVOKABLE QString segmentationModelVariant();
    // Interactive prompting session driving the segmentation window. beginSegmentationSession
    // encodes the reference frame off the GUI thread; point edits after that only re-run the
    // cheap decoder.
    Q_INVOKABLE void beginSegmentationSession(int trackIndex, int clipIndex, double seconds,
                                              bool forTemplate = false);
    Q_INVOKABLE void endSegmentationSession();
    void openSegmentationForTemplate(int trackIndex, int clipIndex);

    // Speed-curve editing session driving SpeedCurveWindow. The curve is held here as a
    // candidate and auditioned through a private single-clip player; the project is not touched
    // until applySpeedCurve mints the retimed copy.
    Q_INVOKABLE void beginSpeedCurveSession(int trackIndex, int clipIndex);
    Q_INVOKABLE void endSpeedCurveSession();
    bool speedCurveSessionActive() const { return m_speedCurveActive; }
    QVariantList speedCurvePoints() const;
    Q_INVOKABLE void setSpeedCurvePoints(const QVariantList &points);
    int speedCurveRevision() const { return m_speedCurveRevision; }
    QSize speedCurveFrameSize() const { return m_speedCurvePlayer.frameSize(); }
    double speedCurveSourceStart() const;
    double speedCurveMediaDuration() const;
    double speedCurveSourceDuration() const;
    double speedCurveRetimedDuration() const;
    double speedCurvePosition() const;
    bool speedCurvePlaying() const { return m_speedCurvePlayer.isPlaying(); }
    QString speedCurveClipName() const { return m_speedCurveClip.name; }
    QString speedCurveClipPath() const { return m_speedCurveClip.path; }
    QString speedCurveFilmstripPath() const { return m_speedCurveClip.filmstripPath; }
    Q_INVOKABLE void playSpeedCurvePreview();
    Q_INVOKABLE void pauseSpeedCurvePreview();
    Q_INVOKABLE void seekSpeedCurvePreview(double seconds);
    double speedCurveSourcePosition() const;
    // Seeks by graph position rather than by retimed time, so clicking the strip lands on the
    // frame under the cursor.
    Q_INVOKABLE void seekSpeedCurvePreviewAtSource(double position);
    Q_INVOKABLE void applySpeedCurve();
    Q_INVOKABLE void clearClipSpeedCurve(int trackIndex, int clipIndex);

    Q_INVOKABLE void beginFadeCurveSession(int trackIndex, int clipIndex);
    Q_INVOKABLE void endFadeCurveSession();
    bool fadeCurveSessionActive() const { return m_fadeCurveActive; }
    QVariantList fadeCurvePoints() const;
    Q_INVOKABLE void setFadeCurvePoints(const QVariantList &points);
    QString fadeCurveClipName() const { return m_fadeCurveClipName; }
    Q_INVOKABLE void applyFadeCurve();
    Q_INVOKABLE void resetFadeCurvePreset(const QString &preset);
    Q_INVOKABLE void setSegmentationFrame(double seconds);
    Q_INVOKABLE void addSegmentationPoint(double x, double y, bool include);
    Q_INVOKABLE void removeSegmentationPoint(int index);
    Q_INVOKABLE void clearSegmentationPoints();
    Q_INVOKABLE void runSegmentationSession(const QString &outputMode);

    // Noise removal (DeepFilterNet3). previewDenoise renders a short window either side of the
    // model — original and cleaned — so the denoise window can A/B them before anything is
    // committed; applyDenoise renders the whole clip and adds it to the timeline.
    Q_INVOKABLE bool denoiseAvailable();
    Q_INVOKABLE void previewDenoise(int trackIndex, int clipIndex, double atSeconds);
    Q_INVOKABLE void applyDenoise(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancelDenoise();

    // Bakes the clip's face landmarks to a sidecar so the face warp effects have something to
    // follow. Runs off the GUI thread; the result lands on the clip through the undo stack.
    Q_INVOKABLE void detectFacesForClip(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancelFaceDetection();
    Q_INVOKABLE void clearFaceTrack(int trackIndex, int clipIndex);
    Q_INVOKABLE bool faceDetectionAvailable();
    // shapeKind/shapeId is a catalog id from builtinShapes(), which is not always a ShapeKind name:
    // "circle" and "ellipse" are the same kind with different default aspects.
    Q_INVOKABLE void addShapeClip(const QString &shapeKind, double atSeconds);
    Q_INVOKABLE void addShapeClipAt(const QString &shapeId, int trackIndex, double atSeconds);
    Q_INVOKABLE void addStickerClip(const QString &stickerId, double atSeconds);
    Q_INVOKABLE QVariantList builtinStickers() const;
    Q_INVOKABLE QVariantList builtinStickerCategories() const;
    // The full emoji set behind the sticker packs; empty until the pack carrying the font is
    // installed.
    Q_INVOKABLE QVariantList emojiCatalog() const;
    Q_INVOKABLE QStringList emojiGroups() const;
    Q_INVOKABLE QString emojiFontFamily() const;
    Q_INVOKABLE void addEmojiClip(const QString &emoji, const QString &name, double atSeconds);
    Q_INVOKABLE QVariantList builtinShapes() const;
    Q_INVOKABLE QVariantList builtinShapeCategories() const;
    // SVG "d" string for the assets-panel thumbnail, on the 0..100 grid ShapePreview.qml uses.
    Q_INVOKABLE QString shapeSvgPath(const QString &shapeId) const;
    Q_INVOKABLE QVariantList previewClipsAtPlayhead() const;
    Q_INVOKABLE void beginPreviewDrag(const QString &undoText = {});
    Q_INVOKABLE void previewSetClipPosition(int trackIndex, int clipIndex, double xPixels, double yPixels);
    Q_INVOKABLE void previewSetClipSize(int trackIndex, int clipIndex, double widthPixels, double heightPixels);
    Q_INVOKABLE void previewSetClipRect(int trackIndex, int clipIndex, double xPixels, double yPixels,
                                        double widthPixels, double heightPixels);
    // Text resizes scale the glyphs along with the box, so the size rides with the rect.
    Q_INVOKABLE void previewSetTextRect(int trackIndex, int clipIndex, double xPixels, double yPixels,
                                        double widthPixels, double heightPixels, int pixelSize);
    Q_INVOKABLE void previewSetClipRotation(int trackIndex, int clipIndex, double degrees);
    Q_INVOKABLE void previewSetClipKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                            double atSeconds, double value);
    Q_INVOKABLE void previewSetEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                           const QString &key, double value);
    Q_INVOKABLE void previewSetClipSpeed(int trackIndex, int clipIndex, double speed);
    Q_INVOKABLE void previewSetClipFade(int trackIndex, int clipIndex, double fadeInSeconds, double fadeOutSeconds);
    Q_INVOKABLE void commitPreviewDrag();
    Q_INVOKABLE void cancelPreviewDrag();
    Q_INVOKABLE int projectWidth() const;
    Q_INVOKABLE int projectHeight() const;
    Q_INVOKABLE int projectFps() const;
    Q_INVOKABLE void setProjectResolution(int width, int height);
    Q_INVOKABLE void setProjectSetup(int width, int height, int fps);
    Q_INVOKABLE void applyCanvasCrop(double x, double y, double width, double height);
    bool canvasCropMode() const { return m_canvasCropMode; }
    void setCanvasCropMode(bool active);
    bool maskEditMode() const { return m_maskEditMode; }
    void setMaskEditMode(bool active);
    int selectedMaskTrack() const { return m_selectedMaskTrack; }
    int selectedMaskIndex() const { return m_selectedMaskIndex; }
    Q_INVOKABLE void selectMask(int trackIndex, int maskIndex);
    Q_INVOKABLE void clearMaskSelection() { selectMask(-1, -1); }
    // The selected cutout's fields, or an empty map when nothing is selected.
    Q_INVOKABLE QVariantMap selectedMaskData() const;
    QString maskViewClipId() const { return m_maskViewClipId; }
    void setMaskViewClipId(const QString &id);
    // Turns mask view on for the selected clip, or off if it is already showing it.
    Q_INVOKABLE void toggleMaskViewForSelectedClip();
    Q_INVOKABLE void setBackground(const QVariantMap &background);
    Q_INVOKABLE bool timelineHasVisualClips() const;
    Q_INVOKABLE bool shouldConfigureProjectForAsset(int assetIndex) const;
    Q_INVOKABLE QVariantMap suggestedProjectSetupForAsset(int assetIndex) const;
    bool projectLayoutChosen() const { return m_projectLayoutChosen; }
    Q_INVOKABLE void markProjectLayoutChosen();
    Q_INVOKABLE void selectClip(int trackIndex, int clipIndex);
    Q_INVOKABLE void addToSelection(int trackIndex, int clipIndex);
    Q_INVOKABLE void setSelection(const QVariantList &pairs);
    Q_INVOKABLE void selectAllClips();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QVariantMap clipAt(int trackIndex, int clipIndex) const;
    Q_INVOKABLE QVariantMap activeVideoClipAtPlayhead() const;
    Q_INVOKABLE QVariantMap activeAudioClipAtPlayhead() const;
    Q_INVOKABLE double sourceTimeAtPlayhead() const;
    Q_INVOKABLE double sourceTimeForClip(const QVariantMap &clip) const;
    Q_INVOKABLE void deleteSelectedClip();
    Q_INVOKABLE void duplicateSelectedClip();
    Q_INVOKABLE void moveClip(int trackIndex, int clipIndex, double newStart);
    Q_INVOKABLE void moveClipToTrack(int trackIndex, int clipIndex, int newTrackIndex, double newStart);
    Q_INVOKABLE void alignSelectedClipLeft();
    Q_INVOKABLE void alignSelectedClipRight();
    Q_INVOKABLE void splitSelectedClipLeft();
    Q_INVOKABLE void splitSelectedClipRight();
    Q_INVOKABLE void splitAtPlayhead();
    Q_INVOKABLE void splitClipAt(int trackIndex, int clipIndex, double seconds);
    Q_INVOKABLE void splitClipLeftAt(int trackIndex, int clipIndex, double seconds);
    Q_INVOKABLE void splitClipRightAt(int trackIndex, int clipIndex, double seconds);
    Q_INVOKABLE void trimClipLeft(int trackIndex, int clipIndex, double newStart);
    Q_INVOKABLE void trimClipRight(int trackIndex, int clipIndex, double newEnd);
    Q_INVOKABLE void setClipTrim(int trackIndex, int clipIndex, double inPoint, double outPoint);
    Q_INVOKABLE void setClipStart(int trackIndex, int clipIndex, double start);
    Q_INVOKABLE void setClipDuration(int trackIndex, int clipIndex, double duration);
    Q_INVOKABLE void setClipTextContent(int trackIndex, int clipIndex, const QString &text);
    // Display label on the timeline / inspector. Does not rename the source file.
    Q_INVOKABLE void setClipName(int trackIndex, int clipIndex, const QString &name);
    // Live text edits (preview drag) keep the canvas and properties panel in sync
    // while typing; commitTextEdit trims and pushes undo.
    Q_INVOKABLE void previewSetClipTextContent(int trackIndex, int clipIndex, const QString &text);
    Q_INVOKABLE void commitTextEdit(int trackIndex, int clipIndex, const QString &text);
    // In-place text editing on the preview: hide the clip's baked raster while the
    // QML inline editor is shown, then restore it. Commit via commitTextEdit.
    Q_INVOKABLE void beginTextEdit(int trackIndex, int clipIndex);
    Q_INVOKABLE void endTextEdit();
    bool inlineTextEditing() const { return m_inlineTextEditing; }
    Q_INVOKABLE void setSubtitleCues(int trackIndex, int clipIndex, const QVariantList &cues);
    Q_INVOKABLE void previewSetSubtitleCues(int trackIndex, int clipIndex, const QVariantList &cues);
    Q_INVOKABLE double subtitleLocalPlayheadSeconds(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void upsertSubtitleCueAtPlayhead(int trackIndex, int clipIndex, const QString &text);
    Q_INVOKABLE void seekToSubtitleCue(int trackIndex, int clipIndex, int cueIndex);
    Q_INVOKABLE void setTextStyle(int trackIndex, int clipIndex, const QVariantMap &style);
    Q_INVOKABLE void applyTextPreset(int trackIndex, int clipIndex, const QString &presetId);
    Q_INVOKABLE QVariantList textPresets() const;
    Q_INVOKABLE QVariantList fontCatalog() const;
    Q_INVOKABLE QVariantList fontCategories() const;
    Q_INVOKABLE void setClipBlendMode(int trackIndex, int clipIndex, const QString &mode);
    Q_INVOKABLE void setClipSpeed(int trackIndex, int clipIndex, double speed);
    Q_INVOKABLE void setClipReverse(int trackIndex, int clipIndex, bool reverse);
    // Turns reverse on for a video clip and renders the proxy that makes it play back smoothly.
    // Reverse itself applies immediately; cancelling the render leaves the clip reversed on the
    // slow live-decode path, which is what clipHasReverseProxy reports.
    // Entry point for the Reverse control. Clips a proxy would do nothing for (audio, or one
    // already covered by a render) reverse straight away; anything else emits
    // reverseConfirmRequested so the dialog can ask before starting a render.
    Q_INVOKABLE void requestClipReverse(int trackIndex, int clipIndex);
    Q_INVOKABLE void applyClipReverse(int trackIndex, int clipIndex);
    Q_INVOKABLE void cancelReverseRender();
    Q_INVOKABLE bool clipHasReverseProxy(int trackIndex, int clipIndex) const;
    Q_INVOKABLE void setClipFlip(int trackIndex, int clipIndex, bool flipH, bool flipV);
    Q_INVOKABLE void setClipRotationSnap(int trackIndex, int clipIndex, double degrees);
    Q_INVOKABLE bool canMergeSelection() const;
    Q_INVOKABLE void mergeSelectedClips();
    Q_INVOKABLE bool canSeparateAudioSelection() const;
    Q_INVOKABLE void separateAudioFromSelection();
    Q_INVOKABLE bool canUnlinkSelection() const;
    Q_INVOKABLE void unlinkSelectedClips();
    // Cutouts on a track's mask lane. Each is a timed bar that masks whichever of that track's
    // clips it overlaps; lane order decides the composite, low lane folding in first.
    Q_INVOKABLE QVariantList trackMasks(int trackIndex) const;
    // Rows the lane needs to draw, or 0 when the track has no cutouts and shows no lane at all.
    Q_INVOKABLE int trackMaskLaneCount(int trackIndex) const;
    Q_INVOKABLE void setTrackMaskAt(int trackIndex, int maskIndex, const QVariantMap &mask);
    Q_INVOKABLE void previewSetTrackMaskAt(int trackIndex, int maskIndex, const QVariantMap &mask);
    // Returns the new cutout's index, or -1 when the track cannot take one.
    Q_INVOKABLE int addTrackMask(int trackIndex, const QString &shape);
    // Adds an image or video cutout. `source` may be a local path or a file:// URL, so the same
    // call serves the asset library and an OS file drop.
    Q_INVOKABLE int addTrackMaskMedia(int trackIndex, const QString &source);
    Q_INVOKABLE int addTrackMaskFromAsset(int trackIndex, int assetIndex);
    Q_INVOKABLE void removeTrackMask(int trackIndex, int maskIndex);
    Q_INVOKABLE void moveTrackMask(int trackIndex, int maskIndex, double startSeconds, int lane);
    Q_INVOKABLE void previewMoveTrackMask(int trackIndex, int maskIndex, double startSeconds,
                                          int lane);
    Q_INVOKABLE void previewTrimTrackMask(int trackIndex, int maskIndex, double startSeconds,
                                          double durationSeconds);
    // Freeform vertex editing. Insert places a straight vertex at `pointIndex`, so passing the
    // index of the edge's far end splits that edge.
    Q_INVOKABLE void insertMaskPoint(int trackIndex, int maskIndex, int pointIndex,
                                     double x, double y);
    Q_INVOKABLE void removeMaskPoint(int trackIndex, int maskIndex, int pointIndex);
    // Partial patch: only the keys present are applied, like setTextStyle.
    Q_INVOKABLE void setShapeStyle(int trackIndex, int clipIndex, const QVariantMap &style);
    Q_INVOKABLE void setClipFade(int trackIndex, int clipIndex, double fadeInSeconds, double fadeOutSeconds);
    Q_INVOKABLE void setClipFadeCurve(int trackIndex, int clipIndex, const QString &curve);
    // which: "animIn" | "animOut". Partial patch: kind / duration / curve (or legacy ease).
    Q_INVOKABLE void setClipAnimation(int trackIndex, int clipIndex, const QString &which,
                                      const QVariantMap &patch);
    Q_INVOKABLE void addTransition(int trackIndex, int clipIndex, const QString &kind, double durationSeconds);
    Q_INVOKABLE void removeTransition(int trackIndex, const QString &transitionId);
    Q_INVOKABLE void setTransitionDuration(int trackIndex, const QString &transitionId, double durationSeconds);
    Q_INVOKABLE void setTransitionKind(int trackIndex, const QString &transitionId, const QString &kind);
    Q_INVOKABLE void setTransitionParam(int trackIndex, const QString &transitionId, const QString &key,
                                        double value);
    Q_INVOKABLE void previewSetTransitionParam(int trackIndex, const QString &transitionId,
                                               const QString &key, double value);
    Q_INVOKABLE QVariantMap transitionBetweenClips(int trackIndex, int clipIndex) const;
    Q_INVOKABLE QVariantList transitionKinds() const;
    Q_INVOKABLE QVariantList transitionCategories() const;
    Q_INVOKABLE void selectTransition(int trackIndex, int leftClipIndex);
    Q_INVOKABLE void clearTransitionSelection();
    Q_INVOKABLE void setClipKeyframe(int trackIndex, int clipIndex, const QString &prop, double atSeconds,
                                     double value);
    Q_INVOKABLE void removeClipKeyframe(int trackIndex, int clipIndex, const QString &prop, double atSeconds);
    Q_INVOKABLE void previewMoveClipKeyframe(int trackIndex, int clipIndex, const QString &prop,
                                             double fromSeconds, double toSeconds, double value);
    Q_INVOKABLE QVariantList clipKeyframes(int trackIndex, int clipIndex, const QString &prop) const;
    // Every property of the clip that carries an animation, in strip order.
    Q_INVOKABLE QStringList clipAnimatedProperties(int trackIndex, int clipIndex) const;
    // Whether a property's keyframes drive it. Switched off, the keys are kept but the property
    // holds its first key's value — the inspector row's label is what toggles this.
    Q_INVOKABLE bool clipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                  const QString &prop) const;
    Q_INVOKABLE void setClipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                     const QString &prop, bool enabled);
    Q_INVOKABLE void toggleClipPropertyKeyframesEnabled(int trackIndex, int clipIndex,
                                                        const QString &prop);
    Q_INVOKABLE double propertyValueAt(int trackIndex, int clipIndex, const QString &prop,
                                       double atSeconds, double fallback) const;
    // What the property evaluates to with no keyframes at all — the same implicit defaults
    // the compositor uses, so an unkeyed curve is drawn where the clip actually sits.
    Q_INVOKABLE double propertyBaseValue(int trackIndex, int clipIndex, const QString &prop,
                                         double fallback = 0.0) const;
    // Tangent handles for the curve editor. dx/dy arrive in seconds / property units, relative
    // to the key. `preview` variants coalesce a drag into one undo entry via begin/commitPreviewDrag.
    Q_INVOKABLE void setKeyframeTangents(int trackIndex, int clipIndex, const QString &prop,
                                         double atSeconds, double inDx, double inDy, double outDx,
                                         double outDy, bool corner);
    Q_INVOKABLE void previewSetKeyframeTangents(int trackIndex, int clipIndex, const QString &prop,
                                                double atSeconds, double inDx, double inDy,
                                                double outDx, double outDy, bool corner);
    Q_INVOKABLE void setKeyframeHold(int trackIndex, int clipIndex, const QString &prop,
                                     double atSeconds, bool hold);
    Q_INVOKABLE void setKeyframeInterpolation(int trackIndex, int clipIndex, const QString &prop,
                                              const QString &mode);
    Q_INVOKABLE void resetClipTransform(int trackIndex, int clipIndex);
    Q_INVOKABLE QVariantList effectCatalog() const;
    Q_INVOKABLE QVariantList effectCategories() const;
    Q_INVOKABLE QVariantList effectTemplateCatalog() const;
    Q_INVOKABLE QVariantList effectTemplateCategories() const;
    Q_INVOKABLE void addEffect(int trackIndex, int clipIndex, const QString &effectId);
    Q_INVOKABLE void applyEffectTemplate(int trackIndex, int clipIndex, const QString &templateId);
    Q_INVOKABLE void removeEffect(int trackIndex, int clipIndex, int effectIndex);
    Q_INVOKABLE void setEffectEnabled(int trackIndex, int clipIndex, int effectIndex, bool enabled);
    Q_INVOKABLE void moveEffect(int trackIndex, int clipIndex, int fromIndex, int toIndex);
    Q_INVOKABLE void setEffectParam(int trackIndex, int clipIndex, int effectIndex, const QString &key,
                                    double value);
    Q_INVOKABLE void setEffectColorParam(int trackIndex, int clipIndex, int effectIndex,
                                         const QString &key, const QString &value);
    Q_INVOKABLE QVariantList audioEffectCatalog() const;
    Q_INVOKABLE QVariantList audioEffectCategories() const;
    Q_INVOKABLE void addAudioEffect(int trackIndex, int clipIndex, const QString &effectId);
    Q_INVOKABLE void removeAudioEffect(int trackIndex, int clipIndex, int effectIndex);
    Q_INVOKABLE void setAudioEffectEnabled(int trackIndex, int clipIndex, int effectIndex, bool enabled);
    Q_INVOKABLE void moveAudioEffect(int trackIndex, int clipIndex, int fromIndex, int toIndex);
    Q_INVOKABLE void setAudioEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                         const QString &key, double value);
    Q_INVOKABLE void previewSetAudioEffectParam(int trackIndex, int clipIndex, int effectIndex,
                                                const QString &key, double value);
    Q_INVOKABLE void setTrackMuted(int trackIndex, bool muted);
    Q_INVOKABLE void setTrackHidden(int trackIndex, bool hidden);
    Q_INVOKABLE bool trackMuted(int trackIndex) const;
    Q_INVOKABLE bool trackHidden(int trackIndex) const;
    Q_INVOKABLE void setTrackShowWaveform(int trackIndex, bool show);
    Q_INVOKABLE bool trackShowWaveform(int trackIndex) const;
    // Per-track row height multiplier (DAW-style lane resize). Clamped to
    // trackHeightScaleMin()..trackHeightScaleMax().
    Q_INVOKABLE void setTrackHeightScale(int trackIndex, double scale);
    Q_INVOKABLE double trackHeightScale(int trackIndex) const;
    Q_INVOKABLE void nudgeTrackHeightScale(int trackIndex, int steps);
    Q_INVOKABLE double trackHeightScaleMin() const { return 0.6; }
    Q_INVOKABLE double trackHeightScaleMax() const { return 4.0; }
    Q_INVOKABLE void moveTrack(int fromIndex, int toIndex);
    Q_INVOKABLE void addTrack(const QString &type);
    Q_INVOKABLE void removeTrack(int trackIndex);
    Q_INVOKABLE void addBookmark(double seconds, const QString &label);
    Q_INVOKABLE void removeBookmark(int index);
    Q_INVOKABLE void updateBookmark(int index, double seconds, const QString &label);
    Q_INVOKABLE void goToBookmark(int index);
    // Seek to the next/previous bookmark by time (wraps). No-op when empty.
    Q_INVOKABLE void goToNextBookmark();
    Q_INVOKABLE void goToPreviousBookmark();
    // Add at the playhead, or remove the nearest bookmark when one already sits
    // within the snap threshold — same key for mark and unmark.
    Q_INVOKABLE void toggleBookmarkAtPlayhead();
    Q_INVOKABLE void removeBookmarkNearPlayhead();
    Q_INVOKABLE void markWorkAreaIn();
    Q_INVOKABLE void markWorkAreaOut();
    Q_INVOKABLE void goToWorkAreaIn();
    Q_INVOKABLE void goToWorkAreaOut();
    Q_INVOKABLE void clearWorkArea();
    Q_INVOKABLE void toggleLoopWorkArea();
    Q_INVOKABLE void freezeFrameAtPlayhead();
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void cutSelection();
    Q_INVOKABLE void pasteAtPlayhead();
    Q_INVOKABLE void nudgeSelection(double deltaSeconds);
    Q_INVOKABLE bool selectionContains(int trackIndex, int clipIndex) const;
    // Premiere-style trim pointer. side: -1=start, 0=off, 1=end.
    // heightPx scales the cursor to the hovered clip/track height.
    Q_INVOKABLE void setTimelineTrimCursor(int side, int heightPx = 0);
    Q_INVOKABLE QString shortcutFor(const QString &actionId) const;
    // Returns an empty string on success, or the label of the action already bound to
    // `keys` when the binding is refused. Qt resolves an ambiguous application
    // shortcut by firing *neither* action, so a silent double-binding would break
    // both with no indication anywhere.
    Q_INVOKABLE QString setShortcut(const QString &actionId, const QString &keys);
    // Restores every default binding. Backspace clears a binding and persists the
    // empty string, so without this there was no route back from having cleared one.
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE void triggerAction(const QString &actionId);
    // Per-tab favorites in the assets panel (effects, sounds, shapes, stickers, transitions, templates).
    Q_INVOKABLE bool isAssetFavorite(const QString &tabId, const QString &itemId) const;
    Q_INVOKABLE void toggleAssetFavorite(const QString &tabId, const QString &itemId);
    Q_INVOKABLE void togglePlayback();
    // Frame-accurate transport. Stepping quantizes to the project's frame grid first: the playhead
    // can sit between frames after a scrub, and adding a frame duration to that would carry the
    // off-grid offset forever.
    Q_INVOKABLE void stepFrames(int frames);
    Q_INVOKABLE void jumpSeconds(double seconds);
    // The transport's jump buttons pick their amount from the modifiers held at click time, and
    // AbstractButton::clicked carries none.
    Q_INVOKABLE int keyboardModifiers() const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE double snapTime(double seconds) const;
    Q_INVOKABLE QVariantList waveformPeaks(const QString &path) const;
    // Whole-file peaks sliced to a source window, for a dialog whose x axis is a clip's trimmed
    // range rather than the whole file. Shares the dense cache and the waveformReady signal with
    // waveformPeaks() — unlike waveformPeaksRange(), which is block-backed for the timeline.
    Q_INVOKABLE QVariantList waveformPeaksForSourceRange(const QString &path, double startSeconds,
                                                         double durSeconds) const;
    // Peaks for just the source window [startSeconds, startSeconds + durSeconds), reduced to
    // `buckets` values, so a clip only ever asks for as many peaks as it has visible pixels.
    Q_INVOKABLE QVariantList waveformPeaksRange(const QString &path, double startSeconds,
                                                double durSeconds, int buckets) const;
    // title / author / description / createdAt / modifiedAt, for the properties dialog.
    QVariantMap projectMetadata() const;
    Q_INVOKABLE void setProjectMetadata(const QString &title, const QString &author,
                                        const QString &description);
    bool packaging() const { return m_packaging; }
    double packageProgress() const { return m_packageProgress; }
    Q_INVOKABLE QVariantList subtitleWaveformPeaks(double startSeconds, double durSeconds,
                                                   int sampleCount = 240) const;
    // Beat / onset detection over the mixed timeline audio in [startSeconds, +durSeconds).
    // Explicitly triggered from the keyframe strip: it renders the mix, so it must never
    // fire off a mere selection change. Result lands in `beatAnalysis`.
    Q_INVOKABLE void analyzeBeats(double startSeconds, double durSeconds);
    Q_INVOKABLE void clearBeatAnalysis();
    QVariantMap beatAnalysis() const { return m_beatAnalysis; }
    bool beatAnalysisRunning() const { return m_beatAnalysisRunning; }
    bool beatGridVisible() const { return m_beatGridVisible; }
    void setBeatGridVisible(bool visible);
    bool onsetsVisible() const { return m_onsetsVisible; }
    void setOnsetsVisible(bool visible);
    // Writes a .drift bundle keeping each asset's current storage mode, so a referencing project
    // stays instant to save and a packaged one stays self-contained.
    Q_INVOKABLE void saveProject(const QUrl &url);
    // Same container, every source asset embedded. Runs off the GUI thread — it copies the media.
    Q_INVOKABLE void packageProject(const QUrl &url);
    Q_INVOKABLE void cancelPackage();
    Q_INVOKABLE void loadProject(const QUrl &url);
    Q_INVOKABLE void newProject();
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
    Q_INVOKABLE QVariantList exportPresets() const; // legacy scale ids/labels
    Q_INVOKABLE QVariantList exportScaleOptions() const;
    // Frame rate choices; the "project" entry is labelled with the current project fps.
    Q_INVOKABLE QVariantList exportFrameRateOptions() const;
    Q_INVOKABLE QVariantList exportVideoCodecs() const;
    Q_INVOKABLE QVariantList exportAudioCodecs() const;
    Q_INVOKABLE bool exportGifAvailable() const;
    Q_INVOKABLE QVariantMap exportDefaultSettings() const;
    // Last dialog choices (scale + encode). Empty until the user has exported once.
    Q_INVOKABLE QVariantMap lastExportSettings() const;
    // Directory of the last successful save-picker choice; empty if never set or gone.
    Q_INVOKABLE QString lastExportFolder() const;
    Q_INVOKABLE QString exportPreferredContainer(const QString &videoCodecId,
                                                const QString &audioCodecId) const;
    Q_INVOKABLE QString exportPreferredAudioOnlyContainer(const QString &audioCodecId) const;
    Q_INVOKABLE QStringList exportSaveFilters(const QString &container, bool audioOnly = false) const;
    Q_INVOKABLE QString exportDefaultSuffix(const QString &container, bool audioOnly = false) const;
    Q_INVOKABLE void exportProject(const QUrl &outputUrl);
    Q_INVOKABLE void exportWithPreset(const QUrl &outputUrl, const QString &presetId);
    Q_INVOKABLE void exportWithSettings(const QUrl &outputUrl, const QVariantMap &settings);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE QUrl fileUrl(const QString &path) const;
    Q_INVOKABLE QString imageUrl(const QString &path) const;
    // Same as imageUrl but requests a single frame of a filmstrip strip (see DriftImageProvider).
    Q_INVOKABLE QString filmstripFrameUrl(const QString &path, int frame, int count) const;
    // Sharp on-demand frame for one filmstrip tile — see FilmstripTileCache. Empty until the
    // decode lands, at which point filmstripTileReady() fires for that source.
    Q_INVOKABLE QString filmstripTileUrl(const QString &path, int level, double index) const;

signals:
    // A text clip was added with no text; the preview should open its inline
    // editor so the user can type straight onto the canvas.
    void inlineTextEditRequested(int trackIndex, int clipIndex);
    void inlineTextEditingChanged();
    void tracksChanged();
    void playheadSecondsChanged();
    void playingChanged();
    void audioOutputDevicesChanged();
    void audioOutputDeviceIdChanged();
    void snapEnabledChanged();
    void rippleEnabledChanged();
    void allowClipOverlapChanged();
    void darkModePreferenceChanged();
    void workspaceLayoutPreferenceChanged();
    void mediaGridModeChanged();
    void autoKeyEnabledChanged();
    void reopenLastProjectChanged();
    void mcpRunningChanged();
    void mcpErrorChanged();
    void uiLanguageChanged();
    void keyframeGraphVisibilityChanged();
    void subtitleEditingChanged();
    void selectedSubtitleCueChanged();
    void undoStackChanged();
    void exportInProgressChanged();
    void exportProgressChanged();
    void subtitleGeneratingChanged();
    void subtitleGenProgressChanged();
    void subtitleGenStatusChanged();
    void subtitleGenerationFinished(bool ok, const QString &message);
    void segmentingChanged();
    void segmentProgressChanged();
    void segmentStatusChanged();
    void reverseRenderingChanged();
    void reverseRenderProgressChanged();
    void reverseRenderStatusChanged();
    void reverseRenderFinished(bool ok, const QString &message);
    void reverseConfirmRequested(int trackIndex, int clipIndex, double seconds);
    void segmentationFinished(bool ok, const QString &message);
    void denoisingChanged();
    void denoiseProgressChanged();
    void denoiseStatusChanged();
    void denoisePreviewReady(const QString &originalPath, const QString &cleanPath);
    void denoiseFinished(bool ok, const QString &message);
    void segmentSessionChanged();
    void openSegmentationWindowRequested(int trackIndex, int clipIndex, double startSeconds,
                                         double durationSeconds);
    void speedCurveSessionChanged();
    void speedCurveChanged();
    void speedCurveFrameChanged();
    void speedCurvePositionChanged();
    void speedCurvePlayingChanged();
    void speedCurveApplied();
    void fadeCurveSessionChanged();
    void fadeCurveChanged();
    void fadeCurveApplied();
    void faceDetectingChanged();
    void faceDetectProgressChanged();
    void faceDetectStatusChanged();
    void faceDetectionFinished(bool ok, const QString &message);
    void selectionChanged();
    void editCapabilitiesChanged();
    void selectedClipDataChanged();
    void selectedTransitionDataChanged();
    void bookmarksChanged();
    void workAreaChanged();
    void loopWorkAreaEnabledChanged();
    void projectNameChanged();
    void projectMetadataChanged();
    void packagingChanged();
    void packageProgressChanged();
    void packageFinished(bool ok, const QString &message);
    // Addons the freshly opened project needs but that are not installed. Each entry is
    // id / name / version / kinds, for MissingAddonsDialog.
    void missingAddons(const QVariantList &addons);
    void lastMessageChanged();
    void draggingAssetIndexChanged();
    void exportFinished(bool success);
    void projectMutated();
    void waveformReady(const QString &path);
    // A block of the timeline waveform landed. Separate from waveformReady so the dialogs,
    // which want the whole file, don't re-fetch (and blank) on every block.
    void waveformRangeReady(const QString &path);
    void filmstripTileReady(const QString &path);
    void subtitleWaveformReady(double startSeconds, double durSeconds, int sampleCount);
    void beatAnalysisChanged();
    void guidesChanged();
    void shortcutsChanged();
    void assetFavoritesChanged();
    void canvasCropModeChanged();
    void maskEditModeChanged();
    void selectedMaskChanged();
    void maskViewClipIdChanged();
    void backgroundChanged();
    void dirtyChanged();
    void currentProjectPathChanged();
    void recoveryChanged();
    void recentProjectsChanged();
    void projectLayoutChosenChanged();
    // The document has been swapped wholesale (New Project, or opening another one). The
    // auxiliary windows edit one clip each, so they have nothing left to act on and close.
    void projectReset();
    void transformBlocked(const QString &reason);
    // Outcome of replaceAssetSource. `message` is a ready-to-show reason on failure and the new
    // media's name on success. `adjustedClips` counts clips whose source range no longer fitted
    // the replacement and was pulled back to it.
    void assetReplaceFinished(bool ok, const QString &message, int adjustedClips);
    void replacingAssetIdChanged();
    // File actions from the shortcut layer — QML owns dialogs and unsaved prompts.
    void newProjectRequested();
    void openRequested();
    void saveRequested();

protected:
    void pushProjectEdit(const drift::Project &before, const QString &text);
    void finishEdit(const QString &message);
    // Applies a finished replace probe as one undoable transaction, or reports why it cannot be.
    void finalizeAssetReplace(const QString &assetId, const drift::MediaAsset &filled, bool ok);
    // Moves every clip bound to `assetId` onto the replacement media. Returns how many had a
    // source range that no longer fitted and had to be pulled back to it.
    int rebindClipsToAsset(const QString &assetId, const drift::MediaAsset &asset);
    // Keeps the keyframe strip's index-addressed hidden series in sync after an effect is removed.
    void dropKeyframeGraphPropertiesForEffect(int removedIndex);
    // Same idea after a reorder: fx.N.* indices move with the effect.
    void remapKeyframeGraphPropertiesForEffectMove(int fromIndex, int toIndex);
    // Renumbers "<prefix>.<index>.<param>" entries in the keyframe-strip selection after a
    // reorder. Prefix is "fx" or "mask".
    void remapKeyframeGraphProperties(const QString &prefix, int fromIndex, int toIndex);
    // Publishes a finished beat analysis into m_beatAnalysis / m_beatSnapTargets.
    void applyBeatAnalysis(const AudioBeatAnalysis &analysis, double startSeconds, double durSeconds,
                           const QByteArray &fingerprint);
    void loadAssetFavorites();
    void saveAssetFavorites(const QString &tabId);
    void applyEffectTemplateInternal(int trackIndex, int clipIndex, const EffectTemplateEntry &entry,
                                   const QString &mattePath = {},
                                   drift::TimeUs matteSrcOffsetUs = 0);
    bool resolveTemplateApplyTarget(int *trackIndex, int *clipIndex) const;
    // Bounds-checked mutable clip lookup; null when either index is out of range.
    drift::Clip *clipRefAt(int trackIndex, int clipIndex);
    const drift::Clip *clipRefAt(int trackIndex, int clipIndex) const;
    drift::Track *trackRefAt(int trackIndex);
    const drift::Track *trackRefAt(int trackIndex) const;
    bool beatAnalysisReadyForClip(const drift::Clip &clip, const QString &sync) const;
    // Digest of everything the AudioMixer reads; a change means detected beats are stale.
    QByteArray audioLayoutFingerprint() const;
    // Single key lookup for the tangent editors; null when nothing sits at `atSeconds`.
    drift::Keyframe<double> *keyframeAt(int trackIndex, int clipIndex, const QString &prop,
                                        double atSeconds);
    static void applyTangents(drift::Keyframe<double> &key, double inDx, double inDy, double outDx,
                              double outDy, bool corner);
    // Recollects m_beatSnapTargets from whichever layers are currently visible.
    void rebuildBeatSnapTargets();
    // Beat onsets plus project bookmarks — anything clips should magnet to when snap is on.
    QList<drift::TimeUs> extraSnapTargets() const;
    void refreshSegmentationPreview();
    void runSegmentationSeed(int generation);
    void finalizeFaceDetection(const QString &clipId, const QString &trackPath,
                               drift::TimeUs srcOffsetUs);
    void finalizeSegmentation(const QString &clipId, const QString &mattePath,
                              drift::TimeUs matteSrcOffsetUs, const QString &outputMode);
    void finalizeGeneratedSubtitles(drift::TimeUs timelineStart, drift::TimeUs timelineDuration,
                                    const QList<drift::SubtitleCue> &cues);
    void finalizeDenoise(const QString &clipId, const QString &audioPath);
    // Shared body of the two denoise jobs: decodes [srcIn, srcIn + span) of `path` at the model's
    // rate, runs each channel through it, and writes the result. Runs on a worker thread.
    // `originalPathOut` is written only when non-empty, for the preview's A/B source.
    bool renderDenoisedAudio(const QString &path, drift::TimeUs srcIn, drift::TimeUs span,
                             const QString &outPath, const QString &originalPath,
                             double progressFrom, double progressTo, QString *errorOut);
    // Defaulted severity so the existing call sites, which report ordinary status,
    // stay unchanged; pass "error"/"warning" explicitly where a failure is reported.
    void setLastMessage(const QString &message,
                        const QString &severity = QStringLiteral("info"));
    drift::TimeUs playheadUs() const { return m_playheadUs; }
    void setPlayheadUs(drift::TimeUs us);

    // Stickers and emoji are both a PNG dropped on an image track at the playhead.
    void addImageOverlayClip(const QString &path, const QString &name, const QString &emoji,
                             double atSeconds, const QString &undoText);

    QVariantMap clipToMap(const drift::Clip &clip) const;
    int assetIndexForClip(const drift::Clip &clip) const;
    drift::TimeUs clipDurationForAssetIndex(int assetIndex) const;
    drift::TimeUs sourceDurationForClip(const drift::Clip &clip) const;
    void startReverseRender(const QString &sourcePath, drift::TimeUs coverInUs,
                            drift::TimeUs coverOutUs);
    // Cached dense peaks for `path`, or nullptr while the off-thread decode is still running
    // (waveformReady is emitted when it lands).
    const MediaWaveform::Dense *densePeaksFor(const QString &path) const;
    void applyRippleShift(drift::Track &track, int fromClipIndex, drift::TimeUs delta);
    void restoreFilmstripsAfterLoad();
    void normalizeSelection();
    bool isValidClipIndex(int trackIndex, int clipIndex) const;

    // Drops everything scoped to the outgoing project — clipboard, timeline-keyed caches, the
    // auxiliary-window sessions. Called by both newProject and applyProjectJson, before the
    // document is replaced, so the two paths cannot drift apart again.
    void resetSessionState();

    QByteArray serializeProjectJson() const;
    bool applyProjectJson(const QByteArray &data, QString *error);
    // Shared by saveProject and packageProject. `embedSource` forces every source asset into the
    // bundle; otherwise each keeps whatever mode it had, tracked in m_embeddedSources. GUI thread
    // only — packageProject builds the request here and hands the finished copy to its worker.
    drift::bundle::WriteRequest buildWriteRequest(bool embedSource) const;
    void rememberEmbeddedSources(const QList<drift::bundle::MediaEntry> &media);
    // Persist the save-picker folder and encode/scale choices for the next Export dialog.
    // Empty `outputPath` updates settings only and leaves lastExportFolder unchanged.
    void rememberExportChoice(const QString &outputPath, const QVariantMap &settings);
    // Repoint every path field the extraction moved. Clips duplicate their asset's path, so this
    // matches on the value rather than walking by id.
    void remapProjectPaths(const QHash<QString, QString> &remap);
    // Drops <AppData>/projects/<id> directories no project in the recents list still refers to.
    void sweepExtractionDirs();
    // Effects and transitions render as no-ops when their package is absent, which is silent and
    // looks like the project is simply wrong. Called after a load to say so instead.
    void reportMissingCatalogEntries();
    void reportMissingAddons(const QList<drift::bundle::AddonRef> &addons);
    void setDirty(bool dirty);
    void setCurrentProjectPath(const QString &path);
    void addRecentProject(const QString &path);
    void writeRecoveryFile();
    void deleteRecoveryFile();
    void detectRecoveryFile();
    static QString recoveryFilePath();

    AssetLibrary *m_assetLibrary = nullptr;
    TimelineModel m_timelineModel;
    ClipListModel m_clipListModel;
    // m_project must outlive m_playback: the playback engine's compositor thread
    // holds a bare pointer to it and may still be mid-composite at teardown.
    // Members are destroyed in reverse declaration order, so the project is
    // declared first and torn down last.
    drift::Project m_project;
    PlaybackEngine m_playback;
    // Only for its audioOutputsChanged signal — the sinks resolve devices themselves.
    QMediaDevices m_mediaDevices;
    QString m_audioOutputDeviceId;
    // The audio error already on screen, so a device that fails repeatedly toasts once.
    QString m_lastAudioError;
    QUndoStack m_undoStack;
    drift::TimeUs m_playheadUs = 0;
    bool m_playing = false;
    bool m_snapEnabled = true;
    bool m_rippleEnabled = false;
    bool m_allowClipOverlap = false;
    bool m_loopWorkAreaEnabled = false;
    bool m_darkModeOverridden = false;
    bool m_darkModePreferred = true;
    bool m_workspaceLayoutOverridden = false;
    QString m_workspaceLayoutPreferred = QStringLiteral("landscape");
    bool m_mediaGridMode = true;
    bool m_autoKeyEnabled = false;
    bool m_reopenLastProject = false;
    QString m_uiLanguage;
    QStringList m_keyframeGraphHiddenProperties;
    bool m_subtitleEditing = false;
    int m_selectedSubtitleCue = -1;
    bool m_exportInProgress = false;
    double m_exportProgress = 0.0;
    QAtomicInt m_exportCancel = 0;
    bool m_subtitleGenerating = false;
    QString m_replacingAssetId;
    double m_subtitleGenProgress = 0.0;
    QString m_subtitleGenStatus;
    QAtomicInt m_subtitleGenCancel = 0;
    bool m_segmenting = false;
    double m_segmentProgress = 0.0;
    QString m_segmentStatus;
    QAtomicInt m_segmentCancel = 0;
    // Speed-curve session: the clip being retimed, the candidate ramp, and the player auditioning it.
    ClipPreviewPlayer m_speedCurvePlayer;
    drift::Clip m_speedCurveClip;
    drift::SpeedCurve m_speedCurve;
    int m_speedCurveTrack = -1;
    int m_speedCurveClipIndex = -1;
    int m_speedCurveRevision = 0;
    bool m_speedCurveActive = false;

    bool m_fadeCurveActive = false;
    int m_fadeCurveTrack = -1;
    int m_fadeCurveClipIndex = -1;
    QString m_fadeCurveClipId;
    QString m_fadeCurveClipName;
    drift::FadeShape m_fadeShape;
    drift::FadeCurve m_fadeCurveBefore = drift::FadeCurve::Smooth;
    drift::FadeShape m_fadeShapeBefore;
    bool m_fadeCurveApplied = false;

    bool m_reverseRendering = false;
    double m_reverseProgress = 0.0;
    QString m_reverseStatus;
    QAtomicInt m_reverseCancel = 0;

    bool m_denoising = false;
    double m_denoiseProgress = 0.0;
    QString m_denoiseStatus;
    QAtomicInt m_denoiseCancel = 0;
    // The A/B snippets currently on offer. Dragging the preview window along a clip re-renders
    // repeatedly, so each pair is deleted as the next supersedes it.
    QString m_denoisePreviewClean;
    QString m_denoisePreviewOriginal;
    // Source paths that were embedded when this project was last read or written, so a plain Save
    // keeps a packaged project packaged instead of quietly making it depend on the cache dir.
    QSet<QString> m_embeddedSources;
    // Handed to applyProjectJson by loadProject, applied alongside the other load-time path
    // migrations and cleared there.
    QHash<QString, QString> m_pendingPathRemap;
    bool m_packaging = false;
    double m_packageProgress = 0.0;
    QAtomicInt m_packageCancel = 0;
    bool m_faceDetecting = false;
    double m_faceDetectProgress = 0.0;
    QString m_faceDetectStatus;
    QAtomicInt m_faceDetectCancel = 0;
    bool m_segSessionActive = false;
    bool m_segForTemplate = false;
    bool m_segEncoding = false;
    int m_segTrack = -1;
    int m_segClip = -1;
    double m_segSeconds = 0.0;
    int m_segRevision = 0;
    int m_segGeneration = 0; // bumped per encode request; stale results are dropped
    int m_segSeedGeneration = 0; // bumped per seed preview; stale masks are dropped
    bool m_segSeedRunning = false;
    int m_loadGeneration = 0; // bumped per loadProject; stale extracts are dropped
    QImage m_segFrame;
    drift::Sam2Embedding m_segEmbedding;
    QVariantList m_segPoints;
    int m_selectedTrack = -1;
    int m_selectedClip = -1;
    int m_selectedTransitionTrack = -1;
    int m_selectedTransitionLeftClip = -1;
    QList<QPair<int, int>> m_selection;
    int m_timelineTrimCursorSide = 0;
    int m_timelineTrimCursorHeight = 0;
    bool m_guidesEnabled = false;
    bool m_canvasCropMode = false;
    bool m_maskEditMode = false;
    int m_selectedMaskTrack = -1;
    int m_selectedMaskIndex = -1;
    QString m_maskViewClipId;
    QString m_guideType = QStringLiteral("thirds");
    QHash<QString, QString> m_shortcuts;
    QHash<QString, QSet<QString>> m_assetFavorites;
    int m_draggingAssetIndex = -1;
    QString m_lastMessage;
    QString m_lastMessageSeverity = QStringLiteral("info");
    bool m_inlineTextEditing = false;
    bool m_previewDragActive = false;
    drift::Project m_previewDragBefore;
    QString m_previewDragText;
    void emitPreviewFrame();
    void syncTextOverlaySkip();
    struct ClipboardItem
    {
        drift::Clip clip;
        drift::TrackType trackType = drift::TrackType::Video;
    };
    QList<ClipboardItem> m_clipboard;

    // Whole-file peaks, for the dialogs that show a complete clip at once (Denoise, Speed
    // Curve). The timeline uses m_waveformBlocks instead — see waveformPeaksRange.
    mutable QHash<QString, MediaWaveform::Dense> m_waveformCache;
    mutable QSet<QString> m_waveformPending;

    mutable WaveformBlockCache m_waveformBlocks;

    mutable FilmstripTileCache m_filmstripTiles;

    // Subtitle-lane voice waveform: the mixed audio underneath a subtitle clip's
    // span, voice band-passed. Keyed by "<startUs>:<durUs>"; invalidated on edits.
    mutable QHash<QString, QVariantList> m_subtitleWaveformCache;
    mutable QSet<QString> m_subtitleWaveformPending;

    // Beat detection. Only one range is live at a time, so this needs no cache — just a
    // generation counter so a job whose range the user has since left is dropped on arrival.
    AudioBeatAnalysis m_beatAnalysisRaw; // kept so snap targets can be rebuilt per layer
    QVariantMap m_beatAnalysis;          // the same result, shaped for QML
    bool m_beatAnalysisRunning = false;
    quint64 m_beatAnalysisGeneration = 0;
    QByteArray m_beatAudioFingerprint;
    bool m_beatGridVisible = false;
    bool m_onsetsVisible = false;
    struct PendingEffectTemplate
    {
        int trackIndex = -1;
        int clipIndex = -1;
        QString templateId;
        bool valid() const { return trackIndex >= 0 && clipIndex >= 0 && !templateId.isEmpty(); }
    };
    std::optional<PendingEffectTemplate> m_pendingEffectTemplate;
    // Beats and onsets as snap targets, thinned so a dense onset list cannot make every
    // position on the timeline snap to something. Only the visible layers contribute.
    QList<drift::TimeUs> m_beatSnapTargets;

    // Save state / autosave / crash recovery.
    QString m_currentProjectPath;
    bool m_dirty = false;
    QTimer *m_autosaveTimer = nullptr;
    bool m_recoveryAvailable = false;
    QVariantMap m_recoveryInfo;
    // Launch layout picker / first-clip setup completed for this empty project.
    bool m_projectLayoutChosen = false;

    std::unique_ptr<drift::mcp::McpServer> m_mcp;
    bool m_mcpUndoSuspended = false;
    int m_mcpBatchDepth = 0;
    drift::Project m_mcpBatchBefore;
    int m_mcpEditRevision = 0;
    mutable QHash<QString, QPair<int, int>> m_mcpClipIndex;
    mutable int m_mcpClipIndexRevision = -1;
    void rebuildMcpClipIndexIfNeeded() const;

    void setProjectLayoutChosen(bool chosen);

    static constexpr int kMaxUndoSteps = 50;
    static constexpr int kAutosaveIntervalMs = 15000;
    static constexpr int kMaxRecentProjects = 10;
};
