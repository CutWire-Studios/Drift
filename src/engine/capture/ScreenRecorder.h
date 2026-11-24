#pragma once

#include "engine/capture/CaptureRequest.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace drift {

// Screen, window and camera recording on Qt Multimedia's capture session.
//
// One instance records one file. A screen capture and a simultaneous webcam capture are therefore
// two instances writing two files, which is both what the session API allows — it takes a single
// video source — and what an editor wants: the camera ends up on its own track, free to be moved,
// scaled and cut against the screen.
//
// Audio is likewise one input per recording, because a capture session holds one QAudioInput.
// Where a platform publishes a loopback or monitor device (PulseAudio and PipeWire both do) that
// device is simply an input in the list, so "record the computer's sound" is a device choice
// rather than a separate code path. Recording a microphone *and* system audio at once needs two
// recordings — in practice the screen takes one and the camera the other.
//
// Qt's capture backends must be driven from the GUI thread, so every method here is for the GUI
// thread only. Nothing blocks: starting is asynchronous and reported through the signals.
class ScreenRecorder : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(double recordedSeconds READ recordedSeconds NOTIFY recordedSecondsChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputPathChanged)

public:
    explicit ScreenRecorder(QObject *parent = nullptr);
    ~ScreenRecorder() override;

    // Whether this build can capture the screen at all. False on a platform or session where Qt
    // has no capture backend — a bare Wayland session without the portal, for one — so the UI can
    // say so instead of offering a button that fails.
    static bool screenCaptureAvailable();
    static bool cameraAvailable();

    // [{name, description, primary, width, height}] for the connected screens, in Qt's order.
    static QVariantList screens();
    // [{id, title}] for the capturable top-level windows. The id is a QCapturableWindow handle,
    // valid only for this process and only until the window closes, so it is re-read each time
    // the picker opens.
    static QVariantList windows();
    // [{id, description, isDefault}] for the cameras.
    static QVariantList cameras();
    // [{id, description, isDefault, loopback}] for the audio inputs. `loopback` marks a monitor
    // or loopback device, i.e. one that hears what the computer is playing rather than a
    // microphone — that is what a "record system sound" switch selects.
    static QVariantList audioInputs();

    bool recording() const;
    bool paused() const;
    double recordedSeconds() const;
    QString outputPath() const;

    // Begins recording `request`. False, with `errorOut` set, when the request cannot be honoured
    // at all; a failure that only surfaces once the backend starts arrives through error() instead.
    bool start(const CaptureRequest &request, QString *errorOut = nullptr);
    void pause();
    void resume();
    // Ends the recording. The file is finalized asynchronously: finished() carries the path Qt
    // actually wrote, which is not always the one that was asked for, since a backend may replace
    // the container suffix with one it can mux.
    void stop();
    // Ends the recording and deletes the file.
    void cancel();

signals:
    void recordingChanged();
    void pausedChanged();
    void recordedSecondsChanged();
    void outputPathChanged();
    // The file is written and closed. `path` is where it really landed.
    void finished(const QString &path);
    void error(const QString &message);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
