#pragma once

#include "core/Project.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>

class AppController;

namespace drift::mcp {
class McpServer;
}

// Session-only localhost MCP for agents. Off at every launch, unless startOnLaunch
// opts back in. Reached from QML as EditorState.mcp.
class McpController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY runningChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString url READ url NOTIFY runningChanged)
    Q_PROPERTY(QString token READ token NOTIFY runningChanged)
    Q_PROPERTY(int port READ port NOTIFY runningChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString cursorSnippet READ cursorSnippet NOTIFY runningChanged)
    Q_PROPERTY(QString claudeCommand READ claudeCommand NOTIFY runningChanged)
    Q_PROPERTY(QString stdioSnippet READ stdioSnippet NOTIFY runningChanged)
    // Persisted opt-in: start the MCP server at launch instead of leaving it off.
    // setEnabled(false) — a manual "turn access off" — clears this, so re-enabling
    // access always starts from an explicit, un-opted-in state rather than quietly
    // carrying an old intent to auto-start past the point the user turned access off.
    Q_PROPERTY(bool startOnLaunch READ startOnLaunch WRITE setStartOnLaunch NOTIFY startOnLaunchChanged)

public:
    explicit McpController(AppController *app, QObject *parent = nullptr);
    ~McpController() override;

    Q_INVOKABLE void setEnabled(bool enabled);
    // Headless wires transports onto the server itself, which the on/off switch above
    // does not expose.
    drift::mcp::McpServer *server() const { return m_mcp.get(); }
    bool enabled() const { return running(); }
    bool running() const;
    QString url() const;
    QString token() const;
    int port() const;
    QString error() const;
    QString cursorSnippet() const;
    QString claudeCommand() const;
    QString stdioSnippet() const;
    Q_INVOKABLE void copyCursorSnippet();
    Q_INVOKABLE void copyClaudeCommand();
    Q_INVOKABLE void copyStdioSnippet();
    Q_INVOKABLE void copyAgentGuide();
    Q_INVOKABLE void rotateToken();
    bool startOnLaunch() const { return m_mcpStartOnLaunch; }
    Q_INVOKABLE void setStartOnLaunch(bool enabled);
    // Starts the server if startOnLaunch is set. GUI-only — called once from
    // Main.qml's startup sequence; headless mode never calls this, since it configures
    // and starts the server itself from --mcp-port/--mcp-token/--mcp-stdio.
    Q_INVOKABLE void applyStartOnLaunch();
    QString agentGuide() const;

    // Helpers (GUI thread). Used by src/mcp, not QML.
    QPair<int, int> locateClip(const QString &id) const;
    QString clipId(int trackIndex, int clipIndex) const;
    QVariantMap compactClip(int trackIndex, int clipIndex, bool includeCanvas = true) const;
    struct InspectOptions {
        bool clips = false;
        bool detail = false;
        bool cues = false;
        bool verbose = false;
        int since = -1;
        int track = -1;
        QString clip;
    };
    QJsonObject inspect(const InspectOptions &options) const;
    QJsonObject inspect(bool includeClips, int sinceRevision = -1, bool detail = false,
                        bool includeCues = false) const
    {
        return inspect(InspectOptions{includeClips, detail, includeCues, false, sinceRevision});
    }
    int revision() const { return m_editRevision; }
    // AppController bumps this from every edit path, including ones that never touch MCP.
    void bumpEditRevision() { ++m_editRevision; }
    bool undoSuspended() const { return m_undoSuspended; }
    const drift::Project &batchBefore() const { return m_batchBefore; }
    bool setClipCanvas(int trackIndex, int clipIndex, const QVariantMap &patch);
    QJsonObject captureFrame(double atSeconds, bool full);

    // Perception for agents: a labelled contact sheet and a text profile of change over time.
    // Both block on the captureFrame pattern.
    struct FrameSheetRequest {
        double start = -1.0;
        double end = -1.0;
        QList<double> at;
        QString sample = QStringLiteral("changes");
        int n = 12;
        int cols = 0;
        int tileWidth = 0;
        int minChange = 12;
        bool label = true;
        bool toPath = false;
        int track = -1;
        int clip = -1;
    };
    QJsonObject frameSheet(const FrameSheetRequest &request);

    struct ActivityRequest {
        double start = -1.0;
        double end = -1.0;
        int samples = 200;
        int peaks = 8;
        bool audio = true;
        int track = -1;
        int clip = -1;
    };
    QJsonObject activity(const ActivityRequest &request);

    bool setWorkArea(double inSeconds, double outSeconds);
    void rememberExportSettings(const QVariantMap &settings);
    QVariantMap lastExportSettings() const;
    void beginBatch();
    void endBatch(const QString &text, bool pushUndo);
    QJsonObject listHistory(int limit = 20) const;
    QJsonObject undoTo(int index, const QString &hash);
    QJsonObject takeSnapshot(const QString &label);
    QJsonObject listSnapshots() const;
    QJsonObject restoreSnapshot(const QString &hash);

    // MCP helpers (GUI thread). Used by src/mcp, not QML.
    // Waveform rendered as an image for agents. Blocks on the captureFrame pattern.
    QJsonObject waveformImage(const QString &mode, int trackIndex, int clipIndex,
                                 const QString &assetId, double startSeconds, double durSeconds,
                                 int width, int height, bool spectrogram,
                                 int summaryBuckets, bool words = true) const;

    // Audio for agents. All of these block: the QML-facing waveform getters return empty on the
    // first call and repaint on a signal, which works for a binding and not at all for a caller
    // that gets one reply. These decode/mix inline instead, on the captureFrame pattern.
    QJsonObject waveformForClip(int trackIndex, int clipIndex, int buckets) const;
    QJsonObject waveformForAsset(const QString &assetId, double startSeconds,
                                    double durSeconds, int buckets) const;
    QJsonObject waveformForTimeline(double startSeconds, double durSeconds, int buckets) const;
    QJsonObject detectBeats(double startSeconds, double durSeconds, bool force);
    QJsonObject beatPayload() const;
    QJsonObject audioSummary() const;
    // Grid times from the current analysis. `unit` is beat, bar or onset; `minStrength` filters
    // onsets only. Empty when nothing has been analysed yet.
    QList<double> beatTimes(const QString &unit, double minStrength) const;
    // --- scene toolbox ---
    QJsonObject detectScenes(int trackIndex, int clipIndex, double threshold, double minScene,
                                bool withObjects);
    // The live analysis, filtered and shaped for MCP. Times are reported in both source and
    // timeline space so an agent never has to redo the trim/speed/reverse mapping itself.
    QJsonObject listScenes(const QString &label, double minScore, const QString &sort,
                              int limit, int trackIndex = -1, int clipIndex = -1) const;
    // Rows for a clip's scene analysis: the live one when it is the last scanned clip, else the
    // on-disk cache. Empty when it was never scanned.
    QVariantList sceneRows(int trackIndex, int clipIndex, const drift::Clip **clip) const;
    QJsonObject describeClip(int topCount, int trackIndex = -1, int clipIndex = -1) const;
    QJsonObject findScenes(const QString &label, double minScore, int trackIndex,
                              int limit) const;
    // Timeline seconds of every detected boundary inside the clip that was analysed.
    QList<double> sceneCutTimes(double minScore, const QString &label) const;
    int bookmarkScenes(double minScore, const QString &label, const QString &labelPrefix);
    // Which model addons are installed, so an agent can say what to install rather than
    // retrying blindly.
    QJsonObject aiCapabilities() const;
    // Transcript-first editing. Transcripts live on the asset (Project::transcript) in source time.
    QJsonObject transcribe(const QStringList &assetIds, const QJsonObject &options);
    QJsonObject getTranscript(const QString &assetId, int trackIndex, int clipIndex,
                                 double startSeconds, double endSeconds, const QJsonObject &options) const;
    QJsonObject diarize(const QString &assetId, const QJsonObject &options);
    QJsonObject keepRanges(int trackIndex, int clipIndex, const QJsonArray &ranges, double padding,
                              double declick, bool ripple);
    QJsonObject assemble(const QJsonArray &edl, int trackIndex, bool atGiven, double atSeconds,
                            double padding, double declick);
    QJsonObject cutWords(int trackIndex, int clipIndex, const QJsonObject &args);
    QJsonObject ttsGenerate(const QJsonObject &args);
    QJsonObject sfxGenerate(const QJsonObject &args);
    QJsonObject listVoices(const QJsonObject &args) const;
    QJsonObject cloudProviderStatus() const;
    QJsonObject getJob(const QString &id) const;
    QJsonObject cancelJob(const QString &id);

    int bookmarkBeats(double startSeconds, double durSeconds, const QString &unit,
                         double minStrength, const QString &labelPrefix);
    QJsonObject setBeatLayers(bool grid, bool onsets);
    QJsonObject setClipVolume(int trackIndex, int clipIndex, double value, bool atGiven,
                                 double atSeconds);
    QJsonObject detectSilence(int trackIndex, int clipIndex, double startSeconds,
                                 double durSeconds, double threshold, double minDuration,
                                 double padding, const QString &method = QStringLiteral("energy")) const;
    QJsonObject removeSilence(int trackIndex, int clipIndex, double threshold,
                                 double minDuration, double padding, double declick = 0.03,
                                 const QString &method = QStringLiteral("energy"));
    QJsonObject analyzeLoudness(int trackIndex, int clipIndex, double startSeconds,
                                   double durSeconds) const;
    QJsonObject normalizeVolume(int trackIndex, int clipIndex, double targetLufs);
    QJsonObject duckUnder(int musicTrack, int musicClip, int overTrack,
                             const QStringList &overClips, double amount, double attack,
                             double release);
    QJsonObject listFaceTrack(int trackIndex, int clipIndex) const;
    QJsonObject autoReframe(int trackIndex, int clipIndex, double aspect, const QString &mode);
    QJsonObject listAddons() const;
    QJsonObject installAddon(const QString &id);
    QJsonObject cancelAddonInstall(const QString &id);
    QJsonObject setAcceleration(const QString &variant);

signals:
    void runningChanged();
    void errorChanged();
    void startOnLaunchChanged();

private:
    void rebuildClipIndexIfNeeded() const;
    // An error object when `provider` can't be used yet (no key, no consent); empty when it can.
    QJsonObject cloudUnavailable(const QString &provider) const;
    // Imports a generated file into the bin, records what made it, optionally places it.
    QJsonObject importGeneratedAudio(const QString &path, const QJsonObject &generator, const QJsonValue &place);
    // Lands a finished transcript on its asset, unless the asset is gone or its file changed.
    void storeTranscript(const QString &assetId, std::shared_ptr<drift::Transcript> transcript);

    AppController *m_app = nullptr;
    std::unique_ptr<drift::mcp::McpServer> m_mcp;
    bool m_mcpStartOnLaunch = false;
    bool m_undoSuspended = false;
    int m_batchDepth = 0;
    drift::Project m_batchBefore;
    int m_editRevision = 0;
    mutable QHash<QString, QPair<int, int>> m_clipIndex;
    mutable int m_clipIndexRevision = -1;
};
