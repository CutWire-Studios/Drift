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
uniform float size;
uniform float rise;
uniform float stagger;
uniform float refraction;

void main() {
    vec2 uv = v_texCoord;
    float aspect = aspectRatio();
    vec2 g = vec2(uv.x * aspect, uv.y) * cells;
    vec2 id = floor(g);
    vec2 f = fract(g) - 0.5;

    float r1 = hash21(id);
    float r2 = hash21(id + 11.3);
    float t = clamp(u_progress * (1.0 + stagger) - r1 * stagger, 0.0, 1.0);

    // uv.y = 0 is the top of the frame, so rising means a negative y drift.
    vec2 c = vec2((r2 - 0.5) * 0.4, (r1 - 0.5) * 0.3 - rise * t);
    float rad = t * (0.55 + r2 * 0.35) * size;
    float d = length(f - c);

    float m = 1.0 - smoothstep(rad - 0.07, rad + 0.07, d);
    m = max(m, smoothstep(0.88, 1.0, u_progress)); // guarantee a clean finish

    // Bubbles act as weak lenses on the incoming clip.
    vec2 refr = normalize(f - c + 1e-5) * m * (1.0 - m) * refraction * 0.06;
    vec4 b = texture(u_toTexture, uv + refr / vec2(aspect, 1.0));

    fragColor = mix(texture(u_fromTexture, uv), b, m);
}
