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
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY currentFrameChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(QString previewQuality READ previewQuality WRITE setPreviewQuality NOTIFY previewQualityChanged)
    Q_PROPERTY(QString playbackMode READ playbackMode WRITE setPlaybackMode NOTIFY playbackModeChanged)
    Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)

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
    bool hasFrame() const;
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

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void refreshFrame();
    Q_INVOKABLE void setPreviewRenderSize(int width, int height);

    // Id of the text clip currently edited in place on the preview; that clip is
    // omitted from the composited frame so the QML inline editor stands in for it.
    void setEditingClipId(const QString &id);

    // Empty id follows the system default. Applied to the sink immediately.
    void setAudioDeviceId(const QByteArray &id) { m_audio.setDeviceId(id); }

    // Id of the clip whose mask coverage the preview should show in place of the composite.
    // Empty renders normally. Export is unaffected: it never reads this.
    void setMaskViewClipId(const QString &id);

signals:
    // Playback cannot produce sound; carries a message meant for the user.
    void audioError(const QString &message);

    void currentFrameChanged();
    void playingChanged();
    void previewQualityChanged();
    void playbackModeChanged();
    void playbackRateChanged();
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
    GpuFrameTexture m_currentFrame;
    mutable QMutex m_frameMutex;
    drift::TimeUs m_playheadUs = 0;
    std::atomic<bool> m_playing = false;
    QString m_previewQuality = QStringLiteral("full");
    QString m_playbackMode = QStringLiteral("fast");
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
    QString m_maskViewClipId;
    int m_previewRenderWidth = 0;
    int m_previewRenderHeight = 0;
    // The rate the sink negotiated, which is what the mixer renders at and what the clock counts
    // samples in — not necessarily the project's rate, since the device has the final say.
    int m_sampleRate = 48000;
    bool m_loopWorkArea = false;
};
