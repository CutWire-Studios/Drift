#include "app/forge/compiler/Helpers.h"

#include <QHash>
#include <QSet>

namespace drift::forge {

namespace {

struct Helper
{
    QStringList deps;
    QString src;
};

// hash/valueNoise/fbm/over match the header every bundled Drift transition carries, so a graph
// reads like the hand-written packages it sits next to.
const QHash<QString, Helper> &helpers()
{
    static const QHash<QString, Helper> table = {
        {QStringLiteral("hash11"), {{}, QStringLiteral(R"(float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
})")}},
        {QStringLiteral("hash21"), {{}, QStringLiteral(R"(float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
})")}},
        {QStringLiteral("hash22"), {{}, QStringLiteral(R"(vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
})")}},
        {QStringLiteral("valueNoise"), {{QStringLiteral("hash21")}, QStringLiteral(R"(float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
})")}},
        {QStringLiteral("fbm"), {{QStringLiteral("valueNoise")}, QStringLiteral(R"(float fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += amp * valueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return v;
})")}},
        {QStringLiteral("over"), {{}, QStringLiteral(R"(vec4 over(vec4 top, vec4 bot) {
    float oa = top.a + bot.a * (1.0 - top.a);
    if (oa <= 0.0001) return vec4(0.0);
    vec3 rgb = (top.rgb * top.a + bot.rgb * bot.a * (1.0 - top.a)) / oa;
    return vec4(rgb, oa);
})")}},
        {QStringLiteral("luma"), {{}, QStringLiteral(R"(float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); })")}},
        {QStringLiteral("hsv"), {{}, QStringLiteral(R"(vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec3 p = abs(fract(c.xxx + vec3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * mix(vec3(1.0), clamp(p - 1.0, 0.0, 1.0), c.y);
})")}},
        {QStringLiteral("rot2"), {{}, QStringLiteral(R"(mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
})")}},
        {QStringLiteral("rot3"), {{}, QStringLiteral(R"(mat3 rotX(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(1.0, 0.0, 0.0, 0.0, c, s, 0.0, -s, c);
}

mat3 rotY(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(c, 0.0, -s, 0.0, 1.0, 0.0, s, 0.0, c);
}

mat3 rotZ(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat3(c, s, 0.0, -s, c, 0.0, 0.0, 0.0, 1.0);
}

// Degrees around X, then Y, then Z.
mat3 rot3(vec3 deg) {
    vec3 r = radians(deg);
    return rotZ(r.z) * rotY(r.y) * rotX(r.x);
})")}},
        {QStringLiteral("invBilinear"), {{}, QStringLiteral(R"(float cross2(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }

// Where p sits inside the quad a-b-c-d (clockwise from top-left) as 0..1 coordinates; outside
// the quad the result leaves 0..1. After Inigo Quilez's inverse bilinear interpolation.
vec2 invBilinear(vec2 p, vec2 a, vec2 b, vec2 c, vec2 d) {
    vec2 e = b - a;
    vec2 f = d - a;
    vec2 g = a - b + c - d;
    vec2 h = p - a;
    float k2 = cross2(g, f);
    float k1 = cross2(e, f) + cross2(h, g);
    float k0 = cross2(h, e);
    if (abs(k2) < 0.0001) return vec2((h.x * k1 + f.x * k0) / (e.x * k1 - g.x * k0), -k0 / k1);
    float w = k1 * k1 - 4.0 * k0 * k2;
    if (w < 0.0) return vec2(-1.0);
    w = sqrt(w);
    float ik2 = 0.5 / k2;
    float v = (-k1 - w) * ik2;
    float u = (h.x - f.x * v) / (e.x + g.x * v);
    if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) {
        v = (-k1 + w) * ik2;
        u = (h.x - f.x * v) / (e.x + g.x * v);
    }
    return vec2(u, v);
})")}},
        {QStringLiteral("rayQuad"), {{}, QStringLiteral(R"(// Camera at the origin looking down +z with y pointing down. Intersects ray rd with a w-by-h
// quad centred at c whose unit axes are ax and ay. Returns the hit's 0..1 position on the quad
// (.xy), its distance (.z, negative for a miss) and 1.0 in .w when the front face is visible.
vec4 rayQuad(vec3 rd, vec3 c, vec3 ax, vec3 ay, float w, float h) {
    vec3 n = cross(ax, ay);
    float den = dot(rd, n);
    if (abs(den) < 1e-6) return vec4(0.0, 0.0, -1.0, 0.0);
    float t = dot(c, n) / den;
    if (t <= 0.0) return vec4(0.0, 0.0, -1.0, 0.0);
    vec3 hit = rd * t - c;
    vec2 q = vec2(dot(hit, ax) / w, dot(hit, ay) / h) + 0.5;
    if (q.x < 0.0 || q.x > 1.0 || q.y < 0.0 || q.y > 1.0) return vec4(q, -1.0, 0.0);
    return vec4(q, t, den > 0.0 ? 1.0 : 0.0);
})")}},
        {QStringLiteral("ycc"), {{}, QStringLiteral(R"(vec3 rgb2ycc(vec3 c) {
    return vec3(
        dot(c, vec3(0.299, 0.587, 0.114)),
        dot(c, vec3(-0.168736, -0.331264, 0.5)) + 0.5,
        dot(c, vec3(0.5, -0.418688, -0.081312)) + 0.5);
})")}},
        {QStringLiteral("blend"), {{}, QStringLiteral(R"(vec3 blendOverlay(vec3 b, vec3 l) {
    return mix(2.0 * b * l, 1.0 - 2.0 * (1.0 - b) * (1.0 - l), step(0.5, b));
}

vec3 blendSoftLight(vec3 b, vec3 l) {
    return mix(2.0 * b * l + b * b * (1.0 - 2.0 * l), sqrt(b) * (2.0 * l - 1.0) + 2.0 * b * (1.0 - l), step(0.5, l));
})")}},
        {QStringLiteral("ease"), {{}, QStringLiteral(R"(float easeOutBounce(float t) {
    if (t < 1.0 / 2.75) return 7.5625 * t * t;
    if (t < 2.0 / 2.75) { t -= 1.5 / 2.75; return 7.5625 * t * t + 0.75; }
    if (t < 2.5 / 2.75) { t -= 2.25 / 2.75; return 7.5625 * t * t + 0.9375; }
    t -= 2.625 / 2.75;
    return 7.5625 * t * t + 0.984375;
})")}},
    };
    return table;
}

}

QStringList resolveHelpers(const QStringList &names)
{
    QStringList out;
    QSet<QString> seen;
    const auto &table = helpers();
    const auto visit = [&](const auto &self, const QString &n) -> void {
        if (seen.contains(n))
            return;
        seen.insert(n);
        const Helper h = table.value(n);
        for (const QString &d : h.deps)
            self(self, d);
        out.append(h.src);
    };
    for (const QString &n : names)
        visit(visit, n);
    return out;
}

}
