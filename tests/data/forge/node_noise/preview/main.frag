#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float k_n2_scale;
uniform float k_n2_speed;
uniform float k_n2_seed;

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
    for (int i = 0; i < 5; ++i) {
        v += amp * valueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return v;
}

float n_n2_value(vec2 uv) {
    return fbm(uv * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0) * k_n2_scale + vec2(u_time * k_n2_speed, u_time * k_n2_speed * 0.7) + vec2(k_n2_seed * 17.31, k_n2_seed * 7.77));
}

void main() {
    fragColor = vec4(vec3(n_n2_value(v_texCoord)), 1.0);
}
