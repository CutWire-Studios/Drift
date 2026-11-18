#pragma once

#include "engine/gpu/effects/GpuEffectDefinition.h"

#include <QImage>
#include <QMap>
#include <QSize>
#include <QString>
#include <QVariant>

// Colour-bar test card the effect thumbnails are drawn over when the caller has none.
QImage defaultEffectBase(int side);
// The built-in bases at an arbitrary size: 0 the test card, 1 and 2 the warm and cool transition pair.
QImage defaultSampleImage(int which, QSize size);

// The cacheKey is passed to GpuEffectExecutor unchanged, so callers add kTransitionCacheKeyPrefix or
// kAnimationCacheKeyPrefix themselves. Every function returns a null image when the GPU pass fails.

// One cell, rendered at a fixed mid-clip time and scaled to `size`. A null `source` uses defaultEffectBase.
QImage renderEffectThumbnail(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                             const QMap<QString, QVariant> &params, QSize size,
                             const QImage &source = {});

// `frames` cells (at least 2) across progress 0..1, endpoints included, left to right. Null
// `from`/`to` use the built-in bases.
QImage renderTransitionStrip(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                             const QMap<QString, QVariant> &params, QSize cellSize, int frames,
                             const QImage &from = {}, const QImage &to = {});

// As renderTransitionStrip with one input. outDirection sets u_direction to 1 (out) or 0 (in).
QImage renderAnimationStrip(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                            QMap<QString, QVariant> params, QSize cellSize, int frames,
                            bool outDirection, const QImage &source = {});
