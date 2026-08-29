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

uniform sampler2D u_texture2; // 16x16 glyph atlas
uniform float columns;
uniform float trail;
uniform float brightness;
uniform float flicker;
uniform vec3 rainColor;

void main() {
    vec2 uv = v_texCoord;
    float aspect = aspectRatio();
    float cols = floor(columns);
    float rows = max(floor(cols / aspect), 1.0);

    float colIdx = floor(uv.x * cols);
    float rowIdx = floor(uv.y * rows);
    float r = hash11(colIdx);

    // Each column runs at its own speed but is always finished by p = 1.
    float head = u_progress * (1.0 + trail) * (0.75 + r * 0.5);
    float y = rowIdx / rows;
    float dist = head - y;

    float lit = clamp(1.0 - dist / max(trail, 0.001), 0.0, 1.0) * step(0.0, dist);
    float glow = smoothstep(trail * 0.2, 0.0, abs(dist));

    // Glyph choice flickers in discrete steps of progress — no wall-clock time.
    float tick = floor(u_progress * 12.0) * step(0.01, flicker);
    float gi = floor(hash21(vec2(colIdx, rowIdx + tick * 7.0)) * 256.0);
    vec2 cell = vec2(mod(gi, 16.0), floor(gi / 16.0));
    vec2 inCell = vec2(fract(uv.x * cols), fract(uv.y * rows));
    float g = texture(u_texture2, (cell + inCell) / 16.0).r;

    float m = smoothstep(0.0, 0.06, dist);
    vec4 c = mix(texture(u_fromTexture, uv), texture(u_toTexture, uv), clamp(m, 0.0, 1.0));

    vec3 rain = mix(rainColor, vec3(0.9, 1.0, 0.95), glow);
    float alive = 1.0 - smoothstep(0.85, 1.0, u_progress);
    c.rgb += rain * g * max(lit, glow) * brightness * alive;

    fragColor = c;
}
