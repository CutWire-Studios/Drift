#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

namespace drift {

// Capturing what the computer is playing, rather than what a microphone hears.
//
// This needs its own interface because Qt cannot reach it: PipeWire and PulseAudio publish a
// monitor for every sink, but Qt filters monitors out of QMediaDevices::audioInputs(), and Windows
// and macOS expose loopback only through their own APIs (WASAPI loopback, ScreenCaptureKit). So
// each platform gets a backend here, the same way screen capture does.
//
// Samples are interleaved float32 at the requested rate and channel count, delivered on the
// object's own thread — exactly what CaptureWriter::writeAudio takes.
class SystemAudioSource : public QObject
{
    Q_OBJECT

public:
    explicit SystemAudioSource(QObject *parent = nullptr) : QObject(parent) {}
    ~SystemAudioSource() override = default;

    virtual bool start(int sampleRate, int channels, QString *errorOut) = 0;
    virtual void stop() = 0;
    virtual bool active() const = 0;

signals:
    void samples(const QVector<float> &interleaved);
    void failed(const QString &message);
};

// The system-audio backend for this platform, or null where there is none compiled in or no
// server to talk to. Null is a real answer: the caller offers the microphone instead and says
// system audio is unavailable, rather than recording a silent track.
std::unique_ptr<SystemAudioSource> makeSystemAudioSource(QObject *parent = nullptr);

} // namespace drift
