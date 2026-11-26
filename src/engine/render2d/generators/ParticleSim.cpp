#include "engine/render2d/generators/ParticleSim.h"

#include "core/project/sources/GeneratorKinds.h"

#include <QHash>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <map>

namespace drift::particles {

namespace {

constexpr double kPi = 3.14159265358979323846;

const QMap<QString, QVariant> &defaults()
{
    static const QMap<QString, QVariant> map = generatorDefaultParameters(*generatorKind(QStringLiteral("gen.particles")));
    return map;
}

QVariant valueOf(const ProceduralSource &source, const QString &key, TimeUs t)
{
    const QVariant v = source.valueAt(key, t);
    return v.isValid() ? v : defaults().value(key);
}

double num(const ProceduralSource &source, const char *key, TimeUs t)
{
    return valueOf(source, QString::fromLatin1(key), t).toDouble();
}

bool flag(const ProceduralSource &source, const char *key, TimeUs t)
{
    return valueOf(source, QString::fromLatin1(key), t).toDouble() > 0.5;
}

QColor colorOf(const ProceduralSource &source, const char *key)
{
    const QString hex = source.parameters.value(QString::fromLatin1(key), defaults().value(QString::fromLatin1(key))).toString();
    QColor c(hex.left(7));
    return c.isValid() ? c : QColor(Qt::white);
}

quint64 mix(quint64 x)
{
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

double rnd(quint64 seed, quint64 id, int channel)
{
    return double(mix(seed * 0x2545f4914f6cdd1dull ^ mix(id * 0x9e3779b1ull + quint64(channel))) >> 11)
           * (1.0 / 9007199254740992.0);
}

double lattice(qint64 ix, qint64 iy, qint64 iz, quint64 seed)
{
    return double(mix(seed ^ mix(quint64(ix) * 0x9e3779b97f4a7c15ull ^ mix(quint64(iy) * 0xc2b2ae3d27d4eb4full ^ quint64(iz))))
                  >> 11)
           * (1.0 / 9007199254740992.0);
}

double smooth(double t)
{
    return t * t * (3.0 - 2.0 * t);
}

double valueNoise(double x, double y, double z, quint64 seed)
{
    const double fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const qint64 ix = qint64(fx), iy = qint64(fy), iz = qint64(fz);
    const double tx = smooth(x - fx), ty = smooth(y - fy), tz = smooth(z - fz);
    const auto at = [&](int dx, int dy, int dz) { return lattice(ix + dx, iy + dy, iz + dz, seed); };
    const auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
    const double x00 = lerp(at(0, 0, 0), at(1, 0, 0), tx);
    const double x10 = lerp(at(0, 1, 0), at(1, 1, 0), tx);
    const double x01 = lerp(at(0, 0, 1), at(1, 0, 1), tx);
    const double x11 = lerp(at(0, 1, 1), at(1, 1, 1), tx);
    return lerp(lerp(x00, x10, ty), lerp(x01, x11, ty), tz);
}

struct State
{
    qint64 step = 0;
    double acc = 0.0;
    quint64 nextId = 0;
    qint64 nextBurst = 0;
    std::vector<Particle> alive;
};

void spawn(State &s, const Params &p, quint64 seed, double aspect, double burstX, double burstY, bool fromBurstPoint)
{
    if (int(s.alive.size()) >= kMaxAlive) {
        ++s.nextId;
        return;
    }
    const double width = kUnitsHigh * aspect;
    const quint64 id = s.nextId++;
    double x = p.emitterX * width;
    double y = p.emitterY * kUnitsHigh;
    if (fromBurstPoint) {
        x = burstX;
        y = burstY;
    } else {
        const double u = rnd(seed, id, 0);
        const double v = rnd(seed, id, 1);
        switch (p.shape) {
        case EmitterShape::Point:
            break;
        case EmitterShape::Line:
            x += (u - 0.5) * p.emitterW * width;
            break;
        case EmitterShape::Rectangle:
            x += (u - 0.5) * p.emitterW * width;
            y += (v - 0.5) * p.emitterH * kUnitsHigh;
            break;
        case EmitterShape::Circle: {
            const double r = std::sqrt(u);
            const double th = 2.0 * kPi * v;
            x += std::cos(th) * r * p.emitterW * width * 0.5;
            y += std::sin(th) * r * p.emitterH * kUnitsHigh * 0.5;
            break;
        }
        }
    }
    const double dir = (p.angle + (rnd(seed, id, 2) - 0.5) * p.spread) * kPi / 180.0;
    const double speed = std::max(0.0, p.speed * (1.0 + (rnd(seed, id, 3) * 2.0 - 1.0) * p.speedVar));
    Particle q;
    q.id = quint32(id);
    q.x = float(x);
    q.y = float(y);
    q.vx = float(std::cos(dir) * speed);
    q.vy = float(std::sin(dir) * speed);
    q.life = float(std::max(0.05, p.life * (1.0 + (rnd(seed, id, 4) * 2.0 - 1.0) * p.lifeVar)));
    q.spin = float(p.spin + (rnd(seed, id, 5) * 2.0 - 1.0) * p.spinVar);
    q.sizeK = float(std::max(0.1, 1.0 + (rnd(seed, id, 6) * 2.0 - 1.0) * p.sizeVar));
    q.hue = float(rnd(seed, id, 7));
    q.rot = p.randomRotation ? float(rnd(seed, id, 8) * 360.0) : 0.0f;
    s.alive.push_back(q);
}

void advance(State &s, const ProceduralSource &source, double aspect, quint64 seed, double t0)
{
    const double t = t0 + double(s.step) * kStep;
    const TimeUs us = std::max<TimeUs>(0, secondsToUs(t));
    const Params p = Params::read(source, us);
    const double dt = kStep;

    const double drag = std::exp(-p.drag * dt);
    const double freq = p.turbulenceScale / 200.0;
    for (Particle &q : s.alive) {
        q.vx += float(p.wind * dt);
        q.vy += float(p.gravity * dt);
        if (p.turbulence > 0.0) {
            const double fx = (valueNoise(q.x * freq, q.y * freq, t * 0.5, seed) * 2.0 - 1.0) * p.turbulence;
            const double fy = (valueNoise(q.x * freq + 31.7, q.y * freq - 17.3, t * 0.5, seed + 1) * 2.0 - 1.0) * p.turbulence;
            q.vx += float(fx * dt);
            q.vy += float(fy * dt);
        }
        q.vx *= float(drag);
        q.vy *= float(drag);
        q.x += q.vx * float(dt);
        q.y += q.vy * float(dt);
        q.rot += q.spin * float(dt);
        q.age += float(dt);
    }
    s.alive.erase(std::remove_if(s.alive.begin(), s.alive.end(), [](const Particle &q) { return q.age >= q.life; }),
                  s.alive.end());

    if (p.rate > 0.0) {
        s.acc += p.rate * dt;
        const int n = int(s.acc);
        s.acc -= n;
        for (int i = 0; i < n; ++i)
            spawn(s, p, seed, aspect, 0, 0, false);
    }

    const bool repeating = p.burstEvery > 0.0;
    while (p.burst > 0 && t + 1e-9 >= double(s.nextBurst) * p.burstEvery && (repeating || s.nextBurst == 0)) {
        const quint64 burstKey = 0x100000000ull + quint64(s.nextBurst);
        double bx = 0, by = 0;
        if (p.burstAtPoint) {
            const double width = kUnitsHigh * aspect;
            bx = p.emitterX * width;
            by = p.emitterY * kUnitsHigh;
            if (p.shape != EmitterShape::Point) {
                const double u = rnd(seed, burstKey, 0), v = rnd(seed, burstKey, 1);
                bx += (u - 0.5) * p.emitterW * width;
                if (p.shape == EmitterShape::Rectangle || p.shape == EmitterShape::Circle)
                    by += (v - 0.5) * p.emitterH * kUnitsHigh;
            }
        }
        for (int i = 0; i < p.burst; ++i)
            spawn(s, p, seed, aspect, bx, by, p.burstAtPoint);
        ++s.nextBurst;
        if (!repeating)
            break;
    }
    ++s.step;
}

struct Entry
{
    quint64 key = 0;
    std::map<qint64, State> checkpoints;
    State last;
    bool hasLast = false;
};

QMutex g_mutex;
QHash<QString, Entry> g_cache;

quint64 sourceKey(const ProceduralSource &source, double aspect)
{
    const QByteArray bytes = QJsonDocument(source.toJson()).toJson(QJsonDocument::Compact);
    return quint64(qHash(bytes, 0x5eed)) ^ (quint64(qHash(QByteArray::number(aspect, 'g', 8), 7)) << 32);
}

Frame finish(const State &s, double t0, double t)
{
    const double rem = std::max(0.0, t - (t0 + double(s.step) * kStep));
    Frame out;
    out.particles = s.alive;
    if (rem > 0.0) {
        for (Particle &q : out.particles) {
            q.x += q.vx * float(rem);
            q.y += q.vy * float(rem);
            q.rot += q.spin * float(rem);
            q.age += float(rem);
        }
    }
    return out;
}

} // namespace

Params Params::read(const ProceduralSource &source, TimeUs t)
{
    Params p;
    p.shape = EmitterShape(std::clamp(int(num(source, "emitterShape", t)), 0, 3));
    p.emitterX = num(source, "emitter.x", t);
    p.emitterY = num(source, "emitter.y", t);
    p.emitterW = num(source, "emitterW", t);
    p.emitterH = num(source, "emitterH", t);
    p.rate = num(source, "rate", t);
    p.burst = int(num(source, "burst", t));
    p.burstEvery = num(source, "burstEvery", t);
    p.burstAtPoint = flag(source, "burstAtPoint", t);
    p.prewarm = num(source, "prewarm", 0);
    p.life = num(source, "life", t);
    p.lifeVar = num(source, "lifeVar", t);
    p.speed = num(source, "speed", t);
    p.speedVar = num(source, "speedVar", t);
    p.angle = num(source, "angle", t);
    p.spread = num(source, "spread", t);
    p.gravity = num(source, "gravity", t);
    p.wind = num(source, "wind", t);
    p.drag = num(source, "drag", t);
    p.turbulence = num(source, "turbulence", t);
    p.turbulenceScale = num(source, "turbulenceScale", t);
    p.spin = num(source, "spin", t);
    p.spinVar = num(source, "spinVar", t);
    p.randomRotation = flag(source, "randomRotation", t);
    p.alignVelocity = flag(source, "alignVelocity", t);
    p.stretch = num(source, "stretch", t);
    p.sprite = int(num(source, "sprite", t));
    p.spriteImage = source.parameters.value(QStringLiteral("spriteImage")).toString();
    p.additive = int(num(source, "blend", t)) == 1;
    p.size = num(source, "size", t);
    p.sizeVar = num(source, "sizeVar", t);
    p.sizeEnd = num(source, "sizeEnd", t);
    p.opacity = num(source, "opacity", t);
    p.opacityEnd = num(source, "opacityEnd", t);
    p.colorStart = colorOf(source, "colorStart");
    p.colorMid = colorOf(source, "colorMid");
    p.colorEnd = colorOf(source, "colorEnd");
    p.rainbow = flag(source, "rainbow", t);
    return p;
}

Frame simulateUncached(const ProceduralSource &source, double aspect, TimeUs clipTimeUs)
{
    const double t0 = -Params::read(source, 0).prewarm;
    const double t = std::max<double>(0.0, usToSeconds(clipTimeUs));
    const qint64 target = qint64(std::floor((t - t0) / kStep + 1e-9));
    State s;
    const quint64 seed = quint64(quint32(source.seed));
    while (s.step < target)
        advance(s, source, aspect, seed, t0);
    return finish(s, t0, t);
}

Frame simulate(const QString &cacheId, const ProceduralSource &source, double aspect, TimeUs clipTimeUs)
{
    const double t0 = -Params::read(source, 0).prewarm;
    const double t = std::max<double>(0.0, usToSeconds(clipTimeUs));
    const qint64 target = qint64(std::floor((t - t0) / kStep + 1e-9));
    const quint64 seed = quint64(quint32(source.seed));
    const quint64 key = sourceKey(source, aspect);

    QMutexLocker lock(&g_mutex);
    if (g_cache.size() > 32 && !g_cache.contains(cacheId))
        g_cache.clear();
    Entry &entry = g_cache[cacheId];
    if (entry.key != key) {
        entry = Entry{};
        entry.key = key;
    }

    State s;
    if (entry.hasLast && entry.last.step <= target) {
        s = entry.last;
    } else {
        auto it = entry.checkpoints.upper_bound(target);
        if (it != entry.checkpoints.begin()) {
            --it;
            s = it->second;
        }
    }
    // A checkpoint can be nearer than the last played frame when playback restarts behind it.
    if (!entry.checkpoints.empty()) {
        auto it = entry.checkpoints.upper_bound(target);
        if (it != entry.checkpoints.begin()) {
            --it;
            if (it->first > s.step)
                s = it->second;
        }
    }

    while (s.step < target) {
        advance(s, source, aspect, seed, t0);
        if (s.step % kCheckpointSteps == 0 && int(entry.checkpoints.size()) < kMaxCheckpoints
            && entry.checkpoints.find(s.step) == entry.checkpoints.end())
            entry.checkpoints.emplace(s.step, s);
    }
    entry.last = s;
    entry.hasLast = true;
    return finish(s, t0, t);
}

void clearCache()
{
    QMutexLocker lock(&g_mutex);
    g_cache.clear();
}

int cachedCheckpointCount(const QString &cacheId)
{
    QMutexLocker lock(&g_mutex);
    const auto it = g_cache.constFind(cacheId);
    return it == g_cache.constEnd() ? 0 : int(it->checkpoints.size());
}

} // namespace drift::particles
