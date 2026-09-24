#pragma once

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QIODevice>
#include <QMediaDevices>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <vector>

#include "AudioFileWriter.h"
#include "core/Time.h"

namespace drift {

class AudioRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY recordingStateChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY pausedChanged)
    Q_PROPERTY(int recordingTrackIndex READ recordingTrackIndex NOTIFY recordingStateChanged)
    Q_PROPERTY(float audioLevel READ audioLevel NOTIFY audioLevelChanged)
    Q_PROPERTY(float gain READ gain WRITE setGain NOTIFY gainChanged)
    Q_PROPERTY(double recordedSeconds READ recordedSeconds NOTIFY recordedSecondsChanged)
    Q_PROPERTY(QVariantList livePeaks READ livePeaks NOTIFY livePeaksChanged)
    Q_PROPERTY(QVariantList availableDevices READ availableDevices NOTIFY availableDevicesChanged)
    Q_PROPERTY(QString currentDeviceName READ currentDeviceName NOTIFY currentDeviceChanged)

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    bool isRecording() const { return m_recording; }
    bool isPaused() const { return m_paused; }
    int recordingTrackIndex() const { return m_recordingTrackIndex; }
    float audioLevel() const { return m_audioLevel; }
    float gain() const { return m_gain; }
    void setGain(float gain);
    double recordedSeconds() const { return m_recordedSeconds; }
    QVariantList livePeaks() const;
    QVariantList availableDevices() const;
    QString currentDeviceName() const;

    Q_INVOKABLE void selectDevice(const QString &id);
    Q_INVOKABLE bool startRecording(int trackIndex, const QString &outputPath, QString *errorOut = nullptr);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE QString stopRecording(drift::TimeUs *recordedDurationUsOut = nullptr);
    Q_INVOKABLE void cancelRecording();

signals:
    void recordingStateChanged();
    void pausedChanged(bool paused);
    void gainChanged(float gain);
    void audioLevelChanged(float level);
    void recordedSecondsChanged(double seconds);
    void livePeaksChanged();
    void availableDevicesChanged();
    void currentDeviceChanged();
    void recordingError(const QString &error);

private slots:
    void onReadyRead();
    void onDevicesChanged();

private:
    static QAudioFormat negotiateFormat(const QAudioDevice &device);

    QMediaDevices *m_mediaDevices = nullptr;
    QAudioDevice m_selectedDevice;
    std::unique_ptr<QAudioSource> m_audioSource;
    QIODevice *m_inputIo = nullptr;
    drift::AudioFileWriter m_writer;

    bool m_recording = false;
    bool m_paused = false;
    int m_recordingTrackIndex = -1;
    QString m_outputPath;
    float m_audioLevel = 0.0f;
    float m_gain = 1.0f;
    double m_recordedSeconds = 0.0;
    qint64 m_totalFramesWritten = 0;
    int m_sampleRate = 48000;
    QAudioFormat m_captureFormat;

    std::vector<float> m_livePeaks;
    int m_peakSampleCounter = 0;
    float m_currentBucketPeak = 0.0f;
};

} // namespace drift
