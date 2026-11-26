#pragma once

#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

namespace drift {

// What one recording captures. Region is a screen capture that gets cropped, because no platform
// offers a native rectangle grab — see normalizeCaptureRegion for what that crop has to satisfy.
enum class CaptureSource { Screen, Window, Region, Camera };

QString captureSourceToString(CaptureSource source); // "screen" | "window" | "region" | "camera"
std::optional<CaptureSource> captureSourceFromString(const QString &text);

// One recording's settings. A screen capture and its webcam companion are two of these, recorded
// at the same time into two files, because a capture session records one video source: keeping
// them separate is also what lets the camera be moved, scaled and cut independently afterwards.
struct CaptureRequest
{
    CaptureSource source = CaptureSource::Screen;
    QString screenName; // Screen/Region: QScreen::name(); empty means the primary screen
    // Window: Qt's QCapturableWindow publishes no stable handle — only a description — so a
    // window is addressed by its place in ScreenRecorder::windows() and confirmed by the title it
    // carried when the user picked it. The list is re-read at start, and a title that no longer
    // matches is searched for by name, because windows open and close between picking and
    // recording and the index alone would silently capture the wrong one.
    int windowIndex = -1;
    QString windowTitle;
    QRect region;       // Region: in the chosen screen's device-independent pixels
    QString cameraId;     // Camera: QCameraDevice::id(); empty means the default camera
    QString audioInputId; // empty means no microphone
    bool systemAudio = false;
    int fps = 30;
    QString outputPath;
};

// Frame rates an encoder and a capture backend will both agree to. Below 1 nothing advances; above
// 120 the compositor cannot keep up with a full-screen grab on any machine this runs on, and the
// file grows without anything to show for it.
int clampCaptureFps(int fps);

// A region an encoder will accept, or nullopt when there is nothing usable left.
//
// Intersected with the screen first: a drag that started on one monitor and ended past its edge
// otherwise asks the backend for pixels that do not exist. Then width and height are rounded
// *down* to even numbers, because H.264's chroma planes are half resolution and an odd dimension
// is rejected outright by most encoders — rounding down rather than up so the result can never
// grow back outside the screen. Finally anything thinner than `minimum` on either side is refused:
// a stray click produces a 1x2 rectangle, and silently recording that is worse than saying no.
std::optional<QRect> normalizeCaptureRegion(const QRect &region, const QSize &screen,
                                            int minimum = 16);

// Why this request cannot be recorded, in words a user can act on, or empty when it can be.
// Checks only what is knowable without touching a device, so it is safe to call from the GUI.
QString captureRequestProblem(const CaptureRequest &request);

} // namespace drift
