#include "engine/capture/ScreenCaptureSource.h"

#include "engine/capture/CameraCaptureSource.h"

#if defined(Q_OS_LINUX) && defined(DRIFT_WITH_PORTAL_CAPTURE)
#include "engine/capture/PortalScreenCapture.h"
#endif

namespace drift {

std::unique_ptr<ScreenCaptureSource> makeCameraCaptureSource(QObject *parent)
{
    if (!CameraCaptureSource::available())
        return nullptr;
    return std::make_unique<CameraCaptureSource>(parent);
}

std::unique_ptr<ScreenCaptureSource> makeScreenCaptureSource(QObject *parent)
{
#if defined(Q_OS_LINUX) && defined(DRIFT_WITH_PORTAL_CAPTURE)
    // Gated on the portal really answering rather than on the platform: a container or a bare
    // session without xdg-desktop-portal has to disable screen recording, not offer it and fail.
    if (PortalScreenCapture::available())
        return std::make_unique<PortalScreenCapture>(parent);
#else
    Q_UNUSED(parent);
#endif
    // Windows (Windows.Graphics.Capture) and macOS (ScreenCaptureKit) still to come. Null is the
    // honest answer until then — see makeScreenCaptureSource's contract.
    return nullptr;
}

} // namespace drift
