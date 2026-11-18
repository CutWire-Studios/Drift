#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float wobbleSpeed;
uniform float wobble;
uniform float tilt;
uniform float stickerSize;
float pv_wobbleSpeed() { return wobbleSpeed; }
float pv_wobble() { return wobble; }
float pv_tilt() { return tilt; }
float pv_stickerSize() { return stickerSize; }

float n_n2_mask(vec2 uv) {
    return driftMask(uv);
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

vec4 n_n14_image(vec2 uv) {
    vec2 p = n_n12_p(uv);
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return vec4(0.0);
    return vec4(vec3(n_n2_mask(p)), 1.0);
}

void main() {
    fragColor = n_n14_image(v_texCoord);
}
