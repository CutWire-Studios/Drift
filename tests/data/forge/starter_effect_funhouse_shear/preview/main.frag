#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform vec2 u_resolution;
uniform float u_time;
uniform float lean;
uniform float wobbleRate;
float pv_lean() { return lean; }
float pv_wobbleRate() { return wobbleRate; }
uniform float k_n6_m00;
uniform float k_n2_seed;
uniform float k_n2_phase;
uniform float k_n2_min;
uniform float k_n2_max;
uniform float k_n4_b;
uniform float k_n5_seed;
uniform float k_n5_phase;
uniform float k_n5_min;
uniform float k_n5_max;
uniform vec2 k_n6_origin;
uniform vec2 k_n6_offset;

float n_n2_value(vec2 uv) {
    float x = u_time * pv_wobbleRate() + k_n2_phase;
    return mix(k_n2_min, k_n2_max, 0.5 - 0.5 * cos(x * 6.2832));
}

float n_n3_value(vec2 uv) {
    return n_n2_value(uv) * pv_lean();
}

float n_n4_value(vec2 uv) {
    return n_n3_value(uv) * k_n4_b;
}

float n_n5_value(vec2 uv) {
    float x = u_time * pv_wobbleRate() + k_n5_phase;
    return mix(k_n5_min, k_n5_max, 0.5 - 0.5 * cos(x * 6.2832));
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n6_image(vec2 uv) {
    mat2 M = mat2(k_n6_m00, n_n4_value(uv), n_n3_value(uv), n_n5_value(uv));
    float det = determinant(M);
    mat2 inv = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = k_n6_origin;
    vec2 p = (inv * ((uv - o - k_n6_offset) * vec2(asp, 1.0))) / vec2(asp, 1.0) + o;
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n6_image(v_texCoord);
}
