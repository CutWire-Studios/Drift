#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QIODevice>
#include <QMediaDevices>
#include <QObject>
#include <QThread>
#include <QVector>

#include <atomic>
#include <functional>

class QAudioSink;
class QTimer;
class AudioOutputChannel;
struct AAudioStreamStruct;

namespace drift {

// Picks a format `device` will actually take. Interleaved stereo float at `preferredRate` is what
// the mixer produces, so that is tried first; failing that the device's own rate, and failing that
// whatever it prefers — which may be neither float nor stereo, hence writeInterleaved() below.
// Returns an invalid format if the device cannot be opened at all.
QAudioFormat negotiateAudioFormat(const QAudioDevice &device, int preferredRate);

// Writes `frames` of interleaved stereo float into `dst` as `format`. Channels above the second
// are silent and a mono device gets the downmix; there is no panning law here because the source
// is already a finished stereo mix.
void writeInterleaved(const float *stereo, int frames, const QAudioFormat &format, char *dst);

} // namespace drift

// Pull-mode source feeding the sink from the channel's fill callback.
class AudioPullDevice : public QIODevice
{
public:
    explicit AudioPullDevice(AudioOutputChannel *channel);

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *data, qint64 len) override;
    // Endless generated source: always advertise data so the sink keeps pulling.
    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }

private:
    AudioOutputChannel *m_channel = nullptr;
};

// One audio output: the sink, the thread it runs on, and the format negotiated with the device.
//
// Both the timeline engine and the speed-curve preview player used to open a QAudioSink by hand
// with a hard-coded format and never touch it again, which went silent on any machine whose
// default device did not happen to take float stereo at the project's rate — and stayed silent for
// the rest of the session once the default device changed underneath it. Recovering from that is
// the same work in both places, so it lives here once.
class AudioOutputChannel : public QObject
{
    Q_OBJECT

public:
    explicit AudioOutputChannel(const QString &threadName, QObject *parent = nullptr);
    ~AudioOutputChannel() override;

    // Renders interleaved stereo float. Called on the audio thread; returns frames written.
    using FillCallback = std::function<int(float *stereo, int frames)>;
    void setFillCallback(FillCallback fill);

    // The rate the caller would like — normally the project's. The device gets the final say.
    void setPreferredSampleRate(int rate);
    // The rate actually negotiated: what the fill callback must render at.
    int sampleRate() const { return m_sampleRate.load(std::memory_order_acquire); }

    // Empty id follows the system default, including when that default later changes.
    void setDeviceId(const QByteArray &id);
    QByteArray deviceId() const { return m_requestedId; }

    void start();
    void stop();

    // Microseconds the device has actually played. Audio thread only.
    qint64 processedUSecs() const;
    // Microseconds handed to the sink since it started, on the same scale as processedUSecs(). The
    // gap between the two is what is still queued ahead of the next fill. Audio thread only.
    qint64 deliveredUSecs() const;

signals:
    // A new sink was opened — a different rate, a different device, or recovery from an error. Its
    // played position starts over from zero and whatever the old one had queued is gone, so the
    // caller's clock and mixer have to re-anchor, at sampleRate() if that moved too.
    void sinkRebuilt();
    // Playback cannot produce sound, with a message fit to show the user.
    void errorOccurred(const QString &message);

private:
    friend class AudioPullDevice;

    QAudioDevice resolveDevice() const;
    void rebuild();
    void onAudioOutputsChanged();
    void handleSinkError(const QString &message);
    // Audio thread only.
    void buildSink(const QAudioDevice &device, const QAudioFormat &format);
    void destroySink();

    // Android, following the system default: Qt's sink pins AAudio to the device id of whatever it
    // thinks the default is, and its device list drops USB and LE Audio headsets, so those never
    // got sound (#391). An AAudio stream with no device set lets Android route it instead.
    bool useNativeStream() const;
    // Audio thread only. Returns the format the stream opened with, invalid on failure.
    QAudioFormat openNativeStream(int preferredRate);
    void startNativeStream();
    void pumpNativeStream();
    void onNativeStreamError(int error);

    QMediaDevices m_mediaDevices;
    QThread m_thread;
    AudioPullDevice *m_pull = nullptr;
    QAudioSink *m_sink = nullptr;
    // Audio thread only, Android.
    AAudioStreamStruct *m_stream = nullptr;
    QTimer *m_pump = nullptr;
    qint64 m_readBase = 0;
    QByteArray m_bytes;
    FillCallback m_fill;

    QByteArray m_requestedId;  // owner thread
    QByteArray m_activeId;     // owner thread
    bool m_haveSink = false;   // owner thread's view of whether a sink was built
    int m_preferredRate = 48000;
    std::atomic<int> m_sampleRate{48000};
    std::atomic<bool> m_running{false};
    // One automatic rebuild per start(): a device that fails the moment it is opened would
    // otherwise have us rebuilding in a loop.
    bool m_recovered = false;

    // Audio thread only, written when the sink is built.
    QAudioFormat m_format;
    bool m_direct = true; // format is float stereo, so the callback can fill the sink buffer
    qint64 m_deliveredFrames = 0;
    QVector<float> m_scratch;
};
