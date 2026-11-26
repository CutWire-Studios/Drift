#pragma once

#include "core/base/Time.h"
#include "core/project/sources/ProceduralSource.h"
#include "core/text/TextStyle.h"
#include "engine/render2d/skia/VectorPainter.h"

#include <QRectF>
#include <QString>

#include <memory>

// The painters of the built-in generator kinds (core/project/sources/GeneratorKinds.h): solid,
// gradient, noise, test patterns, the countdown leader and the particle emitter. Everything is a
// function of the parameters and the clip time, so a scrub, a playback and an export agree.
// Skia-free header, like the shape and text painters.

namespace drift::skia {

struct GeneratorPaintRequest
{
    ProceduralSource source;   // as stored: keyframes intact, for the particle sim's per-step reads
    ProceduralSource resolved; // source.resolvedAt(clipTimeUs): what the picture is drawn from
    QString clipId;
    QRectF layoutRect;         // the clip's frame in canvas px
    double renderScale = 1.0;  // canvas px per project px
    TimeUs clipTimeUs = 0;
    int frameRate = 30;
    TextStyle textStyle;       // the countdown's number
    QString spriteImagePath;   // particle sprite file, already resolved from an emoji if need be
};

struct GeneratorPainterResult
{
    std::shared_ptr<const VectorPainter> painter; // null for an unknown kind
    QRectF rect;                                  // destination in canvas px
};

GeneratorPainterResult makeGeneratorPainter(const GeneratorPaintRequest &request);

// Static kinds draw once and are served from the GPU cache; this says whether the request needs a
// redraw every frame. For tests.
bool generatorIsTimeDriven(const GeneratorPaintRequest &request);

} // namespace drift::skia
