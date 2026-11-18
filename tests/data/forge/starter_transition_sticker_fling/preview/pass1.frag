#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float spin;
uniform float flingSize;
float pv_spin() { return spin; }
float pv_flingSize() { return flingSize; }
uniform float k_n7_b;
uniform float k_n10_inMin;
uniform float k_n10_inMax;
uniform float k_n10_outMin;
uniform vec2 k_n17_origin;
uniform float k_n14_b;
uniform float k_n15_b;

float n_n3_mask(vec2 uv) {
    return driftMask(uv);
}

float n_n4_value(vec2 uv) {
    return u_progress;
}

float n_n5_value(vec2 uv) {
    float t = clamp(n_n4_value(uv), 0.0, 1.0);
    return t * t * t;
}

float n_n6_value(vec2 uv) {
    return n_n5_value(uv) * pv_spin();
}

float n_n7_value(vec2 uv) {
    return n_n6_value(uv) * k_n7_b;
}

float n_n8_value(vec2 uv) {
    return cos(n_n7_value(uv));
}

float n_n10_value(vec2 uv) {
    float t = clamp((n_n5_value(uv) - k_n10_inMin) / (k_n10_inMax - k_n10_inMin + 1e-6), 0.0, 1.0);
    return mix(k_n10_outMin, pv_flingSize(), t);
}

float n_n11_value(vec2 uv) {
    return n_n8_value(uv) * n_n10_value(uv);
}

float n_n9_value(vec2 uv) {
    return sin(n_n7_value(uv));
}

float n_n12_value(vec2 uv) {
    return n_n9_value(uv) * n_n10_value(uv);
}

float n_n13_value(vec2 uv) {
    return -n_n12_value(uv);
}

float n_n14_value(vec2 uv) {
    return n_n5_value(uv) * k_n14_b;
}

float n_n15_value(vec2 uv) {
    return n_n5_value(uv) * k_n15_b;
}

vec2 n_n16_p(vec2 uv) {
    return vec2(n_n14_value(uv), n_n15_value(uv));
}

vec2 n_n17_p(vec2 uv) {
    mat2 M = mat2(n_n11_value(uv), n_n12_value(uv), n_n13_value(uv), n_n11_value(uv));
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = k_n17_origin;
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + n_n16_p(uv);
}

vec4 n_n19_image(vec2 uv) {
    vec2 p = n_n17_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return vec4(vec3(n_n3_mask(p)), 1.0);
}

void main() {
    fragColor = n_n19_image(v_texCoord);
}
