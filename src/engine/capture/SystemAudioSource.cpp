#include "engine/capture/SystemAudioSource.h"

#if defined(Q_OS_LINUX) && defined(DRIFT_WITH_PORTAL_CAPTURE)
#include "engine/capture/PipeWireAudioSource.h"
#endif

namespace drift {

std::unique_ptr<SystemAudioSource> makeSystemAudioSource(QObject *parent)
{
#if defined(Q_OS_LINUX) && defined(DRIFT_WITH_PORTAL_CAPTURE)
    // Gated on a server answering, not on the platform: the library can be installed with nothing
    // running, which is the case in a container.
    if (PipeWireAudioSource::available())
        return std::make_unique<PipeWireAudioSource>(parent);
#else
    Q_UNUSED(parent);
#endif
    // Windows (WASAPI loopback) and macOS (ScreenCaptureKit audio) still to come.
    return nullptr;
}

} // namespace drift
