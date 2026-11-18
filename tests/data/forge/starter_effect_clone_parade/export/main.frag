#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform vec2 u_resolution;
uniform float u_time;
uniform float swayRate;
uniform float spread;
uniform float cloneSize;
uniform float cloneTint;
uniform vec3 rightColor;
uniform vec3 leftColor;
uniform vec3 haloColor;
uniform float halo;
float pv_swayRate() { return swayRate; }
float pv_spread() { return spread; }
float pv_cloneSize() { return cloneSize; }
float pv_cloneTint() { return cloneTint; }
vec3 pv_rightColor() { return rightColor; }
vec3 pv_leftColor() { return leftColor; }
vec3 pv_haloColor() { return haloColor; }
float pv_halo() { return halo; }

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n18_image(vec2 uv) {
    return texture(u_texture1, uv);
}

float n_n19_r(vec2 uv) {
    return n_n18_image(uv).r;
}

vec4 n_n20_image(vec2 uv) {
    return mix(vec4(0.0, 0.0, 0.0, 1.0), vec4(pv_haloColor(), 1.0), clamp(n_n19_r(uv), 0.0, 1.0));
}

vec4 n_n21_image(vec2 uv) {
    vec4 b = n_n1_image(uv);
    vec4 l = n_n20_image(uv);
    vec3 rgb = 1.0 - (1.0 - b.rgb) * (1.0 - l.rgb);
    return vec4(mix(b.rgb, rgb, clamp(pv_halo() * l.a, 0.0, 1.0)), b.a);
}

float n_n3_value(vec2 uv) {
    float x = u_time * pv_swayRate() + 0.0;
    return mix(0.8, 1.0, 0.5 - 0.5 * cos(x * 6.2832));
}

float n_n4_value(vec2 uv) {
    return pv_spread() * n_n3_value(uv);
}

vec2 n_n5_p(vec2 uv) {
    return vec2(n_n4_value(uv), 0.0);
}

vec2 n_n6_p(vec2 uv) {
    return n_n5_p(uv) * -1.0;
}

vec2 n_n7_p(vec2 uv) {
    mat2 M = mat2(pv_cloneSize(), 0.0, 0.0, pv_cloneSize());
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = vec2(0.5, 1.0);
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + n_n6_p(uv);
}

vec4 n_n9_image(vec2 uv) {
    vec2 p = n_n7_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return n_n1_image(p);
}

vec4 n_n13_image(vec2 uv) {
    vec4 col = n_n9_image(uv);
    vec3 tinted = luma(col) * vec4(pv_rightColor(), 1.0).rgb * 1.6;
    return vec4(mix(col.rgb, clamp(tinted, 0.0, 1.0), pv_cloneTint()), col.a);
}

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
}

vec4 n_n11_image(vec2 uv) {
    vec2 p = n_n7_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return vec4(vec3(n_n2_mask(p)), 1.0);
}

float n_n15_r(vec2 uv) {
    return n_n11_image(uv).r;
}

vec4 n_n22_image(vec2 uv) {
    return mix(n_n21_image(uv), n_n13_image(uv), clamp(n_n15_r(uv), 0.0, 1.0));
}

vec2 n_n8_p(vec2 uv) {
    mat2 M = mat2(pv_cloneSize(), 0.0, 0.0, pv_cloneSize());
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = vec2(0.5, 1.0);
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + n_n5_p(uv);
}

vec4 n_n10_image(vec2 uv) {
    vec2 p = n_n8_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return n_n1_image(p);
}

vec4 n_n14_image(vec2 uv) {
    vec4 col = n_n10_image(uv);
    vec3 tinted = luma(col) * vec4(pv_leftColor(), 1.0).rgb * 1.6;
    return vec4(mix(col.rgb, clamp(tinted, 0.0, 1.0), pv_cloneTint()), col.a);
}

vec4 n_n12_image(vec2 uv) {
    vec2 p = n_n8_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return vec4(vec3(n_n2_mask(p)), 1.0);
}

float n_n16_r(vec2 uv) {
    return n_n12_image(uv).r;
}

vec4 n_n23_image(vec2 uv) {
    return mix(n_n22_image(uv), n_n14_image(uv), clamp(n_n16_r(uv), 0.0, 1.0));
}

vec4 n_n24_image(vec2 uv) {
    return mix(n_n23_image(uv), n_n1_image(uv), clamp(n_n2_mask(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n24_image(v_texCoord);
}
