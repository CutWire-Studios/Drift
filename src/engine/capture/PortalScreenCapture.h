#pragma once

#include "engine/capture/ScreenCaptureSource.h"

#include <QVariantMap>

#include <memory>

namespace drift {

// Linux screen capture through xdg-desktop-portal's ScreenCast interface and PipeWire.
//
// This is the only way to record the screen on a Wayland session: the compositor will not let an
// application read the screen, or another window's pixels, without the user agreeing through the
// portal's own dialog. The portal backends implement it on X11 sessions too, so there is no
// second Linux path to maintain.
//
// Drift therefore does not choose what to record. start() opens a session, the compositor shows
// its picker, and what the user agreed to comes back as a PipeWire node to read frames from —
// which is why capabilities() reports platformChoosesSource. A UI that offered its own screen
// list here would be asking the user to choose twice and lying about the first answer.
//
// The portal is a four-step conversation, each step answered asynchronously on its own D-Bus
// request object: CreateSession, SelectSources, Start, then OpenPipeWireRemote for the file
// descriptor. onPortalResponse walks that sequence.
class PortalScreenCapture : public ScreenCaptureSource
{
    Q_OBJECT

public:
    explicit PortalScreenCapture(QObject *parent = nullptr);
    ~PortalScreenCapture() override;

    // Whether the portal's ScreenCast interface is on the bus, without starting anything. For UI
    // gating: a session with no portal must disable screen recording rather than offer a button
    // that cannot work.
    static bool available();

    Capabilities capabilities() const override;
    void start(const CaptureRequest &request) override;
    void stop() override;
    bool active() const override;

    // Defined only in the .cpp. Public because PipeWire's listeners are plain C function pointers
    // taking a void* — they cannot be members, and they have to name this type to cast back to it.
    struct Impl;

private slots:
    // Marshals one frame off the PipeWire thread onto this object's own thread.
    void deliverFrame(const QImage &image, qint64 timestampUs);
    void deliverFailure(const QString &message);

private:
    std::unique_ptr<Impl> d;
};

} // namespace drift
