#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float u_direction;
uniform float softness;
uniform float scale;

float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
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
    for (int i = 0; i < 4; ++i) {
        v += amp * valueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return v / 0.9375;
}

void main() {
    vec4 color = texture(u_currentTexture, v_texCoord);
    float settled = mix(u_progress, 1.0 - u_progress, u_direction);
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    float n = fbm(vec2(v_texCoord.x * aspect, v_texCoord.y) * scale);
    float s = max(softness, 0.0001);
    float keep = smoothstep(n, n + s, settled * (1.0 + s));
    fragColor = vec4(color.rgb, color.a * keep);
}
