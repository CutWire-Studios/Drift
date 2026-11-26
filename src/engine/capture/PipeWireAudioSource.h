#pragma once

#include "engine/capture/SystemAudioSource.h"

#include <QString>
#include <QVector>

#include <memory>

namespace drift {

// Captures what the computer is playing, on Linux, by reading the default sink's monitor straight
// from PipeWire.
//
// Qt cannot do this: PipeWire and PulseAudio both publish a monitor for every sink, but Qt filters
// monitors out of QMediaDevices::audioInputs(), so there is no device for a capture session to
// attach to. Going to PipeWire directly also avoids the portal — recording the machine's own
// output needs no consent dialog, unlike recording its screen.
//
// Samples arrive as interleaved float32 at the requested rate and channel count, on the thread
// this object lives on, which is what CaptureWriter::writeAudio takes.
class PipeWireAudioSource : public SystemAudioSource
{
    Q_OBJECT

public:
    explicit PipeWireAudioSource(QObject *parent = nullptr);
    ~PipeWireAudioSource() override;

    // Whether a PipeWire server can be reached at all, without starting a stream. For UI gating.
    static bool available();

    // Begins capturing the default sink's output. False with `errorOut` set when the stream could
    // not be created; a server that goes away afterwards arrives as failed().
    bool start(int sampleRate, int channels, QString *errorOut) override;
    void stop() override;
    bool active() const override;

private slots:
    void deliver(const QVector<float> &interleaved);
    void deliverFailure(const QString &message);

public:
    // Defined in the .cpp. Public because PipeWire's listeners are C function pointers taking a
    // void*, so they cannot be members and must name this type to cast back to it.
    struct Impl;

private:
    std::unique_ptr<Impl> d;
};

} // namespace drift
