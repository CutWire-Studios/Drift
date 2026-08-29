#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture; // pass input 0
uniform sampler2D u_fromTexture;    // outgoing clip layer
uniform sampler2D u_toTexture;      // incoming clip layer
uniform vec2 u_resolution;
uniform float u_progress;           // 0..1 across the transition window

float hash11(float p) {
    p = fract(p * 0.1031);
    p *= p + 33.33;
    p *= p + p;
    return fract(p);
}

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += amp * valueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return v;
}

// Straight-alpha source-over. Clip layers are transparent outside the clip rect.
vec4 over(vec4 top, vec4 bot) {
    float oa = top.a + bot.a * (1.0 - top.a);
    if (oa <= 0.0001) return vec4(0.0);
    vec3 rgb = (top.rgb * top.a + bot.rgb * bot.a * (1.0 - top.a)) / oa;
    return vec4(rgb, oa);
}

float aspectRatio() { return u_resolution.x / max(u_resolution.y, 1.0); }

uniform float cells;
uniform float push;
uniform float stagger;
uniform float spin;

void main() {
    vec2 uv = v_texCoord;
    float aspect = aspectRatio();
    vec2 g = vec2(uv.x * aspect, uv.y) * cells;
    vec2 base = floor(g);

    // F1 Voronoi over a jittered lattice — the winning cell id owns this pixel.
    float bestD = 1e9;
    vec2 bestId = base;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 nb = base + vec2(float(i), float(j));
            vec2 pt = nb + hash22(nb);
            float d = distance(pt, g);
            if (d < bestD) {
                bestD = d;
                bestId = nb;
            }
        }
    }

    float rnd = hash21(bestId);
    float t = clamp(u_progress * (1.0 + stagger) - rnd * stagger, 0.0, 1.0);

    vec2 dir = normalize(hash22(bestId + 3.7) * 2.0 - 1.0);
    float ang = (hash21(bestId + 9.1) - 0.5) * spin * t;
    float cs = cos(ang), sn = sin(ang);
    vec2 rot = mat2(cs, -sn, sn, cs) * dir;

    // Shards accelerate outward, then fade.
    vec2 off = rot * t * t * push / vec2(aspect, 1.0);
    vec4 a = texture(u_fromTexture, uv + off);
    a.a *= 1.0 - smoothstep(0.35, 1.0, t);

    fragColor = over(a, texture(u_toTexture, uv));
}
