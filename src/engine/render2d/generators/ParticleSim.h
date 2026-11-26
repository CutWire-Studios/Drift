#pragma once

#include "core/base/Time.h"
#include "core/project/sources/ProceduralSource.h"

#include <QColor>
#include <QString>

#include <vector>

// The particle emitter's simulation, Skia-free. It is a pure function of the parameters, the clip
// time and the seed: the state is stepped from the clip start (or the pre-warm start before it) on a
// fixed 1/60 s clock and every random number is a hash of (seed, particle id, channel), so a scrub,
// a playback and an export of the same instant agree to the bit. Checkpoints of the state are kept
// every few seconds per clip, which is what makes seeking cost a couple of seconds of steps instead
// of a replay from the start.
//
// Space is "units": the layer is 1000 units tall and 1000 * aspect wide, origin top-left, y down.

namespace drift::particles {

constexpr double kStep = 1.0 / 60.0;
constexpr int kCheckpointSteps = 120;
constexpr int kMaxCheckpoints = 400;
constexpr int kMaxAlive = 5000;
constexpr double kUnitsHigh = 1000.0;

enum class EmitterShape { Point, Line, Rectangle, Circle };

// Everything the parameters say, read at one instant. The sim reads the motion half at every step;
// the painter reads the look half once per frame.
struct Params
{
    EmitterShape shape = EmitterShape::Point;
    double emitterX = 0.5, emitterY = 0.5, emitterW = 0.3, emitterH = 0.3;
    double rate = 0.0;
    int burst = 0;
    double burstEvery = 0.0;
    bool burstAtPoint = false;
    double prewarm = 0.0;
    double life = 2.0, lifeVar = 0.3;
    double speed = 300.0, speedVar = 0.3, angle = 270.0, spread = 30.0;
    double gravity = 0.0, wind = 0.0, drag = 0.0, turbulence = 0.0, turbulenceScale = 1.0;
    double spin = 0.0, spinVar = 0.0;
    bool randomRotation = true;
    bool alignVelocity = false;
    double stretch = 0.0;
    int sprite = 0;
    QString spriteImage;
    bool additive = false;
    double size = 16.0, sizeVar = 0.3, sizeEnd = 1.0;
    double opacity = 1.0, opacityEnd = 0.0;
    QColor colorStart, colorMid, colorEnd;
    bool rainbow = false;

    // `t` is clip-local; keyframes are evaluated there.
    static Params read(const ProceduralSource &source, TimeUs clipTimeUs);
};

struct Particle
{
    float x = 0, y = 0, vx = 0, vy = 0;
    float age = 0, life = 1;
    float rot = 0, spin = 0;
    float sizeK = 1, hue = 0;
    quint32 id = 0;
};

struct Frame
{
    std::vector<Particle> particles; // positions already carried on to the exact instant
};

// The particles alive at `clipTimeUs`. `cacheId` (the clip id) names the checkpoint set; the key
// covers every parameter and keyframe, so editing the clip drops the old checkpoints.
Frame simulate(const QString &cacheId, const ProceduralSource &source, double aspect, TimeUs clipTimeUs);

// The same, never touching the cache. For tests and one-off renders.
Frame simulateUncached(const ProceduralSource &source, double aspect, TimeUs clipTimeUs);

void clearCache();
int cachedCheckpointCount(const QString &cacheId);

} // namespace drift::particles
