#include "engine/render3d/splats/GlSplatRenderer.h"

#include "engine/render3d/models/GlModelRenderer.h"
#include "engine/render3d/models/ModelClipRenderer.h"
#include "engine/render3d/models/ModelClipTransform.h"
#include "engine/render3d/splats/SplatAsset.h"

#include <QDebug>
#include <QOpenGLShaderProgram>
#include <QtGlobal>
#include <qfloat16.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <memory>
#include <vector>

#ifndef GL_RGBA32UI
#define GL_RGBA32UI 0x8D70
#endif
#ifndef GL_RGBA_INTEGER
#define GL_RGBA_INTEGER 0x8D99
#endif

namespace drift::gl {
namespace {

// Two RGBA32UI texels per splat, fetched by instance index (no SSBOs on GLES 3.0 / WebGL2):
//   A = position bits xyz, RGBA8 colour
//   B = covariance as six halves (xx xy | xz yy | yz zz), then the float that scales them back.
// The 2D footprint comes from the EWA projection of that covariance through the full MVP, so the
// scene camera needs no separate view and projection.
constexpr const char *kSplatVert = R"(#version 330 core
layout(location = 0) in uint a_index;
uniform highp usampler2D u_splats;
uniform mat4 u_mvp;
uniform vec2 u_halfViewport;
uniform int u_width;
out vec2 v_pos;
flat out vec4 v_color;

// GLSL 3.30 has neither unpackHalf2x16 nor unpackUnorm4x8 (they arrive in 4.20 / 4.10).
float halfToFloat(uint h) {
    uint e = (h >> 10) & 31u;
    uint m = h & 1023u;
    float v = e == 0u ? float(m) * 5.9604645e-8 : uintBitsToFloat(((e + 112u) << 23) | (m << 13));
    return (h & 32768u) != 0u ? -v : v;
}
vec2 unpackHalves(uint p) {
    return vec2(halfToFloat(p & 65535u), halfToFloat(p >> 16));
}

void main() {
    int i = int(a_index) * 2;
    ivec2 at = ivec2(i % u_width, i / u_width);
    uvec4 a = texelFetch(u_splats, at, 0);
    uvec4 b = texelFetch(u_splats, at + ivec2(1, 0), 0);
    vec4 c = u_mvp * vec4(uintBitsToFloat(a.xyz), 1.0);
    vec2 centre = c.xy / c.w;
    if (c.w <= 1e-4 || abs(centre.x) > 1.3 || abs(centre.y) > 1.3) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        v_pos = vec2(0.0);
        v_color = vec4(0.0);
        return;
    }

    float k = uintBitsToFloat(b.w);
    vec2 h0 = unpackHalves(b.x);
    vec2 h1 = unpackHalves(b.y);
    vec2 h2 = unpackHalves(b.z);
    mat3 cov = mat3(h0.x, h0.y, h1.x,
                    h0.y, h1.y, h2.x,
                    h1.x, h2.x, h2.y) * k;

    // d(ndc)/d(p) of the perspective divide, scaled to pixels.
    vec3 jx = (vec3(u_mvp[0][0], u_mvp[1][0], u_mvp[2][0]) * c.w - vec3(u_mvp[0][3], u_mvp[1][3], u_mvp[2][3]) * c.x)
              / (c.w * c.w) * u_halfViewport.x;
    vec3 jy = (vec3(u_mvp[0][1], u_mvp[1][1], u_mvp[2][1]) * c.w - vec3(u_mvp[0][3], u_mvp[1][3], u_mvp[2][3]) * c.y)
              / (c.w * c.w) * u_halfViewport.y;
    float sa = dot(jx, cov * jx) + 0.3;
    float sb = dot(jx, cov * jy);
    float sd = dot(jy, cov * jy) + 0.3;

    float mid = 0.5 * (sa + sd);
    float disc = sqrt(max(mid * mid - (sa * sd - sb * sb), 0.0));
    float l1 = mid + disc;
    float l2 = max(mid - disc, 0.0);
    vec2 v = vec2(sb, l1 - sa);
    vec2 axis1 = dot(v, v) > 1e-12 ? normalize(v) : vec2(1.0, 0.0);
    vec2 axis2 = vec2(axis1.y, -axis1.x);
    float r1 = min(3.0 * sqrt(l1), 1024.0);
    float r2 = min(3.0 * sqrt(l2), 1024.0);

    vec2 q = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    vec2 offset = (q.x * r1 * axis1 + q.y * r2 * axis2) / u_halfViewport;
    gl_Position = vec4(centre + offset, 0.0, 1.0);
    // In sigmas along each axis: the quad reaches 3.
    v_pos = q * 3.0;
    v_color = vec4(a.w & 255u, (a.w >> 8) & 255u, (a.w >> 16) & 255u, a.w >> 24) / 255.0;
}
)";

constexpr const char *kSplatFrag = R"(#version 330 core
in vec2 v_pos;
flat in vec4 v_color;
out vec4 fragColor;
void main() {
    float power = -0.5 * dot(v_pos, v_pos);
    if (power < -4.0)
        discard;
    float alpha = exp(power) * v_color.a;
    if (alpha < 1.0 / 255.0)
        discard;
    fragColor = vec4(v_color.rgb * alpha, alpha);
}
)";

struct GlSplatGpu
{
    const SplatData *key = nullptr;
    std::weak_ptr<const SplatData> cpu;
    GLuint texture = 0;
    GLuint vao = 0;
    GLuint order = 0;
    int texWidth = 0;
    int drawable = 0; // splats that fit the texture

    // The last sort, kept while the camera has not moved.
    std::vector<uint32_t> indices;
    QMatrix4x4 sortedMvp;
    bool sorted = false;
    std::vector<float> depth;
    std::vector<uint32_t> keys;
};

std::list<GlSplatGpu> g_splats; // most recently drawn first
constexpr size_t kMaxSplatSets = 2;

void deleteGpu(QOpenGLExtraFunctions *gl, GlSplatGpu &s)
{
    if (s.texture)
        gl->glDeleteTextures(1, &s.texture);
    if (s.vao)
        gl->glDeleteVertexArrays(1, &s.vao);
    if (s.order)
        gl->glDeleteBuffers(1, &s.order);
}

uint16_t halfBits(float v)
{
    const qfloat16 h(v);
    uint16_t bits;
    std::memcpy(&bits, &h, sizeof bits);
    return bits;
}

uint32_t pack2(float a, float b)
{
    return uint32_t(halfBits(a)) | (uint32_t(halfBits(b)) << 16);
}

uint32_t floatBits(float v)
{
    uint32_t u;
    std::memcpy(&u, &v, sizeof u);
    return u;
}

bool upload(QOpenGLExtraFunctions *gl, const std::shared_ptr<const SplatData> &cpu, GlSplatGpu &out)
{
    GLint maxTex = 2048;
    gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
    qint64 n = cpu->count;
    int width = std::min<int>(maxTex, 2048);
    qint64 rows = (n * 2 + width - 1) / width;
    if (rows > maxTex) {
        width = std::min<int>(maxTex, 4096);
        rows = (n * 2 + width - 1) / width;
    }
    if (rows > maxTex) {
        const qint64 fit = qint64(width) * maxTex / 2;
        qWarning() << "splat scene has" << n << "splats; drawing the first" << fit << "(texture limit" << maxTex << ")";
        n = fit;
        rows = (n * 2 + width - 1) / width;
    }

    std::vector<uint32_t> texels(size_t(width) * size_t(rows) * 4, 0u);
    for (qint64 i = 0; i < n; ++i) {
        const float *p = &cpu->positions[size_t(i) * 3];
        const float *q = &cpu->rotations[size_t(i) * 4];
        const float *sc = &cpu->scales[size_t(i) * 3];
        uint32_t *a = &texels[size_t(i) * 8];
        uint32_t *b = a + 4;
        std::memcpy(a, p, sizeof(float) * 3);
        std::memcpy(&a[3], &cpu->colors[size_t(i) * 4], 4);

        const float w = q[0], x = q[1], y = q[2], z = q[3];
        const float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                               {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                               {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
        float m[3][3];
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                m[row][col] = r[row][col] * sc[col];
        float cov[3][3];
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                cov[row][col] = m[row][0] * m[col][0] + m[row][1] * m[col][1] + m[row][2] * m[col][2];
        // Halves hold six bits of dynamic range at best, so scale by the largest term.
        float k = std::max({cov[0][0], cov[1][1], cov[2][2]});
        if (!(k > 0.f))
            k = 1.f;
        const float inv = 1.f / k;
        b[0] = pack2(cov[0][0] * inv, cov[0][1] * inv);
        b[1] = pack2(cov[0][2] * inv, cov[1][1] * inv);
        b[2] = pack2(cov[1][2] * inv, cov[2][2] * inv);
        b[3] = floatBits(k);
    }

    gl->glGenTextures(1, &out.texture);
    gl->glBindTexture(GL_TEXTURE_2D, out.texture);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, width, int(rows), 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT,
                     texels.data());
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glBindTexture(GL_TEXTURE_2D, 0);

    gl->glGenBuffers(1, &out.order);
    gl->glGenVertexArrays(1, &out.vao);
    gl->glBindVertexArray(out.vao);
    gl->glBindBuffer(GL_ARRAY_BUFFER, out.order);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribIPointer(0, 1, GL_UNSIGNED_INT, 0, nullptr);
    gl->glVertexAttribDivisor(0, 1);
    gl->glBindVertexArray(0);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);

    out.texWidth = width;
    out.drawable = int(n);
    return gl->glGetError() == GL_NO_ERROR;
}

GlSplatGpu *acquire(QOpenGLExtraFunctions *gl, const std::shared_ptr<const SplatData> &cpu)
{
    for (auto it = g_splats.begin(); it != g_splats.end();) {
        if (it->cpu.expired()) {
            deleteGpu(gl, *it);
            it = g_splats.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = g_splats.begin(); it != g_splats.end(); ++it) {
        if (it->key == cpu.get()) {
            g_splats.splice(g_splats.begin(), g_splats, it);
            return &g_splats.front();
        }
    }
    GlSplatGpu entry;
    entry.key = cpu.get();
    entry.cpu = cpu;
    if (!upload(gl, cpu, entry)) {
        deleteGpu(gl, entry);
        return nullptr;
    }
    g_splats.push_front(std::move(entry));
    while (g_splats.size() > kMaxSplatSets) {
        deleteGpu(gl, g_splats.back());
        g_splats.pop_back();
    }
    return &g_splats.front();
}

// Back to front by clip w (view depth), 16-bit counting sort. Splats behind the eye and fully
// transparent ones are left out.
void sortBackToFront(const SplatData &d, GlSplatGpu &s, const QMatrix4x4 &mvp)
{
    const int n = s.drawable;
    s.depth.resize(size_t(n));
    s.keys.resize(size_t(n));
    const QVector4D row = mvp.row(3);
    const float rx = row.x(), ry = row.y(), rz = row.z(), rw = row.w();

    constexpr uint32_t kSkip = 0xFFFFFFFFu;
    float lo = std::numeric_limits<float>::max();
    float hi = std::numeric_limits<float>::lowest();
    for (int i = 0; i < n; ++i) {
        const float *p = &d.positions[size_t(i) * 3];
        const float w = rx * p[0] + ry * p[1] + rz * p[2] + rw;
        if (w <= 1e-4f || d.colors[size_t(i) * 4 + 3] == 0) {
            s.depth[size_t(i)] = -1.f;
            continue;
        }
        s.depth[size_t(i)] = w;
        lo = std::min(lo, w);
        hi = std::max(hi, w);
    }

    std::vector<uint32_t> counts(65537, 0u);
    const float scale = hi > lo ? 65535.f / (hi - lo) : 0.f;
    for (int i = 0; i < n; ++i) {
        const float w = s.depth[size_t(i)];
        if (w < 0.f) {
            s.keys[size_t(i)] = kSkip;
            continue;
        }
        const uint32_t key = 65535u - uint32_t((w - lo) * scale);
        s.keys[size_t(i)] = key;
        ++counts[key + 1];
    }
    for (size_t k = 1; k < counts.size(); ++k)
        counts[k] += counts[k - 1];
    s.indices.resize(size_t(counts.back()));
    for (int i = 0; i < n; ++i) {
        const uint32_t key = s.keys[size_t(i)];
        if (key != kSkip)
            s.indices[counts[key]++] = uint32_t(i);
    }
}

} // namespace

GlTarget drawSplatClip(GlRuntime &rt, QOpenGLExtraFunctions *gl, const model3d::ModelDrawRequest &request,
                       const QSize &canvasSize, const QMatrix4x4 &viewProjection)
{
    if (!gl || !request.splat || canvasSize.isEmpty() || request.splat->count <= 0)
        return {};
    const SplatData &data = *request.splat;

    QOpenGLShaderProgram *prog = rt.builtinProgram(QStringLiteral("splat_clip"), kSplatVert, kSplatFrag);
    if (!prog)
        return {};
    GlSplatGpu *splat = acquire(gl, request.splat);
    if (!splat)
        return {};

    const int outW = canvasSize.width();
    const int outH = canvasSize.height();
    // No supersampling: the 0.3 px low-pass in the footprint already antialiases, and a splat
    // scene is overdraw-bound, so 4x the pixels would cost far more than it gains.
    GlTarget scratch = rt.acquireTarget(outW, outH, /*wantDepth=*/false);
    if (!scratch.isValid())
        return {};

    const QSizeF canvas = request.canvas.isEmpty() ? QSizeF(canvasSize) : request.canvas;
    const QMatrix4x4 world = modelClipWorld(request.rect, request.rotation, request.pose3d, data.fitMin,
                                            data.fitMax, canvas, request.params.scale,
                                            request.params.scaleZ);
    QMatrix4x4 ndc;
    ndc.translate(-1.f, -1.f);
    ndc.scale(2.f / float(outW), 2.f / float(outH));
    const QMatrix4x4 mvp = ndc * viewProjection * world;

    if (!splat->sorted || splat->sortedMvp != mvp) {
        sortBackToFront(data, *splat, mvp);
        splat->sortedMvp = mvp;
        splat->sorted = true;
        gl->glBindBuffer(GL_ARRAY_BUFFER, splat->order);
        gl->glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(splat->indices.size() * sizeof(uint32_t)),
                         splat->indices.data(), GL_STREAM_DRAW);
        gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    {
        GlStateGuard guard(gl);
        GLint blendFunc[4];
        gl->glGetIntegerv(GL_BLEND_SRC_RGB, &blendFunc[0]);
        gl->glGetIntegerv(GL_BLEND_DST_RGB, &blendFunc[1]);
        gl->glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendFunc[2]);
        gl->glGetIntegerv(GL_BLEND_DST_ALPHA, &blendFunc[3]);

        scratch.fbo->bind();
        gl->glViewport(0, 0, outW, outH);
        gl->glClearColor(0.f, 0.f, 0.f, 0.f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        gl->glDisable(GL_DEPTH_TEST);
        gl->glDepthMask(GL_FALSE);
        gl->glDisable(GL_CULL_FACE);
        gl->glEnable(GL_BLEND);
        gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        prog->bind();
        prog->setUniformValue("u_mvp", mvp);
        prog->setUniformValue("u_halfViewport", QVector2D(outW * 0.5f, outH * 0.5f));
        prog->setUniformValue("u_width", splat->texWidth);
        prog->setUniformValue("u_splats", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, splat->texture);
        gl->glBindVertexArray(splat->vao);
        if (!splat->indices.empty())
            gl->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, GLsizei(splat->indices.size()));
        gl->glBindVertexArray(0);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        prog->release();

        gl->glBlendFuncSeparate(GLenum(blendFunc[0]), GLenum(blendFunc[1]), GLenum(blendFunc[2]),
                                GLenum(blendFunc[3]));
        scratch.fbo->release();
    }

    return resolveModelClipScratch(rt, gl, std::move(scratch), canvasSize, /*supersample=*/false);
}

void destroyGlSplats(GlRuntime &, QOpenGLExtraFunctions *gl)
{
    if (gl) {
        for (GlSplatGpu &s : g_splats)
            deleteGpu(gl, s);
    }
    g_splats.clear();
}

} // namespace drift::gl
