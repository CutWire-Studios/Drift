#include "engine/render3d/procedural/GlProceduralRenderer.h"

#include "engine/gpu/effects/GpuEffectDefinition.h"
#include "engine/procedural/ProceduralCamera.h"
#include "engine/procedural/ProceduralDiagnostics.h"
#include "engine/procedural/ProceduralSceneCache.h"
#include "engine/render3d/models/GlModelRenderer.h"
#include "engine/render3d/procedural/ProceduralRequest.h"

#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>

#include <algorithm>
#include <cmath>

namespace drift::gl {
namespace {

// GL_PROGRAM_POINT_SIZE: core desktop GL ignores gl_PointSize without it; ES always honours it.
constexpr GLenum kProgramPointSize = 0x8642;
constexpr int kMaxLights = 8;
constexpr size_t kMaxMeshBytes = GlRuntime::ProceduralCache::kMaxBytes;

QMatrix4x4 fromColumnMajor(const std::array<float, 16> &a)
{
    return QMatrix4x4(a.data()).transposed();
}

// A mesh attribute only counts when its array is the size the vertex count implies; anything else
// is treated as absent rather than read past its end.
struct MeshShape
{
    size_t vertices = 0;
    bool normal = false;
    bool uv = false;
    bool color = false;
    bool instances = false;
    std::vector<bool> custom;

    explicit MeshShape(const ProceduralMesh &mesh)
        : vertices(mesh.vertexCount())
        , normal(mesh.normal.size() == mesh.position.size())
        , uv(mesh.uv.size() == vertices * 2)
        , color(mesh.color.size() == vertices * 4)
        , instances(!mesh.instances.empty() && mesh.instances.size() % 16 == 0)
    {
        for (const ProceduralMesh::Custom &c : mesh.custom) {
            custom.push_back(c.components >= 1 && c.components <= 4
                             && c.data.size() == vertices * size_t(c.components));
        }
    }
};

template <typename T>
GLuint uploadBuffer(QOpenGLExtraFunctions *gl, GLenum target, const std::vector<T> &data, size_t *bytes)
{
    GLuint buffer = 0;
    gl->glGenBuffers(1, &buffer);
    gl->glBindBuffer(target, buffer);
    gl->glBufferData(target, GLsizeiptr(data.size() * sizeof(T)), data.data(), GL_STATIC_DRAW);
    *bytes += data.size() * sizeof(T);
    return buffer;
}

void deleteMesh(QOpenGLExtraFunctions *gl, ProceduralMeshGpu &mesh)
{
    for (const auto &[signature, vao] : mesh.vaos)
        gl->glDeleteVertexArrays(1, &vao);
    std::vector<GLuint> buffers{mesh.position, mesh.normal, mesh.uv, mesh.color, mesh.indices, mesh.instances};
    buffers.insert(buffers.end(), mesh.custom.begin(), mesh.custom.end());
    for (GLuint b : buffers) {
        if (b)
            gl->glDeleteBuffers(1, &b);
    }
}

ProceduralMeshGpu &acquireMesh(GlRuntime &rt, QOpenGLExtraFunctions *gl, std::uint64_t hash,
                               const ProceduralMesh &mesh, const MeshShape &shape)
{
    auto &cache = rt.procedurals;
    if (const auto found = cache.index.find(hash); found != cache.index.end()) {
        cache.lru.splice(cache.lru.begin(), cache.lru, found->second);
        return *found->second;
    }
    ProceduralMeshGpu gpu;
    gpu.hash = hash;
    gpu.position = uploadBuffer(gl, GL_ARRAY_BUFFER, mesh.position, &gpu.bytes);
    if (shape.normal)
        gpu.normal = uploadBuffer(gl, GL_ARRAY_BUFFER, mesh.normal, &gpu.bytes);
    if (shape.uv)
        gpu.uv = uploadBuffer(gl, GL_ARRAY_BUFFER, mesh.uv, &gpu.bytes);
    if (shape.color)
        gpu.color = uploadBuffer(gl, GL_ARRAY_BUFFER, mesh.color, &gpu.bytes);
    for (size_t i = 0; i < mesh.custom.size(); ++i)
        gpu.custom.push_back(shape.custom[i] ? uploadBuffer(gl, GL_ARRAY_BUFFER, mesh.custom[i].data, &gpu.bytes) : 0u);
    if (shape.instances)
        gpu.instances = uploadBuffer(gl, GL_ARRAY_BUFFER, mesh.instances, &gpu.bytes);
    if (!mesh.indices.empty())
        gpu.indices = uploadBuffer(gl, GL_ELEMENT_ARRAY_BUFFER, mesh.indices, &gpu.bytes);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    cache.totalBytes += gpu.bytes;
    cache.lru.push_front(std::move(gpu));
    cache.index[hash] = cache.lru.begin();
    return cache.lru.front();
}

// The VAO for this mesh as this program reads it: custom attributes go where the program declared
// them, so the same buffers can serve two shaders that order their inputs differently.
GLuint vaoFor(QOpenGLExtraFunctions *gl, ProceduralMeshGpu &gpu, const ProceduralMesh &mesh,
              const MeshShape &shape, const UserProgram &program)
{
    std::string signature;
    for (size_t i = 0; i < mesh.custom.size(); ++i) {
        const int slot = program.customNames.indexOf(QString::fromStdString(mesh.custom[i].name));
        if (slot >= 0 && shape.custom[i])
            signature += mesh.custom[i].name + '@' + std::to_string(program.customLocations[slot]) + ';';
    }
    for (const auto &[existing, vao] : gpu.vaos) {
        if (existing == signature)
            return vao;
    }

    GLuint vao = 0;
    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    const auto attribute = [gl](GLuint location, GLuint buffer, int components) {
        gl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        gl->glEnableVertexAttribArray(location);
        gl->glVertexAttribPointer(location, components, GL_FLOAT, GL_FALSE, 0, nullptr);
        gl->glVertexAttribDivisor(location, 0);
    };
    attribute(0, gpu.position, 3);
    if (gpu.normal)
        attribute(1, gpu.normal, 3);
    if (gpu.uv)
        attribute(2, gpu.uv, 2);
    if (gpu.color)
        attribute(3, gpu.color, 4);
    for (size_t i = 0; i < mesh.custom.size(); ++i) {
        const int slot = program.customNames.indexOf(QString::fromStdString(mesh.custom[i].name));
        if (slot >= 0 && gpu.custom[i])
            attribute(GLuint(program.customLocations[slot]), gpu.custom[i], mesh.custom[i].components);
    }
    if (gpu.instances) {
        gl->glBindBuffer(GL_ARRAY_BUFFER, gpu.instances);
        for (int column = 0; column < 4; ++column) {
            const GLuint location = GLuint(12 + column);
            gl->glEnableVertexAttribArray(location);
            gl->glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, 16 * sizeof(float),
                                      reinterpret_cast<const void *>(size_t(column) * 4 * sizeof(float)));
            gl->glVertexAttribDivisor(location, 1);
        }
    }
    if (gpu.indices)
        gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu.indices);
    gl->glBindVertexArray(0);
    gl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gpu.vaos.emplace_back(std::move(signature), vao);
    return vao;
}

void evictMeshes(GlRuntime &rt, QOpenGLExtraFunctions *gl)
{
    auto &cache = rt.procedurals;
    while (cache.totalBytes > kMaxMeshBytes && cache.lru.size() > 1) {
        ProceduralMeshGpu &oldest = cache.lru.back();
        deleteMesh(gl, oldest);
        cache.totalBytes -= oldest.bytes;
        cache.index.erase(oldest.hash);
        cache.lru.pop_back();
    }
}

void setMeshUniforms(QOpenGLExtraFunctions *gl, QOpenGLShaderProgram *program, const ProceduralMeshEntry &entry)
{
    for (const auto &[name, values] : entry.uniforms) {
        const QString key = QString::fromStdString(name);
        if (drift::isReservedGpuUniform(key))
            continue;
        const int location = program->uniformLocation(key);
        if (location < 0)
            continue;
        switch (values.size()) {
        case 1: gl->glUniform1fv(location, 1, values.data()); break;
        case 2: gl->glUniform2fv(location, 1, values.data()); break;
        case 3: gl->glUniform3fv(location, 1, values.data()); break;
        case 4: gl->glUniform4fv(location, 1, values.data()); break;
        case 16: gl->glUniformMatrix4fv(location, 1, GL_FALSE, values.data()); break;
        default: break;
        }
    }
}

// Lights are written in the script's space, so they travel with the scene when the clip moves it.
void setLights(QOpenGLShaderProgram *program, const ProceduralScene &scene, const QMatrix4x4 &model)
{
    QVector3D positions[kMaxLights];
    QVector3D directions[kMaxLights];
    QVector3D colors[kMaxLights];
    QVector3D params[kMaxLights];
    const int count = int(std::min<size_t>(scene.lights.size(), kMaxLights));
    for (int i = 0; i < count; ++i) {
        const ProceduralLight &light = scene.lights[size_t(i)];
        positions[i] = model.map(QVector3D(light.pos[0], light.pos[1], light.pos[2]));
        QVector3D dir = model.mapVector(QVector3D(light.dir[0], light.dir[1], light.dir[2]));
        if (dir.lengthSquared() > 1e-12f)
            dir.normalize();
        directions[i] = dir;
        colors[i] = QVector3D(light.color[0], light.color[1], light.color[2]) * light.intensity;
        params[i] = QVector3D(float(int(light.type)), light.range, light.cone);
    }
    program->setUniformValue("u_lightCount", count);
    program->setUniformValueArray("u_lightPos", positions, kMaxLights);
    program->setUniformValueArray("u_lightDir", directions, kMaxLights);
    program->setUniformValueArray("u_lightColor", colors, kMaxLights);
    program->setUniformValueArray("u_lightParams", params, kMaxLights);
}

GLenum primitiveFor(ProceduralMesh::Topology topology)
{
    switch (topology) {
    case ProceduralMesh::Topology::Lines: return GL_LINES;
    case ProceduralMesh::Topology::Points: return GL_POINTS;
    case ProceduralMesh::Topology::Triangles: break;
    }
    return GL_TRIANGLES;
}

} // namespace

void destroyGlProcedurals(GlRuntime &rt, QOpenGLExtraFunctions *gl)
{
    auto &cache = rt.procedurals;
    for (ProceduralMeshGpu &mesh : cache.lru)
        deleteMesh(gl, mesh);
    cache.lru.clear();
    cache.index.clear();
    cache.totalBytes = 0;
}

GlTarget drawProceduralClip(GlRuntime &rt, QOpenGLExtraFunctions *gl, const ProceduralRequest &request,
                            const QSize &canvasSize, const QMatrix4x4 &viewProjection, GlTarget *depthOut,
                            const std::array<float, 3> &globalAudio)
{
    if (!gl || !request.scene || canvasSize.isEmpty() || request.scene->meshes.empty())
        return {};
    const ProceduralScene &scene = *request.scene;
    const bool ownCamera = request.ownCamera();
    if (ownCamera)
        depthOut = nullptr;

    const int outW = canvasSize.width();
    const int outH = canvasSize.height();
    const bool supersample = (qint64(outW) * outH) <= (1920LL * 1080LL);
    const int drawW = supersample ? outW * 2 : outW;
    const int drawH = supersample ? outH * 2 : outH;

    GLuint depthTexture = 0;
    GlTarget scratch = rt.acquireTarget(drawW, drawH, /*wantDepth=*/!depthOut);
    if (!scratch.isValid())
        return {};
    if (depthOut) {
        depthTexture = modelClipDepthTexture(rt, gl, drawW, drawH);
        scratch.fbo->bind();
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);
        if (gl->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
            scratch.fbo->release();
            rt.releaseTarget(std::move(scratch));
            return drawProceduralClip(rt, gl, request, canvasSize, viewProjection, nullptr, globalAudio);
        }
        scratch.fbo->release();
    }
    const auto detachDepth = [&] {
        if (!depthTexture)
            return;
        scratch.fbo->bind();
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
        scratch.fbo->release();
    };

    // u_view * u_model takes script space to the pixels the projection starts from, and u_proj takes
    // those to clip space. Evaluation happens in project px; the render scale only enters through
    // u_view, so a half-size preview is the full-size picture shrunk.
    QMatrix4x4 model = request.model;
    QMatrix4x4 view;
    QMatrix4x4 proj;
    QVector3D cameraPos;
    if (ownCamera) {
        const ProceduralSceneCamera &camera = scene.camera;
        model = QMatrix4x4();
        view = fromColumnMajor(proceduralcam::lookAt(camera.eye, camera.target, camera.up));
        const float aspect = float(request.projectSize.width()) / float(std::max(1, request.projectSize.height()));
        // The layer is stored top row first, so the picture is flipped here, once, rather than
        // asking every shader to.
        proj = QMatrix4x4();
        proj.scale(1.f, -1.f, 1.f);
        proj *= fromColumnMajor(proceduralcam::perspective(camera.fov, aspect, camera.nearPlane, camera.farPlane));
        cameraPos = QVector3D(camera.eye[0], camera.eye[1], camera.eye[2]);
    } else {
        QMatrix4x4 toRender;
        toRender.scale(float(request.renderScale));
        view = viewProjection * toRender;
        proj.translate(-1.f, -1.f);
        proj.scale(2.f / float(outW), 2.f / float(outH));
        bool invertible = false;
        const QVector4D eye = view.inverted(&invertible).map(QVector4D(0.f, 0.f, 1.f, 0.f));
        cameraPos = invertible && std::abs(eye.w()) > 1e-9f
                        ? eye.toVector3DAffine()
                        : QVector3D(float(request.projectSize.width()) * 0.5f, float(request.projectSize.height()) * 0.5f,
                                    -float(request.pose3d.perspective / std::max(1e-6, request.renderScale)));
    }

    PipelineAux aux;
    aux.frameRate = request.frameRate;
    aux.globalAudio = globalAudio;
    drift::GpuEffectDefinition packageUniforms;
    packageUniforms.regionParams = request.regionParams;
    const QString sourceHash = QString::fromStdString(request.sourceHash);

    {
        GlStateGuard guard(gl);
        const bool es = QOpenGLContext::currentContext() && QOpenGLContext::currentContext()->isOpenGLES();
        const GLboolean pointSize = es ? GL_TRUE : gl->glIsEnabled(kProgramPointSize);
        GLint blendFunc[4];
        gl->glGetIntegerv(GL_BLEND_SRC_RGB, &blendFunc[0]);
        gl->glGetIntegerv(GL_BLEND_DST_RGB, &blendFunc[1]);
        gl->glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendFunc[2]);
        gl->glGetIntegerv(GL_BLEND_DST_ALPHA, &blendFunc[3]);
        scratch.fbo->bind();
        gl->glViewport(0, 0, drawW, drawH);
        gl->glClearColor(0.f, 0.f, 0.f, 0.f);
        gl->glDepthMask(GL_TRUE);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        gl->glEnable(GL_DEPTH_TEST);
        gl->glDepthFunc(GL_LESS);
        // No culling: a script's plane should be visible from both sides. Front faces still come
        // out as gl_FrontFacing == true for the outward (v1-v0)x(v2-v0) winding, which is clockwise
        // once the canvas's top-first row order is accounted for.
        gl->glDisable(GL_CULL_FACE);
        gl->glFrontFace(GL_CW);
        // Shaders emit straight alpha; blending it into the transparent scratch leaves premultiplied
        // colour, which is what the resolve expects.
        gl->glEnable(GL_BLEND);
        gl->glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        if (!es)
            gl->glEnable(kProgramPointSize);

        for (size_t i = 0; i < scene.meshes.size(); ++i) {
            const ProceduralMeshEntry &entry = scene.meshes[i];
            if (!entry.mesh || entry.mesh->vertexCount() == 0)
                continue;
            const ProceduralMesh &mesh = *entry.mesh;

            const QString shaderName = QString::fromStdString(entry.shader);
            const auto pair = request.shaders.constFind(shaderName);
            if (pair == request.shaders.constEnd()) {
                const QString key = sourceHash + QLatin1Char('/') + shaderName;
                if (!rt.userPrograms.count(key)) {
                    UserProgram missing;
                    missing.log = QStringLiteral("shader '%1' is not part of package '%2'")
                                      .arg(shaderName, request.packageId);
                    drift::ProceduralDiagnostics::shared().setShader(
                        request.sourceHash, entry.shader, {false, missing.log.toStdString()});
                    rt.userPrograms.emplace(key, std::move(missing));
                }
                continue;
            }
            const UserProgram *user = rt.compileUserProgram(sourceHash, shaderName, pair->vert, pair->frag);
            if (!user)
                continue;
            QOpenGLShaderProgram *program = user->program.get();

            const MeshShape shape(mesh);
            const std::uint64_t hash = i < scene.meshHashes.size() && scene.meshHashes[i]
                                           ? scene.meshHashes[i]
                                           : proceduralMeshHash(mesh);
            ProceduralMeshGpu &gpu = acquireMesh(rt, gl, hash, mesh, shape);

            program->bind();
            setPackageUniforms(program, packageUniforms, request.uniforms, request.projectSize,
                               request.sourceTimeUs, 0.0, &aux);
            program->setUniformValue("u_model", model);
            program->setUniformValue("u_view", view);
            program->setUniformValue("u_proj", proj);
            program->setUniformValue("u_normalMatrix", model.normalMatrix());
            program->setUniformValue("u_cameraPos", cameraPos);
            program->setUniformValue("u_renderResolution", QVector2D(float(outW), float(outH)));
            setLights(program, scene, model);
            setMeshUniforms(gl, program, entry);

            gl->glBindVertexArray(vaoFor(gl, gpu, mesh, shape, *user));
            if (!shape.normal)
                gl->glVertexAttrib3f(1, 0.f, 0.f, 0.f);
            if (!shape.uv)
                gl->glVertexAttrib2f(2, 0.f, 0.f);
            if (!shape.color)
                gl->glVertexAttrib4f(3, 1.f, 1.f, 1.f, 1.f);
            for (int slot = 0; slot < user->customNames.size(); ++slot) {
                bool provided = false;
                for (size_t c = 0; c < mesh.custom.size(); ++c) {
                    provided = provided || (shape.custom[c] && mesh.custom[c].name == user->customNames[slot].toStdString());
                }
                if (!provided)
                    gl->glVertexAttrib4f(GLuint(user->customLocations[slot]), 0.f, 0.f, 0.f, 0.f);
            }
            if (!shape.instances) {
                for (int column = 0; column < 4; ++column) {
                    gl->glVertexAttrib4f(GLuint(12 + column), column == 0 ? 1.f : 0.f, column == 1 ? 1.f : 0.f,
                                         column == 2 ? 1.f : 0.f, column == 3 ? 1.f : 0.f);
                }
            }

            const GLenum primitive = primitiveFor(mesh.topology);
            const GLsizei instances = shape.instances ? GLsizei(mesh.instanceCount()) : 1;
            if (!mesh.indices.empty()) {
                gl->glDrawElementsInstanced(primitive, GLsizei(mesh.indices.size()), GL_UNSIGNED_INT, nullptr, instances);
            } else {
                gl->glDrawArraysInstanced(primitive, 0, GLsizei(mesh.vertexCount()), instances);
            }
            gl->glBindVertexArray(0);
            program->release();
        }

        if (!es && !pointSize)
            gl->glDisable(kProgramPointSize);
        gl->glBlendFuncSeparate(GLenum(blendFunc[0]), GLenum(blendFunc[1]), GLenum(blendFunc[2]), GLenum(blendFunc[3]));
        scratch.fbo->release();
    }
    evictMeshes(rt, gl);

    detachDepth();
    if (depthTexture && depthOut) {
        GlTarget depth = packModelClipDepth(rt, gl, depthTexture, canvasSize, supersample);
        if (depth.isValid())
            *depthOut = std::move(depth);
    }
    return resolveModelClipScratch(rt, gl, std::move(scratch), canvasSize, supersample);
}

} // namespace drift::gl
