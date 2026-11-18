#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float melt;
float pv_melt() { return melt; }
uniform float k_n4_scale;
uniform float k_n4_speed;
uniform float k_n4_seed;

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

float n_n4_value(vec2 uv) {
    return fbm(uv * vec2(u_resolution.x / max(u_resolution.y, 1.0), 1.0) * k_n4_scale + vec2(u_progress * k_n4_speed, u_progress * k_n4_speed * 0.7) + vec2(k_n4_seed * 17.31, k_n4_seed * 7.77));
}

float n_n3_value(vec2 uv) {
    return u_progress;
}

float n_n5_value(vec2 uv) {
    return n_n3_value(uv) * pv_melt();
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n8_image(vec2 uv) {
    vec2 p = uv + (vec4(vec3(n_n4_value(uv)), 1.0).rg - 0.5) * 2.0 * n_n5_value(uv);
    return n_n1_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

float n_n6_value(vec2 uv) {
    return 1.0 - n_n3_value(uv);
}

float n_n7_value(vec2 uv) {
    return n_n6_value(uv) * pv_melt();
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec4 n_n9_image(vec2 uv) {
    vec2 p = uv + (vec4(vec3(n_n4_value(uv)), 1.0).rg - 0.5) * 2.0 * n_n7_value(uv);
    return n_n2_image(1.0 - abs(mod(p, 2.0) - 1.0));
}

float n_n10_value(vec2 uv) {
    float t = clamp(u_progress, 0.0, 1.0);
    return t < 0.5 ? 4.0 * t * t * t : 1.0 - pow(-2.0 * t + 2.0, 3.0) / 2.0;
}

vec4 n_n11_image(vec2 uv) {
    return mix(n_n8_image(uv), n_n9_image(uv), clamp(n_n10_value(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n11_image(v_texCoord);
}
