#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform sampler2D u_texture1;
uniform sampler2D u_texture2;
uniform sampler2D u_texture3;
uniform vec2 u_resolution;
uniform float u_time;
uniform float wobbleSpeed;
uniform float wobble;
uniform float tilt;
uniform float stickerSize;
uniform float shadow;
uniform vec3 borderColor;
float pv_wobbleSpeed() { return wobbleSpeed; }
float pv_wobble() { return wobble; }
float pv_tilt() { return tilt; }
float pv_stickerSize() { return stickerSize; }
float pv_shadow() { return shadow; }
vec3 pv_borderColor() { return borderColor; }

float luma(vec4 c) { return dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)); }

mat2 rot2(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

vec4 n_n25_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n26_image(vec2 uv) {
    vec4 col = n_n25_image(uv);
    vec3 rgb = (col.rgb - 0.5) * 1.0 + 0.5 + -0.15;
    return vec4(clamp(rgb, 0.0, 1.0), col.a);
}

vec4 n_n16_image(vec2 uv) {
    return texture(u_texture1, uv);
}

vec2 n_n19_uv(vec2 uv) {
    return uv;
}

vec2 n_n20_p(vec2 uv) {
    return n_n19_uv(uv) + vec2(-0.012, -0.022);
}

vec4 n_n21_image(vec2 uv) {
    vec2 p = n_n20_p(uv);
    return n_n16_image(clamp(p, 0.0, 1.0));
}

float n_n22_r(vec2 uv) {
    return n_n21_image(uv).r;
}

float n_n23_value(vec2 uv) {
    return smoothstep(0.02, max(0.5, 0.02 + 1e-5), n_n22_r(uv));
}

float n_n24_value(vec2 uv) {
    return n_n23_value(uv) * pv_shadow();
}

vec4 n_n27_image(vec2 uv) {
    return mix(n_n26_image(uv), vec4(0.0, 0.0, 0.0, 1.0), clamp(n_n24_value(uv), 0.0, 1.0));
}

float n_n17_r(vec2 uv) {
    return n_n16_image(uv).r;
}

float n_n18_value(vec2 uv) {
    return smoothstep(0.03, max(0.1, 0.03 + 1e-5), n_n17_r(uv));
}

vec4 n_n28_image(vec2 uv) {
    return mix(n_n27_image(uv), vec4(pv_borderColor(), 1.0), clamp(n_n18_value(uv), 0.0, 1.0));
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_texture2, uv);
}

float n_n3_value(vec2 uv) {
    float x = u_time * pv_wobbleSpeed() + 0.0;
    return mix(-1.0, 1.0, 0.5 - 0.5 * cos(x * 6.2832));
}

float n_n4_value(vec2 uv) {
    return n_n3_value(uv) * pv_wobble();
}

float n_n5_value(vec2 uv) {
    return n_n4_value(uv) + pv_tilt();
}

float n_n6_value(vec2 uv) {
    return n_n5_value(uv) * 0.017453;
}

float n_n7_value(vec2 uv) {
    return cos(n_n6_value(uv));
}

float n_n9_value(vec2 uv) {
    return n_n7_value(uv) * pv_stickerSize();
}

float n_n8_value(vec2 uv) {
    return sin(n_n6_value(uv));
}

float n_n10_value(vec2 uv) {
    return n_n8_value(uv) * pv_stickerSize();
}

float n_n11_value(vec2 uv) {
    return -n_n10_value(uv);
}

vec2 n_n12_p(vec2 uv) {
    mat2 M = mat2(n_n9_value(uv), n_n10_value(uv), n_n11_value(uv), n_n9_value(uv));
    float det = determinant(M);
    M = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = vec2(0.5, 0.6);
    vec2 q = (uv - o) * vec2(asp, 1.0);
    return (M * q) / vec2(asp, 1.0) + o + vec2(0.0, 0.0);
}

vec4 n_n13_image(vec2 uv) {
    vec2 p = n_n12_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return n_n1_image(p);
}

float n_n15_r(vec2 uv) {
    return texture(u_texture3, uv).r;
}

vec4 n_n29_image(vec2 uv) {
    return mix(n_n28_image(uv), n_n13_image(uv), clamp(n_n15_r(uv), 0.0, 1.0));
}

void main() {
    fragColor = n_n29_image(v_texCoord);
}
