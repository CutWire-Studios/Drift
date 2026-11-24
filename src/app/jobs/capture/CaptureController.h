#pragma once

#include "engine/capture/CaptureRequest.h"

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

class AppController;

// Screen and camera recording, reached from QML as EditorState.capture.
//
// One recording at a time. A screen capture and a webcam capture running together would be two of
// these; that is deliberately not offered yet, because the second one needs its own audio device
// and its own file and there is nowhere in the UI to say so.
//
// What comes out lands in the media bin rather than straight on the timeline: a recording is
// source footage, and the user almost always wants to trim the first few seconds — the ones spent
// reaching for the mouse — before placing it.
class CaptureController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(double recordedSeconds READ recordedSeconds NOTIFY recordedSecondsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    explicit CaptureController(AppController *app, QObject *parent = nullptr);
    ~CaptureController() override;

    bool recording() const;
    double recordedSeconds() const;
    QString status() const;

    // Whether this machine can record the screen at all. False where there is no capture backend
    // — a Linux build without PipeWire, a Wayland session with no portal, or a platform whose
    // backend is not written yet — and the UI must hide the button rather than offer it.
    Q_INVOKABLE bool screenAvailable() const;
    Q_INVOKABLE bool cameraAvailable() const;

    // True when the platform shows its own share picker and ignores any screen the app nominated.
    // A UI that offered its own screen list here would make the user choose twice and honour only
    // the second answer, so it must show one "Choose what to share…" action instead.
    Q_INVOKABLE bool platformChoosesScreen() const;

    Q_INVOKABLE QVariantList cameras() const;
    Q_INVOKABLE QVariantList audioInputs() const;
    // The audio input that records the computer's own sound, or empty when the platform lists
    // none — which is the usual answer. See CaptureDevices::audioInputs.
    Q_INVOKABLE QString systemAudioInput() const;

    // Starts recording. `audioInputId` empty records no sound. False, with a message on the app's
    // status line, when the recording could not be started at all; a failure that only surfaces
    // once the platform answers arrives through failed() instead.
    Q_INVOKABLE bool recordScreen(const QString &audioInputId = QString(), int fps = 30);
    Q_INVOKABLE bool recordCamera(const QString &cameraId = QString(),
                                  const QString &audioInputId = QString(), int fps = 30);

    // Stops and keeps the recording, importing it into the bin.
    Q_INVOKABLE void stop();
    // Stops and deletes it.
    Q_INVOKABLE void cancel();

signals:
    void recordingChanged();
    void recordedSecondsChanged();
    void statusChanged();
    // The file is written and has been handed to the bin.
    void finished(const QString &path);
    void failed(const QString &message);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
