#include "GlRuntime.h"

#include "GlModelRenderer.h"

#include <QColor>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector3D>

#include <cmath>
#include <mutex>

namespace drift::gl {

namespace {

constexpr const char *kCopyFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
void main() {
    fragColor = texture(u_currentTexture, v_texCoord);
}
)";

// BT.709 limited-range NV12 → straight RGBA (opaque).
constexpr const char *kNv12FragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_y;
uniform sampler2D u_uv;
void main() {
    float y = texture(u_y, v_texCoord).r;
    vec2 uv = texture(u_uv, v_texCoord).rg - vec2(0.5);
    float r = y + 1.5748 * uv.y;
    float g = y - 0.1873 * uv.x - 0.4681 * uv.y;
    float b = y + 1.8556 * uv.x;
    fragColor = vec4(clamp(vec3(r, g, b), 0.0, 1.0), 1.0);
}
)";

// Fullscreen triangle strip with standard GL UVs (v=0 at bottom / NDC bottom).
// Source QImages are copied into an FBO once so every pass samples FBO-backed
// textures only (same Y layout). Readback uses toImage(false).
constexpr float kQuad[] = {
    // pos      // uv
    -1.f, -1.f, 0.f, 0.f,
     1.f, -1.f, 1.f, 0.f,
    -1.f,  1.f, 0.f, 1.f,
     1.f,  1.f, 1.f, 1.f,
};

uint64_t targetPoolKey(int width, int height, bool wantDepth)
{
    // w/h fit in 31 bits; the low bit tags depth so colour-only and depth targets never share.
    return (uint64_t(uint32_t(width) & 0x7fffffffu) << 33)
           | (uint64_t(uint32_t(height) & 0x7fffffffu) << 1)
           | (wantDepth ? 1u : 0u);
}

GlRuntime *g_runtime = nullptr;

void destroyGpuRuntime()
{
    if (!g_runtime)
        return;
    g_runtime->shutdown();
    delete g_runtime;
    g_runtime = nullptr;
}

} // namespace

const char *const kQuadVertexShader = R"(#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
out vec2 v_texCoord;
void main() {
    v_texCoord = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

GlRuntime &runtime()
{
    // Reached from the compositor thread, the export job and the GUI thread, so
    // the lazy construction has to be race-free — two runtimes would mean two GL
    // contexts and two post-routines.
    static std::once_flag once;
    std::call_once(once, [] {
        g_runtime = new GlRuntime;
        qAddPostRoutine(destroyGpuRuntime);
    });
    return *g_runtime;
}

bool GlRuntime::initGlObjects()
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);

    // Surface and context are both created here, on the GL thread, and never
    // leave it. Creating the context on one thread and using it on another is
    // what QOpenGLContext forbids ("cannot make current in a different thread").
    surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(format);
    surface->create();
    if (!surface->isValid()) {
        qWarning("GlRuntime: failed to create offscreen surface");
        surface.reset();
        return false;
    }

    context = std::make_unique<QOpenGLContext>();
    context->setFormat(format);
    // Share with the Qt Quick scene-graph context so composited textures can be
    // handed to the preview item without a readback. Requires
    // Qt::AA_ShareOpenGLContexts, set in main() before the QApplication.
    if (QOpenGLContext *shared = QOpenGLContext::globalShareContext())
        context->setShareContext(shared);
    if (!context->create()) {
        qWarning("GlRuntime: failed to create OpenGL context");
        context.reset();
        surface.reset();
        return false;
    }

    if (!context->makeCurrent(surface.get())) {
        qWarning("GlRuntime: makeCurrent failed on the GL thread");
        context.reset();
        surface.reset();
        return false;
    }

    auto *gl = context->extraFunctions();
    if (!gl) {
        qWarning("GlRuntime: OpenGL extra functions unavailable");
        context->doneCurrent();
        return false;
    }

    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    gl->glGenBuffers(1, &vbo);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void *>(2 * sizeof(float)));
    gl->glBindVertexArray(0);

    copyProgram = std::make_unique<QOpenGLShaderProgram>();
    if (!copyProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kQuadVertexShader)
        || !copyProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kCopyFragShader)
        || !copyProgram->link()) {
        qWarning("GlRuntime: copy shader failed: %s", qPrintable(copyProgram->log()));
        copyProgram.reset();
        context->doneCurrent();
        return false;
    }

    context->doneCurrent();
    return true;
}

bool GlRuntime::ensureReady()
{
    QMutexLocker lock(&m_initMutex);
    if (m_initTried)
        return m_ok;
    m_initTried = true;

    if (!QCoreApplication::instance()) {
        qWarning("GlRuntime: no QCoreApplication; OpenGL unavailable");
        return false;
    }

    m_glThread = new QThread;
    m_glThread->setObjectName(QStringLiteral("DriftGL"));
    m_glThread->start();

    m_glOwner = new QObject;
    m_glOwner->moveToThread(m_glThread);

    bool initialized = false;
    QMetaObject::invokeMethod(
        m_glOwner, [this] { return initGlObjects(); }, Qt::BlockingQueuedConnection, &initialized);

    if (!initialized) {
        delete m_glOwner;
        m_glOwner = nullptr;
        m_glThread->quit();
        m_glThread->wait();
        delete m_glThread;
        m_glThread = nullptr;
        return false;
    }

    m_ok = true;
    return m_ok;
}

bool GlRuntime::available()
{
    return ensureReady();
}

bool GlRuntime::exec(const std::function<void()> &fn)
{
    if (!ensureReady())
        return false;

    bool ran = false;
    QMetaObject::invokeMethod(
        m_glOwner,
        [this, &fn]() -> bool {
            if (!context->makeCurrent(surface.get())) {
                qWarning("GlRuntime: makeCurrent failed");
                return false;
            }
            fn();
            context->doneCurrent();
            return true;
        },
        Qt::BlockingQueuedConnection, &ran);
    return ran;
}

QOpenGLExtraFunctions *GlRuntime::functions()
{
    return context ? context->extraFunctions() : nullptr;
}

void GlRuntime::shutdown()
{
    {
        QMutexLocker lock(&m_initMutex);
        if (!m_ok || !m_glOwner)
            return;
    }

    QMetaObject::invokeMethod(
        m_glOwner,
        [this] {
            if (!context->makeCurrent(surface.get()))
                return;
            if (auto *gl = context->extraFunctions()) {
                for (int i = 0; i < kPresentRingSize; ++i) {
                    if (m_presentFence[i]) {
                        gl->glDeleteSync(m_presentFence[i]);
                        m_presentFence[i] = nullptr;
                    }
                }
                destroyImageUploadCache();
            }
            for (GlTarget &target : m_presentRing)
                target.fbo.reset();
            m_targetPool.clear();
            m_pooledTargets = 0;
            programs.clear();
            copyProgram.reset();
            if (auto *gl = context->extraFunctions()) {
                destroyGlModels(*this, gl);
                for (const auto &entry : staticTextures) {
                    GLuint tex = entry.second;
                    gl->glDeleteTextures(1, &tex);
                }
                staticTextures.clear();
                if (vbo) {
                    gl->glDeleteBuffers(1, &vbo);
                    vbo = 0;
                }
                if (vao) {
                    gl->glDeleteVertexArrays(1, &vao);
                    vao = 0;
                }
            }
            context->doneCurrent();
        },
        Qt::BlockingQueuedConnection);

    // Stop the thread before deleting the object that lives on it — deleting a
    // QObject from a thread other than its own is undefined behaviour.
    m_glThread->quit();
    m_glThread->wait();
    delete m_glOwner;
    m_glOwner = nullptr;
    delete m_glThread;
    m_glThread = nullptr;

    context.reset();
    surface.reset();
    m_ok = false;
}


GlTarget GlRuntime::acquireTarget(int width, int height, bool wantDepth)
{
    GlTarget target;
    target.width = qMax(1, width);
    target.height = qMax(1, height);
    target.hasDepth = wantDepth;

    const uint64_t key = targetPoolKey(target.width, target.height, wantDepth);
    const auto it = m_targetPool.find(key);
    if (it != m_targetPool.end()) {
        target.fbo = std::move(it->second);
        m_targetPool.erase(it);
        --m_pooledTargets;
        return target;
    }

    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(wantDepth ? QOpenGLFramebufferObject::Depth
                                : QOpenGLFramebufferObject::NoAttachment);
    target.fbo = std::make_unique<QOpenGLFramebufferObject>(target.width, target.height, fmt);
    return target;
}

void GlRuntime::releaseTarget(GlTarget &&target)
{
    if (!target.isValid())
        return;
    if (m_pooledTargets >= kMaxPooledTargets)
        return; // let it drop

    const uint64_t key = targetPoolKey(target.width, target.height, target.hasDepth);
    m_targetPool.emplace(key, std::move(target.fbo));
    ++m_pooledTargets;
}

void GlRuntime::waitPresentFence(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kPresentRingSize)
        return;
    GLsync &fence = m_presentFence[slotIndex];
    if (!fence)
        return;
    auto *gl = functions();
    if (!gl) {
        fence = nullptr;
        return;
    }
    // Only wait for this slot's prior publish — not the whole GPU pipeline.
    gl->glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, GLuint64(100'000'000)); // 100 ms
    gl->glDeleteSync(fence);
    fence = nullptr;
}

void GlRuntime::destroyImageUploadCache()
{
    auto *gl = functions();
    for (CachedUpload &entry : m_imageUploadLru) {
        if (gl && entry.texture)
            gl->glDeleteTextures(1, &entry.texture);
        entry.texture = 0;
    }
    m_imageUploadLru.clear();
    m_imageUploadIndex.clear();
}

GlTarget &GlRuntime::acquirePresentTarget(int width, int height)
{
    const int w = qMax(1, width);
    const int h = qMax(1, height);

    const int slotIndex = m_presentNext;
    GlTarget &slot = m_presentRing[slotIndex];
    m_presentNext = (m_presentNext + 1) % kPresentRingSize;

    // The scene graph may still be sampling this ring slot from a previous publish.
    // Wait for that fence before redrawing into the same FBO.
    waitPresentFence(slotIndex);

    if (!slot.isValid() || slot.width != w || slot.height != h) {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        slot.fbo = std::make_unique<QOpenGLFramebufferObject>(w, h, fmt);
        slot.width = w;
        slot.height = h;
    }
    return slot;
}

void GlRuntime::markPresentReady(GlTarget &presentTarget)
{
    auto *gl = functions();
    if (!gl || !presentTarget.isValid())
        return;

    int slotIndex = -1;
    for (int i = 0; i < kPresentRingSize; ++i) {
        if (&m_presentRing[i] == &presentTarget) {
            slotIndex = i;
            break;
        }
    }
    if (slotIndex < 0)
        return;

    waitPresentFence(slotIndex);
    gl->glFlush();
    m_presentFence[slotIndex] = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    // Ensure the just-inserted fence has completed before the texture id is
    // published to the scene graph (avoids sampling a half-drawn frame).
    if (m_presentFence[slotIndex]) {
        gl->glClientWaitSync(m_presentFence[slotIndex], GL_SYNC_FLUSH_COMMANDS_BIT,
                             GLuint64(16'000'000)); // ~1 frame @ 60 Hz
    }
}

QOpenGLShaderProgram *GlRuntime::builtinProgram(const QString &id, const char *vertexSource,
                                                const char *fragmentSource)
{
    CompiledEffect &cached = programs[id];
    if (cached.ok)
        return cached.passes[0].program.get();

    cached = CompiledEffect{};
    cached.id = id;

    CompiledPass pass;
    pass.program = std::make_unique<QOpenGLShaderProgram>();
    if (!pass.program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource)
        || !pass.program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)
        || !pass.program->link()) {
        qWarning("GlRuntime: builtin program '%s' failed: %s", qPrintable(id),
                 qPrintable(pass.program->log()));
        programs.erase(id);
        return nullptr;
    }

    cached.passes.push_back(std::move(pass));
    cached.ok = true;
    return cached.passes[0].program.get();
}

CompiledEffect *GlRuntime::compile(const QString &cacheKey, const drift::GpuEffectDefinition &gpu)
{
    QString sourceSig;
    for (const drift::GpuEffectPass &pass : gpu.passes)
        sourceSig += pass.fragmentShaderSource;
    sourceSig += QLatin1Char('#');
    sourceSig += QString::number(gpu.passes.size());

    CompiledEffect &cached = programs[cacheKey];
    if (cached.ok && cached.id == cacheKey && cached.sourceSig == sourceSig)
        return &cached;

    cached = CompiledEffect{};
    cached.id = cacheKey;
    cached.sourceSig = sourceSig;
    cached.passes.reserve(static_cast<size_t>(gpu.passes.size()));

    for (const drift::GpuEffectPass &pass : gpu.passes) {
        CompiledPass cp;
        cp.program = std::make_unique<QOpenGLShaderProgram>();
        if (!cp.program->addShaderFromSourceCode(QOpenGLShader::Vertex, kQuadVertexShader)) {
            qWarning("GlRuntime: vertex shader compile failed for %s: %s", qPrintable(cacheKey),
                     qPrintable(cp.program->log()));
            programs.erase(cacheKey);
            return nullptr;
        }
        if (!cp.program->addShaderFromSourceCode(QOpenGLShader::Fragment, pass.fragmentShaderSource)) {
            qWarning("GlRuntime: fragment compile failed for %s pass %d (%s): %s", qPrintable(cacheKey),
                     pass.passIndex, qPrintable(pass.fragmentShaderFile), qPrintable(cp.program->log()));
            programs.erase(cacheKey);
            return nullptr;
        }
        if (!cp.program->link()) {
            qWarning("GlRuntime: link failed for %s pass %d: %s", qPrintable(cacheKey), pass.passIndex,
                     qPrintable(cp.program->log()));
            programs.erase(cacheKey);
            return nullptr;
        }
        cached.passes.push_back(std::move(cp));
    }
    cached.ok = true;
    return &cached;
}

GLuint uploadTexture(QOpenGLExtraFunctions *gl, const QImage &image, bool flipVertically)
{
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (flipVertically)
        rgba = rgba.mirrored(false, true);
    GLuint tex = 0;
    gl->glGenTextures(1, &tex);
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba.constBits());
    return tex;
}

bool blitTextureToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, GLuint srcTex, GlTarget &dest)
{
    if (!rt.copyProgram || !dest.isValid())
        return false;
    dest.fbo->bind();
    gl->glViewport(0, 0, dest.width, dest.height);
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    rt.copyProgram->bind();
    rt.copyProgram->setUniformValue("u_currentTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, srcTex);
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    rt.copyProgram->release();
    dest.fbo->release();
    return true;
}

// Static package assets live for the process lifetime. They are plain uploads (not FBO-backed),
// so they must be flipped to match the Y layout of the FBO-promoted source targets.
GLuint staticTexture(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path)
{
    const auto it = rt.staticTextures.find(path);
    if (it != rt.staticTextures.end())
        return it->second;

    QImage image;
    if (!image.load(path) || image.isNull()) {
        qWarning("GlRuntime: failed to load texture '%s'", qPrintable(path));
        rt.staticTextures[path] = 0;
        return 0;
    }
    const GLuint tex = uploadTexture(gl, image, /*flipVertically=*/true);
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    rt.staticTextures[path] = tex;
    return tex;
}

GLuint cachedUploadTexture(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image)
{
    if (image.isNull() || !gl)
        return 0;

    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    const qint64 key = rgba.cacheKey();
    auto indexIt = rt.m_imageUploadIndex.find(key);
    if (indexIt != rt.m_imageUploadIndex.end()) {
        // LRU touch.
        rt.m_imageUploadLru.splice(rt.m_imageUploadLru.begin(), rt.m_imageUploadLru, indexIt->second);
        return indexIt->second->texture;
    }

    const GLuint tex = uploadTexture(gl, rgba);
    if (!tex)
        return 0;

    while (rt.m_imageUploadLru.size() >= GlRuntime::kMaxCachedUploads) {
        GlRuntime::CachedUpload &old = rt.m_imageUploadLru.back();
        rt.m_imageUploadIndex.erase(old.cacheKey);
        if (old.texture)
            gl->glDeleteTextures(1, &old.texture);
        rt.m_imageUploadLru.pop_back();
    }

    rt.m_imageUploadLru.push_front(GlRuntime::CachedUpload{key, tex, rgba.width(), rgba.height()});
    rt.m_imageUploadIndex[key] = rt.m_imageUploadLru.begin();
    return tex;
}

GlTarget promoteImageToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image,
                              const QSize &fallbackSize)
{
    QImage rgba = image.isNull() ? QImage(fallbackSize, QImage::Format_RGBA8888)
                                 : image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull())
        rgba.fill(Qt::transparent);

    GlTarget target = rt.acquireTarget(rgba.width(), rgba.height());
    if (!target.isValid())
        return {};

    const GLuint uploaded = uploadTexture(gl, rgba);
    const bool ok = blitTextureToTarget(rt, gl, uploaded, target);
    gl->glDeleteTextures(1, &uploaded);
    if (!ok) {
        rt.releaseTarget(std::move(target));
        return {};
    }
    return target;
}

GlTarget promoteImageToTargetCached(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image,
                                    const QSize &fallbackSize)
{
    if (image.isNull())
        return promoteImageToTarget(rt, gl, image, fallbackSize);

    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    GlTarget target = rt.acquireTarget(rgba.width(), rgba.height());
    if (!target.isValid())
        return {};

    const GLuint uploaded = cachedUploadTexture(rt, gl, rgba);
    if (!uploaded) {
        rt.releaseTarget(std::move(target));
        return {};
    }
    const bool ok = blitTextureToTarget(rt, gl, uploaded, target);
    // uploaded stays in the LRU cache — do not delete.
    if (!ok) {
        rt.releaseTarget(std::move(target));
        return {};
    }
    return target;
}

GlTarget promoteNv12ToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QByteArray &nv12,
                             int width, int height)
{
    if (!gl || width <= 0 || height <= 0 || (height % 2) != 0 || (width % 2) != 0)
        return {};

    const qsizetype yBytes = qsizetype(width) * height;
    const qsizetype uvBytes = qsizetype(width) * (height / 2);
    if (nv12.size() < yBytes + uvBytes)
        return {};

    GlTarget target = rt.acquireTarget(width, height);
    if (!target.isValid())
        return {};

    QOpenGLShaderProgram *program =
        rt.builtinProgram(QStringLiteral("__nv12__"), kQuadVertexShader, kNv12FragShader);
    if (!program) {
        rt.releaseTarget(std::move(target));
        return {};
    }

    GLuint textures[2] = {0, 0};
    gl->glGenTextures(2, textures);

    gl->glBindTexture(GL_TEXTURE_2D, textures[0]);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE,
                     nv12.constData());

    gl->glBindTexture(GL_TEXTURE_2D, textures[1]);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width / 2, height / 2, 0, GL_RG, GL_UNSIGNED_BYTE,
                     nv12.constData() + yBytes);

    target.fbo->bind();
    gl->glViewport(0, 0, width, height);
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    program->bind();
    program->setUniformValue("u_y", 0);
    program->setUniformValue("u_uv", 1);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, textures[0]);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, textures[1]);
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    program->release();
    target.fbo->release();

    gl->glDeleteTextures(2, textures);
    return target;
}

void setPackageUniforms(QOpenGLShaderProgram *program, const QMap<QString, QVariant> &parameters,
                        const QSize &resolution, drift::TimeUs timeUs, double progress)
{
    program->setUniformValue("u_currentTexture", 0);
    program->setUniformValue("u_resolution",
                             QVector2D(float(resolution.width()), float(resolution.height())));
    const float timeSec = float(timeUs) / 1'000'000.f;
    program->setUniformValue("u_time", timeSec);
    program->setUniformValue("u_frameIndex", int(std::floor(timeSec * 30.f)));
    // Integer microseconds for hash-stable glitch effects that mirror CPU seeding.
    program->setUniformValue("u_timeUs", float(timeUs));
    program->setUniformValue("u_progress", float(qBound(0.0, progress, 1.0)));

    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        if (drift::isEngineBoundGpuUniform(it.key()))
            continue;
        const int loc = program->uniformLocation(it.key());
        if (loc < 0)
            continue;
        const QVariant &value = it.value();
        if (value.typeId() == QMetaType::Bool) {
            program->setUniformValue(loc, value.toBool() ? 1.f : 0.f);
        } else if (value.userType() == qMetaTypeId<drift::GpuFloatArray>()) {
            const auto array = value.value<drift::GpuFloatArray>();
            if (array.tupleSize > 0 && !array.values.isEmpty()) {
                program->setUniformValueArray(loc, array.values.constData(),
                                              array.values.size() / array.tupleSize,
                                              array.tupleSize);
            }
        } else if (value.typeId() == QMetaType::QString) {
            const QString s = value.toString();
            // File-path params must not fall through to s.toFloat(). A path currently binds as
            // 0.0, which is harmless today but bites the moment a gpu package grows a file param.
            if (s.contains(QLatin1Char('/')) || s.endsWith(QLatin1String(".glb"))
                || s.endsWith(QLatin1String(".gltf"))) {
                continue;
            }
            if (s.startsWith(QLatin1Char('#'))) {
                const QColor c(s);
                program->setUniformValue(loc,
                                         QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF())));
            } else if (it.key() == QLatin1String("position") || it.key() == QLatin1String("blendMode")) {
                float mode = 0.f;
                if (s == QLatin1String("right") || s == QLatin1String("add"))
                    mode = 1.f;
                else if (s == QLatin1String("top") || s == QLatin1String("screen"))
                    mode = 2.f;
                else if (s == QLatin1String("bottom"))
                    mode = 3.f;
                else if (s == QLatin1String("random"))
                    mode = 4.f;
                program->setUniformValue(loc, mode);
            } else {
                program->setUniformValue(loc, s.toFloat());
            }
        } else {
            program->setUniformValue(loc, float(value.toDouble()));
        }
    }
}

GlTarget runPipeline(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &cacheKey,
                     const drift::GpuEffectDefinition &gpu, const std::vector<const GlTarget *> &sources,
                     const QMap<QString, QVariant> &parameters, drift::TimeUs timeUs, double progress,
                     const QSize &canvasSize)
{
    CompiledEffect *compiled = rt.compile(cacheKey, gpu);
    if (!compiled || !compiled->ok || compiled->passes.size() != size_t(gpu.passes.size()))
        return {};

    auto sourceTexAt = [&](int index) -> GLuint {
        if (index < 0 || index >= int(sources.size()))
            return sources.empty() ? 0 : sources[0]->texture();
        return sources[size_t(index)]->texture();
    };

    std::map<QString, GlTarget> buffers;
    bool failed = false;
    for (const drift::GpuEffectBufferSpec &spec : gpu.intermediateBuffers) {
        const int w = qMax(1, int(std::lround(canvasSize.width() * spec.scale)));
        const int h = qMax(1, int(std::lround(canvasSize.height() * spec.scale)));
        GlTarget target = rt.acquireTarget(w, h);
        if (!target.isValid()) {
            qWarning("GlRuntime: FBO alloc failed for buffer %s", qPrintable(spec.id));
            failed = true;
            break;
        }
        buffers.emplace(spec.id, std::move(target));
    }

    std::map<QString, GLuint> textures;
    for (const drift::GpuEffectTextureSpec &spec : gpu.textures)
        textures[spec.id] = staticTexture(rt, gl, spec.path);

    GlTarget canvas = rt.acquireTarget(canvasSize.width(), canvasSize.height());
    if (!canvas.isValid()) {
        qWarning("GlRuntime: canvas FBO alloc failed");
        failed = true;
    }

    for (int i = 0; !failed && i < gpu.passes.size(); ++i) {
        const drift::GpuEffectPass &pass = gpu.passes[i];
        QOpenGLShaderProgram *program = compiled->passes[size_t(i)].program.get();

        QSize inputSize = canvasSize;

        GlTarget *outTarget = nullptr;
        if (pass.output.type == drift::GpuEffectPassOutput::Type::Canvas) {
            outTarget = &canvas;
        } else {
            const auto it = buffers.find(pass.output.bufferId);
            if (it == buffers.end()) {
                failed = true;
                break;
            }
            outTarget = &it->second;
        }

        outTarget->fbo->bind();
        gl->glViewport(0, 0, outTarget->width, outTarget->height);
        gl->glDisable(GL_BLEND);
        gl->glClearColor(0.f, 0.f, 0.f, 0.f);
        gl->glClear(GL_COLOR_BUFFER_BIT);

        program->bind();
        setPackageUniforms(program, parameters, inputSize, timeUs, progress);

        // Bind all declared inputs: unit 0 → u_currentTexture, unit i → u_texture{i}.
        const QList<drift::GpuEffectPassInput> inputs =
            pass.inputs.isEmpty() ? QList<drift::GpuEffectPassInput>{drift::GpuEffectPassInput{}}
                                  : pass.inputs;
        int fromUnit = -1;
        int toUnit = -1;
        for (int texUnit = 0; texUnit < inputs.size(); ++texUnit) {
            const drift::GpuEffectPassInput &in = inputs[texUnit];
            GLuint tex = 0;
            switch (in.type) {
            case drift::GpuEffectPassInput::Type::SourceTexture:
                tex = sourceTexAt(in.sourceIndex);
                if (in.sourceIndex == 0)
                    fromUnit = texUnit;
                else if (in.sourceIndex == 1)
                    toUnit = texUnit;
                break;
            case drift::GpuEffectPassInput::Type::Buffer: {
                const auto it = buffers.find(in.bufferId);
                if (it == buffers.end()) {
                    failed = true;
                    break;
                }
                tex = it->second.texture();
                if (texUnit == 0)
                    inputSize = QSize(it->second.width, it->second.height);
                break;
            }
            case drift::GpuEffectPassInput::Type::Texture: {
                const auto it = textures.find(in.textureId);
                tex = it == textures.end() ? 0 : it->second;
                break;
            }
            }
            if (failed)
                break;

            gl->glActiveTexture(GL_TEXTURE0 + texUnit);
            gl->glBindTexture(GL_TEXTURE_2D, tex);
            if (texUnit == 0)
                program->setUniformValue("u_currentTexture", 0);
            else
                program->setUniformValue(qPrintable(QStringLiteral("u_texture%1").arg(texUnit)), texUnit);
        }

        if (!failed) {
            // Transition-friendly aliases pointing at whichever units hold source 0 and source 1.
            // uniformLocation() returns -1 for names a shader does not declare, so this is free.
            if (fromUnit >= 0)
                program->setUniformValue("u_fromTexture", fromUnit);
            if (toUnit >= 0)
                program->setUniformValue("u_toTexture", toUnit);

            // Re-apply resolution after possible buffer-sized primary input.
            program->setUniformValue("u_resolution",
                                     QVector2D(float(inputSize.width()), float(inputSize.height())));

            gl->glBindVertexArray(rt.vao);
            gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            gl->glBindVertexArray(0);
        }

        program->release();
        outTarget->fbo->release();
    }

    for (auto &entry : buffers)
        rt.releaseTarget(std::move(entry.second));
    buffers.clear();

    if (failed) {
        qWarning("GlRuntime: pass failed for %s — passthrough", qPrintable(cacheKey));
        rt.releaseTarget(std::move(canvas));
        return {};
    }
    return canvas;
}

} // namespace drift::gl
