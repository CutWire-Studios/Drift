#pragma once

#include "AudioOutputChannel.h"
#include "PlaybackClock.h"
#include "core/Clip.h"
#include "core/SpeedCurve.h"
#include "core/Time.h"
#include "engine/audio/ClipAudioRetimer.h"

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSize>
#include <QThread>
#include <QTimer>

#include <atomic>

// Decodes frames off the GUI thread. Requests are stamped with a token and stale ones are
// dropped, so a scrub that outruns the decoder collapses to its newest position instead of
// grinding through every intermediate frame.
class ClipPreviewFrameWorker : public QObject
{
    Q_OBJECT

public:
    std::atomic<quint64> latestToken{0};

public slots:
    void decode(const QString &path, quint64 streamId, qint64 sourceUs, int maxWidth, int maxHeight,
                quint64 token, int rotationOverride = -1);

signals:
    void decoded(const QImage &image, quint64 token);
};

// Auditions a single clip at a candidate speed curve: the clip alone, no compositing, no
// effects, no other tracks — which is exactly what the speed-curve editor wants to show.
//
// Audio comes from AudioMixer::readClipAudio, so what you hear here is the very same retiming
// the timeline will produce once the curve is applied, pitch-preserved through ClipAudioRetimer.
// The clock is audio-master, like the main engine, so picture follows sound.
//
// ClipReaderPool's workers are shared with the timeline, so the caller must stop main playback
// before driving this — the same rule the export, denoise and segmentation jobs follow.
class ClipPreviewPlayer : public QObject
{
    Q_OBJECT

public:
    explicit ClipPreviewPlayer(QObject *parent = nullptr);
    ~ClipPreviewPlayer() override;

    // Rebases `clip` to start at 0 and auditions it. Pass the project's rate and fps so the
    // preview runs on the same grid the timeline will.
    void setClip(const drift::Clip &clip, int sampleRate, int fps);
    // Swaps the ramp in place, keeping playback running where it can.
    void setSpeedCurve(const drift::SpeedCurve &curve);
    void clear();

    drift::TimeUs durationUs() const;
    drift::TimeUs positionUs() const;
    QSize frameSize() const;
    bool isPlaying() const { return m_playing; }

    void play();
    void pause();
    void seek(drift::TimeUs us);

    // Empty id follows the system default.
    void setAudioDeviceId(const QByteArray &id) { m_audio.setDeviceId(id); }

signals:
    void frameChanged();
    void frameSizeChanged();
    void positionChanged();
    void playingChanged();
    void durationChanged();

private:
    int fillAudio(float *buffer, int sampleCount);
    void ensureAudioSink();
    void onAudioSampleRateChanged();
    void onPositionTick();
    void requestFrame(drift::TimeUs positionUs);
    void onDecoded(const QImage &image, quint64 token);

    drift::Clip m_clip;
    mutable QMutex m_clipMutex; // m_clip is read on the audio thread while the GUI edits it

    // Touched only from the audio thread. Nothing resets it from the GUI side on purpose: seek()
    // does not stop the sink, so a reset from there would race fillAudio(). A seek shows up as a
    // timeline discontinuity and a new clip or curve as a new identity, and the retimer restarts
    // itself on either.
    drift::ClipAudioRetimer m_retimer;

    PlaybackClock m_clock;
    AudioOutputChannel m_audio;

    QThread m_frameThread;
    ClipPreviewFrameWorker *m_frameWorker = nullptr;
    std::atomic<quint64> m_frameToken{0};
    quint64 m_shownToken = 0;

    QTimer m_positionTimer;
    QTimer m_frameTimer;
    QSize m_frameSize;
    drift::TimeUs m_positionUs = 0;
    std::atomic<bool> m_playing = false;
    // The rate the device negotiated, not necessarily the project's — see PlaybackEngine.
    int m_sampleRate = 48000;
    int m_fps = 30;
};
