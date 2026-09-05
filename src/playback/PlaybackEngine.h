#pragma once

#include "AudioOutputChannel.h"
#include "CompositorService.h"
#include "PlaybackClock.h"
#include "core/Project.h"
#include "core/Time.h"
#include "engine/AudioMixer.h"
#include "engine/audio/ClipAudioRetimer.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QTimer>

#include <atomic>

// Audio-master playback: mixes timeline audio and composites preview frames off the GUI thread.
class PlaybackEngine : public QObject
{
    Q_OBJECT

    // The composited frame lives in GPU memory; the preview item wraps this
    // texture directly rather than uploading a QImage every frame.
    Q_PROPERTY(int previewTextureId READ previewTextureId NOTIFY currentFrameChanged)
    Q_PROPERTY(QSize previewTextureSize READ previewTextureSize NOTIFY currentFrameChanged)
    Q_PROPERTY(QImage previewImage READ previewImage NOTIFY currentFrameChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY currentFrameChanged)
    // hasFrame alone cannot tell "the playhead is over a gap" from "the GPU
    // compositor never started", and the preview used to report the second as the
    // first. These carry the difference. The status is a stable id, not a
    // sentence, so the message retranslates with the rest of the UI.
    Q_PROPERTY(bool gpuCompositorReady READ gpuCompositorReady NOTIFY gpuCompositorStatusChanged)
    Q_PROPERTY(QString gpuCompositorStatus READ gpuCompositorStatus NOTIFY gpuCompositorStatusChanged)
    // Raw driver output, e.g. "OpenGL 3.0 — llvmpipe (LLVM 3.6, 128 bits)". Never translated.
    Q_PROPERTY(QString gpuCompositorDetail READ gpuCompositorDetail NOTIFY gpuCompositorStatusChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(QString previewQuality READ previewQuality WRITE setPreviewQuality NOTIFY previewQualityChanged)
    Q_PROPERTY(QString playbackMode READ playbackMode WRITE setPlaybackMode NOTIFY playbackModeChanged)
    Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(QString decodeMode READ decodeMode WRITE setDecodeMode NOTIFY decodeModeChanged)

public:
    explicit PlaybackEngine(QObject *parent = nullptr);
    ~PlaybackEngine() override;

    void setProject(drift::Project *project);
    void setPlayheadUs(drift::TimeUs us);
    drift::TimeUs playheadUs() const { return m_playheadUs; }

    void setLoopWorkArea(bool enabled) { m_loopWorkArea = enabled; }
    bool loopWorkArea() const { return m_loopWorkArea; }

    int previewTextureId() const;
    QSize previewTextureSize() const;
    // Readback fallback for Android drivers that refuse to share the GL context with the scene
    // graph (see GpuFrameTexture). Null whenever the texture path above is usable.
    QImage previewImage() const;
    bool hasFrame() const;
    bool gpuCompositorReady() const { return m_gpuStatusId == QStringLiteral("ready"); }
    // "unknown" until the first probe runs, so the UI can hold off rather than
    // flash a failure during startup.
    QString gpuCompositorStatus() const { return m_gpuStatusId; }
    QString gpuCompositorDetail() const { return m_gpuStatusDetail; }
    bool isPlaying() const { return m_playing; }
    QString previewQuality() const;
    void setPreviewQuality(const QString &quality);
    // "fast": realtime playback with audio, late frames dropped. "quality":
    // every frame is rendered and shown, silently and slower than realtime.
    QString playbackMode() const;
    void setPlaybackMode(const QString &mode);
    // Timeline seconds covered per real second. Audio keeps its pitch at every rate; see fillAudio.
    // Only the values the preview offers are accepted, so a typo in QML cannot put the transport
    // somewhere the stretcher has never been tested.
    double playbackRate() const { return m_playbackRate; }
    void setPlaybackRate(double rate);
    // Preview video decode: "auto" (default, per-clip heuristic), "software", or
    // "hw:<backend>" naming one of decodeModes(). Auto keeps cheap clips on the CPU and
    // uses the GPU for 4K / heavy bitrates; the other two force that path for every
    // clip. A backend this machine does not have resolves back to "auto".
    QString decodeMode() const;
    void setDecodeMode(const QString &mode);
    // Picker model: {id, label} rows, hardware entries only for backends that open
    // here. Not a constant — it depends on the GPU and driver the app started with.
    Q_INVOKABLE QVariantList decodeModes() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void refreshFrame();
    Q_INVOKABLE void setPreviewRenderSize(int width, int height);
    // Restart the composite tick from the current project fps and rate. Playback
    // samples `m_project` live, but the QTimer interval is snapped at play().
    void syncDisplayCadence();

    // Id of the text clip currently edited in place on the preview; that clip is
    // omitted from the composited frame so the QML inline editor stands in for it.
    void setEditingClipId(const QString &id);

    // Empty id follows the system default. Applied to the sink immediately.
    void setAudioDeviceId(const QByteArray &id) { m_audio.setDeviceId(id); }

signals:
    // Playback cannot produce sound; carries a message meant for the user.
    void audioError(const QString &message);
    // A reader hit a driver failure and went sticky-software. `backendName` is the
    // backend the user pinned, empty when Auto chose it.
    void hardwareDecodeFellBack(const QString &backendName);
    // The GPU compositor will not come up on this machine. Fires once per session;
    // `statusId` is drift::gl::statusId(), `detail` the raw GL version and renderer.
    void gpuCompositorUnavailable(const QString &statusId, const QString &detail);

    void gpuCompositorStatusChanged();
    void currentFrameChanged();
    void playingChanged();
    void previewQualityChanged();
    void playbackModeChanged();
    void playbackRateChanged();
    void decodeModeChanged();
    void playheadUsChanged(quint64 us);

private:
    int fillAudio(float *buffer, int sampleCount);
    void ensureAudioSink();
    void onAudioSampleRateChanged();
    void onPlayheadTick();
    void onCompositeTick();
    void onCompositeFinished();
    void onFrameReady(const GpuFrameTexture &frame);
    void checkEndOfTimeline(drift::TimeUs timeUs);
    void checkHardwareFallback();
    void probeGpuCompositor();
    bool isQualityMode() const { return m_playbackMode == QStringLiteral("quality"); }
    bool isAutoQuality() const { return m_previewQuality == QStringLiteral("auto"); }
    bool shouldLoopWorkArea(drift::TimeUs *loopInOut, drift::TimeUs *loopOutOut) const;
    drift::TimeUs frameStepUs() const;
    FrameCompositor::RenderOptions playbackRenderOptions() const;

    drift::Project *m_project = nullptr;
    PlaybackClock m_clock;
    CompositorService m_compositor;
    AudioMixer m_mixer;
    AudioOutputChannel m_audio;
    QTimer m_playheadTimer;
    QTimer m_compositeTimer;
    QTimer m_gpuProbeTimer;
    int m_gpuProbeAttempts = 0;
    bool m_gpuUnavailableNotified = false;
    QString m_gpuStatusId = QStringLiteral("unknown");
    QString m_gpuStatusDetail;
    GpuFrameTexture m_currentFrame;
    mutable QMutex m_frameMutex;
    drift::TimeUs m_playheadUs = 0;
    std::atomic<bool> m_playing = false;
    QString m_previewQuality = QStringLiteral("full");
    QString m_playbackMode = QStringLiteral("fast");
    QString m_decodeMode = QStringLiteral("auto");
    // Baseline for ClipReader's process-wide fallback counter, so the notice fires on
    // a new fallback rather than on every frame after the first one.
    quint64 m_hwFallbackCount = 0;
    // Not persisted, unlike quality and mode: a session left at 4x would otherwise come back at 4x
    // with nothing to explain why playback runs away.
    double m_playbackRate = 1.0;
    // Global retiming for non-1x rates. Touched only from the audio thread; the GUI thread asks for
    // a restart by bumping the generation below rather than reaching into its state.
    drift::ClipAudioRetimer m_rateRetimer;
    std::atomic<quint64> m_audioStreamGeneration{1};
    // Playhead position the in-flight quality-mode frame was requested for; a
    // seek that lands elsewhere while it renders must not be stepped over.
    drift::TimeUs m_qualityRequestUs = -1;
    QString m_editingClipId;
    int m_previewRenderWidth = 0;
    int m_previewRenderHeight = 0;
    // The rate the sink negotiated, which is what the mixer renders at and what the clock counts
    // samples in — not necessarily the project's rate, since the device has the final say.
    int m_sampleRate = 48000;
    bool m_loopWorkArea = false;
    // processedUSecs() is cumulative from QAudioSink::start(), not from the last clock reset.
    // Subtracting this (captured whenever the clock is re-anchored) keeps a seek from landing
    // at seekTarget + time-since-play instead of seekTarget.
    qint64 m_sinkPlayedUsOffset = 0;
};
