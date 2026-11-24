#pragma once

#include "core/base/Time.h"
#include "engine/capture/CaptureRequest.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <memory>

namespace drift {

// One platform's screen capture, delivering frames to whoever is listening. The three
// implementations — xdg-desktop-portal with PipeWire on Linux, Windows.Graphics.Capture, and
// ScreenCaptureKit — differ enough that only this much is shared: start, frames with timestamps,
// stop.
//
// Qt's own QScreenCapture is not one of them, and that is from measurement rather than taste: it
// reports itself active with no error and then delivers zero frames on a Wayland session, XWayland
// included, because it has no Wayland path at all. Since Wayland is what most Linux desktops now
// run, the portal is not a fallback here — it is the Linux implementation.
//
// Everything is asynchronous. The portal in particular hands the choice of what to share to the
// compositor's own picker, so "start" means "begin asking", and the size of the frames is not
// known until started() arrives.
class ScreenCaptureSource : public QObject
{
    Q_OBJECT

public:
    explicit ScreenCaptureSource(QObject *parent = nullptr) : QObject(parent) {}
    ~ScreenCaptureSource() override = default;

    struct Capabilities
    {
        bool screens = false;
        bool windows = false;
        // The platform asks the user what to share through its own dialog and ignores whichever
        // screen or window the caller nominated. Wayland works this way by design — an
        // application cannot enumerate or grab another's window without consent — so a UI must
        // not present its own picker here, or the user picks twice and the first choice is a lie.
        bool platformChoosesSource = false;
        // The platform can be asked to leave the pointer out of the recording.
        bool cursorOptional = false;
    };

    virtual Capabilities capabilities() const = 0;

    // Begins capturing. May show the platform's own picker, so this can take as long as the user
    // does. Exactly one of started() or failed() follows.
    virtual void start(const CaptureRequest &request) = 0;
    // Stops capturing and releases the platform's session. stopped() follows unless the source
    // never started.
    virtual void stop() = 0;
    virtual bool active() const = 0;

signals:
    // Capture is running and frames are about to arrive at this size.
    void started(const QSize &size);
    // `timestampUs` is measured from the first frame, so a recording always begins at zero no
    // matter what clock the platform counts from.
    void frame(const QImage &image, drift::TimeUs timestampUs);
    void failed(const QString &message);
    void stopped();
};

// The capture source for this platform, or null where there is none compiled in. Null is a real
// answer, not an error: a build without the portal, or on a platform whose backend is not written
// yet, should disable screen recording in its UI rather than offer a button that cannot work.
std::unique_ptr<ScreenCaptureSource> makeScreenCaptureSource(QObject *parent = nullptr);

// The camera source, or null when no camera is connected. Separate from the screen factory
// because the two answer different questions: a machine can have one and not the other.
std::unique_ptr<ScreenCaptureSource> makeCameraCaptureSource(QObject *parent = nullptr);

} // namespace drift
