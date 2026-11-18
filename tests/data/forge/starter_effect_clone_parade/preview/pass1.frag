#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float swayRate;
uniform float spread;
uniform float cloneSize;
float pv_swayRate() { return swayRate; }
float pv_spread() { return spread; }
float pv_cloneSize() { return cloneSize; }
uniform float k_n7_m10;
uniform float k_n7_m01;
uniform vec2 k_n7_origin;
uniform float k_n3_seed;
uniform float k_n3_phase;
uniform float k_n3_min;
uniform float k_n3_max;
uniform float k_n5_y;
uniform vec2 k_n6_b;
uniform float k_n6_s;
uniform float k_n8_m10;
uniform float k_n8_m01;
uniform vec2 k_n8_origin;
uniform float k_n17_opacity;

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
}

float n_n3_value(vec2 uv) {
    float x = u_time * pv_swayRate() + k_n3_phase;
    return mix(k_n3_min, k_n3_max, 0.5 - 0.5 * cos(x * 6.2832));
}

float n_n4_value(vec2 uv) {
    return pv_spread() * n_n3_value(uv);
}

vec2 n_n5_p(vec2 uv) {
    return vec2(n_n4_value(uv), k_n5_y);
}

vec2 n_n6_p(vec2 uv) {
    return n_n5_p(uv) * k_n6_s;
}

vec2 n_n7_p(vec2 uv) {
    mat2 M = mat2(pv_cloneSize(), k_n7_m10, k_n7_m01, pv_cloneSize());
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = k_n7_origin;
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + n_n6_p(uv);
}

vec4 n_n11_image(vec2 uv) {
    vec2 p = n_n7_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return vec4(vec3(n_n2_mask(p)), 1.0);
}

vec2 n_n8_p(vec2 uv) {
    mat2 M = mat2(pv_cloneSize(), k_n8_m10, k_n8_m01, pv_cloneSize());
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = k_n8_origin;
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + n_n5_p(uv);
}

vec4 n_n12_image(vec2 uv) {
    vec2 p = n_n8_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return vec4(vec3(n_n2_mask(p)), 1.0);
}

vec4 n_n17_image(vec2 uv) {
    vec4 b = n_n11_image(uv);
    vec4 l = n_n12_image(uv);
    vec3 rgb = min(b.rgb + l.rgb, 1.0);
    return vec4(mix(b.rgb, rgb, clamp(k_n17_opacity * l.a, 0.0, 1.0)), b.a);
}

void main() {
    fragColor = n_n17_image(v_texCoord);
}
