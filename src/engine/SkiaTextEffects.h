#pragma once

#include "core/TextShading.h"

#include <QRectF>

#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"

// The paint sources of a text shading layer in Skia form: gradients mapped onto a box, image
// textures, and the SkSL effect registry. Skia-including header: only Skia*.cpp may include it.

namespace drift::skia {

// Built in unit space and mapped onto `box` with a local matrix, so one definition serves every
// mapping space; `timeSec` drives offsetSpeed.
sk_sp<SkShader> gradientShaderFor(const TextGradient &gradient, const QRectF &box, double timeSec);

// Null when the file cannot be decoded (the caller falls back to the tint).
sk_sp<SkShader> textureShaderFor(const TextTexture &texture, const QRectF &box);

// The effect over `base` (the layer's own paint). Compiled once per id and cached; null for an
// unknown id or a compile failure (the caller falls back to the tint).
sk_sp<SkShader> effectShaderFor(const TextShaderEffect &effect, sk_sp<SkShader> base, const QRectF &box,
                                double timeSec, double progress);
bool textEffectIsAnimated(const TextShaderEffect &effect);

// Compile error text for an id, empty when the effect compiles (tests).
QString textEffectCompileError(const QString &id);

void clearTextEffectCaches();

} // namespace drift::skia
