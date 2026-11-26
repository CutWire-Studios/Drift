#include "app/headless/EglPlatform.h"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QtGui/qpa/qplatformintegration.h>
#include <QtGui/qpa/qplatformintegrationplugin.h>
#include <QtGui/qpa/qplatformoffscreensurface.h>
#include <QtGui/qpa/qplatformopenglcontext.h>
#include <QtGui/qpa/qplatformscreen.h>
#include <QtGui/qpa/qplatformwindow.h>
#include <QtGui/qpa/qwindowsysteminterface.h>
#include <QtGui/private/qgenericunixeventdispatcher_p.h>

#include <dlfcn.h>

#include <algorithm>
#include <vector>

// A Qt platform plugin with no windowing system behind it: just EGL contexts made current with
// no surface (EGL_KHR_surfaceless_context). The stock `offscreen` plugin can only hand out GL
// through GLX, and QNativeInterface::QEGLContext::fromNative() refuses to wrap a context unless
// the platform already is an EGL one — so on a box with no display server and no xvfb the only
// way to a QOpenGLContext is to be the platform. libEGL is dlopen'ed, like GpuDevice does,
// so the build needs no EGL headers and a host without EGL just falls through to the next
// platform in QT_QPA_PLATFORM.
namespace drift::headless {
namespace {

using EGLDisplay = void *;
using EGLConfig = void *;
using EGLContext = void *;
using EGLSurface = void *;
using EGLDeviceEXT = void *;
using EGLint = int;
using EGLBoolean = unsigned int;
using EGLenum = unsigned int;

constexpr EGLint kNone = 0x3038;
constexpr EGLint kExtensions = 0x3055;
constexpr EGLint kRenderableType = 0x3040;
constexpr EGLint kOpenGlBit = 0x8;
constexpr EGLint kSurfaceType = 0x3033;
constexpr EGLint kPbufferBit = 0x1;
constexpr EGLenum kOpenGlApi = 0x30A2;
constexpr EGLint kContextMajor = 0x3098;
constexpr EGLint kContextMinor = 0x30FB;
constexpr EGLint kContextProfileMask = 0x30FD;
constexpr EGLint kCoreProfileBit = 0x1;
constexpr EGLint kCompatProfileBit = 0x2;
constexpr EGLenum kPlatformDeviceExt = 0x313F;
constexpr EGLenum kPlatformSurfacelessMesa = 0x31DD;
constexpr EGLint kDrmDeviceFileExt = 0x3233;
constexpr EGLint kDrmRenderNodeFileExt = 0x3377;
constexpr unsigned kGlRenderer = 0x1F01;
constexpr unsigned kGlMajorVersion = 0x821B;
constexpr unsigned kGlMinorVersion = 0x821C;

struct Egl
{
    void *lib = nullptr;
    void *(*getProcAddress)(const char *) = nullptr;
    const char *(*queryString)(EGLDisplay, EGLint) = nullptr;
    EGLBoolean (*initialize)(EGLDisplay, EGLint *, EGLint *) = nullptr;
    EGLBoolean (*bindApi)(EGLenum) = nullptr;
    EGLBoolean (*chooseConfig)(EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *) = nullptr;
    EGLContext (*createContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint *) = nullptr;
    EGLBoolean (*destroyContext)(EGLDisplay, EGLContext) = nullptr;
    EGLBoolean (*makeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext) = nullptr;
    EGLDisplay (*getPlatformDisplay)(EGLenum, void *, const EGLint *) = nullptr;
    EGLBoolean (*queryDevices)(EGLint, EGLDeviceEXT *, EGLint *) = nullptr;
    const char *(*queryDeviceString)(EGLDeviceEXT, EGLint) = nullptr;
};

const Egl &egl()
{
    static const Egl api = [] {
        Egl e;
        e.lib = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!e.lib)
            return e;
        const auto sym = [&e](const char *name) { return dlsym(e.lib, name); };
#define DRIFT_EGL(field, name) e.field = reinterpret_cast<decltype(e.field)>(sym(name))
        DRIFT_EGL(getProcAddress, "eglGetProcAddress");
        DRIFT_EGL(queryString, "eglQueryString");
        DRIFT_EGL(initialize, "eglInitialize");
        DRIFT_EGL(bindApi, "eglBindAPI");
        DRIFT_EGL(chooseConfig, "eglChooseConfig");
        DRIFT_EGL(createContext, "eglCreateContext");
        DRIFT_EGL(destroyContext, "eglDestroyContext");
        DRIFT_EGL(makeCurrent, "eglMakeCurrent");
#undef DRIFT_EGL
        if (e.getProcAddress) {
            const auto proc = [&e](const char *name) { return e.getProcAddress(name); };
            e.getPlatformDisplay = reinterpret_cast<decltype(e.getPlatformDisplay)>(proc("eglGetPlatformDisplayEXT"));
            e.queryDevices = reinterpret_cast<decltype(e.queryDevices)>(proc("eglQueryDevicesEXT"));
            e.queryDeviceString = reinterpret_cast<decltype(e.queryDeviceString)>(proc("eglQueryDeviceStringEXT"));
        }
        if (!e.getProcAddress || !e.queryString || !e.initialize || !e.bindApi || !e.chooseConfig
            || !e.createContext || !e.destroyContext || !e.makeCurrent || !e.getPlatformDisplay)
            e.lib = nullptr;
        return e;
    }();
    return api;
}

struct Candidate
{
    EglDevice info;
    EGLDisplay display = nullptr;
    EGLConfig config = nullptr;
};

std::vector<EGLint> contextAttribs(int major, int minor, QSurfaceFormat::OpenGLContextProfile profile)
{
    const bool compat = profile == QSurfaceFormat::CompatibilityProfile;
    std::vector<EGLint> attribs{kContextMajor, major, kContextMinor, minor};
    if (major > 3 || (major == 3 && minor >= 2)) {
        attribs.push_back(kContextProfileMask);
        attribs.push_back(compat ? kCompatProfileBit : kCoreProfileBit);
    }
    attribs.push_back(kNone);
    return attribs;
}

bool chooseConfig(EGLDisplay display, EGLConfig *config)
{
    // NVIDIA's device platform only offers pbuffer-capable configs; Mesa surfaceless takes either.
    for (const EGLint surfaceType : {kPbufferBit, 0}) {
        const EGLint attribs[] = {kRenderableType, kOpenGlBit, kSurfaceType, surfaceType, kNone};
        EGLint count = 0;
        if (egl().chooseConfig(display, attribs, config, 1, &count) && count > 0)
            return true;
    }
    return false;
}

// Makes a throwaway 3.3 core context current to learn what the device renders with.
bool probe(Candidate &candidate)
{
    const Egl &e = egl();
    if (!e.initialize(candidate.display, nullptr, nullptr))
        return false;
    e.bindApi(kOpenGlApi);
    if (!chooseConfig(candidate.display, &candidate.config))
        return false;
    const auto attribs = contextAttribs(3, 3, QSurfaceFormat::CoreProfile);
    EGLContext context = e.createContext(candidate.display, candidate.config, nullptr, attribs.data());
    if (!context)
        return false;
    bool ok = false;
    if (e.makeCurrent(candidate.display, nullptr, nullptr, context)) {
        using GetString = const unsigned char *(*)(unsigned);
        const auto getString = reinterpret_cast<GetString>(e.getProcAddress("glGetString"));
        if (getString) {
            if (const unsigned char *name = getString(kGlRenderer)) {
                candidate.info.renderer = QString::fromUtf8(reinterpret_cast<const char *>(name));
                ok = true;
            }
        }
        e.makeCurrent(candidate.display, nullptr, nullptr, nullptr);
    }
    e.destroyContext(candidate.display, context);
    const QString lower = candidate.info.renderer.toLower();
    candidate.info.software = lower.contains(QLatin1String("llvmpipe"))
        || lower.contains(QLatin1String("softpipe")) || lower.contains(QLatin1String("swrast"))
        || lower.contains(QLatin1String("software"));
    return ok;
}

std::vector<Candidate> probeAll()
{
    std::vector<Candidate> found;
    const Egl &e = egl();
    if (!e.lib)
        return found;
    const QByteArray clientExtensions(e.queryString(nullptr, kExtensions) ? e.queryString(nullptr, kExtensions) : "");
    const bool hasDevices = e.queryDevices && e.queryDeviceString
        && clientExtensions.contains("EGL_EXT_platform_device")
        && clientExtensions.contains("EGL_EXT_device_enumeration");
    if (hasDevices) {
        EGLDeviceEXT devices[16] = {};
        EGLint count = 0;
        if (e.queryDevices(16, devices, &count)) {
            for (EGLint i = 0; i < count; ++i) {
                Candidate candidate;
                candidate.display = e.getPlatformDisplay(kPlatformDeviceExt, devices[i], nullptr);
                if (!candidate.display || !probe(candidate))
                    continue;
                for (const EGLint attribute : {kDrmRenderNodeFileExt, kDrmDeviceFileExt}) {
                    if (const char *path = e.queryDeviceString(devices[i], attribute); path && *path) {
                        candidate.info.drmNode = QString::fromUtf8(path);
                        break;
                    }
                }
                candidate.info.index = static_cast<int>(found.size());
                found.push_back(candidate);
            }
        }
    }
    if (found.empty() && clientExtensions.contains("EGL_MESA_platform_surfaceless")) {
        Candidate candidate;
        candidate.display = e.getPlatformDisplay(kPlatformSurfacelessMesa, nullptr, nullptr);
        if (candidate.display && probe(candidate))
            found.push_back(candidate);
    }
    return found;
}

QString g_description;
EGLDisplay g_display = nullptr;
EGLConfig g_config = nullptr;

class HeadlessGlContext final : public QPlatformOpenGLContext
{
public:
    explicit HeadlessGlContext(QOpenGLContext *context)
    {
        const Egl &e = egl();
        const QSurfaceFormat requested = context->format();
        if (requested.renderableType() == QSurfaceFormat::OpenGLES)
            return;
        e.bindApi(kOpenGlApi);
        int major = requested.majorVersion();
        int minor = requested.minorVersion();
        if (major < 3) {
            major = 3;
            minor = 3;
        }
        EGLContext share = nullptr;
        if (auto *sharedWith = static_cast<HeadlessGlContext *>(context->shareHandle()))
            share = sharedWith->m_context;
        const auto attribs = contextAttribs(major, minor, requested.profile());
        m_context = e.createContext(g_display, g_config, share, attribs.data());
        if (!m_context)
            return;
        m_sharing = share != nullptr;
        m_format = requested;
        m_format.setRenderableType(QSurfaceFormat::OpenGL);
        m_format.setRedBufferSize(8);
        m_format.setGreenBufferSize(8);
        m_format.setBlueBufferSize(8);
        m_format.setAlphaBufferSize(8);
        m_format.setVersion(major, minor);
        // The driver may have given more than was asked for; Qt reports whatever format() says.
        if (e.makeCurrent(g_display, nullptr, nullptr, m_context)) {
            using GetIntegerv = void (*)(unsigned, int *);
            if (const auto getIntegerv = reinterpret_cast<GetIntegerv>(e.getProcAddress("glGetIntegerv"))) {
                int realMajor = 0;
                int realMinor = 0;
                getIntegerv(kGlMajorVersion, &realMajor);
                getIntegerv(kGlMinorVersion, &realMinor);
                if (realMajor > 0)
                    m_format.setVersion(realMajor, realMinor);
            }
            e.makeCurrent(g_display, nullptr, nullptr, nullptr);
        }
    }

    ~HeadlessGlContext() override
    {
        if (m_context)
            egl().destroyContext(g_display, m_context);
    }

    QSurfaceFormat format() const override { return m_format; }
    void swapBuffers(QPlatformSurface *) override {}
    bool makeCurrent(QPlatformSurface *) override
    {
        return egl().makeCurrent(g_display, nullptr, nullptr, m_context);
    }
    void doneCurrent() override { egl().makeCurrent(g_display, nullptr, nullptr, nullptr); }
    bool isSharing() const override { return m_sharing; }
    bool isValid() const override { return m_context != nullptr; }
    QFunctionPointer getProcAddress(const char *name) override
    {
        return reinterpret_cast<QFunctionPointer>(egl().getProcAddress(name));
    }

private:
    EGLContext m_context = nullptr;
    QSurfaceFormat m_format;
    bool m_sharing = false;
};

class HeadlessOffscreenSurface final : public QPlatformOffscreenSurface
{
public:
    explicit HeadlessOffscreenSurface(QOffscreenSurface *surface) : QPlatformOffscreenSurface(surface) {}
    QSurfaceFormat format() const override { return offscreenSurface()->requestedFormat(); }
    bool isValid() const override { return true; }
};

class HeadlessScreen final : public QPlatformScreen
{
public:
    QRect geometry() const override { return QRect(0, 0, 1920, 1080); }
    int depth() const override { return 32; }
    QImage::Format format() const override { return QImage::Format_RGB32; }
    QString name() const override { return QStringLiteral("headless"); }
};

class HeadlessIntegration final : public QPlatformIntegration
{
public:
    HeadlessIntegration()
    {
        QWindowSystemInterface::handleScreenAdded(new HeadlessScreen);
    }

    bool hasCapability(Capability cap) const override
    {
        switch (cap) {
        case OpenGL:
        case ThreadedOpenGL:
            return true;
        default:
            return QPlatformIntegration::hasCapability(cap);
        }
    }

    QPlatformWindow *createPlatformWindow(QWindow *window) const override
    {
        return new QPlatformWindow(window);
    }
    QPlatformBackingStore *createPlatformBackingStore(QWindow *) const override { return nullptr; }
    QPlatformOpenGLContext *createPlatformOpenGLContext(QOpenGLContext *context) const override
    {
        return new HeadlessGlContext(context);
    }
    QPlatformOffscreenSurface *createPlatformOffscreenSurface(QOffscreenSurface *surface) const override
    {
        return new HeadlessOffscreenSurface(surface);
    }
    QAbstractEventDispatcher *createEventDispatcher() const override
    {
        return QtGenericUnixDispatcher::createUnixEventDispatcher();
    }
};

} // namespace

QString activeEglDescription()
{
    return g_description;
}

QList<EglDevice> listEglDevices()
{
    QList<EglDevice> out;
    for (const Candidate &candidate : probeAll())
        out.append(candidate.info);
    return out;
}

void selectNativeEglPlatform(int argc, char *argv[], const QString &gpu)
{
    if (!qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        return;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "-platform") == 0)
            return;
    }
    QByteArray value = "driftheadless";
    if (!gpu.isEmpty())
        value += ":gpu=" + gpu.toUtf8();
    // The stock platforms stay behind it: a host with a display but no usable EGL device still
    // gets what it had before.
    value += ";xcb;wayland;offscreen";
    qputenv("QT_QPA_PLATFORM", value);
}

class EglIntegrationPlugin final : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "driftheadless.json")
public:
    QPlatformIntegration *create(const QString &key, const QStringList &params) override
    {
        if (key.compare(QLatin1String("driftheadless"), Qt::CaseInsensitive) != 0)
            return nullptr;
        QString gpu;
        for (const QString &param : params) {
            if (param.startsWith(QLatin1String("gpu=")))
                gpu = param.mid(4);
        }
        const std::vector<Candidate> found = probeAll();
        if (found.empty())
            return nullptr;

        const Candidate *chosen = nullptr;
        bool isIndex = false;
        const int wanted = gpu.toInt(&isIndex);
        if (!gpu.isEmpty()) {
            for (const Candidate &candidate : found) {
                if (isIndex ? candidate.info.index == wanted
                            : candidate.info.renderer.contains(gpu, Qt::CaseInsensitive)
                                || candidate.info.drmNode.contains(gpu, Qt::CaseInsensitive))
                    chosen = &candidate;
                if (chosen)
                    break;
            }
            if (!chosen)
                qWarning("drift: no EGL device matches --gpu %s, picking automatically", qPrintable(gpu));
        }
        if (!chosen) {
            const auto hardware = std::find_if(found.begin(), found.end(),
                                               [](const Candidate &c) { return !c.info.software; });
            chosen = hardware != found.end() ? &*hardware : &found.front();
        }
        g_display = chosen->display;
        g_config = chosen->config;
        g_description = QStringLiteral("EGL device %1: %2%3")
                            .arg(chosen->info.index)
                            .arg(chosen->info.renderer,
                                 chosen->info.drmNode.isEmpty() ? QString()
                                                                : QStringLiteral(" (%1)").arg(chosen->info.drmNode));
        return new HeadlessIntegration;
    }
    QPlatformIntegration *create(const QString &key, const QStringList &params, int &, char **) override
    {
        return create(key, params);
    }
};

} // namespace drift::headless

#include "EglPlatform.moc"
