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

uniform float zoom;
uniform float swirl;
uniform float blur;

void main() {
    vec2 uv = v_texCoord;
    float aspect = aspectRatio();
    vec2 d = (uv - 0.5) * vec2(aspect, 1.0);

    float r = length(d);
    float amt = sin(u_progress * 3.14159265);

    // Twist falls off with radius, so the centre spins hardest.
    float ang = atan(d.y, d.x) + amt * swirl * (1.0 - smoothstep(0.0, 0.75, r));
    vec2 dir = vec2(cos(ang), sin(ang));

    float zoomA = 1.0 + amt * zoom;       // A pushes in
    float zoomB = 1.0 - amt * zoom * 0.8; // B pulls back out

    float mixT = smoothstep(0.35, 0.65, u_progress);
    vec4 acc = vec4(0.0);
    for (int i = 0; i < 12; ++i) {
        float s = float(i) / 11.0 - 0.5;
        float scale = 1.0 + s * amt * blur * 0.25;
        vec2 uvA = 0.5 + dir * r * zoomA * scale / vec2(aspect, 1.0);
        vec2 uvB = 0.5 + dir * r * zoomB * scale / vec2(aspect, 1.0);
        acc += mix(texture(u_fromTexture, uvA), texture(u_toTexture, uvB), mixT);
    }
    fragColor = acc / 12.0;
}
