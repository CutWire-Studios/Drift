#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform sampler2D u_texture2;
uniform sampler2D u_texture3;
uniform vec2 u_resolution;
uniform float u_progress;
uniform float spin;
uniform float flingSize;
uniform vec3 borderColor;
float pv_spin() { return spin; }
float pv_flingSize() { return flingSize; }
vec3 pv_borderColor() { return borderColor; }

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n2_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n4_value(vec2 uv) {
    return u_progress;
}

float n_n26_value(vec2 uv) {
    return smoothstep(0.0, max(0.5, 0.0 + 1e-5), n_n4_value(uv));
}

vec4 n_n27_image(vec2 uv) {
    return mix(n_n1_image(uv), n_n2_image(uv), clamp(n_n26_value(uv), 0.0, 1.0));
}

vec4 n_n21_image(vec2 uv) {
    return texture(u_texture2, uv);
}

float n_n22_r(vec2 uv) {
    return n_n21_image(uv).r;
}

float n_n23_value(vec2 uv) {
    return smoothstep(0.03, max(0.1, 0.03 + 1e-5), n_n22_r(uv));
}

float n_n24_value(vec2 uv) {
    return smoothstep(0.0, max(0.08, 0.0 + 1e-5), n_n4_value(uv));
}

float n_n25_value(vec2 uv) {
    return n_n23_value(uv) * n_n24_value(uv);
}

vec4 n_n28_image(vec2 uv) {
    return mix(n_n27_image(uv), vec4(pv_borderColor(), 1.0), clamp(n_n25_value(uv), 0.0, 1.0));
}

float n_n5_value(vec2 uv) {
    float t = clamp(n_n4_value(uv), 0.0, 1.0);
    return t * t * t;
}

float n_n6_value(vec2 uv) {
    return n_n5_value(uv) * pv_spin();
}

float n_n7_value(vec2 uv) {
    return n_n6_value(uv) * 0.017453;
}

float n_n8_value(vec2 uv) {
    return cos(n_n7_value(uv));
}

float n_n10_value(vec2 uv) {
    float t = clamp((n_n5_value(uv) - 0.0) / (1.0 - 0.0 + 1e-6), 0.0, 1.0);
    return mix(1.0, pv_flingSize(), t);
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
    return n_n5_value(uv) * -0.25;
}

float n_n15_value(vec2 uv) {
    return n_n5_value(uv) * 1.4;
}

vec2 n_n16_p(vec2 uv) {
    return vec2(n_n14_value(uv), n_n15_value(uv));
}

vec2 n_n17_p(vec2 uv) {
    mat2 M = mat2(n_n11_value(uv), n_n12_value(uv), n_n13_value(uv), n_n11_value(uv));
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = vec2(0.5, 0.6);
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + n_n16_p(uv);
}

vec4 n_n18_image(vec2 uv) {
    vec2 p = n_n17_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return n_n1_image(p);
}

float n_n20_r(vec2 uv) {
    return texture(u_texture3, uv).r;
}

vec4 n_n29_image(vec2 uv) {
    return mix(n_n28_image(uv), n_n18_image(uv), clamp(n_n20_r(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n29_image(v_texCoord);
}
