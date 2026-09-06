#include "engine/GpuStatus.h"

#include <QLatin1StringView>

namespace drift::gl {

const char *statusId(GlStatus status)
{
    switch (status) {
    case GlStatus::NotAttempted:
        return "unknown";
    case GlStatus::Ready:
        return "ready";
    case GlStatus::NoApplication:
        return "no-application";
    case GlStatus::NoShareContext:
        return "no-share-context";
    case GlStatus::SurfaceFailed:
        return "surface-failed";
    case GlStatus::ContextFailed:
        return "context-failed";
    case GlStatus::MakeCurrentFailed:
        return "make-current-failed";
    case GlStatus::NoFunctions:
        return "no-functions";
    case GlStatus::VersionTooLow:
        return "version-too-low";
    case GlStatus::ShaderLinkFailed:
        return "shader-failed";
    }
    return "unknown";
}

bool isTransient(GlStatus status)
{
    switch (status) {
    case GlStatus::NotAttempted:
    case GlStatus::NoApplication:
    case GlStatus::NoShareContext:
        return true;
    case GlStatus::Ready:
    case GlStatus::SurfaceFailed:
    case GlStatus::ContextFailed:
    case GlStatus::MakeCurrentFailed:
    case GlStatus::NoFunctions:
    case GlStatus::VersionTooLow:
    case GlStatus::ShaderLinkFailed:
        return false;
    }
    return false;
}

bool isSoftwareRenderer(const QString &renderer)
{
    if (renderer.isEmpty())
        return false;
    // Mesa's software rasterizers, then the two Windows fallbacks and the WARP
    // adapter D3D12 exposes under a name that reads like a real GPU.
    static const char *const markers[] = {
        "llvmpipe", "softpipe", "swrast", "SWR", "GDI Generic", "Microsoft Basic Render",
        "D3D12 (Microsoft",
    };
    for (const char *marker : markers) {
        if (renderer.contains(QLatin1StringView(marker), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

QString describeGl(const GlStatusInfo &info)
{
    QString version;
    if (info.major > 0) {
        version = info.isEs ? QStringLiteral("OpenGL ES %1.%2") : QStringLiteral("OpenGL %1.%2");
        version = version.arg(info.major).arg(info.minor);
    }
    if (info.renderer.isEmpty())
        return version;
    if (version.isEmpty())
        return info.renderer;
    return QStringLiteral("%1 — %2").arg(version, info.renderer);
}

} // namespace drift::gl
