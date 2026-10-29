#include "playback/AudioOutputChannel.h"

#include <QAudioSink>
#include <QDebug>
#include <QTimer>

#include <cstring>

#ifdef Q_OS_ANDROID
#include <aaudio/AAudio.h>
#endif

namespace {

QString describeSinkError(QAudio::Error error)
{
    switch (error) {
    case QAudio::OpenError:
        return AudioOutputChannel::tr(
            "The audio device could not be opened. Another program may be using it exclusively.");
    case QAudio::IOError:
        return AudioOutputChannel::tr("The audio device stopped responding.");
    case QAudio::FatalError:
        return AudioOutputChannel::tr("The audio device was disconnected.");
    case QAudio::NoError:
        break;
    default:
        break; // UnderrunError is deprecated and no backend emits it
    }
    return QString();
}

} // namespace

namespace drift {

QAudioFormat negotiateAudioFormat(const QAudioDevice &device, int preferredRate)
{
    if (device.isNull())
        return QAudioFormat();

    QAudioFormat format;
    format.setSampleRate(preferredRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    if (device.isFormatSupported(format))
        return format;

    // Same shape at a rate the device admits to. Worth trying on its own because a device that
    // only runs at 44100 is far more common than one that cannot do float stereo at all, and
    // keeping float stereo means the mixer's output reaches the sink untouched.
    const QAudioFormat preferred = device.preferredFormat();
    format.setSampleRate(preferred.sampleRate());
    if (device.isFormatSupported(format))
        return format;

    return preferred;
}

void writeInterleaved(const float *stereo, int frames, const QAudioFormat &format, char *dst)
{
    const int channels = format.channelCount();
    const int bytesPerSample = format.bytesPerSample();
    if (!stereo || !dst || frames <= 0 || channels <= 0 || bytesPerSample <= 0)
        return;

    for (int frame = 0; frame < frames; ++frame) {
        const float left = stereo[frame * 2];
        const float right = stereo[frame * 2 + 1];
        for (int channel = 0; channel < channels; ++channel) {
            float value = 0.0f;
            if (channels == 1)
                value = 0.5f * (left + right);
            else if (channel == 0)
                value = left;
            else if (channel == 1)
                value = right;

            value = qBound(-1.0f, value, 1.0f);
            char *out = dst + static_cast<qsizetype>(frame * channels + channel) * bytesPerSample;
            switch (format.sampleFormat()) {
            case QAudioFormat::UInt8: {
                const auto sample = static_cast<quint8>(value * 127.0f + 128.0f);
                std::memcpy(out, &sample, sizeof(sample));
                break;
            }
            case QAudioFormat::Int16: {
                const auto sample = static_cast<qint16>(value * 32767.0f);
                std::memcpy(out, &sample, sizeof(sample));
                break;
            }
            case QAudioFormat::Int32: {
                // Through double: 2147483647.0f rounds up to 2^31 as a float, which overflows the
                // cast at full scale.
                const auto sample = static_cast<qint32>(static_cast<double>(value) * 2147483647.0);
                std::memcpy(out, &sample, sizeof(sample));
                break;
            }
            case QAudioFormat::Float:
                std::memcpy(out, &value, sizeof(value));
                break;
            default:
                std::memset(out, 0, static_cast<size_t>(bytesPerSample));
                break;
            }
        }
    }
}

} // namespace drift

AudioPullDevice::AudioPullDevice(AudioOutputChannel *channel)
    : m_channel(channel)
{
    open(QIODevice::ReadOnly);
}

qint64 AudioPullDevice::readData(char *data, qint64 maxlen)
{
    if (!m_channel || !m_channel->m_fill || maxlen <= 0)
        return 0;

    const int bytesPerFrame = m_channel->m_format.bytesPerFrame();
    if (bytesPerFrame <= 0)
        return 0;

    const int frames = static_cast<int>(maxlen / bytesPerFrame);
    if (frames <= 0)
        return 0;

    if (m_channel->m_direct) {
        const int filled = m_channel->m_fill(reinterpret_cast<float *>(data), frames);
        m_channel->m_deliveredFrames += filled;
        return static_cast<qint64>(filled) * bytesPerFrame;
    }

    m_channel->m_scratch.resize(frames * 2);
    const int filled = m_channel->m_fill(m_channel->m_scratch.data(), frames);
    drift::writeInterleaved(m_channel->m_scratch.constData(), filled, m_channel->m_format, data);
    m_channel->m_deliveredFrames += filled;
    return static_cast<qint64>(filled) * bytesPerFrame;
}

qint64 AudioPullDevice::writeData(const char *data, qint64 len)
{
    Q_UNUSED(data);
    Q_UNUSED(len);
    return -1;
}

qint64 AudioPullDevice::bytesAvailable() const
{
    return (1 << 20) + QIODevice::bytesAvailable();
}

AudioOutputChannel::AudioOutputChannel(const QString &threadName, QObject *parent)
    : QObject(parent)
    , m_pull(new AudioPullDevice(this))
{
    // The sink and its pull device run on their own thread so that refills — which synchronously
    // decode and mix audio — never block the GUI thread.
    m_pull->moveToThread(&m_thread);
    m_thread.setObjectName(threadName);
    // The one thread whose lateness is audible. Honoured on Windows and macOS; a normal Linux
    // thread cannot raise itself, which is why background work there is lowered instead.
    m_thread.start(QThread::TimeCriticalPriority);

    // Fires for a new or removed device *and* for the default one changing, which on Windows is
    // every headphone jack, HDMI monitor and Bluetooth headset.
    connect(&m_mediaDevices, &QMediaDevices::audioOutputsChanged, this,
            &AudioOutputChannel::onAudioOutputsChanged);
}

AudioOutputChannel::~AudioOutputChannel()
{
    m_running.store(false, std::memory_order_release);
    QMetaObject::invokeMethod(m_pull, [this] { destroySink(); }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    delete m_pull;
    m_pull = nullptr;
}

void AudioOutputChannel::setFillCallback(FillCallback fill)
{
    m_fill = std::move(fill);
}

void AudioOutputChannel::setPreferredSampleRate(int rate)
{
    if (rate <= 0 || rate == m_preferredRate)
        return;

    m_preferredRate = rate;
    // Without this the sink kept whatever rate the first project opened at, so a 44.1 kHz project
    // played back through a sink still running at 48 kHz.
    if (m_haveSink)
        rebuild();
}

void AudioOutputChannel::setDeviceId(const QByteArray &id)
{
    if (id == m_requestedId)
        return;

    m_requestedId = id;
    if (m_haveSink || m_running.load(std::memory_order_acquire))
        rebuild();
}

void AudioOutputChannel::start()
{
    m_recovered = false;
    m_running.store(true, std::memory_order_release);

    if (!m_haveSink) {
        rebuild(); // builds and, seeing m_running, starts
        return;
    }

    // Never block the GUI on sink I/O: start() may pull the first buffer, which opens and seeks
    // media, and that must stay on the audio thread.
    //
    // Pausing playback leaves the sink running on silence (stop() is for teardown), so a play
    // after a pause finds it already started; starting it again only earns a Qt warning.
    QMetaObject::invokeMethod(
        m_pull,
        [this] {
            if (m_stream) {
                startNativeStream();
                return;
            }
            if (!m_sink)
                return;
            if (m_sink->state() == QAudio::SuspendedState)
                m_sink->resume();
            else if (m_sink->state() == QAudio::StoppedState) {
                m_deliveredFrames = 0;
                m_sink->start(m_pull);
            }
        },
        Qt::QueuedConnection);
}

void AudioOutputChannel::stop()
{
    m_running.store(false, std::memory_order_release);
    // Blocking, so no further refill can run once this returns and the caller is free to reset the
    // state those refills read.
    QMetaObject::invokeMethod(
        m_pull,
        [this] {
            if (m_sink)
                m_sink->stop();
#ifdef Q_OS_ANDROID
            if (m_stream) {
                m_pump->stop();
                // Pause and flush rather than stop: a stopped output stream plays out what it has
                // queued first, and QAudioSink::stop() drops it, which is what callers expect.
                AAudioStream_requestPause(m_stream);
                aaudio_stream_state_t state = AAUDIO_STREAM_STATE_UNKNOWN;
                AAudioStream_waitForStateChange(m_stream, AAUDIO_STREAM_STATE_PAUSING, &state,
                                                100'000'000);
                AAudioStream_requestFlush(m_stream);
            }
#endif
        },
        Qt::BlockingQueuedConnection);
}

qint64 AudioOutputChannel::processedUSecs() const
{
#ifdef Q_OS_ANDROID
    if (m_stream) {
        const int rate = m_format.sampleRate();
        const qint64 frames = qMax<qint64>(0, AAudioStream_getFramesRead(m_stream) - m_readBase);
        return rate > 0 ? frames * 1'000'000 / rate : 0;
    }
#endif
    return m_sink ? m_sink->processedUSecs() : 0;
}

qint64 AudioOutputChannel::deliveredUSecs() const
{
    const int rate = m_format.sampleRate();
    return rate > 0 ? m_deliveredFrames * 1'000'000 / rate : 0;
}

QAudioDevice AudioOutputChannel::resolveDevice() const
{
    if (m_requestedId.isEmpty())
        return QMediaDevices::defaultAudioOutput();

    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    for (const QAudioDevice &device : outputs) {
        if (device.id() == m_requestedId)
            return device;
    }
    // The chosen device is gone (unplugged, or a profile switch renamed it). Fall back to the
    // default without forgetting the choice, so re-plugging it picks it up again.
    return QMediaDevices::defaultAudioOutput();
}

void AudioOutputChannel::rebuild()
{
    // Blocking, and first: with the old sink closed no refill can be in flight, so the rate below
    // — which the fill callback reads — can be swapped without racing a running mix.
    QMetaObject::invokeMethod(m_pull, [this] { destroySink(); }, Qt::BlockingQueuedConnection);

    if (useNativeStream()) {
        // Opened here rather than in a queued build because only the opened stream knows its
        // rate, and sinkRebuilt has to carry it.
        QAudioFormat format;
        QMetaObject::invokeMethod(
            m_pull, [this, &format] { format = openNativeStream(m_preferredRate); },
            Qt::BlockingQueuedConnection);
        m_activeId.clear();
        if (!format.isValid()) {
            m_haveSink = false;
            emit errorOccurred(describeSinkError(QAudio::OpenError));
            return;
        }
        m_haveSink = true;
        m_sampleRate.store(format.sampleRate(), std::memory_order_release);
        emit sinkRebuilt();
        QMetaObject::invokeMethod(
            m_pull,
            [this] {
                if (m_stream && m_running.load(std::memory_order_acquire))
                    startNativeStream();
            },
            Qt::QueuedConnection);
        return;
    }

    const QAudioDevice device = resolveDevice();
    const QAudioFormat format = drift::negotiateAudioFormat(device, m_preferredRate);
    if (device.isNull() || !format.isValid()) {
        m_haveSink = false;
        m_activeId.clear();
        emit errorOccurred(device.isNull()
                               ? tr("No audio output device is available.")
                               : tr("The audio device does not support playback of this project."));
        return;
    }

    m_activeId = device.id();
    m_haveSink = true;
    // Before the sink is built, so the caller has re-anchored its clock and mixer by the time the
    // first buffer is pulled from it.
    m_sampleRate.store(format.sampleRate(), std::memory_order_release);
    emit sinkRebuilt();

    QMetaObject::invokeMethod(
        m_pull, [this, device, format] { buildSink(device, format); }, Qt::QueuedConnection);
}

void AudioOutputChannel::buildSink(const QAudioDevice &device, const QAudioFormat &format)
{
    destroySink();

    m_format = format;
    m_direct = format.sampleFormat() == QAudioFormat::Float && format.channelCount() == 2;
    m_sink = new QAudioSink(device, format);
    // Keep the platform default buffer: a too-small buffer starves the pull loop and makes
    // playback run slow. Callers anchor the playhead to processedUSecs(), so buffer depth doesn't
    // affect sync.

    // Context is the sink, so this runs on the audio thread and dies with it.
    connect(m_sink, &QAudioSink::stateChanged, m_sink, [this](QAudio::State state) {
        if (state != QAudio::StoppedState || !m_running.load(std::memory_order_acquire))
            return;
        const QString message = describeSinkError(m_sink->error());
        if (message.isEmpty())
            return;
        QMetaObject::invokeMethod(
            this, [this, message] { handleSinkError(message); }, Qt::QueuedConnection);
    });

    m_deliveredFrames = 0;
    if (m_running.load(std::memory_order_acquire))
        m_sink->start(m_pull);
}

void AudioOutputChannel::destroySink()
{
#ifdef Q_OS_ANDROID
    if (m_stream) {
        m_pump->stop();
        AAudioStream_requestStop(m_stream);
        AAudioStream_close(m_stream);
        m_stream = nullptr;
    }
#endif
    if (!m_sink)
        return;
    m_sink->stop();
    delete m_sink;
    m_sink = nullptr;
}

void AudioOutputChannel::onAudioOutputsChanged()
{
    if (!m_haveSink)
        return; // nothing open; the next start() negotiates against whatever is there then
    if (useNativeStream())
        return; // Android moves the stream itself, and disconnects it when it cannot

    const QAudioDevice target = resolveDevice();
    if (!target.isNull() && target.id() == m_activeId)
        return;

    rebuild();
}

void AudioOutputChannel::handleSinkError(const QString &message)
{
    qWarning("AudioOutputChannel: %s", qPrintable(message));

    if (!m_recovered) {
        // Reopening is usually enough — the endpoint moved, or the session was invalidated under
        // us — and there is nothing to tell the user about a gap they did not hear. rebuild()
        // reports it if that fails too.
        m_recovered = true;
        rebuild();
        return;
    }

    emit errorOccurred(message);
}

bool AudioOutputChannel::useNativeStream() const
{
#ifdef Q_OS_ANDROID
    return m_requestedId.isEmpty();
#else
    return false;
#endif
}

QAudioFormat AudioOutputChannel::openNativeStream(int preferredRate)
{
#ifdef Q_OS_ANDROID
    AAudioStreamBuilder *builder = nullptr;
    if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK)
        return QAudioFormat();

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_MEDIA);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, preferredRate);
    // Called on a thread of AAudio's own, where the stream must not be closed. The hop through the
    // audio thread drops errors from a stream that has since been replaced.
    AAudioStreamBuilder_setErrorCallback(
        builder,
        [](AAudioStream *stream, void *userData, aaudio_result_t error) {
            auto *self = static_cast<AudioOutputChannel *>(userData);
            QMetaObject::invokeMethod(
                self->m_pull,
                [self, stream, error] {
                    if (stream != self->m_stream)
                        return;
                    QMetaObject::invokeMethod(
                        self, [self, error] { self->onNativeStreamError(error); },
                        Qt::QueuedConnection);
                },
                Qt::QueuedConnection);
        },
        this);

    AAudioStream *stream = nullptr;
    aaudio_result_t result = AAudioStreamBuilder_openStream(builder, &stream);
    if (result != AAUDIO_OK) {
        AAudioStreamBuilder_setSampleRate(builder, AAUDIO_UNSPECIFIED);
        result = AAudioStreamBuilder_openStream(builder, &stream);
    }
    AAudioStreamBuilder_delete(builder);
    if (result != AAUDIO_OK) {
        qWarning("AudioOutputChannel: AAudio open failed: %s", AAudio_convertResultToText(result));
        return QAudioFormat();
    }

    QAudioFormat format;
    format.setSampleRate(AAudioStream_getSampleRate(stream));
    format.setChannelCount(AAudioStream_getChannelCount(stream));
    switch (AAudioStream_getFormat(stream)) {
    case AAUDIO_FORMAT_PCM_FLOAT:
        format.setSampleFormat(QAudioFormat::Float);
        break;
    case AAUDIO_FORMAT_PCM_I16:
        format.setSampleFormat(QAudioFormat::Int16);
        break;
    default:
        break;
    }
    if (!format.isValid()) {
        AAudioStream_close(stream);
        return QAudioFormat();
    }

    // Deep enough to ride out a slow mix between pump ticks. The playhead follows frames read, so
    // the depth costs start latency only, not sync.
    AAudioStream_setBufferSizeInFrames(stream, format.sampleRate() * 80 / 1000);

    m_stream = stream;
    m_format = format;
    m_direct = format.sampleFormat() == QAudioFormat::Float && format.channelCount() == 2;
    m_deliveredFrames = 0;
    m_readBase = AAudioStream_getFramesWritten(stream);
    if (!m_pump) {
        m_pump = new QTimer(m_pull);
        m_pump->setTimerType(Qt::PreciseTimer);
        m_pump->setInterval(5);
        connect(m_pump, &QTimer::timeout, m_pull, [this] { pumpNativeStream(); });
    }
    return format;
#else
    Q_UNUSED(preferredRate);
    return QAudioFormat();
#endif
}

void AudioOutputChannel::startNativeStream()
{
#ifdef Q_OS_ANDROID
    const aaudio_stream_state_t state = AAudioStream_getState(m_stream);
    if (state == AAUDIO_STREAM_STATE_STARTED || state == AAUDIO_STREAM_STATE_STARTING)
        return;
    // Flushed or never written to, so nothing written so far is still to be played.
    m_deliveredFrames = 0;
    m_readBase = AAudioStream_getFramesWritten(m_stream);
    AAudioStream_requestStart(m_stream);
    m_pump->start();
    pumpNativeStream();
#endif
}

void AudioOutputChannel::pumpNativeStream()
{
#ifdef Q_OS_ANDROID
    if (!m_stream || !m_fill)
        return;

    const qint64 queued = AAudioStream_getFramesWritten(m_stream) - AAudioStream_getFramesRead(m_stream);
    const int room = static_cast<int>(AAudioStream_getBufferSizeInFrames(m_stream) - queued);
    if (room <= 0)
        return;

    m_bytes.resize(static_cast<qsizetype>(room) * m_format.bytesPerFrame());
    int filled = 0;
    if (m_direct) {
        filled = m_fill(reinterpret_cast<float *>(m_bytes.data()), room);
    } else {
        m_scratch.resize(room * 2);
        filled = m_fill(m_scratch.data(), room);
        drift::writeInterleaved(m_scratch.constData(), filled, m_format, m_bytes.data());
    }
    if (filled <= 0)
        return;

    m_deliveredFrames += filled;
    AAudioStream_write(m_stream, m_bytes.constData(), filled, 0);
#endif
}

void AudioOutputChannel::onNativeStreamError(int error)
{
#ifdef Q_OS_ANDROID
    if (!m_haveSink || !useNativeStream())
        return;
    // Disconnected is how AAudio says the route moved under a stream it could not take along —
    // headphones in or out, a Bluetooth headset connecting. Not a failure, so reopening does not
    // spend the one recovery a start() gets.
    if (error == AAUDIO_ERROR_DISCONNECTED) {
        rebuild();
        return;
    }
    handleSinkError(describeSinkError(QAudio::IOError));
#else
    Q_UNUSED(error);
#endif
}
