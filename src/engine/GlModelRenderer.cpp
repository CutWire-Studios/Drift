#include "GlModelRenderer.h"

#include "ModelAsset.h"

#include <QFileInfo>
#include <QMatrix4x4>
#include <QOpenGLShaderProgram>
#include <QtMath>
#include <QVector3D>

#include <cmath>
#include <cstring>
#include <list>
#include <unordered_map>
#include <vector>

namespace drift::gl {
namespace {

// Restores depth/cull/blend/colour-mask/depth-mask on every exit path. Nothing else in GlRuntime
// enables GL_DEPTH_TEST or GL_CULL_FACE, so a leak here makes later fullscreen quads vanish
// intermittently on some drivers.
struct GlStateGuard
{
    QOpenGLExtraFunctions *gl;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean depthMask = GL_TRUE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint depthFunc = GL_LESS;
    GLint cullFaceMode = GL_BACK;
    GLint frontFace = GL_CCW;

    explicit GlStateGuard(QOpenGLExtraFunctions *g)
        : gl(g)
    {
        depthTest = gl->glIsEnabled(GL_DEPTH_TEST);
        cullFace = gl->glIsEnabled(GL_CULL_FACE);
        blend = gl->glIsEnabled(GL_BLEND);
        gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        gl->glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        gl->glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        gl->glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
        gl->glGetIntegerv(GL_FRONT_FACE, &frontFace);
    }

    ~GlStateGuard()
    {
        if (depthTest)
            gl->glEnable(GL_DEPTH_TEST);
        else
            gl->glDisable(GL_DEPTH_TEST);
        if (cullFace)
            gl->glEnable(GL_CULL_FACE);
        else
            gl->glDisable(GL_CULL_FACE);
        if (blend)
            gl->glEnable(GL_BLEND);
        else
            gl->glDisable(GL_BLEND);
        gl->glDepthMask(depthMask);
        gl->glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        gl->glDepthFunc(GLenum(depthFunc));
        gl->glCullFace(GLenum(cullFaceMode));
        gl->glFrontFace(GLenum(frontFace));
    }
};

constexpr const char *kModelVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_mvp;
uniform mat3 u_normalMatrix;
out vec3 v_nrm;
out vec2 v_uv;
void main() {
    v_nrm = normalize(u_normalMatrix * a_nrm);
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

// Approximate Blinn-Phong in sRGB (the rest of the compositor is untagged 8-bit). Not
// glTF-conformant PBR — documented as such. Premultiplied output for the compositor.
constexpr const char *kModelFrag = R"(#version 330 core
in vec3 v_nrm;
in vec2 v_uv;
out vec4 fragColor;
uniform vec4 u_baseColorFactor;
uniform sampler2D u_baseColorTexture;
uniform float u_hasBaseColorTexture;
uniform float u_metallic;
uniform float u_roughness;
uniform vec3 u_emissive;
uniform float u_alphaCutoff;
uniform float u_alphaMode; // 0 opaque, 1 mask, 2 blend
uniform vec3 u_lightDir;   // screen-space, toward the surface
uniform float u_lightIntensity;
uniform float u_ambient;
void main() {
    vec4 base = u_baseColorFactor;
    if (u_hasBaseColorTexture > 0.5)
        base *= texture(u_baseColorTexture, v_uv);
    if (u_alphaMode > 0.5 && u_alphaMode < 1.5 && base.a < u_alphaCutoff)
        discard;

    vec3 n = normalize(v_nrm);
    // Two-sided: light the back face when the material asks for it (handled by disabling cull).
    if (!gl_FrontFacing) n = -n;
    vec3 l = normalize(u_lightDir);
    vec3 v = vec3(0.0, 0.0, 1.0);
    vec3 h = normalize(l + v);
    float ndotl = max(dot(n, l), 0.0);
    float ndoth = max(dot(n, h), 0.0);
    float specPower = mix(64.0, 4.0, clamp(u_roughness, 0.0, 1.0));
    vec3 diffuse = base.rgb * (u_ambient + u_lightIntensity * ndotl);
    vec3 specCol = mix(vec3(0.04), base.rgb, clamp(u_metallic, 0.0, 1.0));
    vec3 specular = specCol * (u_lightIntensity * pow(ndoth, specPower));
    vec3 rgb = diffuse + specular + u_emissive;
    float a = (u_alphaMode > 1.5) ? base.a : 1.0;
    fragColor = vec4(rgb * a, a);
}
)";

constexpr const char *kProxyVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
uniform mat4 u_mvp;
void main() {
    gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)";

constexpr const char *kProxyFrag = R"(#version 330 core
out vec4 fragColor;
void main() {
    fragColor = vec4(0.0);
}
)";

constexpr const char *kCompositeFrag = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture; // source video
uniform sampler2D u_texture1;       // model overlay (premultiplied)
void main() {
    vec4 base = texture(u_currentTexture, v_texCoord);
    vec4 over = texture(u_texture1, v_texCoord);
    // Premultiplied over: rgb = over.rgb + base.rgb * (1 - over.a)
    fragColor = vec4(over.rgb + base.rgb * (1.0 - over.a),
                     over.a + base.a * (1.0 - over.a));
}
)";

constexpr const char *kDownsampleFrag = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_texel; // 1/srcSize
void main() {
    // 2×2 box for supersample resolve.
    vec4 c = texture(u_currentTexture, v_texCoord + u_texel * vec2(-0.5, -0.5));
    c += texture(u_currentTexture, v_texCoord + u_texel * vec2( 0.5, -0.5));
    c += texture(u_currentTexture, v_texCoord + u_texel * vec2(-0.5,  0.5));
    c += texture(u_currentTexture, v_texCoord + u_texel * vec2( 0.5,  0.5));
    fragColor = c * 0.25;
}
)";

void buildUvSphere(int segments, int rings, QVector<float> *verts, QVector<uint32_t> *indices)
{
    // pos3 only — the proxy never shades.
    for (int y = 0; y <= rings; ++y) {
        const float v = float(y) / float(rings);
        const float phi = v * float(M_PI);
        const float sp = std::sin(phi);
        const float cp = std::cos(phi);
        for (int x = 0; x <= segments; ++x) {
            const float u = float(x) / float(segments);
            const float theta = u * float(2.0 * M_PI);
            verts->append(sp * std::cos(theta));
            verts->append(cp);
            verts->append(sp * std::sin(theta));
        }
    }
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t i0 = uint32_t(y * (segments + 1) + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + uint32_t(segments + 1);
            const uint32_t i3 = i2 + 1;
            indices->append(i0);
            indices->append(i2);
            indices->append(i1);
            indices->append(i1);
            indices->append(i2);
            indices->append(i3);
        }
    }
}

QVector3D screenLightDir(double yawDeg, double pitchDeg)
{
    const double yaw = qDegreesToRadians(yawDeg);
    const double pitch = qDegreesToRadians(pitchDeg);
    // Screen space: +x right, +y up (NDC), +z toward camera. Light direction toward the surface.
    const float x = float(std::sin(yaw) * std::cos(pitch));
    const float y = float(std::sin(pitch));
    const float z = float(std::cos(yaw) * std::cos(pitch));
    QVector3D d(x, y, z);
    if (d.lengthSquared() < 1e-8f)
        return QVector3D(0.f, 0.f, 1.f);
    return d.normalized();
}

} // namespace

GlModelGpu *acquireGlModel(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path)
{
    if (path.isEmpty())
        return nullptr;

    auto cpu = loadModelAssetCached(path);
    if (!cpu)
        return nullptr;

    const QFileInfo info(path);
    const QString key =
        info.absoluteFilePath() + QLatin1Char(':') + QString::number(info.lastModified().toMSecsSinceEpoch());

    auto &cache = rt.models;
    const auto existing = cache.index.find(key);
    if (existing != cache.index.end()) {
        cache.lru.splice(cache.lru.begin(), cache.lru, existing->second);
        return &(*existing->second);
    }

    GlModelGpu gpu;
    gpu.key = key;
    gpu.cpu = cpu;

    gl->glGenVertexArrays(1, &gpu.vao);
    gl->glGenBuffers(1, &gpu.vbo);
    gl->glGenBuffers(1, &gpu.ibo);
    gl->glBindVertexArray(gpu.vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, cpu->vertices.size() * int(sizeof(float)),
                     cpu->vertices.constData(), GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, cpu->indices.size() * int(sizeof(uint32_t)),
                     cpu->indices.constData(), GL_STATIC_DRAW);
    const int stride = 8 * int(sizeof(float));
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void *>(3 * sizeof(float)));
    gl->glEnableVertexAttribArray(2);
    gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<void *>(6 * sizeof(float)));
    gl->glBindVertexArray(0);

    gpu.textures.resize(cpu->images.size());
    for (int i = 0; i < cpu->images.size(); ++i) {
        GLuint tex = 0;
        gl->glGenTextures(1, &tex);
        gl->glBindTexture(GL_TEXTURE_2D, tex);
        const QImage &img = cpu->images.at(i);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, img.constBits());
        gl->glGenerateMipmap(GL_TEXTURE_2D);
        // Wrap modes come from the first material that references this image; default REPEAT.
        int wrapS = 10497, wrapT = 10497;
        for (const ModelMaterial &m : cpu->materials) {
            if (m.baseColorTexture == i) {
                wrapS = m.wrapS;
                wrapT = m.wrapT;
                break;
            }
        }
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gpu.textures[i] = tex;
        gpu.vramBytes += size_t(img.width()) * size_t(img.height()) * 4;
    }
    gpu.vramBytes += size_t(cpu->vertices.size()) * sizeof(float)
                     + size_t(cpu->indices.size()) * sizeof(uint32_t);

    cache.lru.push_front(std::move(gpu));
    cache.index[key] = cache.lru.begin();
    cache.totalBytes += cache.lru.front().vramBytes;

    while ((cache.lru.size() > cache.kMaxModels || cache.totalBytes > cache.kMaxBytes)
           && cache.lru.size() > 1) {
        auto last = std::prev(cache.lru.end());
        if (last->vao) {
            gl->glDeleteVertexArrays(1, &last->vao);
            gl->glDeleteBuffers(1, &last->vbo);
            gl->glDeleteBuffers(1, &last->ibo);
            for (GLuint t : last->textures)
                gl->glDeleteTextures(1, &t);
        }
        cache.totalBytes -= last->vramBytes;
        cache.index.erase(last->key);
        cache.lru.erase(last);
    }

    return &cache.lru.front();
}

void destroyGlModels(GlRuntime &rt, QOpenGLExtraFunctions *gl)
{
    if (gl) {
        for (GlModelGpu &m : rt.models.lru) {
            if (m.vao)
                gl->glDeleteVertexArrays(1, &m.vao);
            if (m.vbo)
                gl->glDeleteBuffers(1, &m.vbo);
            if (m.ibo)
                gl->glDeleteBuffers(1, &m.ibo);
            for (GLuint t : m.textures)
                gl->glDeleteTextures(1, &t);
        }
        if (rt.headProxy.vao) {
            gl->glDeleteVertexArrays(1, &rt.headProxy.vao);
            gl->glDeleteBuffers(1, &rt.headProxy.vbo);
            gl->glDeleteBuffers(1, &rt.headProxy.ibo);
            rt.headProxy = {};
        }
    }
    rt.models.lru.clear();
    rt.models.index.clear();
    rt.models.totalBytes = 0;
}

void ensureHeadProxy(GlRuntime &rt, QOpenGLExtraFunctions *gl)
{
    if (rt.headProxy.vao)
        return;
    QVector<float> verts;
    QVector<uint32_t> indices;
    buildUvSphere(24, 16, &verts, &indices);
    rt.headProxy.indexCount = indices.size();
    gl->glGenVertexArrays(1, &rt.headProxy.vao);
    gl->glGenBuffers(1, &rt.headProxy.vbo);
    gl->glGenBuffers(1, &rt.headProxy.ibo);
    gl->glBindVertexArray(rt.headProxy.vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, rt.headProxy.vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, verts.size() * int(sizeof(float)), verts.constData(),
                     GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rt.headProxy.ibo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * int(sizeof(uint32_t)),
                     indices.constData(), GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * int(sizeof(float)), nullptr);
    gl->glBindVertexArray(0);
}

GlTarget drawFaceModelEffect(GlRuntime &rt, QOpenGLExtraFunctions *gl, const FaceModelParams &params,
                             const QList<FaceAnchors> &faceSlots, const GlTarget &source)
{
    if (!source.isValid() || !gl)
        return {};
    if (params.modelPath.isEmpty())
        return {}; // freshly added effect — silent

    if (params.faceIndex < 0 || params.faceIndex >= faceSlots.size())
        return {};
    const FaceAnchors &face = faceSlots.at(params.faceIndex);
    if (!face.valid || !face.hasPose)
        return {};

    GlModelGpu *model = acquireGlModel(rt, gl, params.modelPath);
    if (!model || !model->cpu)
        return {};

    const int srcW = source.width;
    const int srcH = source.height;
    const bool supersample = (qint64(srcW) * srcH) <= (1920LL * 1080LL);
    const int drawW = supersample ? srcW * 2 : srcW;
    const int drawH = supersample ? srcH * 2 : srcH;
    const double aspect = double(srcH) / double(srcW); // height/width — WYSIWYG uses this alone

    GlTarget overlay = rt.acquireTarget(drawW, drawH, /*wantDepth=*/true);
    if (!overlay.isValid())
        return {};

    GlStateGuard guard(gl);

    overlay.fbo->bind();
    gl->glViewport(0, 0, drawW, drawH);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gl->glEnable(GL_DEPTH_TEST);
    gl->glDepthFunc(GL_LESS);
    gl->glEnable(GL_CULL_FACE);
    gl->glCullFace(GL_BACK);
    gl->glFrontFace(GL_CCW);

    const QMatrix4x4 modelMvp = faceModelMvp(face, params, aspect);
    // Normal matrix: upper-left 3×3 of the head*user transform (without the NDC map). Approximate
    // with the MVP's 3×3 — lighting is screen-space anyway, so absolute orientation matters less
    // than relative facing.
    QMatrix3x3 normalMatrix;
    {
        const QMatrix4x4 nm = modelMvp;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                normalMatrix(r, c) = nm(r, c);
    }

    // Head proxy occlusion: depth-only. Cull is off — the Z-negation that makes glTF winding
    // face the camera also flips the sphere, and a culled proxy writes no depth at all.
    if (params.occlusion) {
        ensureHeadProxy(rt, gl);
        QOpenGLShaderProgram *proxyProg =
            rt.builtinProgram(QStringLiteral("face_head_proxy"), kProxyVert, kProxyFrag);
        if (proxyProg) {
            proxyProg->bind();
            proxyProg->setUniformValue("u_mvp", faceHeadProxyMvp(face, params, aspect));
            gl->glDisable(GL_CULL_FACE);
            gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            gl->glDepthMask(GL_TRUE);
            gl->glBindVertexArray(rt.headProxy.vao);
            gl->glDrawElements(GL_TRIANGLES, rt.headProxy.indexCount, GL_UNSIGNED_INT, nullptr);
            gl->glBindVertexArray(0);
            gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            gl->glEnable(GL_CULL_FACE);
            proxyProg->release();
        }
    }

    QOpenGLShaderProgram *prog =
        rt.builtinProgram(QStringLiteral("face_model"), kModelVert, kModelFrag);
    if (!prog) {
        overlay.fbo->release();
        rt.releaseTarget(std::move(overlay));
        return {};
    }

    prog->bind();
    prog->setUniformValue("u_mvp", modelMvp);
    prog->setUniformValue("u_normalMatrix", normalMatrix);
    prog->setUniformValue("u_lightDir", screenLightDir(params.lightYaw, params.lightPitch));
    prog->setUniformValue("u_lightIntensity", float(params.lightIntensity));
    prog->setUniformValue("u_ambient", float(params.ambient));
    prog->setUniformValue("u_baseColorTexture", 0);

    gl->glBindVertexArray(model->vao);

    auto drawPrim = [&](const ModelPrimitive &prim, bool depthWrite) {
        const ModelMaterial &mat = model->cpu->materials.at(
            qBound(0, prim.material, model->cpu->materials.size() - 1));
        prog->setUniformValue("u_baseColorFactor", mat.baseColorFactor);
        prog->setUniformValue("u_metallic", mat.metallicFactor);
        prog->setUniformValue("u_roughness", mat.roughnessFactor);
        prog->setUniformValue("u_emissive", mat.emissiveFactor);
        prog->setUniformValue("u_alphaCutoff", mat.alphaCutoff);
        float mode = 0.f;
        if (mat.alphaMode == ModelMaterial::AlphaMode::Mask)
            mode = 1.f;
        else if (mat.alphaMode == ModelMaterial::AlphaMode::Blend)
            mode = 2.f;
        prog->setUniformValue("u_alphaMode", mode);

        const bool hasTex = mat.baseColorTexture >= 0 && mat.baseColorTexture < model->textures.size();
        prog->setUniformValue("u_hasBaseColorTexture", hasTex ? 1.f : 0.f);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, hasTex ? model->textures.at(mat.baseColorTexture) : 0);

        if (mat.doubleSided)
            gl->glDisable(GL_CULL_FACE);
        else
            gl->glEnable(GL_CULL_FACE);

        gl->glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
        if (mat.alphaMode == ModelMaterial::AlphaMode::Blend) {
            gl->glEnable(GL_BLEND);
            gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            gl->glDisable(GL_BLEND);
        }

        gl->glDrawElements(GL_TRIANGLES, prim.indexCount, GL_UNSIGNED_INT,
                           reinterpret_cast<void *>(qint64(prim.firstIndex) * sizeof(uint32_t)));
    };

    // Opaque / MASK first (depth write), then BLEND (depth test, no write), matching CPU sort.
    for (const ModelPrimitive &prim : model->cpu->primitives) {
        const ModelMaterial &mat = model->cpu->materials.at(
            qBound(0, prim.material, model->cpu->materials.size() - 1));
        if (mat.alphaMode != ModelMaterial::AlphaMode::Blend)
            drawPrim(prim, true);
    }
    for (const ModelPrimitive &prim : model->cpu->primitives) {
        const ModelMaterial &mat = model->cpu->materials.at(
            qBound(0, prim.material, model->cpu->materials.size() - 1));
        if (mat.alphaMode == ModelMaterial::AlphaMode::Blend)
            drawPrim(prim, false);
    }

    gl->glBindVertexArray(0);
    prog->release();
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_CULL_FACE);
    gl->glDisable(GL_BLEND);
    overlay.fbo->release();

    // Resolve supersample, then composite over source.
    GlTarget overlayFull;
    if (supersample) {
        overlayFull = rt.acquireTarget(srcW, srcH);
        if (!overlayFull.isValid()) {
            rt.releaseTarget(std::move(overlay));
            return {};
        }
        QOpenGLShaderProgram *down =
            rt.builtinProgram(QStringLiteral("face_model_downsample"), kQuadVertexShader,
                              kDownsampleFrag);
        if (!down) {
            rt.releaseTarget(std::move(overlay));
            rt.releaseTarget(std::move(overlayFull));
            return {};
        }
        overlayFull.fbo->bind();
        gl->glViewport(0, 0, srcW, srcH);
        down->bind();
        down->setUniformValue("u_currentTexture", 0);
        down->setUniformValue("u_texel", QVector2D(1.f / float(drawW), 1.f / float(drawH)));
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, overlay.texture());
        gl->glBindVertexArray(rt.vao);
        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl->glBindVertexArray(0);
        down->release();
        overlayFull.fbo->release();
        rt.releaseTarget(std::move(overlay));
    } else {
        overlayFull = std::move(overlay);
    }

    GlTarget result = rt.acquireTarget(srcW, srcH);
    if (!result.isValid()) {
        rt.releaseTarget(std::move(overlayFull));
        return {};
    }

    QOpenGLShaderProgram *comp =
        rt.builtinProgram(QStringLiteral("face_model_composite"), kQuadVertexShader, kCompositeFrag);
    if (!comp) {
        rt.releaseTarget(std::move(overlayFull));
        rt.releaseTarget(std::move(result));
        return {};
    }

    result.fbo->bind();
    gl->glViewport(0, 0, srcW, srcH);
    comp->bind();
    comp->setUniformValue("u_currentTexture", 0);
    comp->setUniformValue("u_texture1", 1);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, source.texture());
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, overlayFull.texture());
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    comp->release();
    result.fbo->release();

    rt.releaseTarget(std::move(overlayFull));
    return result;
}

} // namespace drift::gl
