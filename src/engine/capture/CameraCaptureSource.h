#pragma once

#include "engine/capture/ScreenCaptureSource.h"

#include <memory>

namespace drift {

// Webcam recording, as a ScreenCaptureSource so it runs through the same encoder as the screen.
//
// Qt's QCamera does deliver frames on every platform Drift targets, unlike its screen capture. But
// handing the camera to QMediaRecorder instead of encoding the frames here would give up what the
// shared writer guarantees: left to choose, Qt's backend encodes High 4:4:4 10-bit and the muxer
// complains about packet durations the whole way through. Reading frames from a video sink and
// writing them with everything else keeps one codec and one notion of time for both kinds of
// recording.
class CameraCaptureSource : public ScreenCaptureSource
{
    Q_OBJECT

public:
    explicit CameraCaptureSource(QObject *parent = nullptr);
    ~CameraCaptureSource() override;

    // Whether any camera is connected.
    static bool available();

    Capabilities capabilities() const override;
    void start(const CaptureRequest &request) override;
    void stop() override;
    bool active() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace drift
