#include "GlModelRenderer.h"

#include "ModelAnimation.h"
#include "ModelClipRenderer.h"
#include "ModelClipTransform.h"

#include "FaceMesh.h"
#include "ModelAsset.h"

#include <QFileInfo>
#include <QMatrix4x4>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QtMath>
#include <QVector>
#include <QVector3D>

#include <cmath>
#include <cstring>
#include <list>
#include <unordered_map>

// Qt's GLES2 headers stop short of these ES 3.0 constants; the functions exist on every context
// Drift creates.
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#include <vector>

namespace drift::gl {
namespace {

constexpr const char *kModelVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec2 a_uv;
uniform mat4 u_mvp;
uniform mat3 u_normalMatrix;
// +1 keep clip z; -1 negate. MediaPipe z is smaller when nearer, but wnToNdc uses
// z_ndc = −z_wn/4 for glTF props whose +Z is toward the viewer. A warped mesh
// round-trips MediaPipe points, so without the flip the far side of the nose
// wins GL_LESS and shows through the near side. XY is unchanged.
uniform float u_flipDepth;
out vec3 v_nrm;
out vec2 v_uv;
void main() {
    v_nrm = normalize(u_normalMatrix * a_nrm);
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_pos, 1.0);
    gl_Position.z *= u_flipDepth;
}
)";

// Skinned variant for model clips: every vertex blends up to four palette rows (a rigid vertex
// has one row at weight one), read from a 4×N RGBA32F texture so the joint count is bounded by
// texture height rather than the vertex uniform budget. `highp` on the sampler is explicit:
// ESSL defaults vertex-stage samplers to lowp, and the ES preamble only patches fragment shaders.
constexpr const char *kModelSkinVert = R"(#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec4 a_joints;
layout(location = 4) in vec4 a_weights;
uniform mat4 u_post;          // projection * view * user transform, applied after skinning
uniform mat3 u_normalMatrix;  // mat3(view * user transform)
uniform highp sampler2D u_palette;
out vec3 v_nrm;
out vec2 v_uv;
mat4 paletteRow(int i) {
    return mat4(texelFetch(u_palette, ivec2(0, i), 0),
                texelFetch(u_palette, ivec2(1, i), 0),
                texelFetch(u_palette, ivec2(2, i), 0),
                texelFetch(u_palette, ivec2(3, i), 0));
}
void main() {
    mat4 skin = a_weights.x * paletteRow(int(a_joints.x + 0.5))
              + a_weights.y * paletteRow(int(a_joints.y + 0.5))
              + a_weights.z * paletteRow(int(a_joints.z + 0.5))
              + a_weights.w * paletteRow(int(a_joints.w + 0.5));
    v_nrm = normalize(u_normalMatrix * (mat3(skin) * a_nrm));
    v_uv = a_uv;
    gl_Position = u_post * (skin * vec4(a_pos, 1.0));
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

// Barycentric wireframe: GS stamps (1,0,0)/(0,1,0)/(0,0,1) so the FS can find triangle edges
// from fwidth(min bary) — thickness stays ~1px regardless of triangle size in clip space.
constexpr const char *kModelGeom = R"(#version 330 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
in vec3 v_nrm[];
in vec2 v_uv[];
out vec3 g_nrm;
out vec2 g_uv;
noperspective out vec3 v_bary;
void main() {
    const vec3 bary[3] = vec3[](vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        g_nrm = v_nrm[i];
        g_uv = v_uv[i];
        v_bary = bary[i];
        EmitVertex();
    }
    EndPrimitive();
}
)";

constexpr const char *kModelFillWireFrag = R"(#version 330 core
in vec3 g_nrm;
in vec2 g_uv;
noperspective in vec3 v_bary;
out vec4 fragColor;
uniform vec4 u_baseColorFactor;
uniform sampler2D u_baseColorTexture;
uniform float u_hasBaseColorTexture;
uniform float u_metallic;
uniform float u_roughness;
uniform vec3 u_emissive;
uniform float u_alphaCutoff;
uniform float u_alphaMode;
uniform vec3 u_lightDir;
uniform float u_lightIntensity;
uniform float u_ambient;
uniform float u_fillOpacity;
uniform float u_wireframe;
uniform vec3 u_wireColor;
uniform float u_wireWidth;
void main() {
    vec4 base = u_baseColorFactor;
    if (u_hasBaseColorTexture > 0.5)
        base *= texture(u_baseColorTexture, g_uv);
    if (u_alphaMode > 0.5 && u_alphaMode < 1.5 && base.a < u_alphaCutoff)
        discard;

    vec3 n = normalize(g_nrm);
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
    a *= u_fillOpacity;

    float minBary = min(min(v_bary.x, v_bary.y), v_bary.z);
    float edgeWidth = fwidth(minBary) * max(u_wireWidth, 1e-4);
    float edge = 1.0 - smoothstep(0.0, edgeWidth, minBary);
    if (u_wireframe < 0.5)
        edge = 0.0;
    rgb = mix(rgb, u_wireColor, edge);
    a = max(a, edge);
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

// Shared by the face-prop effect and the model clip. The GL call order here is what the face
// effect has always emitted; keep it, the effect's output must stay bit-identical.
void bindModelMaterial(QOpenGLExtraFunctions *gl, QOpenGLShaderProgram *prog,
                       const ModelMaterial &mat, const QVector<GLuint> *textures, bool depthWrite,
                       bool forceBlend)
{
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

    const bool hasTex =
        textures && mat.baseColorTexture >= 0 && mat.baseColorTexture < textures->size();
    prog->setUniformValue("u_hasBaseColorTexture", hasTex ? 1.f : 0.f);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, hasTex ? textures->at(mat.baseColorTexture) : 0);

    if (mat.doubleSided)
        gl->glDisable(GL_CULL_FACE);
    else
        gl->glEnable(GL_CULL_FACE);

    gl->glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    if (forceBlend || mat.alphaMode == ModelMaterial::AlphaMode::Blend) {
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        gl->glDisable(GL_BLEND);
    }
}

// Caller binds the VAO. Opaque / MASK first (depth write), then BLEND (depth test, no write).
void drawModelPrimitives(QOpenGLExtraFunctions *gl, QOpenGLShaderProgram *prog,
                         const QList<ModelPrimitive> &primitives,
                         const QList<ModelMaterial> &materials, const QVector<GLuint> *textures,
                         bool forceBlend)
{
    auto drawPrim = [&](const ModelPrimitive &prim, bool depthWrite) {
        const ModelMaterial &mat = materials.at(qBound(0, prim.material, materials.size() - 1));
        bindModelMaterial(gl, prog, mat, textures, depthWrite, forceBlend);
        gl->glDrawElements(GL_TRIANGLES, prim.indexCount, GL_UNSIGNED_INT,
                           reinterpret_cast<void *>(qint64(prim.firstIndex) * sizeof(uint32_t)));
    };

    if (forceBlend) {
        for (const ModelPrimitive &prim : primitives)
            drawPrim(prim, false);
        return;
    }
    for (const ModelPrimitive &prim : primitives) {
        const ModelMaterial &mat = materials.at(qBound(0, prim.material, materials.size() - 1));
        if (mat.alphaMode != ModelMaterial::AlphaMode::Blend)
            drawPrim(prim, true);
    }
    for (const ModelPrimitive &prim : primitives) {
        const ModelMaterial &mat = materials.at(qBound(0, prim.material, materials.size() - 1));
        if (mat.alphaMode == ModelMaterial::AlphaMode::Blend)
            drawPrim(prim, false);
    }
}

// Resolve for the model clip: flip rows (the draw is +y up, the compositor wants v=0 on top),
// box-filter when supersampled, and unpremultiply because layer targets carry straight alpha.
constexpr const char *kModelClipResolveFrag = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_texel;  // 1/srcSize
uniform float u_taps;  // 1 = single sample, 2 = 2x2 box
void main() {
    vec2 uv = vec2(v_texCoord.x, 1.0 - v_texCoord.y);
    vec4 c;
    if (u_taps > 1.5) {
        c  = texture(u_currentTexture, uv + u_texel * vec2(-0.5, -0.5));
        c += texture(u_currentTexture, uv + u_texel * vec2( 0.5, -0.5));
        c += texture(u_currentTexture, uv + u_texel * vec2(-0.5,  0.5));
        c += texture(u_currentTexture, uv + u_texel * vec2( 0.5,  0.5));
        c *= 0.25;
    } else {
        c = texture(u_currentTexture, uv);
    }
    fragColor = vec4(c.a > 0.0 ? c.rgb / c.a : vec3(0.0), c.a);
}
)";

} // namespace

namespace {

void deleteRigGpu(QOpenGLExtraFunctions *gl, GlModelGpu &m)
{
    if (m.rigVao)
        gl->glDeleteVertexArrays(1, &m.rigVao);
    if (m.rigVbo)
        gl->glDeleteBuffers(1, &m.rigVbo);
    if (m.rigIbo)
        gl->glDeleteBuffers(1, &m.rigIbo);
    if (m.paletteTex)
        gl->glDeleteTextures(1, &m.paletteTex);
    m.rigVao = m.rigVbo = m.rigIbo = m.paletteTex = 0;
    m.paletteRows = 0;
}

// Uploads the rig's vertex streams and allocates the palette texture the first time an animated
// file is drawn as a clip. The face effect never reaches this, so it never pays for it.
bool ensureRigGpu(GlRuntime &rt, QOpenGLExtraFunctions *gl, GlModelGpu &m)
{
    if (m.rigVao)
        return true;
    if (!m.cpu || !m.cpu->rig)
        return false;
    const ModelRig &rig = *m.cpu->rig;

    gl->glGenVertexArrays(1, &m.rigVao);
    gl->glGenBuffers(1, &m.rigVbo);
    gl->glGenBuffers(1, &m.rigIbo);
    gl->glBindVertexArray(m.rigVao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, m.rigVbo);
    gl->glBufferData(GL_ARRAY_BUFFER, rig.vertices.size() * int(sizeof(float)),
                     rig.vertices.constData(), GL_STATIC_DRAW);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.rigIbo);
    gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER, rig.indices.size() * int(sizeof(uint32_t)),
                     rig.indices.constData(), GL_STATIC_DRAW);
    const int stride = kRigVertStride * int(sizeof(float));
    const int sizes[5] = {3, 3, 2, 4, 4};
    int offset = 0;
    for (int i = 0; i < 5; ++i) {
        gl->glEnableVertexAttribArray(GLuint(i));
        gl->glVertexAttribPointer(GLuint(i), sizes[i], GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>(qintptr(offset) * qintptr(sizeof(float))));
        offset += sizes[i];
    }
    gl->glBindVertexArray(0);

    m.paletteRows = rig.paletteSize;
    gl->glGenTextures(1, &m.paletteTex);
    gl->glBindTexture(GL_TEXTURE_2D, m.paletteTex);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, m.paletteRows, 0, GL_RGBA, GL_FLOAT, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glBindTexture(GL_TEXTURE_2D, 0);

    const size_t bytes = size_t(rig.vertices.size()) * sizeof(float)
                         + size_t(rig.indices.size()) * sizeof(uint32_t)
                         + size_t(m.paletteRows) * 4 * 16;
    m.vramBytes += bytes;
    rt.models.totalBytes += bytes;
    return true;
}

// Each palette row is one mat4, column by column — QMatrix4x4::constData() is column-major, so
// the sixteen floats map straight onto the four texels.
void uploadPalette(QOpenGLExtraFunctions *gl, const GlModelGpu &m, const QVector<QMatrix4x4> &palette)
{
    const int rows = std::min(m.paletteRows, int(palette.size()));
    if (rows <= 0)
        return;
    QVector<float> texels(rows * 16);
    for (int r = 0; r < rows; ++r)
        std::memcpy(texels.data() + r * 16, palette[r].constData(), 16 * sizeof(float));
    gl->glBindTexture(GL_TEXTURE_2D, m.paletteTex);
    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, rows, GL_RGBA, GL_FLOAT, texels.constData());
}

} // namespace

GlModelGpu *acquireGlModel(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path)
{
    if (path.isEmpty())
        return nullptr;
    return acquireGlModel(rt, gl, path, loadModelAssetCached(path));
}

GlModelGpu *acquireGlModel(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path,
                           std::shared_ptr<const ModelAsset> cpu)
{
    if (path.isEmpty() || !cpu)
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
        // GLES 3.0 headers (what QOpenGLExtraFunctions sees on Android) do not define
        // GL_RGBA8. Unsized GL_RGBA is the same upload GlRuntime already uses for 2D textures.
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0, GL_RGBA,
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
        deleteRigGpu(gl, *last);
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
            deleteRigGpu(gl, m);
        }
        if (rt.headProxy.vao) {
            gl->glDeleteVertexArrays(1, &rt.headProxy.vao);
            gl->glDeleteBuffers(1, &rt.headProxy.vbo);
            gl->glDeleteBuffers(1, &rt.headProxy.ibo);
            rt.headProxy = {};
        }
        if (rt.faceMesh.vao) {
            gl->glDeleteVertexArrays(1, &rt.faceMesh.vao);
            gl->glDeleteBuffers(1, &rt.faceMesh.vbo);
            gl->glDeleteBuffers(1, &rt.faceMesh.ibo);
            rt.faceMesh = {};
        }
        if (rt.faceSwapMesh.vao) {
            gl->glDeleteVertexArrays(1, &rt.faceSwapMesh.vao);
            gl->glDeleteBuffers(1, &rt.faceSwapMesh.vbo);
            gl->glDeleteBuffers(1, &rt.faceSwapMesh.ibo);
            rt.faceSwapMesh = {};
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

namespace {

bool uploadFaceMeshGpu(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path,
                       const FaceMeshRest &rest, const QVector<QVector3D> &pos,
                       const QVector<QVector3D> &nrm)
{
    if (pos.isEmpty() || rest.indices.isEmpty())
        return false;

    const int vertexCount = pos.size();
    QVector<float> verts(vertexCount * 8);
    for (int i = 0; i < vertexCount; ++i) {
        const QVector3D &p = pos.at(i);
        const QVector3D n = (i < nrm.size()) ? nrm.at(i) : QVector3D(0.f, 0.f, 1.f);
        verts[i * 8 + 0] = p.x();
        verts[i * 8 + 1] = p.y();
        verts[i * 8 + 2] = p.z();
        verts[i * 8 + 3] = n.x();
        verts[i * 8 + 4] = n.y();
        verts[i * 8 + 5] = n.z();
        verts[i * 8 + 6] = 0.f;
        verts[i * 8 + 7] = 0.f;
    }

    auto &gpu = rt.faceMesh;
    const int stride = 8 * int(sizeof(float));
    if (!gpu.vao) {
        gl->glGenVertexArrays(1, &gpu.vao);
        gl->glGenBuffers(1, &gpu.vbo);
        gl->glGenBuffers(1, &gpu.ibo);
        gl->glBindVertexArray(gpu.vao);
        gl->glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void *>(0));
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>(3 * sizeof(float)));
        gl->glEnableVertexAttribArray(2);
        gl->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<void *>(6 * sizeof(float)));
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
        gl->glBindVertexArray(0);
    }

    gl->glBindVertexArray(gpu.vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, gpu.vbo);
    const GLsizeiptr vboBytes = GLsizeiptr(verts.size()) * GLsizeiptr(sizeof(float));
    if (gpu.vertexCount != vertexCount) {
        gl->glBufferData(GL_ARRAY_BUFFER, vboBytes, verts.constData(), GL_STREAM_DRAW);
        gpu.vertexCount = vertexCount;
    } else {
        gl->glBufferSubData(GL_ARRAY_BUFFER, 0, vboBytes, verts.constData());
    }

    const int indexCount = rest.indices.size();
    if (gpu.path != path || gpu.indexCount != indexCount) {
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.ibo);
        gl->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         GLsizeiptr(indexCount) * GLsizeiptr(sizeof(uint32_t)),
                         rest.indices.constData(), GL_STATIC_DRAW);
        gpu.path = path;
        gpu.indexCount = indexCount;
    }
    gl->glBindVertexArray(0);
    return true;
}

} // namespace

GlTarget drawFaceModelEffect(GlRuntime &rt, QOpenGLExtraFunctions *gl, const FaceModelParams &params,
                             const QList<FaceAnchors> &faceSlots, const GlTarget &source)
{
    if (!source.isValid() || !gl)
        return {};

    if (params.faceIndex < 0 || params.faceIndex >= faceSlots.size())
        return {};
    const FaceAnchors &face = faceSlots.at(params.faceIndex);
    if (!face.valid || !face.hasPose)
        return {};

    GlModelGpu *model = nullptr;
    if (params.warpMesh) {
        if (!face.hasMesh || face.mesh.size() != kFaceMeshPoints)
            return {};
        auto rest = loadFaceMeshRest(params.modelPath);
        if (!rest)
            return {};
        QVector<QVector3D> pos;
        QVector<QVector3D> nrm;
        warpFaceMesh(*rest, face, &pos, &nrm);
        if (!uploadFaceMeshGpu(rt, gl, params.modelPath, *rest, pos, nrm))
            return {};
    } else {
        if (params.modelPath.isEmpty())
            return {}; // freshly added effect — silent
        model = acquireGlModel(rt, gl, params.modelPath);
        if (!model || !model->cpu)
            return {};
    }

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
    // Warped overlay: real MediaPipe frontal is a half-turn about X (forward.z < 0), which
    // reverses screen winding versus the identity-quat tests. GL_BACK would cull the whole
    // face. Props keep CCW cull; the warp path disables it just before draw.
    if (params.warpMesh) {
        gl->glDisable(GL_CULL_FACE);
    } else {
        gl->glEnable(GL_CULL_FACE);
        gl->glCullFace(GL_BACK);
        gl->glFrontFace(GL_CCW);
    }

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
    //
    // Skip it for the warped face mesh: that mesh *is* the face surface, so the ellipsoid
    // clips cheeks and the nose instead of hiding a prop behind the head.
    if (params.occlusion && !params.warpMesh) {
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

    // Opaque props stay on face_model (no GS) so they remain bit-identical. Fill+wire uses a
    // separate program id so we never relink the cached opaque shader with a geometry stage.
    //
    // GLES has neither a geometry stage nor `noperspective`, so face_model_fillwire cannot be
    // compiled there at all. The effect's defaults ask for it (wireframe on, fillOpacity 0.25),
    // so an ES context is forced onto the opaque program rather than failing to link; the
    // inspector hides the four fill/wire controls to match.
    const QOpenGLContext *context = QOpenGLContext::currentContext();
    const bool esContext = context && context->isOpenGLES();
    constexpr double kOpaqueFillEps = 1e-6;
    const bool useFillWire =
        !esContext && (params.wireframe || (params.fillOpacity < 1.0 - kOpaqueFillEps));
    const bool forceBlend = !esContext && (params.fillOpacity < 1.0 - kOpaqueFillEps);

    QOpenGLShaderProgram *prog =
        useFillWire ? rt.builtinProgram(QStringLiteral("face_model_fillwire"), kModelVert,
                                        kModelFillWireFrag, kModelGeom)
                    : rt.builtinProgram(QStringLiteral("face_model"), kModelVert, kModelFrag);
    if (!prog) {
        overlay.fbo->release();
        rt.releaseTarget(std::move(overlay));
        return {};
    }

    prog->bind();
    prog->setUniformValue("u_mvp", modelMvp);
    prog->setUniformValue("u_normalMatrix", normalMatrix);
    prog->setUniformValue("u_flipDepth", params.warpMesh ? -1.f : 1.f);
    prog->setUniformValue("u_lightDir", screenLightDir(params.lightYaw, params.lightPitch));
    prog->setUniformValue("u_lightIntensity", float(params.lightIntensity));
    prog->setUniformValue("u_ambient", float(params.ambient));
    prog->setUniformValue("u_baseColorTexture", 0);
    if (useFillWire) {
        prog->setUniformValue("u_fillOpacity", float(qBound(0.0, params.fillOpacity, 1.0)));
        prog->setUniformValue("u_wireframe", params.wireframe ? 1.f : 0.f);
        prog->setUniformValue("u_wireColor", params.wireColor);
        prog->setUniformValue("u_wireWidth", float(params.wireWidth));
    }

    auto bindMaterial = [&](const ModelMaterial &mat, const QVector<GLuint> *textures,
                            bool depthWrite) {
        bindModelMaterial(gl, prog, mat, textures, depthWrite, forceBlend);
    };

    if (model) {
        gl->glBindVertexArray(model->vao);
        drawModelPrimitives(gl, prog, model->cpu->primitives, model->cpu->materials,
                            &model->textures, forceBlend);
    } else {
        ModelMaterial meshMat;
        meshMat.metallicFactor = 0.f;
        meshMat.roughnessFactor = 0.6f;
        // MediaPipe frontal: right=+X, up=−Y, forward=−Z. That winding is opposite the
        // identity quaternion, so keep both sides. Clip-z flip + depth write still pick
        // the nearer surface (the far side of the nose must not win GL_LESS).
        meshMat.doubleSided = true;
        // Overlay FBO starts cleared. Blending overlapping triangles (and skipping depth write)
        // lets the far cheek composite through the nose — "occlusion is bugged" while the fit
        // still looks locked. Write nearest-surface colour with depth; translucency is the
        // later composite over the video. Shader already bakes fillOpacity into alpha.
        bindMaterial(meshMat, nullptr, /*depthWrite=*/true);
        gl->glDisable(GL_BLEND);
        gl->glDisable(GL_CULL_FACE);
        gl->glDepthMask(GL_TRUE);
        gl->glBindVertexArray(rt.faceMesh.vao);
        gl->glDrawElements(GL_TRIANGLES, rt.faceMesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }

    gl->glBindVertexArray(0);
    prog->release();
    gl->glDepthMask(GL_TRUE);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_CULL_FACE);
    gl->glDisable(GL_BLEND);
    overlay.fbo->release();

    return resolveFaceOverlay(rt, gl, std::move(overlay), source);
}

GlTarget resolveFaceOverlay(GlRuntime &rt, QOpenGLExtraFunctions *gl, GlTarget &&overlay,
                            const GlTarget &source)
{
    const int srcW = source.width;
    const int srcH = source.height;
    const int drawW = overlay.width;
    const int drawH = overlay.height;

    GlTarget overlayFull;
    if (drawW != srcW || drawH != srcH) {
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

GlTarget drawModelClip(GlRuntime &rt, QOpenGLExtraFunctions *gl,
                       const model3d::ModelDrawRequest &request, const QSize &canvasSize)
{
    if (!gl || !request.asset || canvasSize.isEmpty())
        return {};

    GlModelGpu *model = acquireGlModel(rt, gl, request.path, request.asset);
    if (!model || !model->cpu)
        return {};

    const int outW = canvasSize.width();
    const int outH = canvasSize.height();
    const bool supersample = (qint64(outW) * outH) <= (1920LL * 1080LL);
    const int drawW = supersample ? outW * 2 : outW;
    const int drawH = supersample ? outH * 2 : outH;
    const double aspect = double(outH) / double(outW);

    GlTarget scratch = rt.acquireTarget(drawW, drawH, /*wantDepth=*/true);
    if (!scratch.isValid())
        return {};

    // Animated files draw through the rig with a palette; a static file (or a rig the GPU cannot
    // take) draws the baked buffer with the face effect's program.
    const bool useRig = model->cpu->rig && ensureRigGpu(rt, gl, *model);
    QOpenGLShaderProgram *prog =
        useRig ? rt.builtinProgram(QStringLiteral("model_clip_skin"), kModelSkinVert, kModelFrag)
               : rt.builtinProgram(QStringLiteral("face_model"), kModelVert, kModelFrag);
    if (!prog && useRig)
        prog = rt.builtinProgram(QStringLiteral("face_model"), kModelVert, kModelFrag);
    QOpenGLShaderProgram *resolve = rt.builtinProgram(QStringLiteral("model_clip_resolve"),
                                                      kQuadVertexShader, kModelClipResolveFrag);
    if (!prog || !resolve) {
        rt.releaseTarget(std::move(scratch));
        return {};
    }
    const bool skinning = useRig && prog->programId() != 0
                          && prog->uniformLocation("u_palette") >= 0;

    {
        GlStateGuard guard(gl);
        scratch.fbo->bind();
        gl->glViewport(0, 0, drawW, drawH);
        gl->glClearColor(0.f, 0.f, 0.f, 0.f);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthFunc(GL_LESS);
        gl->glEnable(GL_CULL_FACE);
        gl->glCullFace(GL_BACK);
        gl->glFrontFace(GL_CCW);

        const ModelAsset &cpu = *model->cpu;
        QVector3D aabbMin;
        QVector3D aabbMax;
        if (skinning)
            modelClipBounds(cpu, &aabbMin, &aabbMax);
        else {
            aabbMin = cpu.aabbMin;
            aabbMax = cpu.aabbMax;
        }
        const ModelClipCamera cam = modelClipCamera(request.params, aabbMin, aabbMax, aspect);

        prog->bind();
        if (skinning) {
            const QVector<QMatrix4x4> &palette =
                request.pose ? request.pose->palette : evaluateModelPose(*cpu.rig, -1, 0.0).palette;
            gl->glActiveTexture(GL_TEXTURE1);
            uploadPalette(gl, *model, palette);
            prog->setUniformValue("u_palette", 1);
            prog->setUniformValue("u_post", cam.mvp);
        } else {
            prog->setUniformValue("u_mvp", cam.mvp);
            prog->setUniformValue("u_flipDepth", 1.f);
        }
        prog->setUniformValue("u_normalMatrix", cam.normalMatrix);
        prog->setUniformValue("u_lightDir",
                              screenLightDir(request.params.lightYaw, request.params.lightPitch));
        prog->setUniformValue("u_lightIntensity", float(request.params.lightIntensity));
        prog->setUniformValue("u_ambient", float(request.params.ambient));
        prog->setUniformValue("u_baseColorTexture", 0);

        gl->glBindVertexArray(skinning ? model->rigVao : model->vao);
        drawModelPrimitives(gl, prog, skinning ? cpu.rig->primitives : cpu.primitives,
                            cpu.materials, &model->textures, /*forceBlend=*/false);
        gl->glBindVertexArray(0);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        gl->glActiveTexture(GL_TEXTURE0);
        prog->release();
        gl->glDepthMask(GL_TRUE);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDisable(GL_CULL_FACE);
        gl->glDisable(GL_BLEND);
        scratch.fbo->release();
    }

    GlTarget result = rt.acquireTarget(outW, outH);
    if (!result.isValid()) {
        rt.releaseTarget(std::move(scratch));
        return {};
    }
    result.fbo->bind();
    gl->glViewport(0, 0, outW, outH);
    resolve->bind();
    resolve->setUniformValue("u_currentTexture", 0);
    resolve->setUniformValue("u_texel", QVector2D(1.f / float(drawW), 1.f / float(drawH)));
    resolve->setUniformValue("u_taps", supersample ? 2.f : 1.f);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, scratch.texture());
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    resolve->release();
    result.fbo->release();
    rt.releaseTarget(std::move(scratch));
    return result;
}

} // namespace drift::gl
