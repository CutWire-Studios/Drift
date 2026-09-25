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

bool isLimitedPreviewRenderer(const QString &renderer)
{
    if (renderer.isEmpty())
        return false;
    // Exact product names. "HD Graphics 520" and "UHD Graphics" must not match.
    // The P-prefixed entries are the Xeon workstation parts of the same two
    // generations — identical silicon, identical flashing, and their renderer
    // string does not contain the consumer name.
    static const char *const markers[] = {
        "HD Graphics 2000",
        "HD Graphics 2500",
        "HD Graphics 3000",
        "HD Graphics 4000",
        "HD Graphics P3000",
        "HD Graphics P4000",
        "Sandy Bridge",
        "Ivy Bridge",
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
