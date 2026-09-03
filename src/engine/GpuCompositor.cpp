#include "GpuCompositor.h"

#include "EffectCatalog.h"
#include "FaceModelTransform.h"
#include "FaceTrack.h"
#include "GlFaceSwapRenderer.h"
#include "GlModelRenderer.h"
#include "GlRuntime.h"
#include "GpuEffectDefinition.h"
#include "MaskApplier.h"

#include <QMatrix4x4>
#include <QMutexLocker>
#include <QOpenGLShaderProgram>
#include <QVector2D>

#include <cmath>
#include <map>
#include <vector>

using namespace drift::gl;

namespace {

// Places a unit quad on the canvas via u_model. The package passes keep using the
// fullscreen kQuadVertexShader; only compositing needs a transform.
constexpr const char *kLayerVertexShader = R"(#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
uniform mat4 u_model;
out vec2 v_texCoord;
void main() {
    v_texCoord = a_texCoord;
    gl_Position = u_model * vec4(a_position, 0.0, 1.0);
}
)";

// Draws a layer with opacity and an optional mask, emitting premultiplied alpha
// so the canvas can blend with GL_ONE / GL_ONE_MINUS_SRC_ALPHA.
constexpr const char *kLayerFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_layer;
uniform sampler2D u_mask;
uniform float u_opacity;
uniform float u_hasMask;
uniform float u_maskInvert;
uniform float u_layerPremul;
void main() {
    vec4 c = texture(u_layer, v_texCoord);
    float s = u_opacity;
    if (u_hasMask > 0.5) {
        float m = texture(u_mask, v_texCoord).r;
        s *= mix(m, 1.0 - m, u_maskInvert);
    }
    float a = c.a * s;
    fragColor = vec4(u_layerPremul > 0.5 ? c.rgb * s : c.rgb * a, a);
}
)";

// Separable blend modes need the destination, which GL fixed-function blending
// cannot express. The canvas is copied into the target first, then the layer's
// quad is drawn through this shader sampling the canvas at the same pixel.
constexpr const char *kBlendFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_layer;
uniform sampler2D u_mask;
uniform sampler2D u_dst;      // canvas, premultiplied
uniform vec2 u_canvasSize;
uniform float u_opacity;
uniform float u_hasMask;
uniform float u_maskInvert;
uniform float u_layerPremul;
uniform int u_blendMode;      // 1 multiply, 2 screen, 3 overlay, 5 darken, 6 lighten

vec3 blendRgb(vec3 base, vec3 src) {
    if (u_blendMode == 1) return base * src;
    if (u_blendMode == 2) return 1.0 - (1.0 - base) * (1.0 - src);
    if (u_blendMode == 3) {
        return mix(2.0 * base * src,
                   1.0 - 2.0 * (1.0 - base) * (1.0 - src),
                   step(0.5, base));
    }
    if (u_blendMode == 5) return min(base, src);
    if (u_blendMode == 6) return max(base, src);
    return src;
}

void main() {
    vec4 src = texture(u_layer, v_texCoord);
    vec3 srcRgb = (u_layerPremul > 0.5 && src.a > 0.0001) ? src.rgb / src.a : src.rgb;
    float sa = src.a * u_opacity;
    if (u_hasMask > 0.5) {
        float m = texture(u_mask, v_texCoord).r;
        sa *= mix(m, 1.0 - m, u_maskInvert);
    }

    vec4 dst = texture(u_dst, gl_FragCoord.xy / u_canvasSize); // premultiplied
    vec3 dstRgb = dst.a > 0.0001 ? dst.rgb / dst.a : vec3(0.0);

    // Qt's blend modes operate on the straight-alpha colours, then composite the
    // result over the destination with source-over.
    vec3 blended = clamp(blendRgb(dstRgb, srcRgb), 0.0, 1.0);
    float outA = sa + dst.a * (1.0 - sa);
    vec3 outRgb = blended * sa + dstRgb * dst.a * (1.0 - sa);
    fragColor = vec4(outRgb, outA);
}
)";

// Cover-fit a texture over the canvas and blur it, for BackgroundKind::Blur.
constexpr const char *kBlurFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
uniform vec2 u_texel;    // direction * 1/size
uniform float u_radius;  // in taps
void main() {
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (float i = -8.0; i <= 8.0; i += 1.0) {
        float w = exp(-(i * i) / (2.0 * max(u_radius, 0.5) * max(u_radius, 0.5) / 9.0));
        sum += texture(u_currentTexture, v_texCoord + u_texel * i * (u_radius / 8.0)) * w;
        total += w;
    }
    fragColor = sum / max(total, 0.0001);
}
)";

int blendModeCode(drift::BlendMode mode)
{
    switch (mode) {
    case drift::BlendMode::Multiply:
        return 1;
    case drift::BlendMode::Screen:
        return 2;
    case drift::BlendMode::Overlay:
        return 3;
    case drift::BlendMode::Add:
        return 4;
    case drift::BlendMode::Darken:
        return 5;
    case drift::BlendMode::Lighten:
        return 6;
    case drift::BlendMode::Normal:
        break;
    }
    return 0;
}

// Modes GL fixed-function blending can do directly.
bool isFixedFunctionBlend(drift::BlendMode mode)
{
    return mode == drift::BlendMode::Normal || mode == drift::BlendMode::Add;
}

// Canvas pixels (top-left origin) → clip space. The FBO's v=0 row corresponds to
// the top of the readback image (see promoteImageToTarget), so y is not flipped.
QMatrix4x4 modelMatrixFor(const GpuLayer &layer, const QSize &canvas)
{
    const double w = layer.rect.width();
    const double h = layer.rect.height();
    const double cx = layer.rect.x() + w * 0.5;
    const double cy = layer.rect.y() + h * 0.5;

    QMatrix4x4 m;
    // px → NDC
    m.translate(float(2.0 * cx / canvas.width() - 1.0), float(2.0 * cy / canvas.height() - 1.0));
    m.scale(2.f / canvas.width(), 2.f / canvas.height());
    // The rotation runs in pixel space, between the px → NDC step and the quad's
    // own sizing: rotating the unrotated unit quad first and applying the layer
    // size after would shear it whenever the layer or the canvas is not square.
    m.rotate(float(layer.rotation), 0.f, 0.f, 1.f);
    // Quad spans [-1, 1], so half the layer size takes it to the full rect.
    m.scale(float(w * 0.5), float(h * 0.5));
    m.scale(layer.flipH ? -1.f : 1.f, layer.flipV ? -1.f : 1.f);
    return m;
}

// Mask coverage maps only change when the mask or the size does, so they are
// rasterized once and kept as GL textures.
GLuint maskTexture(GlRuntime &rt, QOpenGLExtraFunctions *gl, const drift::Mask &mask, const QSize &size)
{
    if (mask.shape == drift::MaskShape::None)
        return 0;

    const QString key = QStringLiteral("__mask__:%1:%2:%3:%4:%5:%6:%7:%8:%9:%10:%11")
                            .arg(int(mask.shape))
                            .arg(mask.x)
                            .arg(mask.y)
                            .arg(mask.w)
                            .arg(mask.h)
                            .arg(mask.rotation)
                            .arg(mask.feather)
                            .arg(mask.invert ? 1 : 0)
                            .arg(mask.points.size())
                            .arg(size.width())
                            .arg(size.height());

    const auto it = rt.staticTextures.find(key);
    if (it != rt.staticTextures.end())
        return it->second;

    const QImage alpha = drift::maskAlphaMap(mask, size.width(), size.height());
    if (alpha.isNull()) {
        rt.staticTextures[key] = 0;
        return 0;
    }

    // Layer textures are FBO-backed (v=0 == image top); a plain upload puts row 0
    // at v=0 too, so no flip is needed to line the mask up with the layer.
    const GLuint tex = uploadTexture(gl, alpha.convertToFormat(QImage::Format_RGBA8888));
    rt.staticTextures[key] = tex;
    return tex;
}

// Source pixels → effects → a canvas-space layer target, still on the GPU.
GlTarget buildLayerTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, const GpuLayer &layer,
                          const QSize &canvasSize)
{
    if (!layer.valid || !layer.hasPixels())
        return {};

    GlTarget target;
    if (layer.video.isValid()) {
        target = promoteVideoFrameToTarget(rt, gl, layer.video);
    } else {
        target = promoteImageToTargetCached(rt, gl, layer.source, layer.source.size());
    }
    if (!target.isValid())
        return {};

    for (const drift::Effect &effect : layer.effects) {
        const EffectPresetEntry *def =
            effect.catalogId.isEmpty() ? nullptr : effectDefForId(effect.catalogId);
        if (!def)
            continue;
        if (def->meta.id == QStringLiteral("time_echo"))
            continue; // history is assembled before the scene is built

        if (def->isModel3d) {
            QMap<QString, QVariant> params = resolvedEffectParameters(effect, *def);
            const drift::FaceModelParams modelParams = drift::faceModelParamsFromMap(params);
            GlTarget next =
                drawFaceModelEffect(rt, gl, modelParams, layer.faceSlots, target);
            if (!next.isValid())
                continue; // grace mode
            rt.releaseTarget(std::move(target));
            target = std::move(next);
            continue;
        }

        if (def->isFaceSwap) {
            QMap<QString, QVariant> params = resolvedEffectParameters(effect, *def);
            const drift::FaceSwapParams swapParams =
                drift::faceSwapParamsFromMap(params, def->gpu.packageDir);
            GlTarget next = drawFaceSwapEffect(rt, gl, swapParams, layer.faceSlots, target);
            if (!next.isValid())
                continue; // grace mode
            rt.releaseTarget(std::move(target));
            target = std::move(next);
            continue;
        }

        if (!def->isGpu || !def->gpu.valid)
            continue;

        QMap<QString, QVariant> params = resolvedEffectParameters(effect, *def);
        if (def->needsFace)
            drift::applyFaceUniforms(&params, layer.faceSlots);

        const std::vector<const GlTarget *> sources{&target};
        GlTarget next = runPipeline(rt, gl, def->meta.id, def->gpu, sources, params,
                                    layer.clipTimeUs, 0.0, target.size());
        if (!next.isValid())
            continue; // grace mode

        rt.releaseTarget(std::move(target));
        target = std::move(next);
    }

    return target;
}

void bindQuad(GlRuntime &rt, QOpenGLExtraFunctions *gl)
{
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
}

// Straight alpha → premultiplied, so a mip chain can be averaged without the
// transparent texels dragging the colour toward black.
constexpr const char *kPremultiplyFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    fragColor = vec4(c.rgb * c.a, c.a);
}
)";

// A layer texture is bounded by the canvas, not by the clip's layout rect, so an overlay
// drawn small minifies by a large factor. GL_LINEAR is a single 2x2 tap: it drops most of
// the source pixels and aliases. Build a premultiplied mip chain instead and let the draw
// sample it trilinearly.
GlTarget mipmappedLayerCopy(GlRuntime &rt, QOpenGLExtraFunctions *gl, const GlTarget &src)
{
    QOpenGLShaderProgram *program =
        rt.builtinProgram(QStringLiteral("__premul__"), kQuadVertexShader, kPremultiplyFragShader);
    if (!program)
        return {};

    GlTarget copy = rt.acquireTarget(src.width, src.height);
    if (!copy.isValid())
        return {};

    copy.fbo->bind();
    gl->glViewport(0, 0, copy.width, copy.height);
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    program->bind();
    program->setUniformValue("u_currentTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, src.texture());
    bindQuad(rt, gl);
    program->release();
    copy.fbo->release();

    gl->glBindTexture(GL_TEXTURE_2D, copy.texture());
    gl->glGenerateMipmap(GL_TEXTURE_2D);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    return copy;
}

// Draw a prepared layer target onto the canvas with transform, opacity, mask and
// blend mode. For non-fixed-function modes the canvas is ping-ponged.
void drawLayerOnCanvas(GlRuntime &rt, QOpenGLExtraFunctions *gl, GlTarget &canvas,
                       const GlTarget &layerTarget, const GpuLayer &layer, drift::BlendMode blend,
                       const QSize &canvasSize)
{
    if (!layerTarget.isValid() || layer.rect.width() < 0.5 || layer.rect.height() < 0.5)
        return;
    if (layer.opacity <= 0.0)
        return;

    // A matte changes every frame, so it goes through the recycled target pool rather than
    // maskTexture()'s static cache, which is keyed by mask parameters and would never evict.
    // Invert cannot be baked in either: the foreground and background clips of a segmentation
    // share one matte file and differ only by this flag.
    GlTarget matteTarget;
    GLuint maskTex = 0;
    float maskInvert = 0.f;
    if (!layer.matte.isNull()) {
        matteTarget = promoteImageToTargetCached(rt, gl, layer.matte, layer.matte.size());
        maskTex = matteTarget.isValid() ? matteTarget.texture() : 0;
        maskInvert = layer.mask.invert ? 1.f : 0.f;
    } else {
        maskTex = maskTexture(rt, gl, layer.mask, layerTarget.size());
    }
    const QMatrix4x4 model = modelMatrixFor(layer, canvasSize);

    // Every return path below must recycle the matte target.
    struct MatteGuard
    {
        GlRuntime &rt;
        GlTarget &target;
        ~MatteGuard()
        {
            if (target.isValid())
                rt.releaseTarget(std::move(target));
        }
    } matteGuard{rt, matteTarget};

    // Only worth it when the quad is actually smaller than the texture; at ~1:1 the
    // single bilinear tap is already exact and the copy would be pure cost.
    const bool minifies = layerTarget.width > layer.rect.width() * 1.05
                          || layerTarget.height > layer.rect.height() * 1.05;
    GlTarget mipTarget = minifies ? mipmappedLayerCopy(rt, gl, layerTarget) : GlTarget{};
    const GLuint layerTex = mipTarget.isValid() ? mipTarget.texture() : layerTarget.texture();
    const float layerPremul = mipTarget.isValid() ? 1.f : 0.f;

    // Targets are pooled by size: hand this one back with the default filter or the next
    // user would sample the mip levels this frame left behind.
    struct MipGuard
    {
        GlRuntime &rt;
        QOpenGLExtraFunctions *gl;
        GlTarget &target;
        ~MipGuard()
        {
            if (!target.isValid())
                return;
            gl->glBindTexture(GL_TEXTURE_2D, target.texture());
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            rt.releaseTarget(std::move(target));
        }
    } mipGuard{rt, gl, mipTarget};

    if (isFixedFunctionBlend(blend)) {
        QOpenGLShaderProgram *program =
            rt.builtinProgram(QStringLiteral("__layer__"), kLayerVertexShader, kLayerFragShader);
        if (!program)
            return;

        canvas.fbo->bind();
        gl->glViewport(0, 0, canvas.width, canvas.height);
        gl->glEnable(GL_BLEND);
        if (blend == drift::BlendMode::Add)
            gl->glBlendFunc(GL_ONE, GL_ONE);
        else
            gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied source-over

        program->bind();
        program->setUniformValue("u_model", model);
        program->setUniformValue("u_opacity", float(layer.opacity));
        program->setUniformValue("u_hasMask", maskTex ? 1.f : 0.f);
        program->setUniformValue("u_maskInvert", maskInvert);
        program->setUniformValue("u_layer", 0);
        program->setUniformValue("u_mask", 1);
        program->setUniformValue("u_layerPremul", layerPremul);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, layerTex);
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, maskTex);
        bindQuad(rt, gl);
        program->release();
        gl->glDisable(GL_BLEND);
        canvas.fbo->release();
        return;
    }

    // Blend modes that read the destination: copy the canvas aside, then draw the
    // quad into a fresh canvas while sampling the copy.
    GlTarget previous = rt.acquireTarget(canvasSize.width(), canvasSize.height());
    if (!previous.isValid())
        return;
    if (!blitTextureToTarget(rt, gl, canvas.texture(), previous)) {
        rt.releaseTarget(std::move(previous));
        return;
    }

    QOpenGLShaderProgram *program =
        rt.builtinProgram(QStringLiteral("__layer_blend__"), kLayerVertexShader, kBlendFragShader);
    if (!program) {
        rt.releaseTarget(std::move(previous));
        return;
    }

    canvas.fbo->bind();
    gl->glViewport(0, 0, canvas.width, canvas.height);
    gl->glDisable(GL_BLEND); // the shader does the compositing itself

    program->bind();
    program->setUniformValue("u_model", model);
    program->setUniformValue("u_opacity", float(layer.opacity));
    program->setUniformValue("u_hasMask", maskTex ? 1.f : 0.f);
    program->setUniformValue("u_maskInvert", maskInvert);
    program->setUniformValue("u_blendMode", blendModeCode(blend));
    program->setUniformValue("u_canvasSize",
                             QVector2D(float(canvasSize.width()), float(canvasSize.height())));
    program->setUniformValue("u_layer", 0);
    program->setUniformValue("u_mask", 1);
    program->setUniformValue("u_dst", 2);
    program->setUniformValue("u_layerPremul", layerPremul);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, layerTex);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, maskTex);
    gl->glActiveTexture(GL_TEXTURE2);
    gl->glBindTexture(GL_TEXTURE_2D, previous.texture());
    bindQuad(rt, gl);
    program->release();
    canvas.fbo->release();

    rt.releaseTarget(std::move(previous));
}

// Render a clip into its own transparent canvas-sized layer, with its transform
// baked in. Transitions need both sides in the same UV space for the shader to
// mix them.
GlTarget renderIsolatedLayer(GlRuntime &rt, QOpenGLExtraFunctions *gl, const GpuLayer &layer,
                             const QSize &canvasSize)
{
    GlTarget isolated = rt.acquireTarget(canvasSize.width(), canvasSize.height());
    if (!isolated.isValid())
        return {};

    isolated.fbo->bind();
    gl->glViewport(0, 0, isolated.width, isolated.height);
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    isolated.fbo->release();

    GlTarget source = buildLayerTarget(rt, gl, layer, canvasSize);
    if (!source.isValid())
        return isolated; // transparent, which is what a shader sampling this side expects

    // Isolated layers always composite source-over: the clip's own blend mode has
    // nothing to blend against here, and the transition shader does the mixing.
    drawLayerOnCanvas(rt, gl, isolated, source, layer, drift::BlendMode::Normal, canvasSize);
    rt.releaseTarget(std::move(source));
    return isolated;
}

void fillBackground(GlRuntime &rt, QOpenGLExtraFunctions *gl, GlTarget &canvas, const GpuScene &scene)
{
    canvas.fbo->bind();
    gl->glViewport(0, 0, canvas.width, canvas.height);
    gl->glDisable(GL_BLEND);
    const QColor &c = scene.backgroundColor.isValid() ? scene.backgroundColor : QColor(Qt::black);
    gl->glClearColor(float(c.redF()), float(c.greenF()), float(c.blueF()), 1.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    canvas.fbo->release();

    if (!scene.backgroundBlur || scene.blurSource.isNull())
        return;

    // Cover-fit the source over the canvas, then separable-blur it. The CPU path
    // did this with a scalar box blur over a second full decode of the clip.
    const QSize canvasSize = canvas.size();
    const QSize srcSize = scene.blurSource.size();
    const double scale = qMax(double(canvasSize.width()) / srcSize.width(),
                              double(canvasSize.height()) / srcSize.height());

    GpuLayer cover;
    cover.valid = true;
    cover.source = scene.blurSource;
    const double w = srcSize.width() * scale;
    const double h = srcSize.height() * scale;
    cover.rect = QRectF((canvasSize.width() - w) * 0.5, (canvasSize.height() - h) * 0.5, w, h);

    GlTarget coverTarget = rt.acquireTarget(canvasSize.width(), canvasSize.height());
    if (!coverTarget.isValid())
        return;
    coverTarget.fbo->bind();
    gl->glViewport(0, 0, coverTarget.width, coverTarget.height);
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.f, 0.f, 0.f, 1.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    coverTarget.fbo->release();

    GlTarget src = promoteImageToTargetCached(rt, gl, scene.blurSource, srcSize);
    if (!src.isValid()) {
        rt.releaseTarget(std::move(coverTarget));
        return;
    }
    drawLayerOnCanvas(rt, gl, coverTarget, src, cover, drift::BlendMode::Normal, canvasSize);
    rt.releaseTarget(std::move(src));

    QOpenGLShaderProgram *blur =
        rt.builtinProgram(QStringLiteral("__bg_blur__"), kQuadVertexShader, kBlurFragShader);
    if (!blur) {
        rt.releaseTarget(std::move(coverTarget));
        return;
    }

    const float radius = float(qBound(1.0, scene.blurStrengthPx, 64.0));
    GlTarget pass = rt.acquireTarget(canvasSize.width(), canvasSize.height());
    if (!pass.isValid()) {
        rt.releaseTarget(std::move(coverTarget));
        return;
    }

    auto blurPass = [&](GlTarget &in, GlTarget &out, float dx, float dy) {
        out.fbo->bind();
        gl->glViewport(0, 0, out.width, out.height);
        gl->glDisable(GL_BLEND);
        blur->bind();
        blur->setUniformValue("u_currentTexture", 0);
        blur->setUniformValue("u_radius", radius);
        blur->setUniformValue("u_texel", QVector2D(dx / canvasSize.width(), dy / canvasSize.height()));
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, in.texture());
        bindQuad(rt, gl);
        blur->release();
        out.fbo->release();
    };

    blurPass(coverTarget, pass, 1.f, 0.f);
    blurPass(pass, coverTarget, 0.f, 1.f);
    rt.releaseTarget(std::move(pass));

    // The blurred cover replaces the cleared canvas.
    blitTextureToTarget(rt, gl, coverTarget.texture(), canvas);
    rt.releaseTarget(std::move(coverTarget));
}


// Draws the whole scene into `canvas`. The caller owns the canvas, which is what
// lets the preview compose straight into a presentation target it then hands to
// the scene graph, while export composes into a pooled target it reads back.
void composeOnGlThread(GlRuntime &rt, const GpuScene &scene, GlTarget &canvas)
{
    auto *gl = rt.functions();
    if (!gl)
        return;

    const QSize canvasSize = scene.canvasSize;

    fillBackground(rt, gl, canvas, scene);

    for (const GpuItem &item : scene.items) {
        if (item.isAdjustment) {
            if (item.layer.effects.isEmpty() || item.layer.opacity <= 0.001)
                continue;

            GlTarget copy = rt.acquireTarget(canvasSize.width(), canvasSize.height());
            if (!copy.isValid())
                continue;
            if (!blitTextureToTarget(rt, gl, canvas.texture(), copy)) {
                rt.releaseTarget(std::move(copy));
                continue;
            }

            GlTarget target = std::move(copy);
            for (const drift::Effect &effect : item.layer.effects) {
                if (!effect.enabled)
                    continue;
                const EffectPresetEntry *def = effectDefForId(effect.catalogId);
                if (!def)
                    continue;

                if (def->isFaceSwap) {
                    QMap<QString, QVariant> params = resolvedEffectParameters(effect, *def);
                    const drift::FaceSwapParams swapParams =
                        drift::faceSwapParamsFromMap(params, def->gpu.packageDir);
                    GlTarget next = drawFaceSwapEffect(rt, gl, swapParams, item.layer.faceSlots, target);
                    if (!next.isValid())
                        continue;
                    rt.releaseTarget(std::move(target));
                    target = std::move(next);
                    continue;
                }

                if (!def->isGpu || !def->gpu.valid)
                    continue;

                QMap<QString, QVariant> params = resolvedEffectParameters(effect, *def);
                if (def->needsFace)
                    drift::applyFaceUniforms(&params, item.layer.faceSlots);

                const std::vector<const GlTarget *> sources{&target};
                GlTarget next = runPipeline(rt, gl, def->meta.id, def->gpu, sources, params,
                                            item.layer.clipTimeUs, 0.0, target.size());
                if (!next.isValid())
                    continue;

                rt.releaseTarget(std::move(target));
                target = std::move(next);
            }

            drawLayerOnCanvas(rt, gl, canvas, target, item.layer, item.blend, canvasSize);
            rt.releaseTarget(std::move(target));
            continue;
        }

        if (!item.isTransition) {
            GlTarget layerTarget = buildLayerTarget(rt, gl, item.layer, canvasSize);
            if (!layerTarget.isValid())
                continue;
            drawLayerOnCanvas(rt, gl, canvas, layerTarget, item.layer, item.blend, canvasSize);
            rt.releaseTarget(std::move(layerTarget));
            continue;
        }

        GlTarget fromTarget = renderIsolatedLayer(rt, gl, item.from, canvasSize);
        GlTarget toTarget = renderIsolatedLayer(rt, gl, item.to, canvasSize);
        if (!fromTarget.isValid() || !toTarget.isValid()) {
            rt.releaseTarget(std::move(fromTarget));
            rt.releaseTarget(std::move(toTarget));
            continue;
        }

        GlTarget mixed;
        if (item.transitionGpu && item.transitionGpu->valid) {
            const std::vector<const GlTarget *> sources{&fromTarget, &toTarget};
            mixed = runPipeline(rt, gl, item.transitionKey, *item.transitionGpu, sources,
                                item.transitionParams, item.transitionTimeUs, item.progress, canvasSize);
        }

        if (mixed.isValid()) {
            // The mixed result already carries both sides' transforms and alpha.
            GpuLayer full;
            full.valid = true;
            full.rect = QRectF(0, 0, canvasSize.width(), canvasSize.height());
            drawLayerOnCanvas(rt, gl, canvas, mixed, full, drift::BlendMode::Normal, canvasSize);
            rt.releaseTarget(std::move(mixed));
        } else {
            // Grace mode: a plain crossfade, still on the GPU.
            const double p = qBound(0.0, item.progress, 1.0);
            GpuLayer a;
            a.valid = true;
            a.rect = QRectF(0, 0, canvasSize.width(), canvasSize.height());
            a.opacity = 1.0 - p;
            drawLayerOnCanvas(rt, gl, canvas, fromTarget, a, drift::BlendMode::Normal, canvasSize);
            GpuLayer b = a;
            b.opacity = p;
            drawLayerOnCanvas(rt, gl, canvas, toTarget, b, drift::BlendMode::Normal, canvasSize);
        }

        rt.releaseTarget(std::move(fromTarget));
        rt.releaseTarget(std::move(toTarget));
    }

}

} // namespace

namespace GpuCompositor {

bool isAvailable()
{
    return runtime().available();
}

QImage render(const GpuScene &scene)
{
    if (scene.canvasSize.isEmpty())
        return {};

    GlRuntime &rt = runtime();
    QImage result;

    // All GL work happens on the runtime's own thread, with the context current.
    rt.exec([&] {
        GlTarget canvas = rt.acquireTarget(scene.canvasSize.width(), scene.canvasSize.height());
        if (!canvas.isValid())
            return;

        composeOnGlThread(rt, scene, canvas);

        // The canvas holds premultiplied alpha; toImage() labels it as such and
        // the conversion un-premultiplies back to straight RGBA.
        result = rt.readTarget(canvas);
        rt.releaseTarget(std::move(canvas));
    });
    return result;
}

GpuFrameTexture renderToTexture(const GpuScene &scene)
{
    GpuFrameTexture out;
    if (scene.canvasSize.isEmpty())
        return out;

    GlRuntime &rt = runtime();

#ifdef Q_OS_ANDROID
    // Handing over a texture *name* only works while both contexts are in one share
    // group; a driver that refuses sharing would leave the preview black rather than
    // merely slow, so fall back to a readback in that case.
    if (!rt.sharesWithGuiContext()) {
        rt.exec([&] {
            GlTarget canvas = rt.acquireTarget(scene.canvasSize.width(), scene.canvasSize.height());
            if (!canvas.isValid())
                return;

            composeOnGlThread(rt, scene, canvas);

            // Straight out of the FBO, premultiplied, which is exactly what the scene
            // graph uploads: render()'s un-premultiply to RGBA8888 and PreviewItem's
            // convert back would both be pure waste on this path.
            out.image = canvas.fbo->toImage(false);
            out.size = out.image.size();
            rt.releaseTarget(std::move(canvas));
        });
        return out;
    }
#endif

    rt.exec([&] {
        GlTarget &canvas = rt.acquirePresentTarget(scene.canvasSize.width(), scene.canvasSize.height());
        if (!canvas.isValid())
            return;

        composeOnGlThread(rt, scene, canvas);

        // Insert a present fence without waiting: Qt Quick samples on the next
        // vsync. acquirePresentTarget waits this fence before reusing the slot.
        rt.markPresentReady(canvas);

        out.textureId = canvas.texture();
        out.size = canvas.size();
    });
    return out;
}

static_assert(kExportNv12Slots == GlRuntime::kExportNv12Slots);

bool beginExportNv12(const GpuScene &scene, int outW, int outH, int slot)
{
    if (scene.canvasSize.isEmpty() || outW < 2 || outH < 2 || (outW % 2) || (outH % 2)
        || slot < 0 || slot >= kExportNv12Slots)
        return false;

    GlRuntime &rt = runtime();
    bool ok = false;
    rt.exec([&] {
        GlTarget canvas = rt.acquireTarget(scene.canvasSize.width(), scene.canvasSize.height());
        if (!canvas.isValid())
            return;

        composeOnGlThread(rt, scene, canvas);
        ok = rt.packCanvasToNv12Slot(canvas, outW, outH, slot);
        rt.releaseTarget(std::move(canvas));
    });
    return ok;
}

bool finishExportNv12(int slot, uint8_t *y, int yStride, uint8_t *uv, int uvStride, int width,
                      int height)
{
    if (!y || !uv || slot < 0 || slot >= kExportNv12Slots)
        return false;

    GlRuntime &rt = runtime();
    bool ok = false;
    rt.exec([&] { ok = rt.mapNv12Slot(slot, y, yStride, uv, uvStride, width, height); });
    return ok;
}

} // namespace GpuCompositor
