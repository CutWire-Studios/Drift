#pragma once

// Why the GPU compositor is (un)available, in a form that can leave src/engine/.
//
// GlRuntime.h is engine-private, but the preview needs to tell the user why the
// picture is missing, so the outcome of GL bring-up travels as this small value
// instead. The strings here are raw driver output and stable machine ids, never
// translated: a tr() baked in at failure time would not follow a language change,
// so each presentation site maps the id to its own catalog.

#include <QString>

namespace drift::gl {

enum class GlStatus {
    NotAttempted,
    Ready,
    NoApplication,
    NoShareContext,
    SurfaceFailed,
    ContextFailed,
    MakeCurrentFailed,
    NoFunctions,
    VersionTooLow,
    ShaderLinkFailed,
};

struct GlStatusInfo
{
    GlStatus status = GlStatus::NotAttempted;
    QString vendor;   // GL_VENDOR, empty when no context ever became current
    QString renderer; // GL_RENDERER, ditto
    int major = 0;
    int minor = 0;
    bool isEs = false;
    bool software = false; // renderer matched a known software rasterizer
    QString detail;        // shader log and the like

    bool isReady() const { return status == GlStatus::Ready; }
};

// Stable id for QML, the debug report and pasted bug reports.
const char *statusId(GlStatus status);

// Whether another attempt could still succeed. Qt Quick creates the global share
// context lazily, so a composite that beats the first QQuickWindow must not
// poison the session — everything a driver decided about itself is permanent.
bool isTransient(GlStatus status);

// GL_RENDERER only. GL_VENDOR says "Mesa" for hardware drivers too.
bool isSoftwareRenderer(const QString &renderer);

// "OpenGL 3.0 — llvmpipe (LLVM 3.6, 128 bits)". Empty when nothing is known.
QString describeGl(const GlStatusInfo &info);

} // namespace drift::gl
