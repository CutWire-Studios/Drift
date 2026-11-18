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

float n_n2_value(vec2 uv) {
    float x = u_time * pv_wobbleRate() + 0.0;
    return mix(-1.0, 1.0, 0.5 - 0.5 * cos(x * 6.2832));
}

float n_n3_value(vec2 uv) {
    return n_n2_value(uv) * pv_lean();
}

float n_n4_value(vec2 uv) {
    return n_n3_value(uv) * -0.5;
}

float n_n5_value(vec2 uv) {
    float x = u_time * pv_wobbleRate() + 0.25;
    return mix(0.85, 1.15, 0.5 - 0.5 * cos(x * 6.2832));
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n6_image(vec2 uv) {
    mat2 M = mat2(1.0, n_n4_value(uv), n_n3_value(uv), n_n5_value(uv));
    float det = determinant(M);
    mat2 inv = mat2(M[1][1], -M[0][1], -M[1][0], M[0][0]) / (abs(det) < 1e-6 ? 1e-6 : det);
    float asp = u_resolution.x / max(u_resolution.y, 1.0);
    vec2 o = vec2(0.5, 0.5);
    vec2 p = (inv * ((uv - o - vec2(0.0, 0.0)) * vec2(asp, 1.0))) / vec2(asp, 1.0) + o;
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n6_image(v_texCoord);
}
