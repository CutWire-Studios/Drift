#include "AudioRecorder.h"

#include <algorithm>
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

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent)
    , m_mediaDevices(new QMediaDevices(this))
{
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, this, &AudioRecorder::onDevicesChanged);
}

AudioRecorder::~AudioRecorder()
{
    if (m_recording)
        cancelRecording();
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

void AudioRecorder::selectDevice(const QString &id)
{
    const QList<QAudioDevice> inputs = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : inputs) {
        if (QString::fromUtf8(device.id()) == id) {
            m_selectedDevice = device;
            emit currentDeviceChanged();
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

    m_captureFormat = negotiateFormat(dev);
    if (!m_captureFormat.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Failed to negotiate supported audio format");
        emit recordingError(*errorOut);
        return false;
    }

    m_sampleRate = m_captureFormat.sampleRate();
    m_outputPath = outputPath;

    QString error;
    if (!m_writer.open(m_outputPath, m_sampleRate, 2, &error)) {
        if (errorOut)
            *errorOut = error;
        emit recordingError(error);
        return false;
    }

    m_audioSource = std::make_unique<QAudioSource>(dev, m_captureFormat, this);
    m_inputIo = m_audioSource->start();
    if (!m_inputIo) {
        m_writer.abort();
        m_audioSource.reset();
        if (errorOut)
            *errorOut = QStringLiteral("Failed to start audio input stream");
        emit recordingError(*errorOut);
        return false;
    }

    connect(m_inputIo, &QIODevice::readyRead, this, &AudioRecorder::onReadyRead);

    m_recording = true;
    m_recordingTrackIndex = trackIndex;
    m_totalFramesWritten = 0;
    m_recordedSeconds = 0.0;
    m_audioLevel = 0.0f;

    emit recordingStateChanged();
    emit recordedSecondsChanged(0.0);
    emit audioLevelChanged(0.0f);
    return true;
}

void AudioRecorder::onReadyRead()
{
    if (!m_recording || !m_inputIo)
        return;

    const QByteArray bytes = m_inputIo->readAll();
    if (bytes.isEmpty())
        return;

    std::vector<float> stereoPcm;
    unpackToStereoFloat(bytes.constData(), bytes.size(), m_captureFormat, stereoPcm);
    if (stereoPcm.empty())
        return;

    const int frames = static_cast<int>(stereoPcm.size() / 2);

    float peak = 0.0f;
    for (int i = 0; i < frames; ++i) {
        peak = std::max(peak, std::abs(stereoPcm[i * 2]));
        peak = std::max(peak, std::abs(stereoPcm[i * 2 + 1]));
    }
    // Exponential smoothing for a responsive, readable meter
    m_audioLevel = std::max(peak, m_audioLevel * 0.75f);
    emit audioLevelChanged(m_audioLevel);

    QString error;
    if (!m_writer.writeFrames(stereoPcm.data(), frames, &error)) {
        qWarning("AudioRecorder: writeFrames failed: %s", qPrintable(error));
        emit recordingError(error);
        cancelRecording();
        return;
    }

    m_totalFramesWritten += frames;
    m_recordedSeconds = double(m_totalFramesWritten) / double(m_sampleRate);
    emit recordedSecondsChanged(m_recordedSeconds);
}

QString AudioRecorder::stopRecording(drift::TimeUs *recordedDurationUsOut)
{
    if (!m_recording)
        return {};

    m_recording = false;
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
    m_inputIo = nullptr;

    QString error;
    if (!m_writer.finish(&error)) {
        qWarning("AudioRecorder: finish failed: %s", qPrintable(error));
        m_writer.abort();
        emit recordingError(error);
        emit recordingStateChanged();
        return {};
    }

    const drift::TimeUs durUs = drift::secondsToUs(m_recordedSeconds);
    if (recordedDurationUsOut)
        *recordedDurationUsOut = durUs;

    const QString finalPath = m_outputPath;
    m_recordingTrackIndex = -1;
    m_audioLevel = 0.0f;
    emit audioLevelChanged(0.0f);
    emit recordingStateChanged();

    return finalPath;
}

void AudioRecorder::cancelRecording()
{
    if (!m_recording)
        return;

    m_recording = false;
    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset();
    }
    m_inputIo = nullptr;

    m_writer.abort();
    m_recordingTrackIndex = -1;
    m_audioLevel = 0.0f;
    m_recordedSeconds = 0.0;
    emit audioLevelChanged(0.0f);
    emit recordingStateChanged();
}

} // namespace drift
