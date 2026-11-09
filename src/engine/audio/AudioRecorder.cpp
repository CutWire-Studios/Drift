#include "engine/audio/AudioRecorder.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace drift {

namespace {

void unpackToStereoFloat(const char *srcBytes, qint64 byteCount, const QAudioFormat &format,
                         std::vector<float> &outStereo)
{
    const int channels = format.channelCount();
    const int bytesPerSample = format.bytesPerSample();
    if (byteCount <= 0 || channels <= 0 || bytesPerSample <= 0)
        return;

    const int totalSamples = static_cast<int>(byteCount / bytesPerSample);
    const int frames = totalSamples / channels;
    if (frames <= 0)
        return;

    outStereo.resize(static_cast<size_t>(frames * 2));

    switch (format.sampleFormat()) {
    case QAudioFormat::Float: {
        const auto *src = reinterpret_cast<const float *>(srcBytes);
        for (int f = 0; f < frames; ++f) {
            float left = src[f * channels];
            float right = channels > 1 ? src[f * channels + 1] : left;
            outStereo[f * 2] = left;
            outStereo[f * 2 + 1] = right;
        }
        break;
    }
    case QAudioFormat::Int16: {
        const auto *src = reinterpret_cast<const int16_t *>(srcBytes);
        constexpr float kScale = 1.0f / 32768.0f;
        for (int f = 0; f < frames; ++f) {
            float left = static_cast<float>(src[f * channels]) * kScale;
            float right = channels > 1 ? static_cast<float>(src[f * channels + 1]) * kScale : left;
            outStereo[f * 2] = left;
            outStereo[f * 2 + 1] = right;
        }
        break;
    }
    case QAudioFormat::Int32: {
        const auto *src = reinterpret_cast<const int32_t *>(srcBytes);
        constexpr float kScale = 1.0f / 2147483648.0f;
        for (int f = 0; f < frames; ++f) {
            float left = static_cast<float>(src[f * channels]) * kScale;
            float right = channels > 1 ? static_cast<float>(src[f * channels + 1]) * kScale : left;
            outStereo[f * 2] = left;
            outStereo[f * 2 + 1] = right;
        }
        break;
    }
    case QAudioFormat::UInt8: {
        const auto *src = reinterpret_cast<const uint8_t *>(srcBytes);
        constexpr float kScale = 1.0f / 128.0f;
        for (int f = 0; f < frames; ++f) {
            float left = (static_cast<float>(src[f * channels]) - 128.0f) * kScale;
            float right = channels > 1 ? (static_cast<float>(src[f * channels + 1]) - 128.0f) * kScale : left;
            outStereo[f * 2] = left;
            outStereo[f * 2 + 1] = right;
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

// Capture, conversion and FLAC encoding run on their own thread so a stalled GUI thread cannot
// overflow the device buffer and drop samples. Every member except `gain` is touched only there.
class AudioRecorder::Worker : public QObject
{
public:
    explicit Worker(AudioRecorder *owner)
        : m_owner(owner)
    {
    }

    std::atomic<float> gain{1.0f};

    bool start(const QAudioDevice &device, const QAudioFormat &format, const QString &path, int session,
               QString *error)
    {
        if (!m_writer.open(path, format.sampleRate(), 2, error))
            return false;

        m_format = format;
        m_sampleRate = format.sampleRate();
        m_session = session;
        m_paused = false;
        m_failed = false;
        m_failError.clear();
        m_totalFrames = 0;
        m_peakCounter = 0;
        m_bucketPeak = 0.0f;
        m_pendingPeak = 0.0f;
        m_pendingPeaks.clear();

        if (!startSource(device)) {
            m_writer.abort();
            *error = QStringLiteral("Failed to start audio input stream");
            return false;
        }
        m_postTimer.start();
        return true;
    }

    void switchDevice(const QAudioDevice &device)
    {
        m_source.reset();
        m_io = nullptr;
        m_format = negotiateFormat(device);
        if (m_format.isValid() && startSource(device) && m_paused)
            m_source->suspend();
    }

    void setPaused(bool paused)
    {
        m_paused = paused;
        if (!m_source)
            return;
        if (paused) {
            m_source->suspend();
            postUpdate(true);
        } else {
            m_source->resume();
        }
    }

    bool stop(qint64 *frames, QString *error)
    {
        if (m_source) {
            if (!m_paused)
                drain();
            m_source->stop();
            m_source.reset();
            m_io = nullptr;
        }
        *frames = m_totalFrames;
        if (m_failed) {
            m_writer.abort();
            *error = m_failError;
            return false;
        }
        if (!m_writer.finish(error)) {
            m_writer.abort();
            return false;
        }
        return true;
    }

    void cancel()
    {
        if (m_source) {
            m_source->stop();
            m_source.reset();
        }
        m_io = nullptr;
        m_writer.abort();
    }

private:
    bool startSource(const QAudioDevice &device)
    {
        m_source = std::make_unique<QAudioSource>(device, m_format);
        m_source->setBufferSize(m_format.bytesForDuration(500000));
        m_io = m_source->start();
        if (!m_io) {
            m_source.reset();
            return false;
        }
        connect(m_io, &QIODevice::readyRead, this, [this] {
            drain();
            postUpdate(false);
        });
        return true;
    }

    void drain()
    {
        if (!m_io || m_paused || m_failed)
            return;

        const QByteArray bytes = m_io->readAll();
        if (bytes.isEmpty())
            return;

        std::vector<float> stereoPcm;
        unpackToStereoFloat(bytes.constData(), bytes.size(), m_format, stereoPcm);
        if (stereoPcm.empty())
            return;

        const float g = gain.load(std::memory_order_relaxed);
        if (g != 1.0f) {
            for (float &s : stereoPcm) {
                s = std::clamp(s * g, -1.0f, 1.0f);
            }
        }

        const int frames = static_cast<int>(stereoPcm.size() / 2);
        const int samplesPerPeak = std::max(1, m_sampleRate / 50);

        for (int i = 0; i < frames; ++i) {
            const float sampleL = std::abs(stereoPcm[i * 2]);
            const float sampleR = std::abs(stereoPcm[i * 2 + 1]);
            const float framePeak = std::max(sampleL, sampleR);
            m_pendingPeak = std::max(m_pendingPeak, framePeak);

            m_bucketPeak = std::max(m_bucketPeak, framePeak);
            m_peakCounter++;
            if (m_peakCounter >= samplesPerPeak) {
                m_pendingPeaks.push_back(m_bucketPeak);
                m_bucketPeak = 0.0f;
                m_peakCounter = 0;
            }
        }

        QString error;
        if (!m_writer.writeFrames(stereoPcm.data(), frames, &error)) {
            qWarning("AudioRecorder: writeFrames failed: %s", qPrintable(error));
            m_failed = true;
            m_failError = error;
            QMetaObject::invokeMethod(
                m_owner,
                [o = m_owner, s = m_session, error] {
                    if (s != o->m_session || !o->m_recording)
                        return;
                    emit o->recordingError(error);
                    o->cancelRecording();
                },
                Qt::QueuedConnection);
            return;
        }
        m_totalFrames += frames;
    }

    void postUpdate(bool force)
    {
        if (m_failed || (!force && !m_postTimer.hasExpired(33)))
            return;
        m_postTimer.restart();
        QMetaObject::invokeMethod(
            m_owner,
            [o = m_owner, s = m_session, f = m_totalFrames, p = m_pendingPeak, v = m_pendingPeaks] {
                o->applyLiveUpdate(s, f, p, v);
            },
            Qt::QueuedConnection);
        m_pendingPeak = 0.0f;
        m_pendingPeaks.clear();
    }

    AudioRecorder *m_owner;
    std::unique_ptr<QAudioSource> m_source;
    QIODevice *m_io = nullptr;
    drift::AudioFileWriter m_writer;
    QAudioFormat m_format;
    int m_sampleRate = 48000;
    int m_session = 0;
    bool m_paused = false;
    bool m_failed = false;
    QString m_failError;
    qint64 m_totalFrames = 0;
    int m_peakCounter = 0;
    float m_bucketPeak = 0.0f;
    float m_pendingPeak = 0.0f;
    std::vector<float> m_pendingPeaks;
    QElapsedTimer m_postTimer;
};

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent)
    , m_mediaDevices(new QMediaDevices(this))
    , m_thread(new QThread(this))
    , m_worker(new Worker(this))
{
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, &AudioRecorder::onDevicesChanged);

    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

AudioRecorder::~AudioRecorder()
{
    if (m_recording)
        cancelRecording();
    m_thread->quit();
    m_thread->wait();
}

QVariantList AudioRecorder::availableDevices() const
{
    QVariantList list;
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    const QAudioDevice def = QMediaDevices::defaultAudioInput();

    for (const QAudioDevice &device : inputs) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), QString::fromUtf8(device.id()));
        map.insert(QStringLiteral("name"), device.description());
        map.insert(QStringLiteral("isDefault"), device.id() == def.id());
        list.append(map);
    }
    return list;
}

QString AudioRecorder::currentDeviceName() const
{
    if (!m_selectedDevice.isNull())
        return m_selectedDevice.description();
    const QAudioDevice def = QMediaDevices::defaultAudioInput();
    return def.isNull() ? QString() : def.description();
}

void AudioRecorder::setGain(float gain)
{
    const float clamped = std::clamp(gain, 0.0f, 3.0f);
    if (qFuzzyCompare(m_gain, clamped))
        return;
    m_gain = clamped;
    m_worker->gain.store(clamped, std::memory_order_relaxed);
    emit gainChanged(m_gain);
}

QVariantList AudioRecorder::livePeakColumns(double first, double count, int columns) const
{
    QVariantList list;
    const qsizetype total = static_cast<qsizetype>(m_livePeaks.size());
    if (columns <= 0 || total == 0) {
        return list;
    }
    list.reserve(columns);
    for (int x = 0; x < columns; ++x) {
        const qsizetype i0 = static_cast<qsizetype>(std::floor(first + x * count / columns));
        qsizetype i1 = static_cast<qsizetype>(std::floor(first + (x + 1) * count / columns));
        if (i1 <= i0) {
            i1 = std::min(total, i0 + 1);
        }
        float peak = 0.0f;
        for (qsizetype i = std::max<qsizetype>(i0, 0); i < i1 && i < total; ++i) {
            peak = std::max(peak, m_livePeaks[static_cast<size_t>(i)]);
        }
        list.append(peak);
    }
    return list;
}

void AudioRecorder::selectDevice(const QString &id)
{
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : inputs) {
        if (QString::fromUtf8(device.id()) == id || device.description() == id) {
            if (m_selectedDevice == device)
                return;
            m_selectedDevice = device;
            emit currentDeviceChanged();

            if (m_recording) {
                QMetaObject::invokeMethod(
                    m_worker, [this, device] { m_worker->switchDevice(device); }, Qt::BlockingQueuedConnection);
            }
            return;
        }
    }
    m_selectedDevice = QAudioDevice();
    emit currentDeviceChanged();
}

void AudioRecorder::onDevicesChanged()
{
    emit availableDevicesChanged();
    emit currentDeviceChanged();
}

QAudioFormat AudioRecorder::negotiateFormat(const QAudioDevice &device)
{
    if (device.isNull())
        return QAudioFormat();

    // Prefer 48000 Hz stereo or mono Float
    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Float);
    if (device.isFormatSupported(format))
        return format;

    format.setChannelCount(1);
    if (device.isFormatSupported(format))
        return format;

    // Try Int16 at 48000 Hz
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    if (device.isFormatSupported(format))
        return format;

    format.setChannelCount(1);
    if (device.isFormatSupported(format))
        return format;

    // Fallback to preferredFormat
    return device.preferredFormat();
}

bool AudioRecorder::startRecording(int trackIndex, const QString &outputPath, QString *errorOut)
{
    if (m_recording) {
        if (errorOut)
            *errorOut = QStringLiteral("Recording already in progress");
        return false;
    }

    QAudioDevice dev = m_selectedDevice.isNull() ? QMediaDevices::defaultAudioInput() : m_selectedDevice;
    if (dev.isNull()) {
        if (errorOut)
            *errorOut = QStringLiteral("No audio input device available");
        emit recordingError(*errorOut);
        return false;
    }

    const QAudioFormat format = negotiateFormat(dev);
    if (!format.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to negotiate supported audio format");
        emit recordingError(*errorOut);
        return false;
    }

    m_sampleRate = format.sampleRate();
    m_outputPath = outputPath;
    ++m_session;

    QString error;
    bool started = false;
    m_worker->gain.store(m_gain, std::memory_order_relaxed);
    QMetaObject::invokeMethod(
        m_worker,
        [&] { started = m_worker->start(dev, format, m_outputPath, m_session, &error); },
        Qt::BlockingQueuedConnection);
    if (!started) {
        if (errorOut)
            *errorOut = error;
        emit recordingError(error);
        return false;
    }

    m_recording = true;
    m_paused = false;
    m_recordingTrackIndex = trackIndex;
    m_recordedSeconds = 0.0;
    m_audioLevel = 0.0f;
    m_livePeaks.clear();

    emit recordingStateChanged();
    emit pausedChanged(false);
    emit recordedSecondsChanged(0.0);
    emit audioLevelChanged(0.0f);
    emit livePeaksChanged();
    return true;
}

void AudioRecorder::pause()
{
    if (!m_recording || m_paused)
        return;

    m_paused = true;
    QMetaObject::invokeMethod(m_worker, [this] { m_worker->setPaused(true); }, Qt::BlockingQueuedConnection);

    m_audioLevel = 0.0f;
    emit audioLevelChanged(0.0f);
    emit pausedChanged(true);
}

void AudioRecorder::resume()
{
    if (!m_recording || !m_paused)
        return;

    m_paused = false;
    QMetaObject::invokeMethod(m_worker, [this] { m_worker->setPaused(false); }, Qt::BlockingQueuedConnection);

    emit pausedChanged(false);
}

void AudioRecorder::applyLiveUpdate(int session, qint64 frames, float peak, const std::vector<float> &peaks)
{
    if (session != m_session || !m_recording)
        return;

    m_livePeaks.insert(m_livePeaks.end(), peaks.begin(), peaks.end());
    m_recordedSeconds = double(frames) / double(m_sampleRate);
    if (!m_paused) {
        // Exponential smoothing for a responsive, readable meter
        m_audioLevel = std::max(peak, m_audioLevel * 0.75f);
        emit audioLevelChanged(m_audioLevel);
    }
    emit recordedSecondsChanged(m_recordedSeconds);
    emit livePeaksChanged();
}

QString AudioRecorder::stopRecording(drift::TimeUs *recordedDurationUsOut)
{
    if (!m_recording)
        return {};

    m_recording = false;
    m_paused = false;
    ++m_session;

    QString error;
    qint64 frames = 0;
    bool ok = false;
    QMetaObject::invokeMethod(m_worker, [&] { ok = m_worker->stop(&frames, &error); }, Qt::BlockingQueuedConnection);
    if (!ok) {
        qWarning("AudioRecorder: finish failed: %s", qPrintable(error));
        emit recordingError(error);
        emit recordingStateChanged();
        emit pausedChanged(false);
        return {};
    }

    m_recordedSeconds = double(frames) / double(m_sampleRate);
    const drift::TimeUs durUs = drift::secondsToUs(m_recordedSeconds);
    if (recordedDurationUsOut)
        *recordedDurationUsOut = durUs;

    const QString finalPath = m_outputPath;
    m_recordingTrackIndex = -1;
    m_audioLevel = 0.0f;
    m_livePeaks.clear();
    emit audioLevelChanged(0.0f);
    emit pausedChanged(false);
    emit livePeaksChanged();
    emit recordingStateChanged();

    return finalPath;
}

void AudioRecorder::cancelRecording()
{
    if (!m_recording)
        return;

    m_recording = false;
    m_paused = false;
    ++m_session;
    QMetaObject::invokeMethod(m_worker, [this] { m_worker->cancel(); }, Qt::BlockingQueuedConnection);
    m_recordingTrackIndex = -1;
    m_audioLevel = 0.0f;
    m_recordedSeconds = 0.0;
    m_livePeaks.clear();
    emit audioLevelChanged(0.0f);
    emit pausedChanged(false);
    emit livePeaksChanged();
    emit recordingStateChanged();
}

} // namespace drift

#include "moc_AudioRecorder.cpp"
