#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;

uniform sampler2D u_currentTexture;
uniform float u_time;
uniform float bps;
float pv_bps() { return bps; }
uniform vec2 k_n9_center;
uniform float k_n3_seed;
uniform float k_n3_phase;
uniform float k_n3_min;
uniform float k_n3_max;
uniform float k_n5_b;
uniform float k_n7_inMin;
uniform float k_n7_inMax;
uniform float k_n7_outMin;
uniform float k_n7_outMax;

float n_n3_value(vec2 uv) {
    float x = u_time * pv_bps() + k_n3_phase;
    return mix(k_n3_min, k_n3_max, fract(x));
}

float n_n4_value(vec2 uv) {
    return 1.0 - n_n3_value(uv);
}

float n_n5_value(vec2 uv) {
    return pow(abs(n_n4_value(uv)), k_n5_b);
}

float n_n7_value(vec2 uv) {
    float t = clamp((n_n5_value(uv) - k_n7_inMin) / (k_n7_inMax - k_n7_inMin + 1e-6), 0.0, 1.0);
    return mix(k_n7_outMin, k_n7_outMax, t);
}

vec4 n_n1_image(vec2 uv) {
    return texture(u_currentTexture, uv);
}

vec4 n_n9_image(vec2 uv) {
    vec2 p = (uv - k_n9_center) / max(n_n7_value(uv), 0.0001) + k_n9_center;
    return n_n1_image(clamp(p, 0.0, 1.0));
}

void main() {
    fragColor = n_n9_image(v_texCoord);
}
