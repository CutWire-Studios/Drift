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

uniform float roll;
uniform float barWidth;
uniform float noise;
uniform float chroma;
uniform float scanline;
uniform float jitter;

void main() {
    vec2 uv = v_texCoord;
    float peak = sin(u_progress * 3.14159265);
    float tick = floor(u_progress * 48.0);

    // Rolling sync bar sweeping down the frame.
    float bar = fract(uv.y + u_progress * roll);
    float inBar = step(bar, barWidth);

    float j = (hash21(vec2(floor(uv.y * 220.0), tick)) - 0.5) * jitter * peak;
    j += inBar * (hash11(tick) - 0.5) * 0.08 * peak;
    vec2 uvJ = vec2(uv.x + j, uv.y);

    float ca = chroma * peak / max(u_resolution.x, 1.0);
    vec4 a = vec4(texture(u_fromTexture, uvJ + vec2(ca, 0.0)).r,
                  texture(u_fromTexture, uvJ).g,
                  texture(u_fromTexture, uvJ - vec2(ca, 0.0)).b,
                  texture(u_fromTexture, uvJ).a);
    vec4 b = vec4(texture(u_toTexture, uvJ + vec2(ca, 0.0)).r,
                  texture(u_toTexture, uvJ).g,
                  texture(u_toTexture, uvJ - vec2(ca, 0.0)).b,
                  texture(u_toTexture, uvJ).a);

    vec4 c = mix(a, b, smoothstep(0.4, 0.6, u_progress));

    float n = hash21(uv * u_resolution + tick);
    c.rgb = mix(c.rgb, vec3(n), noise * peak * (0.25 + inBar * 0.75));
    c.rgb *= 1.0 - scanline * peak * (0.5 + 0.5 * sin(uv.y * u_resolution.y * 3.14159265));

    fragColor = c;
}
